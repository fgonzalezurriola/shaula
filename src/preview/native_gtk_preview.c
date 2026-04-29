#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum {
  PREVIEW_MIN_W = 860,
  PREVIEW_MIN_H = 560,
  TOOLBAR_H = 48,
};

typedef struct {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *area;
  GdkPixbuf *image;
  char *path;
  double zoom;
  double fit_zoom;
  double pan_x;
  double pan_y;
  gboolean fit_mode;
  gboolean dragging;
  double drag_start_x;
  double drag_start_y;
  double pan_origin_x;
  double pan_origin_y;
  gboolean copied;
  gboolean saved;
} ShaulaPreviewState;

static ShaulaPreviewState state;

static void queue_draw(void) {
  if (state.area != NULL) gtk_widget_queue_draw(state.area);
}

static void update_fit_zoom(void) {
  if (state.area == NULL || state.image == NULL) return;
  int area_w = MAX(1, gtk_widget_get_width(state.area));
  int area_h = MAX(1, gtk_widget_get_height(state.area));
  int image_w = MAX(1, gdk_pixbuf_get_width(state.image));
  int image_h = MAX(1, gdk_pixbuf_get_height(state.image));
  double scale_x = (double)(area_w - 48) / (double)image_w;
  double scale_y = (double)(area_h - 48) / (double)image_h;
  state.fit_zoom = MIN(1.0, MAX(0.05, MIN(scale_x, scale_y)));
  if (state.fit_mode) {
    state.zoom = state.fit_zoom;
    state.pan_x = ((double)area_w - (double)image_w * state.zoom) / 2.0;
    state.pan_y = ((double)area_h - (double)image_h * state.zoom) / 2.0;
  }
}

static void set_fit_mode(gboolean fit) {
  state.fit_mode = fit;
  update_fit_zoom();
  queue_draw();
}

static gboolean copy_file_bytes(const char *source, const char *target, GError **error) {
  gchar *contents = NULL;
  gsize len = 0;
  if (!g_file_get_contents(source, &contents, &len, error)) return FALSE;
  gboolean ok = g_file_set_contents(target, contents, len, error);
  g_free(contents);
  return ok;
}

static gboolean publish_clipboard(const char *path) {
  gchar *quoted = g_shell_quote(path);
  gchar *command = g_strdup_printf("wl-copy --type image/png < %s", quoted);
  int status = 1;
  gboolean spawned = g_spawn_command_line_sync(command, NULL, NULL, &status, NULL);
  g_free(command);
  g_free(quoted);
  return spawned && status == 0;
}

static void on_copy_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  state.copied = publish_clipboard(state.path);
}

static void on_save_response(GtkNativeDialog *dialog, int response, gpointer data) {
  (void)data;
  if (response == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    GFile *file = gtk_file_chooser_get_file(chooser);
    if (file != NULL) {
      char *target = g_file_get_path(file);
      if (target != NULL) {
        GError *error = NULL;
        state.saved = copy_file_bytes(state.path, target, &error);
        if (error != NULL) g_error_free(error);
        g_free(target);
      }
      g_object_unref(file);
    }
  }
  g_object_unref(dialog);
}

static void on_save_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  GtkFileChooserNative *dialog = gtk_file_chooser_native_new(
    "Save Shaula Preview",
    GTK_WINDOW(state.window),
    GTK_FILE_CHOOSER_ACTION_SAVE,
    "Save",
    "Cancel"
  );
  if (state.path != NULL) {
    char *basename = g_path_get_basename(state.path);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), basename);
    g_free(basename);
  }
  g_signal_connect(dialog, "response", G_CALLBACK(on_save_response), NULL);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static void on_discard_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  if (state.app != NULL) g_application_quit(G_APPLICATION(state.app));
}

static void on_fit_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  set_fit_mode(TRUE);
}

