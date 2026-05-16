#include <gtk/gtk.h>
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
  RESIZE_HIT_RADIUS = 10,
  MOVE_HIT_RADIUS = 24,
  CREATE_THRESHOLD = 6,
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
  ASPECT_CUSTOM,
  ASPECT_COUNT,
} ShaulaAspectChoice;

static const char *ASPECT_LABELS[ASPECT_COUNT] = {
    "Free",          "1:1 Square",   "16:9 Widescreen",
    "4:3 Standard",  "3:2 Photo",    "16:10 Desktop",
    "9:16 Vertical", "4:5 Portrait", "3:4 Phone photo",
    "Custom..."};

static const int ASPECT_WIDTHS[ASPECT_COUNT] = {0, 1, 16, 4, 3, 16, 9, 4, 3, 0};
static const int ASPECT_HEIGHTS[ASPECT_COUNT] = {0,  1,  9, 3, 2,
                                                 10, 16, 5, 4, 0};

typedef enum {
  INTERACTION_QUICK,
  INTERACTION_AREA,
} ShaulaInteractionMode;

typedef struct {
  int x;
  int y;
  int width;
  int height;
} ShaulaRect;

typedef struct {
  int x;
  int y;
} ShaulaPoint;

typedef struct {
  int width;
  int height;
} ShaulaAspect;

typedef enum {
  DRAG_NONE,
  DRAG_CREATE,
  DRAG_MOVE,
  DRAG_RESIZE,
  DRAG_TOOLBAR,
} ShaulaDragMode;

typedef enum {
  HANDLE_NONE,
  HANDLE_TOP_LEFT,
  HANDLE_TOP,
  HANDLE_TOP_RIGHT,
  HANDLE_RIGHT,
  HANDLE_BOTTOM_RIGHT,
  HANDLE_BOTTOM,
  HANDLE_BOTTOM_LEFT,
  HANDLE_LEFT,
} ShaulaResizeHandle;

typedef enum {
  CURSOR_UNSET,
  CURSOR_DEFAULT,
  CURSOR_CROSSHAIR,
  CURSOR_POINTER,
  CURSOR_GRAB,
  CURSOR_GRABBING,
  CURSOR_RESIZE_EW,
  CURSOR_RESIZE_NS,
  CURSOR_RESIZE_NWSE,
  CURSOR_RESIZE_NESW,
} ShaulaCursorShape;

