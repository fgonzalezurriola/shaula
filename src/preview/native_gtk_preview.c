#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum {
  PREVIEW_MIN_W = 860,
  PREVIEW_MIN_H = 560,
  PREVIEW_CONTENT_INSET = 24,

};

typedef struct {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *area;
  GtkWidget *zoom_label;
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
  char *saved_path;
  const char *last_action;
  gboolean is_dark;
} ShaulaPreviewState;

static ShaulaPreviewState state;

static gboolean detect_dark_theme(void) {
  GtkSettings *settings = gtk_settings_get_default();
  if (settings == NULL)
    return TRUE;
  gchar *theme = NULL;
  g_object_get(settings, "gtk-theme-name", &theme, NULL);
  if (theme != NULL) {
    gboolean dark =
        (g_str_has_suffix(theme, "-dark") || g_str_has_suffix(theme, "-Dark"));
    g_free(theme);
    if (dark)
      return TRUE;
  }
  gint pref = 0;
  g_object_get(settings, "gtk-application-prefer-dark-theme", &pref, NULL);
  if (pref)
    return TRUE;
  return FALSE;
}

static void update_theme_state(void) { state.is_dark = detect_dark_theme(); }

static void queue_draw(void) {
  if (state.area != NULL)
    gtk_widget_queue_draw(state.area);
}

static void update_zoom_label(void) {
  if (state.zoom_label == NULL)
    return;
  int pct = (int)(state.zoom * 100.0 + 0.5);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d%% Zoom", pct);
  gtk_label_set_text(GTK_LABEL(state.zoom_label), buf);
}

static void get_preview_content_rect(int width, int height, int *x, int *y,
                                     int *content_w, int *content_h) {
  int inset = PREVIEW_CONTENT_INSET;
  if (x != NULL)
    *x = inset;
  if (y != NULL)
    *y = inset;
  if (content_w != NULL)
    *content_w = MAX(1, width - inset * 2);
  if (content_h != NULL)
    *content_h = MAX(1, height - inset * 2);
}

static void update_fit_zoom(void) {
  if (state.area == NULL || state.image == NULL)
    return;
  int area_w = MAX(1, gtk_widget_get_width(state.area));
  int area_h = MAX(1, gtk_widget_get_height(state.area));
  int image_w = MAX(1, gdk_pixbuf_get_width(state.image));
  int image_h = MAX(1, gdk_pixbuf_get_height(state.image));
  int content_x = 0;
  int content_y = 0;
  int content_w = 0;
  int content_h = 0;
  get_preview_content_rect(area_w, area_h, &content_x, &content_y, &content_w,
                           &content_h);
  double scale_x = (double)content_w / (double)image_w;
  double scale_y = (double)content_h / (double)image_h;
  state.fit_zoom = MIN(1.0, MAX(0.05, MIN(scale_x, scale_y)));
  if (state.fit_mode) {
    state.zoom = state.fit_zoom;
    state.pan_x = (double)content_x +
                  ((double)content_w - (double)image_w * state.zoom) / 2.0;
    state.pan_y = (double)content_y +
                  ((double)content_h - (double)image_h * state.zoom) / 2.0;
  }
}

static void set_fit_mode(gboolean fit) {
  state.fit_mode = fit;
  update_fit_zoom();
  update_zoom_label();
  queue_draw();
}

static gboolean copy_file_bytes(const char *source, const char *target,
                                GError **error) {
  gchar *contents = NULL;
  gsize len = 0;
  if (!g_file_get_contents(source, &contents, &len, error))
    return FALSE;
  gboolean ok = g_file_set_contents(target, contents, len, error);
  g_free(contents);
  return ok;
}

static gboolean publish_clipboard(const char *path) {
  gchar *quoted = g_shell_quote(path);
  gchar *command = g_strdup_printf("wl-copy --type image/png < %s", quoted);
  int status = 1;
  gboolean spawned =
      g_spawn_command_line_sync(command, NULL, NULL, &status, NULL);
  g_free(command);
  g_free(quoted);
  return spawned && status == 0;
}