static void on_actual_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  state.fit_mode = FALSE;
  state.zoom = 1.0;
  update_fit_zoom();
  int area_w = MAX(1, gtk_widget_get_width(state.area));
  int area_h = MAX(1, gtk_widget_get_height(state.area));
  int image_w = MAX(1, gdk_pixbuf_get_width(state.image));
  int image_h = MAX(1, gdk_pixbuf_get_height(state.image));
  state.pan_x = ((double)area_w - (double)image_w) / 2.0;
  state.pan_y = ((double)area_h - (double)image_h) / 2.0;
  queue_draw();
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
  (void)area;
  (void)data;
  update_fit_zoom();

  cairo_set_source_rgb(cr, 0.11, 0.11, 0.12);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  int tile = 18;
  for (int y = 0; y < height; y += tile) {
    for (int x = 0; x < width; x += tile) {
      gboolean alt = ((x / tile) + (y / tile)) % 2 == 0;
      cairo_set_source_rgb(cr, alt ? 0.16 : 0.13, alt ? 0.16 : 0.13, alt ? 0.17 : 0.14);
      cairo_rectangle(cr, x, y, tile, tile);
      cairo_fill(cr);
    }
  }

  if (state.image == NULL) return;

  int image_w = gdk_pixbuf_get_width(state.image);
  int image_h = gdk_pixbuf_get_height(state.image);
  double draw_w = (double)image_w * state.zoom;
  double draw_h = (double)image_h * state.zoom;

  cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
  cairo_rectangle(cr, state.pan_x + 2, state.pan_y + 4, draw_w, draw_h);
  cairo_fill(cr);

  cairo_save(cr);
  cairo_translate(cr, state.pan_x, state.pan_y);
  cairo_scale(cr, state.zoom, state.zoom);
  gdk_cairo_set_source_pixbuf(cr, state.image, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);

  cairo_set_source_rgba(cr, 1, 1, 1, 0.22);
  cairo_set_line_width(cr, 1);
  cairo_rectangle(cr, state.pan_x + 0.5, state.pan_y + 0.5, draw_w, draw_h);
  cairo_stroke(cr);
}

static gboolean on_scroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data) {
  (void)controller;
  (void)dx;
  (void)data;
  if (state.image == NULL) return TRUE;
  state.fit_mode = FALSE;

  double old_zoom = state.zoom;
  double next = dy < 0 ? old_zoom * 1.12 : old_zoom / 1.12;
  next = MAX(0.05, MIN(8.0, next));
  if (fabs(next - old_zoom) < 0.001) return TRUE;

  int area_w = MAX(1, gtk_widget_get_width(state.area));
  int area_h = MAX(1, gtk_widget_get_height(state.area));
  double cx = (double)area_w / 2.0;
  double cy = (double)area_h / 2.0;
  double image_cx = (cx - state.pan_x) / old_zoom;
  double image_cy = (cy - state.pan_y) / old_zoom;
  state.zoom = next;
  state.pan_x = cx - image_cx * state.zoom;
  state.pan_y = cy - image_cy * state.zoom;
  queue_draw();
  return TRUE;
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data) {
  (void)gesture;
  (void)data;
  state.dragging = TRUE;
  state.drag_start_x = x;
  state.drag_start_y = y;
  state.pan_origin_x = state.pan_x;
  state.pan_origin_y = state.pan_y;
  gtk_widget_set_cursor_from_name(state.area, "grabbing");
}

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
  (void)gesture;
  (void)data;
  state.fit_mode = FALSE;
  state.pan_x = state.pan_origin_x + dx;
  state.pan_y = state.pan_origin_y + dy;
  queue_draw();
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
  (void)gesture;
  (void)dx;
  (void)dy;
  (void)data;
  state.dragging = FALSE;
  gtk_widget_set_cursor_from_name(state.area, "grab");
}

