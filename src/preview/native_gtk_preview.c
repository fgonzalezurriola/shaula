#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdio.h>

#include "preview_canvas.h"
#include "preview_icons.h"
#include "preview_state.h"
#include "preview_toolbar.h"

static ShaulaPreviewState state;

static void sweep_stale_temp_dir(const char *dir, gint64 now_us,
                                 gint64 ttl_us) {
  if (dir == NULL || dir[0] == '\0')
    return;

  GDir *handle = g_dir_open(dir, 0, NULL);
  if (handle == NULL)
    return;

  const char *name = NULL;
  while ((name = g_dir_read_name(handle)) != NULL) {
    if (!(g_str_has_prefix(name, "capture-") ||
          g_str_has_prefix(name, "shaula-preview-")) ||
        !g_str_has_suffix(name, ".png"))
      continue;

    char *path = g_build_filename(dir, name, NULL);
    GStatBuf st;
    if (g_stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
      gint64 age_us = now_us - ((gint64)st.st_mtime * G_USEC_PER_SEC);
      if (age_us > ttl_us)
        g_unlink(path);
    }
    g_free(path);
  }

  g_dir_close(handle);
}

static void sweep_stale_shaula_temps(void) {
  const gint64 ttl_us = ((gint64)24) * 60 * 60 * G_USEC_PER_SEC;
  const gint64 now_us = g_get_real_time();
  sweep_stale_temp_dir("/tmp/shaula/captures", now_us, ttl_us);
  sweep_stale_temp_dir(g_get_tmp_dir(), now_us, ttl_us);

  const char *runtime_dir = g_getenv("XDG_RUNTIME_DIR");
  if (runtime_dir != NULL && runtime_dir[0] != '\0') {
    char *runtime_captures =
        g_build_filename(runtime_dir, "shaula", "captures", NULL);
    sweep_stale_temp_dir(runtime_captures, now_us, ttl_us);
    g_free(runtime_captures);
  }
}

static void print_json_string(const char *value) {
  fputc('"', stdout);
  for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
    switch (*p) {
    case '"':
      fputs("\\\"", stdout);
      break;
    case '\\':
      fputs("\\\\", stdout);
      break;
    case '\b':
      fputs("\\b", stdout);
      break;
    case '\f':
      fputs("\\f", stdout);
      break;
    case '\n':
      fputs("\\n", stdout);
      break;
    case '\r':
      fputs("\\r", stdout);
      break;
    case '\t':
      fputs("\\t", stdout);
      break;
    default:
      if (*p < 0x20) {
        fprintf(stdout, "\\u%04x", *p);
      } else {
        fputc(*p, stdout);
      }
      break;
    }
  }
  fputc('"', stdout);
}

static void emit_preview_result(void) {
  const char *action = state.last_action != NULL ? state.last_action : "close";
  fputs("{\"closed\":true,\"action\":", stdout);
  print_json_string(action);
  fprintf(stdout, ",\"copied\":%s,\"saved\":%s,\"notified\":%s,\"saved_path\":",
          state.copied ? "true" : "false", state.saved ? "true" : "false",
          state.notified ? "true" : "false");
  if (state.saved_path != NULL) {
    print_json_string(state.saved_path);
  } else {
    fputs("null", stdout);
  }
  fputs("}\n", stdout);
  fflush(stdout);
}

static void on_activate(GtkApplication *app, gpointer data) {
  (void)data;
  state.app = app;
  shaula_preview_register_custom_icons(&state);

  GtkWidget *window = gtk_application_window_new(app);
  state.window = window;
  gtk_window_set_title(GTK_WINDOW(window), "Shaula Preview");
  gtk_widget_set_size_request(window, PREVIEW_MIN_W, PREVIEW_MIN_H);
  gtk_window_set_default_size(GTK_WINDOW(window), PREVIEW_DEFAULT_W,
                              PREVIEW_DEFAULT_H);
  gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

  GtkWidget *canvas = shaula_preview_canvas_build(&state);
  GtkWidget *topbar = shaula_preview_toolbar_build(&state);
  gtk_window_set_titlebar(GTK_WINDOW(window), topbar);
  gtk_window_set_child(GTK_WINDOW(window), canvas);

  gtk_window_present(GTK_WINDOW(window));
  gtk_widget_grab_focus(state.area);
  shaula_preview_set_fit_mode(&state, TRUE);
}

int main(int argc, char **argv) {
  sweep_stale_shaula_temps();

  const char *path = argc >= 2 ? argv[1] : getenv("SHAULA_PREVIEW_PATH");
  if (path == NULL || path[0] == '\0') {
    fprintf(stderr, "shaula-preview requires an image path\n");
    return 2;
  }

  GError *error = NULL;
  GdkPixbuf *image = gdk_pixbuf_new_from_file(path, &error);
  if (image == NULL) {
    if (error != NULL) {
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
    }
    return 43;
  }

  shaula_preview_state_init(&state, path, image);
  const char *close_on_save = getenv("SHAULA_PREVIEW_CLOSE_ON_SAVE");
  state.close_preview_on_save =
      close_on_save != NULL && g_strcmp0(close_on_save, "1") == 0;
  GtkApplication *app =
      gtk_application_new("dev.shaula.preview", G_APPLICATION_DEFAULT_FLAGS);
  if (app == NULL) {
    shaula_preview_state_free(&state);
    return 44;
  }

  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  int rc = g_application_run(G_APPLICATION(app), 0, NULL);
  if (rc == 0)
    emit_preview_result();

  g_object_unref(app);
  shaula_preview_state_free(&state);
  return rc > 255 ? 255 : rc;
}
