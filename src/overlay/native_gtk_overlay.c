#include <gtk/gtk.h>

#include "overlay_selection.h"
#include "overlay_selection_session.h"
#include "overlay_protocol.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk4-layer-shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>

enum {
  TOOLBAR_W = 280,
  TOOLBAR_H = 40,
  PADDING = 12,
  HANDLE_CLEARANCE = 18,
  BADGE_CLEARANCE = 30,
  JITTER = 6,
  PILL_RADIUS = 8,
  TOOLBAR_RADIUS = 14,
  BADGE_RADIUS = 8,
  DROPDOWN_ITEM_H = 32,
  DROPDOWN_PADDING = 6,
  DROPDOWN_RADIUS = 10,
};

typedef enum {
  ASPECT_FREE,
  ASPECT_1_1,
  ASPECT_16_9,
  ASPECT_4_3,
  ASPECT_3_2,
  ASPECT_16_10,
  ASPECT_9_16,
  ASPECT_4_5,
  ASPECT_3_4,
  ASPECT_COUNT,
} ShaulaAspectChoice;

static const char *ASPECT_LABELS[ASPECT_COUNT] = {
    "Free",          "1:1 Square",   "16:9 Widescreen",
    "4:3 Standard",  "3:2 Photo",    "16:10 Desktop",
    "9:16 Vertical", "4:5 Portrait", "3:4 Phone photo"};

static const int ASPECT_WIDTHS[ASPECT_COUNT] = {0, 1, 16, 4, 3, 16, 9, 4, 3};
static const int ASPECT_HEIGHTS[ASPECT_COUNT] = {0,  1,  9, 3, 2,
                                                 10, 16, 5, 4};

typedef enum {
  INTERACTION_QUICK,
  INTERACTION_AREA,
} ShaulaInteractionMode;

typedef struct {
  GMainLoop *main_loop;
  GtkWidget *window;
  GtkWidget *area;
  GdkPixbuf *background;
  cairo_surface_t *background_surface;
  int background_surface_width;
  int background_surface_height;
  ShaulaPoint output_origin;
  ShaulaOverlaySelectionSession *selection_session;
  ShaulaOverlayLaunch launch;
  ShaulaOverlaySelectionView selection_view;
  gboolean has_toolbar;
  ShaulaInteractionMode interaction_mode;
  ShaulaPoint toolbar;
  gboolean dropdown_open;
  ShaulaAspectChoice aspect_choice;
  char aspect_output_label[32];
  gboolean suppress_pointer_drag;
  ShaulaOverlayCursor cursor_shape;
  /* GTK widget toolbar */
  GtkWidget *overlay_container;
  GtkWidget *toolbar_box;
  GtkWidget *aspect_button;
  GtkWidget *aspect_label;
  GtkWidget *capture_button;
  GtkWidget *cancel_button;
  GtkWidget *aspect_popover;
  GtkWidget *aspect_list_box;
} ShaulaOverlayState;

static ShaulaOverlayState state;

int shaula_native_gtk_overlay_run(void);

static GdkMonitor *monitor_for_output(void);
static ShaulaPoint initial_surface_size(void);
static void reposition_toolbar_widget(void);
static void update_aspect_label(void);
static void prefer_fast_overlay_renderer(void);
static void setup_overlay_window(void);
static void clear_background_surface(void);

static void emit_overlay_error(const char *message) {
  ShaulaOverlayOutcome outcome;
  shaula_overlay_outcome_init(&outcome);
  shaula_overlay_outcome_set_error(&outcome, "ERR_OVERLAY_UNAVAILABLE",
                                   message);
  g_autofree char *json = shaula_overlay_outcome_json_new(&outcome);
  printf("%s\n", json);
  fflush(stdout);
  shaula_overlay_outcome_clear(&outcome);
}

static void clear_overlay_state(void) {
  clear_background_surface();
  g_clear_object(&state.background);
  g_clear_pointer(&state.selection_session,
                  shaula_overlay_selection_session_free);
  shaula_overlay_launch_clear(&state.launch);
}

static gboolean capture_on_release(void) {
  return state.interaction_mode == INTERACTION_QUICK;
}

static ShaulaPoint current_output_origin(void) {
  GdkMonitor *monitor = monitor_for_output();
  if (monitor != NULL) {
    GdkRectangle rect;
    gdk_monitor_get_geometry(monitor, &rect);
    g_object_unref(monitor);
    return (ShaulaPoint){.x = rect.x, .y = rect.y};
  }
  return (ShaulaPoint){.x = 0, .y = 0};
}