static gboolean on_key(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType modifiers, gpointer data) {
  (void)controller;
  (void)keycode;
  (void)modifiers;
  (void)data;
  if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_q) {
    if (state.app != NULL) g_application_quit(G_APPLICATION(state.app));
    return TRUE;
  }
  if (keyval == GDK_KEY_0) {
    on_actual_clicked(NULL, NULL);
    return TRUE;
  }
  if (keyval == GDK_KEY_f) {
    set_fit_mode(TRUE);
    return TRUE;
  }
  if (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal) {
    state.fit_mode = FALSE;
    state.zoom = MIN(8.0, state.zoom * 1.12);
    queue_draw();
    return TRUE;
  }
  if (keyval == GDK_KEY_minus) {
    state.fit_mode = FALSE;
    state.zoom = MAX(0.05, state.zoom / 1.12);
    queue_draw();
    return TRUE;
  }
  return FALSE;
}

static GtkWidget *toolbar_button(const char *label, GCallback callback) {
  GtkWidget *button = gtk_button_new_with_label(label);
  g_signal_connect(button, "clicked", callback, NULL);
  return button;
}

static void on_activate(GtkApplication *app, gpointer data) {
  (void)data;
  state.app = app;
  GtkWidget *window = gtk_application_window_new(app);
  state.window = window;
  gtk_window_set_title(GTK_WINDOW(window), "Shaula Preview");
  gtk_window_set_default_size(GTK_WINDOW(window), PREVIEW_MIN_W, PREVIEW_MIN_H);
  gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top(toolbar, 8);
  gtk_widget_set_margin_bottom(toolbar, 8);
  gtk_widget_set_margin_start(toolbar, 10);
  gtk_widget_set_margin_end(toolbar, 10);

  gtk_box_append(GTK_BOX(toolbar), toolbar_button("Copy", G_CALLBACK(on_copy_clicked)));
  gtk_box_append(GTK_BOX(toolbar), toolbar_button("Save As", G_CALLBACK(on_save_clicked)));
  gtk_box_append(GTK_BOX(toolbar), toolbar_button("Fit", G_CALLBACK(on_fit_clicked)));
  gtk_box_append(GTK_BOX(toolbar), toolbar_button("100%", G_CALLBACK(on_actual_clicked)));
  gtk_box_append(GTK_BOX(toolbar), toolbar_button("Discard", G_CALLBACK(on_discard_clicked)));

  GtkWidget *area = gtk_drawing_area_new();
  state.area = area;
  gtk_widget_set_focusable(area, TRUE);
  gtk_widget_set_hexpand(area, TRUE);
  gtk_widget_set_vexpand(area, TRUE);
  gtk_widget_set_cursor_from_name(area, "grab");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);

  GtkGesture *drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
  g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
  g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), NULL);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

  GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), NULL);
  gtk_widget_add_controller(area, scroll);

  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), NULL);
  gtk_widget_add_controller(window, keys);

  gtk_box_append(GTK_BOX(root), toolbar);
  gtk_box_append(GTK_BOX(root), area);
  gtk_window_set_child(GTK_WINDOW(window), root);
  gtk_window_present(GTK_WINDOW(window));
  gtk_widget_grab_focus(area);
  set_fit_mode(TRUE);
}

int main(int argc, char **argv) {
  const char *path = argc >= 2 ? argv[1] : getenv("SHAULA_PREVIEW_PATH");
  if (path == NULL || path[0] == '\0') {
    fprintf(stderr, "shaula-preview requires an image path\n");
    return 2;
  }

  GError *error = NULL;
  memset(&state, 0, sizeof(state));
  state.path = g_strdup(path);
  state.image = gdk_pixbuf_new_from_file(path, &error);
  if (state.image == NULL) {
    if (error != NULL) {
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
    }
    g_free(state.path);
    return 43;
  }
  state.zoom = 1.0;
  state.fit_zoom = 1.0;
  state.fit_mode = TRUE;

  GtkApplication *app = gtk_application_new("dev.shaula.preview", G_APPLICATION_DEFAULT_FLAGS);
  if (app == NULL) {
    g_object_unref(state.image);
    g_free(state.path);
    return 44;
  }
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  int rc = g_application_run(G_APPLICATION(app), 0, NULL);
  g_object_unref(app);
  g_object_unref(state.image);
  g_free(state.path);
  return rc > 255 ? 255 : rc;
}