static void on_copy_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  state.last_action = "copy";
  state.copied = publish_clipboard(state.path);
}

static void on_save_response(GtkNativeDialog *dialog, int response,
                             gpointer data) {
  (void)data;
  if (response == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    GFile *file = gtk_file_chooser_get_file(chooser);
    if (file != NULL) {
      char *target = g_file_get_path(file);
      if (target != NULL) {
        GError *error = NULL;
        state.saved = copy_file_bytes(state.path, target, &error);
        if (state.saved) {
          g_free(state.saved_path);
          state.saved_path = g_strdup(target);
          state.last_action = "save";
        }
        if (error != NULL)
          g_error_free(error);
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
      "Save Shaula Preview", GTK_WINDOW(state.window),
      GTK_FILE_CHOOSER_ACTION_SAVE, "Save", "Cancel");
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
  state.last_action = "discard";
  if (state.app != NULL)
    g_application_quit(G_APPLICATION(state.app));
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
  int content_x = 0;
  int content_y = 0;
  int content_w = 0;
  int content_h = 0;
  get_preview_content_rect(area_w, area_h, &content_x, &content_y, &content_w,
                           &content_h);
  state.pan_x = (double)content_x + ((double)content_w - (double)image_w) / 2.0;
  state.pan_y = (double)content_y + ((double)content_h - (double)image_h) / 2.0;
  update_zoom_label();
  queue_draw();
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer data) {
  (void)area;
  (void)data;
  update_theme_state();
  update_fit_zoom();

  int content_x = 0;
  int content_y = 0;
  int content_w = 0;
  int content_h = 0;
  get_preview_content_rect(width, height, &content_x, &content_y, &content_w,
                           &content_h);

  if (state.is_dark) {
    cairo_set_source_rgb(cr, 0.11, 0.11, 0.12);
  } else {
    cairo_set_source_rgb(cr, 0.93, 0.93, 0.93);
  }
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  int tile = 18;
  for (int y = 0; y < height; y += tile) {
    for (int x = 0; x < width; x += tile) {
      gboolean alt = ((x / tile) + (y / tile)) % 2 == 0;
      if (state.is_dark) {
        cairo_set_source_rgb(cr, alt ? 0.16 : 0.13, alt ? 0.16 : 0.13,
                             alt ? 0.17 : 0.14);
      } else {
        cairo_set_source_rgb(cr, alt ? 0.90 : 0.87, alt ? 0.90 : 0.87,
                             alt ? 0.91 : 0.88);
      }
      cairo_rectangle(cr, x, y, tile, tile);
      cairo_fill(cr);
    }
  }

  if (state.image == NULL)
    return;

  int image_w = gdk_pixbuf_get_width(state.image);
  int image_h = gdk_pixbuf_get_height(state.image);
  double draw_w = (double)image_w * state.zoom;
  double draw_h = (double)image_h * state.zoom;

  if (state.is_dark) {
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  } else {
    cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
  }
  cairo_rectangle(cr, state.pan_x + 2, state.pan_y + 4, draw_w, draw_h);
  cairo_fill(cr);

  cairo_save(cr);
  cairo_translate(cr, state.pan_x, state.pan_y);
  cairo_scale(cr, state.zoom, state.zoom);
  gdk_cairo_set_source_pixbuf(cr, state.image, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);

  if (state.is_dark) {
    cairo_set_source_rgba(cr, 1, 1, 1, 0.14);
  } else {
    cairo_set_source_rgba(cr, 0, 0, 0, 0.12);
  }
  cairo_set_line_width(cr, 1);
  cairo_rectangle(cr, state.pan_x + 0.5, state.pan_y + 0.5, draw_w, draw_h);
  cairo_stroke(cr);
}

static gboolean on_scroll(GtkEventControllerScroll *controller, double dx,
                          double dy, gpointer data) {
  (void)controller;
  (void)dx;
  (void)data;
  if (state.image == NULL)
    return TRUE;
  state.fit_mode = FALSE;

  double old_zoom = state.zoom;
  double next = dy < 0 ? old_zoom * 1.12 : old_zoom / 1.12;
  next = MAX(0.05, MIN(8.0, next));
  if (fabs(next - old_zoom) < 0.001)
    return TRUE;

  int area_w = MAX(1, gtk_widget_get_width(state.area));
  int area_h = MAX(1, gtk_widget_get_height(state.area));
  int content_x = 0;
  int content_y = 0;
  int content_w = 0;
  int content_h = 0;
  get_preview_content_rect(area_w, area_h, &content_x, &content_y, &content_w,
                           &content_h);
  double cx = (double)content_x + (double)content_w / 2.0;
  double cy = (double)content_y + (double)content_h / 2.0;
  double image_cx = (cx - state.pan_x) / old_zoom;
  double image_cy = (cy - state.pan_y) / old_zoom;
  state.zoom = next;
  state.pan_x = cx - image_cx * state.zoom;
  state.pan_y = cy - image_cy * state.zoom;
  update_zoom_label();
  queue_draw();
  return TRUE;
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y,
                          gpointer data) {
  (void)gesture;
  (void)data;
  state.dragging = TRUE;
  state.drag_start_x = x;
  state.drag_start_y = y;
  state.pan_origin_x = state.pan_x;
  state.pan_origin_y = state.pan_y;
  gtk_widget_set_cursor_from_name(state.area, "grabbing");
}

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy,
                           gpointer data) {
  (void)gesture;
  (void)data;
  state.fit_mode = FALSE;
  state.pan_x = state.pan_origin_x + dx;
  state.pan_y = state.pan_origin_y + dy;
  queue_draw();
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                        gpointer data) {
  (void)gesture;
  (void)dx;
  (void)dy;
  (void)data;
  state.dragging = FALSE;
  gtk_widget_set_cursor_from_name(state.area, "grab");
}

static gboolean on_key(GtkEventControllerKey *controller, guint keyval,
                       guint keycode, GdkModifierType modifiers,
                       gpointer data) {
  (void)controller;
  (void)keycode;
  (void)modifiers;
  (void)data;
  if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_q) {
    if (state.last_action == NULL)
      state.last_action = "close";
    if (state.app != NULL)
      g_application_quit(G_APPLICATION(state.app));
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
    update_zoom_label();
    queue_draw();
    return TRUE;
  }
  if (keyval == GDK_KEY_minus) {
    state.fit_mode = FALSE;
    state.zoom = MAX(0.05, state.zoom / 1.12);
    update_zoom_label();
    queue_draw();
    return TRUE;
  }
  if (keyval == GDK_KEY_c && (modifiers & GDK_CONTROL_MASK)) {
    on_copy_clicked(NULL, NULL);
    return TRUE;
  }
  if (keyval == GDK_KEY_s && (modifiers & GDK_CONTROL_MASK)) {
    on_save_clicked(NULL, NULL);
    return TRUE;
  }
  return FALSE;
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

static void on_noop_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
}