static void install_transparent_overlay_css(void) {
  GtkCssProvider *provider = gtk_css_provider_new();
  const char *css = ".shaula-overlay-window, .shaula-overlay-area {"
                    "  background: transparent;"
                    "  background-color: transparent;"
                    "}"
                    ".shaula-overlay-toolbar {"
                    "  background: alpha(@theme_bg_color, 0.92);"
                    "  border: 1px solid alpha(@borders, 0.6);"
                    "  border-radius: 12px;"
                    "  padding: 4px 6px;"
                    "  box-shadow: 0 2px 8px alpha(black, 0.3);"
                    "}"
                    ".shaula-overlay-toolbar button {"
                    "  color: @theme_fg_color;"
                    "  border-radius: 8px;"
                    "  padding: 4px 12px;"
                    "  min-height: 24px;"
                    "}"
                    ".shaula-overlay-toolbar button.flat:hover {"
                    "  background: alpha(@theme_fg_color, 0.08);"
                    "}"
                    ".shaula-overlay-toolbar .shaula-capture-btn {"
                    "  background: alpha(@accent_bg_color, 0.85);"
                    "  color: @accent_fg_color;"
                    "  font-weight: bold;"
                    "  border-radius: 8px;"
                    "}"
                    ".shaula-overlay-toolbar .shaula-capture-btn:hover {"
                    "  background: @accent_bg_color;"
                    "}"
                    ".shaula-overlay-toolbar .shaula-capture-btn:disabled {"
                    "  background: alpha(@theme_fg_color, 0.10);"
                    "  color: alpha(@theme_fg_color, 0.45);"
                    "}"
                    ".shaula-overlay-toolbar .shaula-cancel-btn {"
                    "  background: alpha(#8f3f46, 0.80);"
                    "  color: white;"
                    "  font-weight: bold;"
                    "  border-radius: 8px;"
                    "}"
                    ".shaula-overlay-toolbar .shaula-cancel-btn:hover {"
                    "  background: #8f3f46;"
                    "}"
                    ".shaula-overlay-toolbar .shaula-aspect-btn {"
                    "  color: @theme_fg_color;"
                    "}"
                    ".shaula-overlay-toolbar .shaula-aspect-btn:hover {"
                    "  background: alpha(@theme_fg_color, 0.08);"
                    "}"
                    ".shaula-overlay-aspect-popover contents {"
                    "  background: alpha(@theme_bg_color, 0.95);"
                    "  color: @theme_fg_color;"
                    "  border: 1px solid @borders;"
                    "  border-radius: 10px;"
                    "  padding: 4px;"
                    "}"
                    ".shaula-overlay-aspect-popover .aspect-row {"
                    "  padding: 6px 16px;"
                    "  border-radius: 6px;"
                    "  min-height: 24px;"
                    "}"
                    ".shaula-overlay-aspect-popover .aspect-row:hover {"
                    "  background: alpha(@theme_fg_color, 0.08);"
                    "}"
                    ".shaula-overlay-aspect-popover .aspect-row:selected,"
                    ".shaula-overlay-aspect-popover .aspect-row.selected {"
                    "  background: alpha(@accent_bg_color, 0.25);"
                    "}";
  gtk_css_provider_load_from_data(provider, css, -1);
  GdkDisplay *display = gdk_display_get_default();
  if (display != NULL) {
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  g_object_unref(provider);
}

static void prefer_fast_overlay_renderer(void) {
  /* The overlay draws its critical first frame with Cairo and simple GTK
   * widgets. Prefer GTK's Cairo renderer to avoid GL/Vulkan startup before
   * the layer-shell surface is visible, while preserving explicit user
   * overrides for renderer debugging.
   */
  if (g_getenv("GSK_RENDERER") == NULL) {
    g_setenv("GSK_RENDERER", "cairo", FALSE);
  }
}

static int clamp_int(int value, int low, int high) {
  if (high < low)
    return low;
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                         double r) {
  r = fmin(r, fmin(w, h) / 2.0);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + r, y + r, r, G_PI, 1.5 * G_PI);
  cairo_arc(cr, x + w - r, y + r, r, 1.5 * G_PI, 2.0 * G_PI);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * G_PI);
  cairo_arc(cr, x + r, y + h - r, r, 0.5 * G_PI, G_PI);
  cairo_close_path(cr);
}

static ShaulaPoint output_size(void) {
  if (state.area != NULL) {
    int width = gtk_widget_get_width(state.area);
    int height = gtk_widget_get_height(state.area);
    if (width > 1 && height > 1) {
      return (ShaulaPoint){.x = width, .y = height};
    }
  }
  return initial_surface_size();
}

static void sync_selection_view(void) {
  state.selection_view =
      *shaula_overlay_selection_session_view(state.selection_session);
}

static void sync_selection_bounds(void) {
  shaula_overlay_selection_session_set_bounds(state.selection_session,
                                              output_size());
  sync_selection_view();
}

static void queue_draw(void) {
  if (state.area != NULL)
    gtk_widget_queue_draw(state.area);
}

static void clear_background_surface(void) {
  if (state.background_surface != NULL) {
    cairo_surface_destroy(state.background_surface);
    state.background_surface = NULL;
  }
  state.background_surface_width = 0;
  state.background_surface_height = 0;
}

static const char *cursor_name(ShaulaOverlayCursor shape) {
  switch (shape) {
  case SHAULA_OVERLAY_CURSOR_CROSSHAIR:
    return "crosshair";
  case SHAULA_OVERLAY_CURSOR_GRAB:
    return "grab";
  case SHAULA_OVERLAY_CURSOR_GRABBING:
    return "grabbing";
  case SHAULA_OVERLAY_CURSOR_RESIZE_EW:
    return "ew-resize";
  case SHAULA_OVERLAY_CURSOR_RESIZE_NS:
    return "ns-resize";
  case SHAULA_OVERLAY_CURSOR_RESIZE_NWSE:
    return "nwse-resize";
  case SHAULA_OVERLAY_CURSOR_RESIZE_NESW:
    return "nesw-resize";
  case SHAULA_OVERLAY_CURSOR_DEFAULT:
  default:
    return NULL;
  }
}