typedef struct {
  GMainLoop *main_loop;
  GtkWidget *window;
  GtkWidget *area;
  GdkPixbuf *background;
  ShaulaPoint output_origin;
  gboolean has_selection;
  ShaulaRect selection;
  gboolean has_toolbar;
  ShaulaInteractionMode interaction_mode;
  ShaulaPoint toolbar;
  gboolean has_aspect;
  ShaulaAspect aspect;
  ShaulaDragMode drag_mode;
  ShaulaResizeHandle active_handle;
  ShaulaResizeHandle hover_handle;
  ShaulaPoint drag_start;
  ShaulaRect drag_origin;
  gboolean dropdown_open;
  ShaulaAspectChoice aspect_choice;
  char aspect_custom_label[32];
  char aspect_output_label[32];
  gboolean suppress_pointer_drag;
  ShaulaCursorShape cursor_shape;
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

static GdkMonitor *monitor_for_output(void);
static ShaulaPoint initial_surface_size(void);
static void open_custom_aspect_dialog(void);
static void reposition_toolbar_widget(void);
static void update_aspect_label(void);
static void prefer_fast_overlay_renderer(void);
static void setup_overlay_window(void);

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

static gboolean point_in_selection(ShaulaRect selection, ShaulaPoint point) {
  return point.x >= selection.x && point.x <= selection.x + selection.width &&
         point.y >= selection.y && point.y <= selection.y + selection.height;
}

static gboolean point_near_selection_border(ShaulaRect selection,
                                            ShaulaPoint point, int radius) {
  if (!point_in_selection(selection, point))
    return FALSE;

  int left = selection.x;
  int top = selection.y;
  int right = selection.x + selection.width;
  int bottom = selection.y + selection.height;
  return abs(point.x - left) <= radius || abs(point.x - right) <= radius ||
         abs(point.y - top) <= radius || abs(point.y - bottom) <= radius;
}

static gboolean point_near(ShaulaPoint a, ShaulaPoint b, int radius) {
  return abs(a.x - b.x) <= radius && abs(a.y - b.y) <= radius;
}

static void queue_draw(void) {
  if (state.area != NULL)
    gtk_widget_queue_draw(state.area);
}

static const char *cursor_name(ShaulaCursorShape shape) {
  switch (shape) {
  case CURSOR_DEFAULT:
    return NULL;
  case CURSOR_CROSSHAIR:
    return "crosshair";
  case CURSOR_POINTER:
    return "pointer";
  case CURSOR_GRAB:
    return "grab";
  case CURSOR_GRABBING:
    return "grabbing";
  case CURSOR_RESIZE_EW:
    return "ew-resize";
  case CURSOR_RESIZE_NS:
    return "ns-resize";
  case CURSOR_RESIZE_NWSE:
    return "nwse-resize";
  case CURSOR_RESIZE_NESW:
    return "nesw-resize";
  case CURSOR_UNSET:
  default:
    return NULL;
  }
}

static ShaulaCursorShape cursor_for_handle(ShaulaResizeHandle handle) {
  switch (handle) {
  case HANDLE_LEFT:
  case HANDLE_RIGHT:
    return CURSOR_RESIZE_EW;
  case HANDLE_TOP:
  case HANDLE_BOTTOM:
    return CURSOR_RESIZE_NS;
  case HANDLE_TOP_LEFT:
  case HANDLE_BOTTOM_RIGHT:
    return CURSOR_RESIZE_NWSE;
  case HANDLE_TOP_RIGHT:
  case HANDLE_BOTTOM_LEFT:
    return CURSOR_RESIZE_NESW;
  case HANDLE_NONE:
  default:
    return CURSOR_DEFAULT;
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

static void apply_cursor(ShaulaCursorShape shape) {
  if (state.area == NULL || state.cursor_shape == shape)
    return;
  gtk_widget_set_cursor_from_name(state.area, cursor_name(shape));
  state.cursor_shape = shape;
}

static void apply_aspect(int *width, int *height, ShaulaAspect ratio) {
  if (*width == 0 && *height > 0) {
    *width = MAX(1, (*height * ratio.width) / ratio.height);
  } else if (*height == 0 && *width > 0) {
    *height = MAX(1, (*width * ratio.height) / ratio.width);
  } else if (*width > 0 && *height > 0) {
    if ((*width * ratio.height) >= (*height * ratio.width)) {
      *width = MAX(1, (*height * ratio.width) / ratio.height);
    } else {
      *height = MAX(1, (*width * ratio.height) / ratio.width);
    }
  }
}

static gboolean clamp_selection(ShaulaRect input, ShaulaPoint bounds,
                                ShaulaRect *out) {
  int left = clamp_int(input.x, 0, MAX(0, bounds.x - 1));
  int top = clamp_int(input.y, 0, MAX(0, bounds.y - 1));
  int right = clamp_int(input.x + input.width, 1, bounds.x);
  int bottom = clamp_int(input.y + input.height, 1, bounds.y);
  if (right <= left)
    right = MIN(bounds.x, left + 1);
  if (bottom <= top)
    bottom = MIN(bounds.y, top + 1);
  if (right <= left || bottom <= top)
    return FALSE;
  *out = (ShaulaRect){
      .x = left, .y = top, .width = right - left, .height = bottom - top};
  return TRUE;
}

static gboolean geometry_from_points(ShaulaPoint anchor, ShaulaPoint point,
                                     ShaulaPoint bounds, ShaulaRect *out) {
  int width = abs(point.x - anchor.x) + 1;
  int height = abs(point.y - anchor.y) + 1;
  if (state.has_aspect)
    apply_aspect(&width, &height, state.aspect);
  if (width <= 0 || height <= 0)
    return FALSE;
  int x = point.x >= anchor.x ? anchor.x : anchor.x - width;
  int y = point.y >= anchor.y ? anchor.y : anchor.y - height;
  return clamp_selection(
      (ShaulaRect){.x = x, .y = y, .width = width, .height = height}, bounds,
      out);
}

static gboolean move_selection(ShaulaRect selection, int dx, int dy,
                               ShaulaPoint bounds, ShaulaRect *out) {
  *out = (ShaulaRect){
      .x = clamp_int(selection.x + dx, 0, MAX(0, bounds.x - selection.width)),
      .y = clamp_int(selection.y + dy, 0, MAX(0, bounds.y - selection.height)),
      .width = selection.width,
      .height = selection.height,
  };
  return TRUE;
}

static gboolean normalize_selection(int left, int top, int right, int bottom,
                                    ShaulaPoint bounds, ShaulaRect *out) {
  int x0 = MIN(left, right);
  int x1 = MAX(left, right);
  int y0 = MIN(top, bottom);
  int y1 = MAX(top, bottom);
  if (x1 <= x0)
    x1 = x0 + 1;
  if (y1 <= y0)
    y1 = y0 + 1;
  return clamp_selection(
      (ShaulaRect){.x = x0, .y = y0, .width = x1 - x0, .height = y1 - y0},
      bounds, out);
}

static gboolean resize_free(ShaulaRect origin, ShaulaResizeHandle handle,
                            ShaulaPoint point, ShaulaPoint bounds,
                            ShaulaRect *out) {
  int left = origin.x;
  int top = origin.y;
  int right = origin.x + origin.width;
  int bottom = origin.y + origin.height;

  switch (handle) {
  case HANDLE_TOP_LEFT:
    left = point.x;
    top = point.y;
    break;
  case HANDLE_TOP:
    top = point.y;
    break;
  case HANDLE_TOP_RIGHT:
    right = point.x + 1;
    top = point.y;
    break;
  case HANDLE_RIGHT:
    right = point.x + 1;
    break;
  case HANDLE_BOTTOM_RIGHT:
    right = point.x + 1;
    bottom = point.y + 1;
    break;
  case HANDLE_BOTTOM:
    bottom = point.y + 1;
    break;
  case HANDLE_BOTTOM_LEFT:
    left = point.x;
    bottom = point.y + 1;
    break;
  case HANDLE_LEFT:
    left = point.x;
    break;
  default:
    return FALSE;
  }

  return normalize_selection(left, top, right, bottom, bounds, out);
}

static gboolean resize_aspect(ShaulaRect origin, ShaulaResizeHandle handle,
                              ShaulaPoint point, ShaulaPoint bounds,
                              ShaulaRect *out) {
  int left = origin.x;
  int top = origin.y;
  int right = origin.x + origin.width;
  int bottom = origin.y + origin.height;
  int center_x = origin.x + origin.width / 2;
  int center_y = origin.y + origin.height / 2;
  int width = origin.width;
  int height = origin.height;

  switch (handle) {
  case HANDLE_TOP_LEFT:
    return geometry_from_points((ShaulaPoint){.x = right, .y = bottom}, point,
                                bounds, out);
  case HANDLE_TOP_RIGHT:
    return geometry_from_points((ShaulaPoint){.x = left, .y = bottom}, point,
                                bounds, out);
  case HANDLE_BOTTOM_RIGHT:
    return geometry_from_points((ShaulaPoint){.x = left, .y = top}, point,
                                bounds, out);
  case HANDLE_BOTTOM_LEFT:
    return geometry_from_points((ShaulaPoint){.x = right, .y = top}, point,
                                bounds, out);
  case HANDLE_LEFT:
    width = abs(right - point.x);
    height = MAX(1, (width * state.aspect.height) / state.aspect.width);
    return normalize_selection(right - width, center_y - height / 2, right,
                               center_y + (height + 1) / 2, bounds, out);
  case HANDLE_RIGHT:
    width = abs(point.x + 1 - left);
    height = MAX(1, (width * state.aspect.height) / state.aspect.width);
    return normalize_selection(left, center_y - height / 2, left + width,
                               center_y + (height + 1) / 2, bounds, out);
  case HANDLE_TOP:
    height = abs(bottom - point.y);
    width = MAX(1, (height * state.aspect.width) / state.aspect.height);
    return normalize_selection(center_x - width / 2, bottom - height,
                               center_x + (width + 1) / 2, bottom, bounds, out);
  case HANDLE_BOTTOM:
    height = abs(point.y + 1 - top);
    width = MAX(1, (height * state.aspect.width) / state.aspect.height);
    return normalize_selection(center_x - width / 2, top,
                               center_x + (width + 1) / 2, top + height, bounds,
                               out);
  default:
    return FALSE;
  }
}

static gboolean resize_selection(ShaulaRect origin, ShaulaResizeHandle handle,
                                 ShaulaPoint point, ShaulaPoint bounds,
                                 ShaulaRect *out) {
  if (state.has_aspect)
    return resize_aspect(origin, handle, point, bounds, out);
  return resize_free(origin, handle, point, bounds, out);
}

static gboolean apply_aspect_from_center(ShaulaRect input, ShaulaAspect aspect,
                                         ShaulaPoint bounds, ShaulaRect *out) {
  int w = input.width;
  int h = input.height;
  apply_aspect(&w, &h, aspect);
  int cx = input.x + input.width / 2;
  int cy = input.y + input.height / 2;
  ShaulaRect centered = {
      .x = cx - w / 2,
      .y = cy - h / 2,
      .width = w,
      .height = h,
  };
  return clamp_selection(centered, bounds, out);
}

static ShaulaResizeHandle resize_handle_at(ShaulaRect s, ShaulaPoint p) {
  int left = s.x;
  int top = s.y;
  int right = s.x + s.width;
  int bottom = s.y + s.height;
  int mid_x = s.x + s.width / 2;
  int mid_y = s.y + s.height / 2;

  if (point_near(p, (ShaulaPoint){.x = left, .y = top}, RESIZE_HIT_RADIUS))
    return HANDLE_TOP_LEFT;
  if (point_near(p, (ShaulaPoint){.x = right, .y = top}, RESIZE_HIT_RADIUS))
    return HANDLE_TOP_RIGHT;
  if (point_near(p, (ShaulaPoint){.x = right, .y = bottom}, RESIZE_HIT_RADIUS))
    return HANDLE_BOTTOM_RIGHT;
  if (point_near(p, (ShaulaPoint){.x = left, .y = bottom}, RESIZE_HIT_RADIUS))
    return HANDLE_BOTTOM_LEFT;
  if (point_near(p, (ShaulaPoint){.x = mid_x, .y = top}, RESIZE_HIT_RADIUS))
    return HANDLE_TOP;
  if (point_near(p, (ShaulaPoint){.x = right, .y = mid_y}, RESIZE_HIT_RADIUS))
    return HANDLE_RIGHT;
  if (point_near(p, (ShaulaPoint){.x = mid_x, .y = bottom}, RESIZE_HIT_RADIUS))
    return HANDLE_BOTTOM;
  if (point_near(p, (ShaulaPoint){.x = left, .y = mid_y}, RESIZE_HIT_RADIUS))
    return HANDLE_LEFT;

  if (p.x >= left - RESIZE_HIT_RADIUS && p.x <= right + RESIZE_HIT_RADIUS) {
    if (abs(p.y - top) <= RESIZE_HIT_RADIUS)
      return HANDLE_TOP;
    if (abs(p.y - bottom) <= RESIZE_HIT_RADIUS)
      return HANDLE_BOTTOM;
  }
  if (p.y >= top - RESIZE_HIT_RADIUS && p.y <= bottom + RESIZE_HIT_RADIUS) {
    if (abs(p.x - left) <= RESIZE_HIT_RADIUS)
      return HANDLE_LEFT;
    if (abs(p.x - right) <= RESIZE_HIT_RADIUS)
      return HANDLE_RIGHT;
  }

  return HANDLE_NONE;
}

static void update_toolbar(void) {
  if (!state.has_selection)
    return;
  ShaulaRect selection = state.selection;
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

static ShaulaCursorShape resolve_hover_cursor(ShaulaPoint p) {
  if (state.has_selection) {
    ShaulaResizeHandle handle = resize_handle_at(state.selection, p);
    if (handle != HANDLE_NONE)
      return cursor_for_handle(handle);
    if (point_near_selection_border(state.selection, p, MOVE_HIT_RADIUS))
      return CURSOR_GRAB;
  }
  return CURSOR_CROSSHAIR;
}

static void apply_aspect_choice(void) {
  if (state.aspect_choice == ASPECT_FREE) {
    state.has_aspect = FALSE;
    state.aspect_custom_label[0] = '\0';
    state.aspect_output_label[0] = '\0';
  } else if (state.aspect_choice == ASPECT_CUSTOM) {
    state.has_aspect = TRUE;
    snprintf(state.aspect_output_label, sizeof(state.aspect_output_label),
             "%d:%d", state.aspect.width, state.aspect.height);
  } else {
    state.has_aspect = TRUE;
    state.aspect = (ShaulaAspect){
        .width = ASPECT_WIDTHS[(int)state.aspect_choice],
        .height = ASPECT_HEIGHTS[(int)state.aspect_choice],
    };
    snprintf(state.aspect_output_label, sizeof(state.aspect_output_label),
             "%d:%d", state.aspect.width, state.aspect.height);
  }
  if (state.has_selection && state.has_aspect) {
    ShaulaPoint bounds = output_size();
    ShaulaRect adj;
    if (apply_aspect_from_center(state.selection, state.aspect, bounds, &adj)) {
      state.selection = adj;
    }
  }
  update_toolbar();
  state.dropdown_open = FALSE;
  if (state.aspect_popover != NULL)
    gtk_popover_popdown(GTK_POPOVER(state.aspect_popover));
}

static gboolean parse_custom_aspect(const char *raw, ShaulaAspect *out) {
  if (raw == NULL)
    return FALSE;
  int w = 0;
  int h = 0;
  char tail = '\0';
  if (sscanf(raw, " %d : %d %c", &w, &h, &tail) != 2 || w <= 0 || h <= 0)
    return FALSE;
  *out = (ShaulaAspect){.width = w, .height = h};
  return TRUE;
}

static void custom_aspect_response(GtkDialog *dialog, int response_id,
                                   gpointer data) {
  GtkEntry *entry = GTK_ENTRY(data);
  if (response_id == GTK_RESPONSE_ACCEPT) {
    ShaulaAspect next;
    const char *raw = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (parse_custom_aspect(raw, &next)) {
      state.aspect = next;
      state.aspect_choice = ASPECT_CUSTOM;
      state.has_aspect = TRUE;
      snprintf(state.aspect_custom_label, sizeof(state.aspect_custom_label),
               "%d:%d", next.width, next.height);
      snprintf(state.aspect_output_label, sizeof(state.aspect_output_label),
               "%d:%d", next.width, next.height);
      apply_aspect_choice();
      update_aspect_label();
      queue_draw();
    }
  }
  gtk_window_destroy(GTK_WINDOW(dialog));
}

static void custom_aspect_entry_activate(GtkEntry *entry, gpointer data) {
  custom_aspect_response(GTK_DIALOG(data), GTK_RESPONSE_ACCEPT, entry);
}

static void open_custom_aspect_dialog(void) {
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Custom aspect", GTK_WINDOW(state.window), GTK_DIALOG_MODAL, "Cancel",
      GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_ACCEPT, NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry),
                        state.has_aspect ? state.aspect_output_label : "16:9");
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "W:H");
  gtk_box_append(GTK_BOX(content), entry);
  g_signal_connect(dialog, "response", G_CALLBACK(custom_aspect_response),
                   entry);
  g_signal_connect(entry, "activate", G_CALLBACK(custom_aspect_entry_activate),
                   dialog);
  gtk_window_present(GTK_WINDOW(dialog));
  gtk_widget_grab_focus(entry);
}

static void draw_background(cairo_t *cr, int width, int height) {
  if (state.background == NULL)
    return;
  GdkPixbuf *scaled = gdk_pixbuf_scale_simple(state.background, width, height,
                                              GDK_INTERP_BILINEAR);
  if (scaled == NULL)
    return;
  gdk_cairo_set_source_pixbuf(cr, scaled, 0, 0);
  cairo_paint(cr);
  g_object_unref(scaled);
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
      state.drag_mode == DRAG_RESIZE ? state.active_handle : state.hover_handle;
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

  if (state.has_selection) {
    ShaulaRect s = state.selection;
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

static void confirm(void) {
  if (!state.has_selection || state.selection.width <= 0 ||
      state.selection.height <= 0)
    return;
  const char *aspect = state.has_aspect ? state.aspect_output_label : "Free";
  printf("{\"status\":\"ok\",\"action\":\"capture\",\"aspect\":\"%s\","
         "\"geometry\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},"
         "\"error\":null}\n",
         aspect, state.selection.x + state.output_origin.x,
         state.selection.y + state.output_origin.y, state.selection.width,
         state.selection.height);
  fflush(stdout);
  if (state.main_loop != NULL)
    g_main_loop_quit(state.main_loop);
}

static void cancel(void) {
  if (state.has_selection && state.selection.width > 0 &&
      state.selection.height > 0) {
    printf("{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":{\"x\":%"
           "d,\"y\":%d,\"width\":%d,\"height\":%d},\"error\":null}\n",
           state.selection.x + state.output_origin.x,
           state.selection.y + state.output_origin.y, state.selection.width,
           state.selection.height);
  } else {
    printf("{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,"
           "\"error\":null}\n");
  }
  fflush(stdout);
  if (state.main_loop != NULL)
    g_main_loop_quit(state.main_loop);
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y,
                          gpointer data) {
  (void)gesture;
  (void)data;
  ShaulaPoint p = {.x = (int)x, .y = (int)y};

  if (!capture_on_release()) {
    if (state.suppress_pointer_drag) {
      state.drag_mode = DRAG_TOOLBAR;
      apply_cursor(resolve_hover_cursor(p));
      return;
    }
  }
  state.drag_start = p;
  state.drag_origin = state.selection;
  state.active_handle = HANDLE_NONE;
  state.hover_handle = HANDLE_NONE;
  if (state.has_selection) {
    ShaulaResizeHandle handle = resize_handle_at(state.selection, p);
    if (handle != HANDLE_NONE) {
      state.active_handle = handle;
      state.hover_handle = handle;
      state.drag_mode = DRAG_RESIZE;
      apply_cursor(cursor_for_handle(handle));
      queue_draw();
      return;
    }
  }
  if (state.has_selection &&
      point_near_selection_border(state.selection, p, MOVE_HIT_RADIUS)) {
    state.drag_mode = DRAG_MOVE;
    apply_cursor(CURSOR_GRABBING);
  } else {
    state.drag_mode = DRAG_CREATE;
    apply_cursor(CURSOR_CROSSHAIR);
  }
  queue_draw();
}

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy,
                           gpointer data) {
  (void)gesture;
  (void)data;
  ShaulaPoint bounds = output_size();
  ShaulaPoint p = {.x = state.drag_start.x + (int)dx,
                   .y = state.drag_start.y + (int)dy};
  ShaulaRect next;
  if (state.drag_mode == DRAG_CREATE) {
    if (abs((int)dx) < CREATE_THRESHOLD && abs((int)dy) < CREATE_THRESHOLD &&
        state.has_selection) {
      queue_draw();
      return;
    }
    state.has_selection =
        geometry_from_points(state.drag_start, p, bounds, &next);
    if (state.has_selection)
      state.selection = next;
  } else if (state.drag_mode == DRAG_MOVE) {
    if (move_selection(state.drag_origin, (int)dx, (int)dy, bounds, &next)) {
      state.selection = next;
      state.has_selection = TRUE;
    }
  } else if (state.drag_mode == DRAG_RESIZE) {
    if (resize_selection(state.drag_origin, state.active_handle, p, bounds,
                         &next)) {
      state.selection = next;
      state.has_selection = TRUE;
    }
  }
  if (state.drag_mode == DRAG_MOVE) {
    apply_cursor(CURSOR_GRABBING);
  } else if (state.drag_mode == DRAG_RESIZE) {
    apply_cursor(cursor_for_handle(state.active_handle));
  } else if (state.drag_mode == DRAG_CREATE) {
    apply_cursor(CURSOR_CROSSHAIR);
  }
  update_toolbar();
  queue_draw();
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                        gpointer data) {
  ShaulaDragMode completed_mode = state.drag_mode;
  on_drag_update(gesture, dx, dy, data);
  gboolean should_confirm =
      capture_on_release() &&
      (completed_mode == DRAG_CREATE || completed_mode == DRAG_MOVE ||
       completed_mode == DRAG_RESIZE) &&
      state.has_selection && state.selection.width > 0 &&
      state.selection.height > 0;
  state.drag_mode = DRAG_NONE;
  state.active_handle = HANDLE_NONE;
  state.suppress_pointer_drag = FALSE;
  ShaulaPoint p = {.x = state.drag_start.x + (int)dx,
                   .y = state.drag_start.y + (int)dy};
  state.hover_handle =
      state.has_selection ? resize_handle_at(state.selection, p) : HANDLE_NONE;
  apply_cursor(resolve_hover_cursor(p));
  queue_draw();
  if (should_confirm) {
    confirm();
  }
}

static void on_motion(GtkEventControllerMotion *controller, double x, double y,
                      gpointer data) {
  (void)controller;
  (void)data;
  if (state.drag_mode != DRAG_NONE)
    return;
  ShaulaPoint p = {.x = (int)x, .y = (int)y};
  ShaulaResizeHandle next_hover =
      state.has_selection ? resize_handle_at(state.selection, p) : HANDLE_NONE;
  if (next_hover != state.hover_handle) {
    state.hover_handle = next_hover;
    queue_draw();
  }
  apply_cursor(resolve_hover_cursor(p));
}

static void on_motion_enter(GtkEventControllerMotion *controller, double x,
                            double y, gpointer data) {
  on_motion(controller, x, y, data);
}

static void on_motion_leave(GtkEventControllerMotion *controller,
                            gpointer data) {
  (void)controller;
  (void)data;
  state.hover_handle = HANDLE_NONE;
  apply_cursor(CURSOR_DEFAULT);
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
  if (keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_n ||
      keyval == GDK_KEY_N || keyval == GDK_KEY_q ||
      ((modifiers & GDK_CONTROL_MASK) != 0 &&
       (keyval == GDK_KEY_c || keyval == GDK_KEY_C))) {
    cancel();
    return TRUE;
  }
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter ||
      keyval == GDK_KEY_y || keyval == GDK_KEY_Y) {
    if (state.aspect_popover != NULL &&
        gtk_widget_get_visible(state.aspect_popover)) {
      gtk_popover_popdown(GTK_POPOVER(state.aspect_popover));
      return TRUE;
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
  if ((dx != 0 || dy != 0) && state.has_selection) {
    int step = (modifiers & GDK_SHIFT_MASK) != 0 ? 10 : 1;
    ShaulaRect next;
    if (move_selection(state.selection, dx * step, dy * step, output_size(),
                       &next)) {
      state.selection = next;
      update_toolbar();
      queue_draw();
    }
    return TRUE;
  }
  return FALSE;
}

static gboolean load_aspect(void) {
  const char *raw = getenv("SHAULA_OVERLAY_ASPECT");
  if (raw == NULL || raw[0] == '\0') {
    state.has_aspect = FALSE;
    state.aspect_choice = ASPECT_FREE;
    state.aspect_output_label[0] = '\0';
    return FALSE;
  }
  int w = 0;
  int h = 0;
  if (sscanf(raw, "%d:%d", &w, &h) != 2 || w <= 0 || h <= 0) {
    state.has_aspect = FALSE;
    state.aspect_choice = ASPECT_FREE;
    state.aspect_custom_label[0] = '\0';
    state.aspect_output_label[0] = '\0';
    return FALSE;
  }
  state.aspect = (ShaulaAspect){.width = w, .height = h};
  state.has_aspect = TRUE;
  state.aspect_choice = ASPECT_CUSTOM;
  snprintf(state.aspect_custom_label, sizeof(state.aspect_custom_label),
           "%d:%d", w, h);
  snprintf(state.aspect_output_label, sizeof(state.aspect_output_label),
           "%d:%d", w, h);
  for (int i = 1; i < ASPECT_COUNT; i += 1) {
    if (ASPECT_WIDTHS[i] == w && ASPECT_HEIGHTS[i] == h) {
      state.aspect_choice = (ShaulaAspectChoice)i;
      state.aspect_custom_label[0] = '\0';
      break;
    }
  }
  return TRUE;
}

static void load_interaction_mode(void) {
  const char *raw = getenv("SHAULA_OVERLAY_INTERACTION_MODE");
  if (raw != NULL && strcmp(raw, "area") == 0) {
    state.interaction_mode = INTERACTION_AREA;
  } else {
    state.interaction_mode = INTERACTION_QUICK;
  }
}

static void load_background(void) {
  const char *path = getenv("SHAULA_OVERLAY_BACKGROUND_PATH");
  if (path == NULL || path[0] == '\0')
    return;
  state.background = gdk_pixbuf_new_from_file(path, NULL);
}

static void load_initial_geometry(void) {
  const char *raw = getenv("SHAULA_OVERLAY_INITIAL_GEOMETRY");
  if (raw == NULL || raw[0] == '\0')
    return;

  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  if (sscanf(raw, "%d,%d,%d,%d", &x, &y, &width, &height) != 4 || width <= 0 ||
      height <= 0) {
    return;
  }

  ShaulaRect rect;
  ShaulaPoint bounds = initial_surface_size();
  if (!clamp_selection(
          (ShaulaRect){
              .x = x - state.output_origin.x,
              .y = y - state.output_origin.y,
              .width = width,
              .height = height,
          },
          bounds, &rect)) {
    return;
  }

  state.selection = rect;
  if (state.has_aspect) {
    ShaulaRect adjusted;
    if (apply_aspect_from_center(state.selection, state.aspect, bounds,
                                 &adjusted))
      state.selection = adjusted;
  }
  state.has_selection = TRUE;
  update_toolbar();
}

static void ensure_default_area_selection(ShaulaPoint bounds) {
  if (state.has_selection || state.interaction_mode != INTERACTION_AREA)
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
  if (state.has_aspect) {
    ShaulaRect adjusted;
    if (apply_aspect_from_center(rect, state.aspect, bounds, &adjusted))
      rect = adjusted;
  }
  if (clamp_selection(rect, bounds, &rect)) {
    state.selection = rect;
    state.has_selection = TRUE;
    update_toolbar();
  }
}

static GdkMonitor *monitor_for_output(void) {
  const char *name = getenv("SHAULA_OVERLAY_OUTPUT_NAME");
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
  if (!state.has_aspect) {
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
  if ((ShaulaAspectChoice)idx == ASPECT_CUSTOM) {
    if (state.aspect_popover != NULL)
      gtk_popover_popdown(GTK_POPOVER(state.aspect_popover));
    open_custom_aspect_dialog();
    return;
  }
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
                             state.has_selection && state.selection.width > 0 &&
                                 state.selection.height > 0);
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
  gtk_widget_set_sensitive(capture_btn, state.has_selection);
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
    printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,"
           "\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":"
           "\"forced unavailable\"}}\n");
    fflush(stdout);
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
    printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,"
           "\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"gtk4-"
           "layer-shell is not supported by this compositor\"}}\n");
    fflush(stdout);
    return 36;
  }

  prefer_fast_overlay_renderer();
  gtk_init();
  memset(&state, 0, sizeof(state));
  state.toolbar = (ShaulaPoint){.x = PADDING, .y = PADDING};
  state.has_toolbar = TRUE;
  state.cursor_shape = CURSOR_UNSET;
  install_transparent_overlay_css();
  load_interaction_mode();
  load_aspect();
  load_background();

  if (!gtk_layer_is_supported()) {
    printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,"
           "\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"gtk4-"
           "layer-shell is not supported by this compositor\"}}\n");
    fflush(stdout);
    if (state.background != NULL)
      g_object_unref(state.background);
    return 36;
  }

  state.main_loop = g_main_loop_new(NULL, FALSE);
  if (state.main_loop == NULL) {
    printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,"
           "\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"gtk "
           "main loop could not be created\"}}\n");
    fflush(stdout);
    if (state.background != NULL)
      g_object_unref(state.background);
    return 36;
  }
  setup_overlay_window();
  g_main_loop_run(state.main_loop);
  g_main_loop_unref(state.main_loop);
  state.main_loop = NULL;
  if (state.background != NULL)
    g_object_unref(state.background);
  return 0;
}

#ifdef SHAULA_OVERLAY_STANDALONE
int main(void) { return shaula_native_gtk_overlay_run(); }
#endif