static GtkWidget *make_toolbar_button(const char *icon_name,
                                      const char *tooltip, GCallback callback) {
  GtkWidget *button = gtk_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(button), icon_name);
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  g_signal_connect(button, "clicked", callback, NULL);
  return button;
}

static GtkWidget *make_toolbar_toggle_button(const char *icon_name,
                                             const char *tooltip,
                                             gboolean active,
                                             GCallback callback) {
  GtkWidget *button = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(button), icon_name);
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), active);
  g_signal_connect(button, "clicked", callback, NULL);
  return button;
}

static GtkWidget *make_dot_separator(void) {
  GtkWidget *label = gtk_label_new("·");
  gtk_widget_add_css_class(label, "dim-label");
  gtk_widget_set_margin_start(label, 6);
  gtk_widget_set_margin_end(label, 6);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  return label;
}

static GtkWidget *make_muted_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_widget_add_css_class(label, "dim-label");
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  return label;
}

static GtkWidget *make_normal_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  return label;
}

static void draw_swatch(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                        gpointer data) {
  (void)area;
  (void)data;
  double rw = (double)w;
  double rh = (double)h;
  double r = 4.0;
  cairo_new_sub_path(cr);
  cairo_arc(cr, r, r, r, G_PI, 1.5 * G_PI);
  cairo_arc(cr, rw - r, r, r, 1.5 * G_PI, 2.0 * G_PI);
  cairo_arc(cr, rw - r, rh - r, r, 0, 0.5 * G_PI);
  cairo_arc(cr, r, rh - r, r, 0.5 * G_PI, G_PI);
  cairo_close_path(cr);
  /* #2A4A66 */
  cairo_set_source_rgb(cr, 0.165, 0.290, 0.400);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.15);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
}