static int handle_index(ShaulaResizeHandle handle) {
  switch (handle) {
  case HANDLE_TOP_LEFT:
    return 0;
  case HANDLE_TOP:
    return 1;
  case HANDLE_TOP_RIGHT:
    return 2;
  case HANDLE_RIGHT:
    return 3;
  case HANDLE_BOTTOM_RIGHT:
    return 4;
  case HANDLE_BOTTOM:
    return 5;
  case HANDLE_BOTTOM_LEFT:
    return 6;
  case HANDLE_LEFT:
    return 7;
  case HANDLE_NONE:
  default:
    return -1;
  }
}

static void apply_cursor(ShaulaOverlayCursor shape) {
  if (state.area == NULL || state.cursor_shape == shape)
    return;
  gtk_widget_set_cursor_from_name(state.area, cursor_name(shape));
  state.cursor_shape = shape;
}

static void update_toolbar(void) {
  if (!state.selection_view.has_selection)
    return;
  ShaulaRect selection = state.selection_view.selection;
  ShaulaPoint bounds = output_size();
  int min_x = PADDING;
  int max_x = bounds.x - PADDING - TOOLBAR_W;
  int min_y = PADDING;
  int max_y = bounds.y - PADDING - TOOLBAR_H;
  int centered_x = selection.x + (selection.width - TOOLBAR_W) / 2;
  int below_y = selection.y + selection.height + HANDLE_CLEARANCE;
  int above_y = selection.y - TOOLBAR_H - HANDLE_CLEARANCE - BADGE_CLEARANCE;
  ShaulaPoint candidate;

  if (below_y <= max_y) {
    candidate =
        (ShaulaPoint){.x = clamp_int(centered_x, min_x, max_x), .y = below_y};
  } else if (above_y >= min_y) {
    candidate =
        (ShaulaPoint){.x = clamp_int(centered_x, min_x, max_x), .y = above_y};
  } else {
    candidate = (ShaulaPoint){
        .x = clamp_int((bounds.x - TOOLBAR_W) / 2, min_x, max_x),
        .y = bounds.y - (selection.y + selection.height) >= selection.y ? max_y
                                                                        : min_y,
    };
  }

  candidate.x = clamp_int(candidate.x, min_x, max_x);
  candidate.y = clamp_int(candidate.y, min_y, max_y);
  if (state.has_toolbar && abs(state.toolbar.x - candidate.x) <= JITTER &&
      abs(state.toolbar.y - candidate.y) <= JITTER) {
    return;
  }
  state.toolbar = candidate;
  state.has_toolbar = TRUE;
  reposition_toolbar_widget();
}

static void apply_aspect_choice(void) {
  sync_selection_bounds();
  gboolean enabled = state.aspect_choice != ASPECT_FREE;
  ShaulaAspect aspect = {0};
  if (enabled) {
    aspect = (ShaulaAspect){
        .width = ASPECT_WIDTHS[(int)state.aspect_choice],
        .height = ASPECT_HEIGHTS[(int)state.aspect_choice],
    };
    snprintf(state.aspect_output_label, sizeof(state.aspect_output_label),
             "%d:%d", aspect.width, aspect.height);
  } else {
    state.aspect_output_label[0] = '\0';
  }
  (void)shaula_overlay_selection_session_set_aspect(
      state.selection_session, enabled, aspect, FALSE);
  sync_selection_view();
  update_toolbar();
  state.dropdown_open = FALSE;
  if (state.aspect_popover != NULL)
    gtk_popover_popdown(GTK_POPOVER(state.aspect_popover));
}

static gboolean ensure_background_surface(int width, int height) {
  if (state.background == NULL)
    return FALSE;
  if (width <= 0 || height <= 0)
    return FALSE;
  if (state.background_surface != NULL &&
      state.background_surface_width == width &&
      state.background_surface_height == height) {
    return TRUE;
  }

  clear_background_surface();
  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return FALSE;
  }

  cairo_t *surface_cr = cairo_create(surface);
  int source_width = MAX(1, gdk_pixbuf_get_width(state.background));
  int source_height = MAX(1, gdk_pixbuf_get_height(state.background));
  cairo_scale(surface_cr, (double)width / (double)source_width,
              (double)height / (double)source_height);
  gdk_cairo_set_source_pixbuf(surface_cr, state.background, 0, 0);
  cairo_paint(surface_cr);
  cairo_destroy(surface_cr);

  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return FALSE;
  }

  state.background_surface = surface;
  state.background_surface_width = width;
  state.background_surface_height = height;
  return TRUE;
}

static void draw_background(cairo_t *cr, int width, int height) {
  /* Pointer resize can redraw dozens of times per second. Cache the scaled
   * background per canvas size so the overlay contract stays interactive
   * instead of re-sampling the full screenshot on every motion frame.
   */
  if (!ensure_background_surface(width, height))
    return;
  cairo_set_source_surface(cr, state.background_surface, 0, 0);
  cairo_paint(cr);
}

