#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>

#include "preview_canvas.h"
#include "preview_icons.h"
#include "preview_state.h"
#include "preview_toolbar.h"

static ShaulaPreviewState state;

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
  fprintf(stdout, ",\"copied\":%s,\"saved\":%s,\"saved_path\":",
          state.copied ? "true" : "false", state.saved ? "true" : "false");
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