static GtkWidget *build_tool_group(void) {
  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);

  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("edit-copy-symbolic", "Copy (Ctrl+C)",
                                     G_CALLBACK(on_copy_clicked)));
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("document-save-as-symbolic",
                                     "Save As (Ctrl+S)",
                                     G_CALLBACK(on_save_clicked)));
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("view-pin-symbolic", "Pin",
                                     G_CALLBACK(on_noop_clicked)));
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("send-to-symbolic", "Share",
                                     G_CALLBACK(on_noop_clicked)));

  gtk_box_append(GTK_BOX(actions), make_dot_separator());

  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("image-crop-symbolic", "Crop",
                                     G_CALLBACK(on_noop_clicked)));
  GtkWidget *select_btn = make_toolbar_toggle_button(
      "edit-select-symbolic", "Select", TRUE, G_CALLBACK(on_noop_clicked));
  gtk_box_append(GTK_BOX(actions), select_btn);
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("mail-forward-symbolic", "Arrow",
                                     G_CALLBACK(on_noop_clicked)));
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("font-x-generic-symbolic", "Text",
                                     G_CALLBACK(on_noop_clicked)));
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("preferences-desktop-display-symbolic",
                                     "Measure", G_CALLBACK(on_noop_clicked)));
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("window-new-symbolic", "Rectangle",
                                     G_CALLBACK(on_noop_clicked)));
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("object-select-symbolic", "Highlight",
                                     G_CALLBACK(on_noop_clicked)));
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("document-edit-symbolic", "Pen",
                                     G_CALLBACK(on_noop_clicked)));

  gtk_box_append(GTK_BOX(actions), make_dot_separator());

  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button("view-more-symbolic", "More",
                                     G_CALLBACK(on_noop_clicked)));

  return actions;
}

static GtkWidget *build_metadata_group(void) {
  GtkWidget *metadata = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_valign(metadata, GTK_ALIGN_CENTER);

  GtkWidget *swatch = gtk_drawing_area_new();
  gtk_widget_set_size_request(swatch, 16, 16);
  gtk_widget_set_valign(swatch, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_end(swatch, 4);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch), draw_swatch, NULL,
                                 NULL);
  gtk_box_append(GTK_BOX(metadata), swatch);

  gtk_box_append(GTK_BOX(metadata), make_normal_label("#2A4A66"));
  gtk_box_append(GTK_BOX(metadata), make_dot_separator());
  gtk_box_append(GTK_BOX(metadata), make_muted_label("Tab to copy"));

  gtk_box_append(GTK_BOX(metadata), make_dot_separator());

  if (state.image != NULL) {
    char size_buf[32];
    snprintf(size_buf, sizeof(size_buf), "%d\xc3\x97%d px",
             gdk_pixbuf_get_width(state.image),
             gdk_pixbuf_get_height(state.image));
    gtk_box_append(GTK_BOX(metadata), make_muted_label(size_buf));
  } else {
    gtk_box_append(GTK_BOX(metadata), make_muted_label("840\xc3\x97"
                                                       "582 px"));
  }

  gtk_box_append(GTK_BOX(metadata), make_dot_separator());

  state.zoom_label = make_muted_label("100% Zoom");
  gtk_box_append(GTK_BOX(metadata), state.zoom_label);
  gtk_widget_set_margin_end(metadata, 8);

  GtkWidget *discard_btn = make_toolbar_button(
      "edit-delete-symbolic", "Discard (Esc)", G_CALLBACK(on_discard_clicked));
  gtk_box_append(GTK_BOX(metadata), discard_btn);

  return metadata;
}