static void draw_selection_guides(cairo_t *cr, ShaulaRect s, int width,
                                  int height) {
  double dash[] = {5.0, 7.0};
  double left = (double)s.x + 0.5;
  double right = (double)(s.x + s.width) + 0.5;
  double top = (double)s.y + 0.5;
  double bottom = (double)(s.y + s.height) + 0.5;

  cairo_save(cr);
  cairo_set_dash(cr, dash, 2, 0);
  cairo_set_line_width(cr, 1);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.20);

  cairo_move_to(cr, left, 0);
  cairo_line_to(cr, left, height);
  cairo_move_to(cr, right, 0);
  cairo_line_to(cr, right, height);
  cairo_move_to(cr, 0, top);
  cairo_line_to(cr, width, top);
  cairo_move_to(cr, 0, bottom);
  cairo_line_to(cr, width, bottom);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_handles(cairo_t *cr, ShaulaRect s) {
  double hw = 6;
  double hh = 6;
  ShaulaResizeHandle emphasized =
      state.selection_view.drag_mode == SHAULA_OVERLAY_DRAG_RESIZE
          ? state.selection_view.active_handle
          : state.selection_view.hover_handle;
  int emphasized_index = handle_index(emphasized);
  ShaulaPoint points[8] = {
      {s.x, s.y},
      {s.x + s.width / 2, s.y},
      {s.x + s.width, s.y},
      {s.x + s.width, s.y + s.height / 2},
      {s.x + s.width, s.y + s.height},
      {s.x + s.width / 2, s.y + s.height},
      {s.x, s.y + s.height},
      {s.x, s.y + s.height / 2},
  };
  for (int i = 0; i < 8; i += 1) {
    double px = (double)points[i].x;
    double py = (double)points[i].y;
    double scale = i == emphasized_index ? 1.45 : 1.0;
    double handle_w = hw * scale;
    double handle_h = hh * scale;
    rounded_rect(cr, px - handle_w / 2, py - handle_h / 2, handle_w, handle_h,
                 2.5);
    if (i == emphasized_index) {
      cairo_set_source_rgba(cr, 0.30, 0.62, 1.0, 0.96);
    } else {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    }
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, i == emphasized_index ? 0.45 : 0.3);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
  }
}

static void draw_badge(cairo_t *cr, ShaulaRect s, int width, int height) {
  char label[48];
  snprintf(label, sizeof(label), "x %d y %d  %d x %d", s.x, s.y, s.width,
           s.height);
  double badge_h = 26;

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, label, &extents);

  double badge_w = MAX(126.0, extents.width + 22.0);
  double badge_x = (double)clamp_int(s.x, 8, MAX(8, width - (int)badge_w - 8));
  double badge_y =
      s.y >= 36 ? (double)(s.y - 32)
                : (double)MIN(height - (int)badge_h - 8, s.y + s.height + 8);

  rounded_rect(cr, badge_x, badge_y, badge_w, badge_h, BADGE_RADIUS);
  cairo_set_source_rgba(cr, 0.06, 0.06, 0.08, 0.82);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
  cairo_set_line_width(cr, 0.5);
  cairo_stroke(cr);

  double text_x = badge_x + (badge_w - extents.width) / 2 - extents.x_bearing;
  double text_y = badge_y + (badge_h - extents.height) / 2 - extents.y_bearing;
  cairo_move_to(cr, text_x, text_y);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
  cairo_show_text(cr, label);
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer data) {
  (void)area;
  (void)data;
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_restore(cr);

  draw_background(cr, width, height);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.42);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  if (state.selection_view.has_selection) {
    ShaulaRect s = state.selection_view.selection;
    if (state.background != NULL) {
      cairo_save(cr);
      cairo_rectangle(cr, s.x, s.y, s.width, s.height);
      cairo_clip(cr);
      draw_background(cr, width, height);
      cairo_restore(cr);
    }
    draw_selection_guides(cr, s, width, height);
    rounded_rect(cr, (double)s.x, (double)s.y, (double)s.width,
                 (double)s.height, 2);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
    draw_handles(cr, s);
    draw_badge(cr, s, width, height);
  }
}

static void confirm_with_action(const char *action) {
  if (!state.selection_view.confirmable)
    return;
  ShaulaRect selection = state.selection_view.selection;
  const char *aspect =
      state.selection_view.has_aspect ? state.aspect_output_label : "Free";
  ShaulaPoint bounds = initial_surface_size();
  ShaulaOverlayAction outcome_action = SHAULA_OVERLAY_ACTION_CAPTURE;
  if (g_str_equal(action, "copy"))
    outcome_action = SHAULA_OVERLAY_ACTION_COPY;
  else if (g_str_equal(action, "save"))
    outcome_action = SHAULA_OVERLAY_ACTION_SAVE;
  ShaulaOverlayOutcome outcome;
  shaula_overlay_outcome_init(&outcome);
  shaula_overlay_outcome_set_success(
      &outcome, outcome_action, aspect,
      (ShaulaRect){.x = selection.x + state.output_origin.x,
                   .y = selection.y + state.output_origin.y,
                   .width = selection.width,
                   .height = selection.height},
      selection,
      (ShaulaRect){.x = state.output_origin.x,
                   .y = state.output_origin.y,
                   .width = bounds.x,
                   .height = bounds.y},
      state.launch.output_name);
  g_autofree char *json = shaula_overlay_outcome_json_new(&outcome);
  printf("%s\n", json);
  shaula_overlay_outcome_clear(&outcome);
  fflush(stdout);
  if (state.main_loop != NULL)
    g_main_loop_quit(state.main_loop);
}

