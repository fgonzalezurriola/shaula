#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>

#include "preview_canvas.h"
#include "preview_icons.h"
#include "preview_state.h"
#include "preview_toolbar.h"

static ShaulaPreviewState state;

static gboolean preview_init_debug_enabled(void) {
  const char *value = g_getenv("SHAULA_DEBUG_PREVIEW_INIT");
  return value != NULL && value[0] != '\0' && g_strcmp0(value, "0") != 0;
}

static void debug_preview_init(const char *stage, int value) {
  if (!preview_init_debug_enabled())
    return;
  g_printerr("[DEBUG-preview-init] %s=%d\n", stage, value);
}

static int positive_env_int(const char *name, int fallback) {
  const char *raw = g_getenv(name);
  if (raw == NULL || raw[0] == '\0')
    return fallback;

  char *end = NULL;
  long value = strtol(raw, &end, 10);
  if (end == raw || *end != '\0' || value <= 0 || value > G_MAXINT)
    return fallback;
  return (int)value;
}

/* The preview must be mapped only after GTK has a stable first-frame contract:
 * loaded pixbuf, full toolbar structure, default size, fit zoom, and initial
 * overflow state. The configured preview preset is the primary size contract;
 * image dimensions only affect fit zoom inside that stable window.
 */
static void compute_initial_window_size(int *out_w, int *out_h) {
  int width = MAX(PREVIEW_READY_DEFAULT_MIN_W,
                  positive_env_int("SHAULA_PREVIEW_WINDOW_WIDTH", 1100));
  int height =
      MAX(PREVIEW_MIN_H, positive_env_int("SHAULA_PREVIEW_WINDOW_HEIGHT", 720));

  GdkDisplay *display = gdk_display_get_default();
  if (display != NULL) {
    GListModel *monitors = gdk_display_get_monitors(display);
    if (monitors != NULL && g_list_model_get_n_items(monitors) > 0) {
      GdkMonitor *monitor = g_list_model_get_item(monitors, 0);
      if (monitor != NULL) {
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);
        width = MIN(width, MAX(PREVIEW_MIN_W, geometry.width - 96));
        height = MIN(height, MAX(PREVIEW_MIN_H, geometry.height - 96));
        g_object_unref(monitor);
      }
    }
  }

  *out_w = width;
  *out_h = height;
}

static void seed_initial_fit(int window_w, int window_h) {
  int image_w = MAX(1, shaula_preview_image_width(&state));
  int image_h = MAX(1, shaula_preview_image_height(&state));
  int area_w = MAX(1, window_w);
  int area_h = MAX(1, window_h - PREVIEW_HEADER_ESTIMATED_H);
  double scale_x = (double)MAX(1, area_w - 48) / (double)image_w;
  double scale_y = (double)MAX(1, area_h - 48) / (double)image_h;

  state.fit_mode = TRUE;
  state.fit_zoom = MIN(1.0, MAX(0.05, MIN(scale_x, scale_y)));
  state.zoom = state.fit_zoom;
  state.pan_x = ((double)area_w - (double)image_w * state.zoom) / 2.0;
  state.pan_y = ((double)area_h - (double)image_h * state.zoom) / 2.0;
}

static GtkWidget *build_preview_root(GtkWidget *topbar, GtkWidget *canvas) {
  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(root, TRUE);
  gtk_widget_set_vexpand(root, TRUE);
  gtk_widget_set_hexpand(topbar, TRUE);
  gtk_widget_set_vexpand(canvas, TRUE);
  gtk_box_append(GTK_BOX(root), topbar);
  gtk_box_append(GTK_BOX(root), canvas);
  return root;
}

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
  debug_preview_init("icon_theme_loaded", state.icon_root_count);

  GtkWidget *window = gtk_application_window_new(app);
  state.window = window;
  gtk_window_set_title(GTK_WINDOW(window), "Shaula Preview");
  gtk_widget_set_size_request(window, PREVIEW_MIN_W, PREVIEW_MIN_H);
  int initial_w = PREVIEW_DEFAULT_W;
  int initial_h = PREVIEW_DEFAULT_H;
  compute_initial_window_size(&initial_w, &initial_h);
  gtk_window_set_default_size(GTK_WINDOW(window), initial_w, initial_h);
  debug_preview_init("default_width", initial_w);
  debug_preview_init("default_height", initial_h);
  gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

  GtkWidget *canvas = shaula_preview_canvas_build(&state);
  GtkWidget *topbar = shaula_preview_toolbar_build(&state);
  seed_initial_fit(initial_w, initial_h);
  shaula_preview_update_zoom_label(&state);
  shaula_preview_toolbar_prepare_initial_layout(&state, initial_w);
  gtk_window_set_child(GTK_WINDOW(window), build_preview_root(topbar, canvas));

  gtk_window_present(GTK_WINDOW(window));
  debug_preview_init("window_presented", 1);
  gtk_widget_grab_focus(state.area);
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
  debug_preview_init("pixbuf_loaded", 1);

  shaula_preview_state_init(&state, path, image);
  const char *close_on_save = getenv("SHAULA_PREVIEW_CLOSE_ON_SAVE");
  state.close_preview_on_save =
      close_on_save != NULL && g_strcmp0(close_on_save, "1") == 0;
  GtkApplication *app =
      gtk_application_new("dev.shaula.preview", G_APPLICATION_NON_UNIQUE);
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