static GtkWidget *build_topbar(void) {
  GtkWidget *overlay = gtk_overlay_new();

  GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_size_request(main_box, -1, 46);

  GtkWidget *toolbar = build_tool_group();
  gtk_widget_set_halign(toolbar, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(toolbar, GTK_ALIGN_CENTER);

  GtkWidget *right_group = build_metadata_group();
  gtk_widget_set_halign(right_group, GTK_ALIGN_END);
  gtk_widget_set_valign(right_group, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_end(right_group, 6);

  gtk_overlay_set_child(GTK_OVERLAY(overlay), main_box);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), toolbar);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), right_group);

  return overlay;
}

static void on_map(GtkWidget *widget, gpointer data) {
  (void)widget;
  (void)data;
  update_theme_state();
  update_zoom_label();
}

static void on_activate(GtkApplication *app, gpointer data) {
  (void)data;
  state.app = app;
  GtkWidget *window = gtk_application_window_new(app);
  state.window = window;
  gtk_window_set_title(GTK_WINDOW(window), "Shaula Preview");
  gtk_window_set_default_size(GTK_WINDOW(window), PREVIEW_MIN_W, PREVIEW_MIN_H);
  gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

  GtkWidget *area = gtk_drawing_area_new();
  state.area = area;
  gtk_widget_set_focusable(area, TRUE);
  gtk_widget_set_hexpand(area, TRUE);
  gtk_widget_set_vexpand(area, TRUE);
  gtk_widget_set_cursor_from_name(area, "grab");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);

  GtkWidget *main_overlay = gtk_overlay_new();
  gtk_overlay_set_child(GTK_OVERLAY(main_overlay), area);

  GtkWidget *topbar = build_topbar();
  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(root, TRUE);
  gtk_widget_set_vexpand(root, TRUE);
  gtk_box_append(GTK_BOX(root), topbar);
  gtk_box_append(GTK_BOX(root), main_overlay);
  gtk_widget_set_hexpand(main_overlay, TRUE);
  gtk_widget_set_vexpand(main_overlay, TRUE);

  gtk_window_set_child(GTK_WINDOW(window), root);

  GtkGesture *drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
  g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
  g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), NULL);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

  GtkEventController *scroll =
      gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), NULL);
  gtk_widget_add_controller(area, scroll);

  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), NULL);
  gtk_widget_add_controller(window, keys);

  g_signal_connect(window, "map", G_CALLBACK(on_map), NULL);

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
  state.last_action = "close";
  state.is_dark = TRUE;

  GtkApplication *app =
      gtk_application_new("dev.shaula.preview", G_APPLICATION_DEFAULT_FLAGS);
  if (app == NULL) {
    g_object_unref(state.image);
    g_free(state.path);
    return 44;
  }
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  int rc = g_application_run(G_APPLICATION(app), 0, NULL);
  if (rc == 0)
    emit_preview_result();
  g_object_unref(app);
  g_object_unref(state.image);
  g_free(state.saved_path);
  g_free(state.path);
  return rc > 255 ? 255 : rc;
}