static void confirm(void) { confirm_with_action("capture"); }

static void copy_now(void) { confirm_with_action("copy"); }

static void save_now(void) { confirm_with_action("save"); }

static void cancel(void) {
  ShaulaOverlayOutcome outcome;
  shaula_overlay_outcome_init(&outcome);
  if (state.selection_view.confirmable) {
    ShaulaRect selection = state.selection_view.selection;
    ShaulaRect global = {.x = selection.x + state.output_origin.x,
                         .y = selection.y + state.output_origin.y,
                         .width = selection.width,
                         .height = selection.height};
    shaula_overlay_outcome_set_cancel(&outcome, &global);
  } else {
    shaula_overlay_outcome_set_cancel(&outcome, NULL);
  }
  g_autofree char *json = shaula_overlay_outcome_json_new(&outcome);
  printf("%s\n", json);
  shaula_overlay_outcome_clear(&outcome);
  fflush(stdout);
  if (state.main_loop != NULL)
    g_main_loop_quit(state.main_loop);
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y,
                          gpointer data) {
  (void)gesture;
  (void)data;
  sync_selection_bounds();
  shaula_overlay_selection_session_begin(
      state.selection_session, (ShaulaPoint){.x = (int)x, .y = (int)y},
      !capture_on_release() && state.suppress_pointer_drag);
  sync_selection_view();
  apply_cursor(state.selection_view.cursor);
  queue_draw();
}

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy,
                           gpointer data) {
  (void)gesture;
  (void)data;
  sync_selection_bounds();
  (void)shaula_overlay_selection_session_update(
      state.selection_session, (int)dx, (int)dy);
  sync_selection_view();
  apply_cursor(state.selection_view.cursor);
  update_toolbar();
  queue_draw();
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                        gpointer data) {
  (void)gesture;
  (void)data;
  sync_selection_bounds();
  gboolean should_confirm = shaula_overlay_selection_session_end(
      state.selection_session, (int)dx, (int)dy, capture_on_release());
  state.suppress_pointer_drag = FALSE;
  sync_selection_view();
  apply_cursor(state.selection_view.cursor);
  update_toolbar();
  queue_draw();
  if (should_confirm)
    confirm();
}

static void on_motion(GtkEventControllerMotion *controller, double x, double y,
                      gpointer data) {
  (void)controller;
  (void)data;
  gboolean changed = shaula_overlay_selection_session_motion(
      state.selection_session, (ShaulaPoint){.x = (int)x, .y = (int)y});
  sync_selection_view();
  if (changed)
    queue_draw();
  apply_cursor(state.selection_view.cursor);
}

static void on_motion_enter(GtkEventControllerMotion *controller, double x,
                            double y, gpointer data) {
  on_motion(controller, x, y, data);
}

static void on_motion_leave(GtkEventControllerMotion *controller,
                            gpointer data) {
  (void)controller;
  (void)data;
  shaula_overlay_selection_session_leave(state.selection_session);
  sync_selection_view();
  apply_cursor(state.selection_view.cursor);
  queue_draw();
}

static void on_click(GtkGestureClick *gesture, int n_press, double x, double y,
                     gpointer data) {
  (void)gesture;
  (void)n_press;
  (void)data;
  (void)x;
  (void)y;

  if (capture_on_release()) {
    state.suppress_pointer_drag = FALSE;
    return;
  }
}

static void on_click_released(GtkGestureClick *gesture, int n_press, double x,
                              double y, gpointer data) {
  (void)gesture;
  (void)n_press;
  (void)x;
  (void)y;
  (void)data;
  state.suppress_pointer_drag = FALSE;
}

static gboolean on_key(GtkEventControllerKey *controller, guint keyval,
                       guint keycode, GdkModifierType modifiers,
                       gpointer data) {
  (void)controller;
  (void)keycode;
  (void)data;
  if (keyval == GDK_KEY_Escape) {
    if (state.aspect_popover != NULL &&
        gtk_widget_get_visible(state.aspect_popover)) {
      gtk_popover_popdown(GTK_POPOVER(state.aspect_popover));
      return TRUE;
    }
    cancel();
    return TRUE;
  }
  if ((modifiers & GDK_CONTROL_MASK) != 0 &&
      (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
    copy_now();
    return TRUE;
  }
  if ((modifiers & GDK_CONTROL_MASK) != 0 &&
      (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
    save_now();
    return TRUE;
  }
  if (keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_n ||
      keyval == GDK_KEY_N || keyval == GDK_KEY_q) {
    cancel();
    return TRUE;
  }
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter ||
      keyval == GDK_KEY_y || keyval == GDK_KEY_Y) {
    if (state.aspect_popover != NULL &&
        gtk_widget_get_visible(state.aspect_popover)) {
      gtk_popover_popdown(GTK_POPOVER(state.aspect_popover));
    }
    confirm();
    return TRUE;
  }

  int dx = 0;
  int dy = 0;
  if (keyval == GDK_KEY_Left)
    dx = -1;
  if (keyval == GDK_KEY_Right)
    dx = 1;
  if (keyval == GDK_KEY_Up)
    dy = -1;
  if (keyval == GDK_KEY_Down)
    dy = 1;
  if ((dx != 0 || dy != 0) && state.selection_view.has_selection) {
    sync_selection_bounds();
    int step = (modifiers & GDK_SHIFT_MASK) != 0 ? 10 : 1;
    if (shaula_overlay_selection_session_nudge(
            state.selection_session, dx, dy, step)) {
      sync_selection_view();
      update_toolbar();
      queue_draw();
    }
    return TRUE;
  }
  return FALSE;
}

static gboolean load_aspect(void) {
  state.aspect_choice = ASPECT_FREE;
  state.aspect_output_label[0] = '\0';
  if (!state.launch.has_aspect) {
    (void)shaula_overlay_selection_session_set_aspect(
        state.selection_session, FALSE, (ShaulaAspect){0}, TRUE);
    sync_selection_view();
    return FALSE;
  }

  int w = state.launch.aspect_width;
  int h = state.launch.aspect_height;

  ShaulaAspect aspect = {.width = w, .height = h};
  (void)shaula_overlay_selection_session_set_aspect(
      state.selection_session, TRUE, aspect, TRUE);
  sync_selection_view();
  snprintf(state.aspect_output_label, sizeof(state.aspect_output_label),
           "%d:%d", w, h);
  for (int i = 1; i < ASPECT_COUNT; i += 1) {
    if (ASPECT_WIDTHS[i] == w && ASPECT_HEIGHTS[i] == h) {
      state.aspect_choice = (ShaulaAspectChoice)i;
      break;
    }
  }
  return TRUE;
}

static void load_interaction_mode(void) {
  if (state.launch.interaction == SHAULA_OVERLAY_INTERACTION_AREA) {
    state.interaction_mode = INTERACTION_AREA;
  } else {
    state.interaction_mode = INTERACTION_QUICK;
  }
}

static void load_background(void) {
  const char *path = state.launch.background_path;
  if (path == NULL || path[0] == '\0')
    return;
  state.background = gdk_pixbuf_new_from_file(path, NULL);
}

static void load_initial_geometry(void) {
  if (!state.launch.has_initial_geometry)
    return;
  ShaulaRect initial = state.launch.initial_geometry;

  ShaulaRect rect;
  ShaulaPoint bounds = initial_surface_size();
  if (state.launch.initial_geometry_legacy) {
    if (initial.x < 0 || initial.y < 0 || initial.width > bounds.x ||
        initial.height > bounds.y || initial.x > bounds.x - initial.width ||
        initial.y > bounds.y - initial.height) {
      return;
    }
  }
  if (!clamp_selection_preserve_size(
          initial, bounds, &rect)) {
    return;
  }

  shaula_overlay_selection_session_set_bounds(state.selection_session, bounds);
  if (shaula_overlay_selection_session_set_selection(
          state.selection_session, rect, TRUE)) {
    sync_selection_view();
    update_toolbar();
  }
}

static void ensure_default_area_selection(ShaulaPoint bounds) {
  if (state.selection_view.has_selection ||
      state.interaction_mode != INTERACTION_AREA)
    return;

  int margin_x = MAX(16, bounds.x / 5);
  int margin_y = MAX(16, bounds.y / 5);
  int width = MAX(1, bounds.x - margin_x * 2);
  int height = MAX(1, bounds.y - margin_y * 2);
  if (width < 320 && bounds.x > 340)
    width = 320;
  if (height < 180 && bounds.y > 200)
    height = 180;
  width = MIN(width, bounds.x - 2 * MIN(16, bounds.x / 10));
  height = MIN(height, bounds.y - 2 * MIN(16, bounds.y / 10));

  ShaulaRect rect = {
      .x = (bounds.x - width) / 2,
      .y = (bounds.y - height) / 2,
      .width = width,
      .height = height,
  };
  shaula_overlay_selection_session_set_bounds(state.selection_session, bounds);
  if (shaula_overlay_selection_session_set_selection(
          state.selection_session, rect, FALSE)) {
    sync_selection_view();
    update_toolbar();
  }
}

static GdkMonitor *monitor_for_output(void) {
  const char *name = state.launch.output_name;
  if (name == NULL || name[0] == '\0')
    return NULL;
  GdkDisplay *display = gdk_display_get_default();
  if (display == NULL)
    return NULL;
  GListModel *monitors = gdk_display_get_monitors(display);
  guint count = g_list_model_get_n_items(monitors);
  for (guint i = 0; i < count; i += 1) {
    GObject *object = g_list_model_get_item(monitors, i);
    if (object == NULL)
      continue;
    GdkMonitor *monitor = GDK_MONITOR(object);
    const char *connector = gdk_monitor_get_connector(monitor);
    if (connector != NULL && strcmp(connector, name) == 0) {
      return monitor;
    }
    g_object_unref(object);
  }
  return NULL;
}

static ShaulaPoint initial_surface_size(void) {
  GdkMonitor *monitor = monitor_for_output();
  if (monitor != NULL) {
    GdkRectangle rect;
    gdk_monitor_get_geometry(monitor, &rect);
    g_object_unref(monitor);
    return (ShaulaPoint){.x = MAX(1, rect.width), .y = MAX(1, rect.height)};
  }
  return (ShaulaPoint){.x = 1920, .y = 1080};
}

/* --- GTK widget toolbar for the overlay --- */

static void update_aspect_label(void) {
  if (state.aspect_button == NULL)
    return;
  const char *label;
  if (!state.selection_view.has_aspect) {
    label = "Free";
  } else if (state.aspect_output_label[0] != '\0') {
    label = state.aspect_output_label;
  } else {
    label = ASPECT_LABELS[state.aspect_choice];
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "%s ▾", label);
  gtk_button_set_label(GTK_BUTTON(state.aspect_button), buf);
}

static void on_aspect_item_clicked(GtkButton *button, gpointer data) {
  (void)button;
  int idx = GPOINTER_TO_INT(data);
  state.aspect_choice = (ShaulaAspectChoice)idx;
  apply_aspect_choice();
  update_aspect_label();
  if (state.aspect_popover != NULL)
    gtk_popover_popdown(GTK_POPOVER(state.aspect_popover));
  queue_draw();
}

static void on_cancel_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  cancel();
}

static void on_capture_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  confirm();
}

static void reposition_toolbar_widget(void) {
  if (state.toolbar_box == NULL)
    return;
  gboolean visible = state.has_toolbar && !capture_on_release();
  gtk_widget_set_visible(state.toolbar_box, visible);
  if (!visible)
    return;
  gtk_widget_set_margin_start(state.toolbar_box, state.toolbar.x);
  gtk_widget_set_margin_top(state.toolbar_box, state.toolbar.y);
  if (state.capture_button != NULL) {
    gtk_widget_set_sensitive(state.capture_button,
                           state.selection_view.confirmable);
  }
}

static GtkWidget *build_widget_toolbar(void) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_add_css_class(box, "shaula-overlay-toolbar");

  /* Aspect ratio menu button with popover */
  GtkWidget *aspect_btn = gtk_button_new_with_label("Free ▾");
  gtk_widget_add_css_class(aspect_btn, "flat");
  gtk_widget_add_css_class(aspect_btn, "shaula-aspect-btn");
  state.aspect_button = aspect_btn;

  GtkWidget *popover = gtk_popover_new();
  gtk_widget_add_css_class(popover, "shaula-overlay-aspect-popover");
  gtk_widget_set_parent(popover, aspect_btn);
  state.aspect_popover = popover;

  GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  for (int i = 0; i < ASPECT_COUNT; i++) {
    GtkWidget *item = gtk_button_new_with_label(ASPECT_LABELS[i]);
    gtk_widget_add_css_class(item, "flat");
    gtk_widget_add_css_class(item, "aspect-row");
    if (i == (int)state.aspect_choice)
      gtk_widget_add_css_class(item, "selected");
    gtk_widget_set_halign(item, GTK_ALIGN_FILL);
    g_signal_connect(item, "clicked", G_CALLBACK(on_aspect_item_clicked),
                     GINT_TO_POINTER(i));
    gtk_box_append(GTK_BOX(list_box), item);
  }
  state.aspect_list_box = list_box;
  gtk_popover_set_child(GTK_POPOVER(popover), list_box);

  g_signal_connect_swapped(aspect_btn, "clicked", G_CALLBACK(gtk_popover_popup),
                           popover);

  GtkWidget *cancel_btn = gtk_button_new_with_label("Discard");
  gtk_widget_add_css_class(cancel_btn, "shaula-cancel-btn");
  g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_clicked), NULL);
  state.cancel_button = cancel_btn;
  gtk_box_append(GTK_BOX(box), cancel_btn);

  gtk_box_append(GTK_BOX(box), aspect_btn);

  GtkWidget *capture_btn = gtk_button_new_with_label("Capture");
  gtk_widget_add_css_class(capture_btn, "shaula-capture-btn");
  gtk_widget_set_sensitive(capture_btn,
                           state.selection_view.confirmable);
  g_signal_connect(capture_btn, "clicked", G_CALLBACK(on_capture_clicked),
                   NULL);
  state.capture_button = capture_btn;
  gtk_box_append(GTK_BOX(box), capture_btn);

  state.toolbar_box = box;
  update_aspect_label();
  return box;
}

static gboolean on_close_request(GtkWindow *window, gpointer data) {
  (void)window;
  (void)data;
  cancel();
  return TRUE;
}

static void setup_overlay_window(void) {
  GtkWidget *window = gtk_window_new();
  state.window = window;

  gtk_window_set_title(GTK_WINDOW(window), "shaula-overlay");
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  gtk_widget_add_css_class(window, "shaula-overlay-window");
  g_signal_connect(window, "close-request", G_CALLBACK(on_close_request),
                   NULL);

  gtk_layer_init_for_window(GTK_WINDOW(window));
  gtk_layer_set_namespace(GTK_WINDOW(window), "shaula-overlay");
  gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
  GdkMonitor *monitor = monitor_for_output();
  if (monitor != NULL) {
    gtk_layer_set_monitor(GTK_WINDOW(window), monitor);
    g_object_unref(monitor);
  }
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window),
                              GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
  gtk_layer_set_exclusive_zone(GTK_WINDOW(window), -1);

  /* Runtime boundary: selection is edited in output-local surface
   * coordinates, but helper JSON must emit compositor-layout coordinates for
   * grim `-g`. This makes monitor-origin changes idempotent after hotplug.
   */
  state.output_origin = current_output_origin();
  ShaulaPoint size = initial_surface_size();
  shaula_overlay_selection_session_set_bounds(state.selection_session, size);
  sync_selection_view();
  gtk_window_set_default_size(GTK_WINDOW(window), size.x, size.y);

  GtkWidget *area = gtk_drawing_area_new();
  state.area = area;
  gtk_widget_add_css_class(area, "shaula-overlay-area");
  gtk_widget_set_focusable(area, TRUE);
  gtk_widget_set_hexpand(area, TRUE);
  gtk_widget_set_vexpand(area, TRUE);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), size.x);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), size.y);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);

  /* Use GtkOverlay so the GTK widget toolbar floats above the Cairo
   * drawing area without adding a fullscreen event target over the canvas.
   */
  GtkWidget *overlay = gtk_overlay_new();
  gtk_overlay_set_child(GTK_OVERLAY(overlay), area);
  state.overlay_container = overlay;

  if (!capture_on_release()) {
    GtkWidget *toolbar_widget = build_widget_toolbar();
    gtk_widget_set_halign(toolbar_widget, GTK_ALIGN_START);
    gtk_widget_set_valign(toolbar_widget, GTK_ALIGN_START);
    gtk_widget_set_can_target(toolbar_widget, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), toolbar_widget);
  }

  gtk_window_set_child(GTK_WINDOW(window), overlay);

  load_initial_geometry();
  ensure_default_area_selection(size);
  reposition_toolbar_widget();

  GtkGesture *drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
  g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
  g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), NULL);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

  GtkGesture *click = gtk_gesture_click_new();
  g_signal_connect(click, "pressed", G_CALLBACK(on_click), NULL);
  g_signal_connect(click, "released", G_CALLBACK(on_click_released), NULL);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(on_motion), NULL);
  g_signal_connect(motion, "enter", G_CALLBACK(on_motion_enter), NULL);
  g_signal_connect(motion, "leave", G_CALLBACK(on_motion_leave), NULL);
  gtk_widget_add_controller(area, motion);

  GtkEventController *keys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), NULL);
  gtk_widget_add_controller(window, keys);

  gtk_window_present(GTK_WINDOW(window));
  gtk_widget_grab_focus(area);

  /* Debug: emit overlay-ready timestamp so the parent can measure
     CLI-to-UI-visible latency. Gated by SHAULA_DEBUG_OVERLAY_LATENCY
     so it never ships active in production. */
  if (getenv("SHAULA_DEBUG_OVERLAY_LATENCY") != NULL) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
      long long ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
      fprintf(stderr, "SHAULA_OVERLAY_READY_TS=%lld\n", ms);
      fflush(stderr);
    }
  }
}

int shaula_native_gtk_overlay_run(void) {
  if (getenv("SHAULA_OVERLAY_HELPER_FORCE_UNAVAILABLE") != NULL) {
    emit_overlay_error("forced unavailable");
    return 36;
  }
  if (getenv("SHAULA_OVERLAY_HELPER_FORCE_TIMEOUT") != NULL) {
    sleep(10);
    return 37;
  }
  if (getenv("SHAULA_OVERLAY_HELPER_PROBE") != NULL) {
    gtk_init();
    if (gtk_layer_is_supported()) {
      printf("{\"status\":\"ok\",\"action\":\"cancel\",\"geometry\":null,"
             "\"error\":null}\n");
      fflush(stdout);
      return 0;
    }
    emit_overlay_error(
        "gtk4-layer-shell is not supported by this compositor");
    return 36;
  }

  prefer_fast_overlay_renderer();
  gtk_init();
  memset(&state, 0, sizeof(state));
  shaula_overlay_launch_init(&state.launch);
  shaula_overlay_launch_load_environment(&state.launch);
  state.toolbar = (ShaulaPoint){.x = PADDING, .y = PADDING};
  state.has_toolbar = TRUE;
  state.cursor_shape = SHAULA_OVERLAY_CURSOR_DEFAULT;
  state.selection_session =
      shaula_overlay_selection_session_new(initial_surface_size());
  if (state.selection_session == NULL) {
    emit_overlay_error("overlay selection session could not be created");
    clear_overlay_state();
    return 36;
  }
  sync_selection_view();
  install_transparent_overlay_css();
  load_interaction_mode();
  load_aspect();
  load_background();

  if (!gtk_layer_is_supported()) {
    emit_overlay_error(
        "gtk4-layer-shell is not supported by this compositor");
    clear_overlay_state();
    return 36;
  }

  state.main_loop = g_main_loop_new(NULL, FALSE);
  if (state.main_loop == NULL) {
    emit_overlay_error("gtk main loop could not be created");
    clear_overlay_state();
    return 36;
  }
  setup_overlay_window();
  g_main_loop_run(state.main_loop);
  g_main_loop_unref(state.main_loop);
  state.main_loop = NULL;
  clear_overlay_state();
  return 0;
}

#ifdef SHAULA_OVERLAY_STANDALONE
int main(void) { return shaula_native_gtk_overlay_run(); }
#endif
