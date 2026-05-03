#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk4-layer-shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

enum {
  TOOLBAR_W = 326,
  TOOLBAR_H = 48,
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
  CAPTURE_ON_RELEASE = 1,
};

typedef enum {
  ASPECT_FREE,
  ASPECT_16_9,
  ASPECT_4_3,
  ASPECT_1_1,
  ASPECT_3_2,
  ASPECT_9_16,
  ASPECT_COUNT,
} ShaulaAspectChoice;

static const char *ASPECT_LABELS[ASPECT_COUNT] = {
  "Free", "16:9", "4:3", "1:1", "3:2", "9:16"
};

static const int ASPECT_WIDTHS[ASPECT_COUNT] =  { 0, 16, 4, 1, 3, 9 };
static const int ASPECT_HEIGHTS[ASPECT_COUNT] = { 0, 9,  3, 1, 2, 16 };

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
  CURSOR_MOVE,
  CURSOR_GRABBING,
  CURSOR_RESIZE_EW,
  CURSOR_RESIZE_NS,
  CURSOR_RESIZE_NWSE,
  CURSOR_RESIZE_NESW,
} ShaulaCursorShape;

typedef struct {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *area;
  GdkPixbuf *background;
  gboolean has_selection;
  ShaulaRect selection;
  gboolean has_toolbar;
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
  gboolean suppress_pointer_drag;
  ShaulaCursorShape cursor_shape;
} ShaulaOverlayState;

static ShaulaOverlayState state;

static void install_transparent_overlay_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        ".shaula-overlay-window, .shaula-overlay-area {"
        "  background: transparent;"
        "  background-color: transparent;"
        "}";
    gtk_css_provider_load_from_data(provider, css, -1);
    GdkDisplay *display = gdk_display_get_default();
    if (display != NULL) {
        gtk_style_context_add_provider_for_display(
            display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
}

static int clamp_int(int value, int low, int high) {
  if (high < low) return low;
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
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
        return (ShaulaPoint){
            .x = MAX(1, gtk_widget_get_width(state.area)),
            .y = MAX(1, gtk_widget_get_height(state.area)),
        };
    }
    return (ShaulaPoint){ .x = 1920, .y = 1080 };
}

static gboolean point_in_selection(ShaulaRect selection, ShaulaPoint point) {
    return point.x >= selection.x &&
        point.x <= selection.x + selection.width &&
        point.y >= selection.y &&
        point.y <= selection.y + selection.height;
}

static gboolean point_near(ShaulaPoint a, ShaulaPoint b, int radius) {
    return abs(a.x - b.x) <= radius && abs(a.y - b.y) <= radius;
}

static void queue_draw(void) {
    if (state.area != NULL) gtk_widget_queue_draw(state.area);
}

static const char *cursor_name(ShaulaCursorShape shape) {
    switch (shape) {
      case CURSOR_DEFAULT:
        return NULL;
      case CURSOR_CROSSHAIR:
        return "crosshair";
      case CURSOR_POINTER:
        return "pointer";
      case CURSOR_MOVE:
        return "move";
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
    if (state.area == NULL || state.cursor_shape == shape) return;
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

static gboolean clamp_selection(ShaulaRect input, ShaulaPoint bounds, ShaulaRect *out) {
    int left = clamp_int(input.x, 0, MAX(0, bounds.x - 1));
    int top = clamp_int(input.y, 0, MAX(0, bounds.y - 1));
    int right = clamp_int(input.x + input.width, 1, bounds.x);
    int bottom = clamp_int(input.y + input.height, 1, bounds.y);
    if (right <= left) right = MIN(bounds.x, left + 1);
    if (bottom <= top) bottom = MIN(bounds.y, top + 1);
    if (right <= left || bottom <= top) return FALSE;
    *out = (ShaulaRect){ .x = left, .y = top, .width = right - left, .height = bottom - top };
    return TRUE;
}

static gboolean geometry_from_points(ShaulaPoint anchor, ShaulaPoint point, ShaulaPoint bounds, ShaulaRect *out) {
    int width = abs(point.x - anchor.x) + 1;
    int height = abs(point.y - anchor.y) + 1;
    if (state.has_aspect) apply_aspect(&width, &height, state.aspect);
    if (width <= 0 || height <= 0) return FALSE;
    int x = point.x >= anchor.x ? anchor.x : anchor.x - width;
    int y = point.y >= anchor.y ? anchor.y : anchor.y - height;
    return clamp_selection((ShaulaRect){ .x = x, .y = y, .width = width, .height = height }, bounds, out);
}

static gboolean move_selection(ShaulaRect selection, int dx, int dy, ShaulaPoint bounds, ShaulaRect *out) {
    *out = (ShaulaRect){
        .x = clamp_int(selection.x + dx, 0, MAX(0, bounds.x - selection.width)),
        .y = clamp_int(selection.y + dy, 0, MAX(0, bounds.y - selection.height)),
        .width = selection.width,
        .height = selection.height,
    };
    return TRUE;
}

static gboolean normalize_selection(int left, int top, int right, int bottom, ShaulaPoint bounds, ShaulaRect *out) {
    int x0 = MIN(left, right);
    int x1 = MAX(left, right);
    int y0 = MIN(top, bottom);
    int y1 = MAX(top, bottom);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    return clamp_selection((ShaulaRect){ .x = x0, .y = y0, .width = x1 - x0, .height = y1 - y0 }, bounds, out);
}

static gboolean resize_free(ShaulaRect origin, ShaulaResizeHandle handle, ShaulaPoint point, ShaulaPoint bounds, ShaulaRect *out) {
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

static gboolean resize_aspect(ShaulaRect origin, ShaulaResizeHandle handle, ShaulaPoint point, ShaulaPoint bounds, ShaulaRect *out) {
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
        return geometry_from_points((ShaulaPoint){ .x = right, .y = bottom }, point, bounds, out);
      case HANDLE_TOP_RIGHT:
        return geometry_from_points((ShaulaPoint){ .x = left, .y = bottom }, point, bounds, out);
      case HANDLE_BOTTOM_RIGHT:
        return geometry_from_points((ShaulaPoint){ .x = left, .y = top }, point, bounds, out);
      case HANDLE_BOTTOM_LEFT:
        return geometry_from_points((ShaulaPoint){ .x = right, .y = top }, point, bounds, out);
      case HANDLE_LEFT:
        width = abs(right - point.x);
        height = MAX(1, (width * state.aspect.height) / state.aspect.width);
        return normalize_selection(right - width, center_y - height / 2, right, center_y + (height + 1) / 2, bounds, out);
      case HANDLE_RIGHT:
        width = abs(point.x + 1 - left);
        height = MAX(1, (width * state.aspect.height) / state.aspect.width);
        return normalize_selection(left, center_y - height / 2, left + width, center_y + (height + 1) / 2, bounds, out);
      case HANDLE_TOP:
        height = abs(bottom - point.y);
        width = MAX(1, (height * state.aspect.width) / state.aspect.height);
        return normalize_selection(center_x - width / 2, bottom - height, center_x + (width + 1) / 2, bottom, bounds, out);
      case HANDLE_BOTTOM:
        height = abs(point.y + 1 - top);
        width = MAX(1, (height * state.aspect.width) / state.aspect.height);
        return normalize_selection(center_x - width / 2, top, center_x + (width + 1) / 2, top + height, bounds, out);
      default:
        return FALSE;
    }
}

static gboolean resize_selection(ShaulaRect origin, ShaulaResizeHandle handle, ShaulaPoint point, ShaulaPoint bounds, ShaulaRect *out) {
    if (state.has_aspect) return resize_aspect(origin, handle, point, bounds, out);
    return resize_free(origin, handle, point, bounds, out);
}

static ShaulaResizeHandle resize_handle_at(ShaulaRect s, ShaulaPoint p) {
  int left = s.x;
  int top = s.y;
  int right = s.x + s.width;
  int bottom = s.y + s.height;
  int mid_x = s.x + s.width / 2;
  int mid_y = s.y + s.height / 2;

  if (point_near(p, (ShaulaPoint){ .x = left, .y = top }, RESIZE_HIT_RADIUS)) return HANDLE_TOP_LEFT;
  if (point_near(p, (ShaulaPoint){ .x = right, .y = top }, RESIZE_HIT_RADIUS)) return HANDLE_TOP_RIGHT;
  if (point_near(p, (ShaulaPoint){ .x = right, .y = bottom }, RESIZE_HIT_RADIUS)) return HANDLE_BOTTOM_RIGHT;
  if (point_near(p, (ShaulaPoint){ .x = left, .y = bottom }, RESIZE_HIT_RADIUS)) return HANDLE_BOTTOM_LEFT;
  if (point_near(p, (ShaulaPoint){ .x = mid_x, .y = top }, RESIZE_HIT_RADIUS)) return HANDLE_TOP;
  if (point_near(p, (ShaulaPoint){ .x = right, .y = mid_y }, RESIZE_HIT_RADIUS)) return HANDLE_RIGHT;
  if (point_near(p, (ShaulaPoint){ .x = mid_x, .y = bottom }, RESIZE_HIT_RADIUS)) return HANDLE_BOTTOM;
  if (point_near(p, (ShaulaPoint){ .x = left, .y = mid_y }, RESIZE_HIT_RADIUS)) return HANDLE_LEFT;

  if (p.x >= left - RESIZE_HIT_RADIUS && p.x <= right + RESIZE_HIT_RADIUS) {
    if (abs(p.y - top) <= RESIZE_HIT_RADIUS) return HANDLE_TOP;
    if (abs(p.y - bottom) <= RESIZE_HIT_RADIUS) return HANDLE_BOTTOM;
  }
  if (p.y >= top - RESIZE_HIT_RADIUS && p.y <= bottom + RESIZE_HIT_RADIUS) {
    if (abs(p.x - left) <= RESIZE_HIT_RADIUS) return HANDLE_LEFT;
    if (abs(p.x - right) <= RESIZE_HIT_RADIUS) return HANDLE_RIGHT;
  }

  return HANDLE_NONE;
}

static void update_toolbar(void) {
    if (!state.has_selection) return;
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
        candidate = (ShaulaPoint){ .x = clamp_int(centered_x, min_x, max_x), .y = below_y };
    } else if (above_y >= min_y) {
        candidate = (ShaulaPoint){ .x = clamp_int(centered_x, min_x, max_x), .y = above_y };
    } else {
        candidate = (ShaulaPoint){
            .x = clamp_int((bounds.x - TOOLBAR_W) / 2, min_x, max_x),
            .y = bounds.y - (selection.y + selection.height) >= selection.y ? max_y : min_y,
        };
    }

    candidate.x = clamp_int(candidate.x, min_x, max_x);
    candidate.y = clamp_int(candidate.y, min_y, max_y);
    if (state.has_toolbar &&
        abs(state.toolbar.x - candidate.x) <= JITTER &&
        abs(state.toolbar.y - candidate.y) <= JITTER) {
        return;
    }
    state.toolbar = candidate;
    state.has_toolbar = TRUE;
}

static gboolean toolbar_aspect_hit(ShaulaPoint point) {
  if (!state.has_toolbar) return FALSE;
  ShaulaPoint t = state.toolbar;
  return point.x >= t.x + 10 && point.x <= t.x + 110 &&
         point.y >= t.y + 9 && point.y <= t.y + 39;
}

static gboolean toolbar_capture_hit(ShaulaPoint point) {
  if (!state.has_toolbar) return FALSE;
  ShaulaPoint t = state.toolbar;
  return point.x >= t.x + 178 && point.x <= t.x + 252 &&
         point.y >= t.y + 9 && point.y <= t.y + 39;
}

static gboolean toolbar_cancel_hit(ShaulaPoint point) {
  if (!state.has_toolbar) return FALSE;
  ShaulaPoint t = state.toolbar;
  return point.x >= t.x + 260 && point.x <= t.x + 316 &&
         point.y >= t.y + 9 && point.y <= t.y + 39;
}

static ShaulaRect dropdown_rect(void) {
  if (!state.has_toolbar) return (ShaulaRect){ .x = 0, .y = 0, .width = 0, .height = 0 };
  ShaulaPoint t = state.toolbar;
  ShaulaPoint bounds = output_size();
  int dd_w = 100;
  int dd_h = DROPDOWN_PADDING * 2 + ASPECT_COUNT * DROPDOWN_ITEM_H;
  int dd_x = clamp_int(t.x + 10, PADDING, MAX(PADDING, bounds.x - PADDING - dd_w));
  int below_y = t.y + TOOLBAR_H + 6;
  int above_y = t.y - dd_h - 6;
  int dd_y = below_y + dd_h <= bounds.y - PADDING ? below_y : above_y;
  dd_y = clamp_int(dd_y, PADDING, MAX(PADDING, bounds.y - PADDING - dd_h));
  return (ShaulaRect){ .x = dd_x, .y = dd_y, .width = dd_w, .height = dd_h };
}

static int dropdown_item_at(ShaulaPoint point) {
  if (!state.dropdown_open || !state.has_toolbar) return -1;
  ShaulaRect dd = dropdown_rect();
  if (point.x < dd.x || point.x > dd.x + dd.width || point.y < dd.y || point.y > dd.y + dd.height) return -1;
  int relative_y = point.y - dd.y - DROPDOWN_PADDING;
  if (relative_y < 0) return -1;
  int idx = relative_y / DROPDOWN_ITEM_H;
  return idx < ASPECT_COUNT ? idx : -1;
}

static ShaulaCursorShape resolve_hover_cursor(ShaulaPoint p) {
  if (!CAPTURE_ON_RELEASE && state.dropdown_open && dropdown_item_at(p) >= 0) return CURSOR_POINTER;
  if (!CAPTURE_ON_RELEASE && state.has_toolbar &&
      (toolbar_aspect_hit(p) ||
       (state.has_selection && toolbar_capture_hit(p)) ||
       toolbar_cancel_hit(p))) {
    return CURSOR_POINTER;
  }
  if (state.has_selection) {
    ShaulaResizeHandle handle = resize_handle_at(state.selection, p);
    if (handle != HANDLE_NONE) return cursor_for_handle(handle);
    if (point_in_selection(state.selection, p)) return CURSOR_MOVE;
  }
  return CURSOR_CROSSHAIR;
}

static void apply_aspect_choice(void) {
  if (state.aspect_choice == ASPECT_FREE) {
    state.has_aspect = FALSE;
    state.aspect_custom_label[0] = '\0';
  } else {
    state.has_aspect = TRUE;
    state.aspect = (ShaulaAspect){
      .width = ASPECT_WIDTHS[(int)state.aspect_choice],
      .height = ASPECT_HEIGHTS[(int)state.aspect_choice],
    };
  }
  if (state.has_selection && state.has_aspect) {
    ShaulaPoint bounds = output_size();
    int w = state.selection.width;
    int h = state.selection.height;
    apply_aspect(&w, &h, state.aspect);
    ShaulaRect adj = { .x = state.selection.x, .y = state.selection.y, .width = w, .height = h };
    if (clamp_selection(adj, bounds, &adj)) {
      state.selection = adj;
    }
  }
  update_toolbar();
  state.dropdown_open = FALSE;
}

static void draw_background(cairo_t *cr, int width, int height) {
    if (state.background == NULL) return;
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(state.background, width, height, GDK_INTERP_BILINEAR);
    if (scaled == NULL) return;
    gdk_cairo_set_source_pixbuf(cr, scaled, 0, 0);
    cairo_paint(cr);
    g_object_unref(scaled);
}

static void draw_pill(cairo_t *cr, int x, int y, int width, int height, const char *label, double r, double g, double b, double a, gboolean active) {
  double rx = (double)x;
  double ry = (double)y;
  double rw = (double)width;
  double rh = (double)height;

  rounded_rect(cr, rx, ry, rw, rh, PILL_RADIUS);
  if (active) {
    cairo_set_source_rgba(cr, r, g, b, a);
  } else {
    cairo_set_source_rgba(cr, r * 0.8, g * 0.8, b * 0.8, 0.45);
  }
  cairo_fill_preserve(cr);

  cairo_set_source_rgba(cr, 1, 1, 1, active ? 0.12 : 0.06);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 12.5);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, label, &extents);
  double text_x = rx + (rw - extents.width) / 2 - extents.x_bearing;
  double text_y = ry + (rh - extents.height) / 2 - extents.y_bearing;
  cairo_move_to(cr, text_x, text_y);
  cairo_set_source_rgba(cr, 1, 1, 1, active ? 0.95 : 0.5);
  cairo_show_text(cr, label);
}

static void draw_chevron(cairo_t *cr, double cx, double cy, gboolean open) {
  double s = 3.5;
  cairo_set_line_width(cr, 1.5);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
  cairo_move_to(cr, cx - s, open ? cy + s : cy - s);
  cairo_line_to(cr, cx, open ? cy : cy);
  cairo_line_to(cr, cx + s, open ? cy + s : cy - s);
  cairo_stroke(cr);
}

static void draw_selection_guides(cairo_t *cr, ShaulaRect s, int width, int height) {
  double dash[] = { 5.0, 7.0 };
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
  ShaulaResizeHandle emphasized = state.drag_mode == DRAG_RESIZE ? state.active_handle : state.hover_handle;
  int emphasized_index = handle_index(emphasized);
  ShaulaPoint points[8] = {
    { s.x, s.y },
    { s.x + s.width / 2, s.y },
    { s.x + s.width, s.y },
    { s.x + s.width, s.y + s.height / 2 },
    { s.x + s.width, s.y + s.height },
    { s.x + s.width / 2, s.y + s.height },
    { s.x, s.y + s.height },
    { s.x, s.y + s.height / 2 },
  };
  for (int i = 0; i < 8; i += 1) {
    double px = (double)points[i].x;
    double py = (double)points[i].y;
    double scale = i == emphasized_index ? 1.45 : 1.0;
    double handle_w = hw * scale;
    double handle_h = hh * scale;
    rounded_rect(cr, px - handle_w / 2, py - handle_h / 2, handle_w, handle_h, 2.5);
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
  snprintf(label, sizeof(label), "x %d y %d  %d x %d", s.x, s.y, s.width, s.height);
  double badge_h = 26;

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, label, &extents);

  double badge_w = MAX(126.0, extents.width + 22.0);
  double badge_x = (double)clamp_int(s.x, 8, MAX(8, width - (int)badge_w - 8));
  double badge_y = s.y >= 36 ? (double)(s.y - 32) : (double)MIN(height - (int)badge_h - 8, s.y + s.height + 8);

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

static void draw_toolbar(cairo_t *cr, ShaulaPoint t, gboolean enabled) {
  double tx = (double)t.x;
  double ty = (double)t.y;

  cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
  rounded_rect(cr, tx + 1, ty + 3, TOOLBAR_W, TOOLBAR_H, TOOLBAR_RADIUS + 1);
  cairo_fill(cr);

  rounded_rect(cr, tx, ty, TOOLBAR_W, TOOLBAR_H, TOOLBAR_RADIUS);
  cairo_set_source_rgba(cr, 0.10, 0.10, 0.13, 0.78);
  cairo_fill_preserve(cr);

  cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);

  rounded_rect(cr, tx + 1, ty + 1, TOOLBAR_W - 2, 1, 0);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
  cairo_fill(cr);

  const char *aspect_lbl = state.has_aspect && state.aspect_choice == ASPECT_FREE && state.aspect_custom_label[0] != '\0'
    ? state.aspect_custom_label
    : ASPECT_LABELS[state.aspect_choice];
  draw_pill(cr, t.x + 10, t.y + 9, 100, 30, aspect_lbl, 0.28, 0.52, 0.83, 0.55, TRUE);
  draw_chevron(cr, tx + 10 + 100 - 16, ty + 9 + 15, state.dropdown_open);

  draw_pill(cr, t.x + 118, t.y + 9, 52, 30, "Area", 0.35, 0.42, 0.52, 0.4, TRUE);
  if (enabled) {
    draw_pill(cr, t.x + 178, t.y + 9, 74, 30, "Capture", 0.22, 0.72, 0.42, 0.6, TRUE);
  } else {
    draw_pill(cr, t.x + 178, t.y + 9, 74, 30, "Capture", 0.30, 0.33, 0.35, 0.3, FALSE);
  }
  draw_pill(cr, t.x + 260, t.y + 9, 56, 30, "Esc", 0.52, 0.28, 0.28, 0.4, TRUE);
}

static void draw_dropdown(cairo_t *cr, ShaulaPoint t) {
  (void)t;
  ShaulaRect dd = dropdown_rect();
  double dd_x = (double)dd.x;
  double dd_y = (double)dd.y;
  double dd_w = (double)dd.width;
  double dd_h = (double)dd.height;

  cairo_set_source_rgba(cr, 0, 0, 0, 0.20);
  rounded_rect(cr, dd_x + 1, dd_y + 3, dd_w, dd_h, DROPDOWN_RADIUS + 1);
  cairo_fill(cr);

  rounded_rect(cr, dd_x, dd_y, dd_w, dd_h, DROPDOWN_RADIUS);
  cairo_set_source_rgba(cr, 0.08, 0.08, 0.11, 0.92);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
  cairo_set_line_width(cr, 0.5);
  cairo_stroke(cr);

  rounded_rect(cr, dd_x + 1, dd_y + 1, dd_w - 2, 1, 0);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
  cairo_fill(cr);

  for (int i = 0; i < ASPECT_COUNT; i += 1) {
    double item_y = dd_y + DROPDOWN_PADDING + i * DROPDOWN_ITEM_H;
    double item_h = (double)DROPDOWN_ITEM_H;

    if (i == (int)state.aspect_choice) {
      rounded_rect(cr, dd_x + 4, item_y + 2, dd_w - 8, item_h - 4, 6);
      cairo_set_source_rgba(cr, 0.28, 0.52, 0.83, 0.25);
      cairo_fill(cr);
    }

    if (i < ASPECT_COUNT - 1) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
      cairo_set_line_width(cr, 0.5);
      cairo_move_to(cr, dd_x + 10, item_y + item_h);
      cairo_line_to(cr, dd_x + dd_w - 10, item_y + item_h);
      cairo_stroke(cr);
    }

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, i == (int)state.aspect_choice ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.5);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, ASPECT_LABELS[i], &extents);
    double text_x = dd_x + (dd_w - extents.width) / 2 - extents.x_bearing;
    double text_y = item_y + (item_h - extents.height) / 2 - extents.y_bearing;
    cairo_move_to(cr, text_x, text_y);
    cairo_set_source_rgba(cr, 1, 1, 1, i == (int)state.aspect_choice ? 0.95 : 0.6);
    cairo_show_text(cr, ASPECT_LABELS[i]);
  }
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
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
    rounded_rect(cr, (double)s.x, (double)s.y, (double)s.width, (double)s.height, 2);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
    draw_handles(cr, s);
    draw_badge(cr, s, width, height);
  }

  if (!CAPTURE_ON_RELEASE) {
    ShaulaPoint t = state.has_toolbar ? state.toolbar : (ShaulaPoint){ .x = PADDING, .y = PADDING };
    draw_toolbar(cr, t, state.has_selection);

    if (state.dropdown_open) {
      draw_dropdown(cr, t);
    }
  }
}

static void confirm(void) {
    if (!state.has_selection || state.selection.width <= 0 || state.selection.height <= 0) return;
    printf("{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},\"error\":null}\n",
        state.selection.x, state.selection.y, state.selection.width, state.selection.height);
    fflush(stdout);
    if (state.app != NULL) g_application_quit(G_APPLICATION(state.app));
}

static void cancel(void) {
    if (state.has_selection && state.selection.width > 0 && state.selection.height > 0) {
        printf("{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},\"error\":null}\n",
            state.selection.x, state.selection.y, state.selection.width, state.selection.height);
    } else {
        printf("{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}\n");
    }
    fflush(stdout);
    if (state.app != NULL) g_application_quit(G_APPLICATION(state.app));
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data) {
  (void)gesture;
  (void)data;
  ShaulaPoint p = { .x = (int)x, .y = (int)y };

  if (!CAPTURE_ON_RELEASE) {
    if (state.suppress_pointer_drag) {
      state.drag_mode = DRAG_TOOLBAR;
      apply_cursor(resolve_hover_cursor(p));
      return;
    }

    if (state.dropdown_open) {
      state.drag_mode = DRAG_TOOLBAR;
      apply_cursor(resolve_hover_cursor(p));
      return;
    }

    if (state.has_toolbar && toolbar_aspect_hit(p)) {
      state.drag_mode = DRAG_TOOLBAR;
      apply_cursor(CURSOR_POINTER);
      return;
    }

    if (state.has_selection && toolbar_capture_hit(p)) {
      state.drag_mode = DRAG_TOOLBAR;
      return;
    }
    if (toolbar_cancel_hit(p)) {
      state.drag_mode = DRAG_TOOLBAR;
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
  if (state.has_selection && point_in_selection(state.selection, p)) {
    state.drag_mode = DRAG_MOVE;
    apply_cursor(CURSOR_GRABBING);
  } else {
    state.drag_mode = DRAG_CREATE;
    state.has_selection = FALSE;
    apply_cursor(CURSOR_CROSSHAIR);
  }
  queue_draw();
}

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
    (void)gesture;
    (void)data;
    ShaulaPoint bounds = output_size();
    ShaulaPoint p = { .x = state.drag_start.x + (int)dx, .y = state.drag_start.y + (int)dy };
    ShaulaRect next;
    if (state.drag_mode == DRAG_CREATE) {
        state.has_selection = geometry_from_points(state.drag_start, p, bounds, &next);
        if (state.has_selection) state.selection = next;
    } else if (state.drag_mode == DRAG_MOVE) {
        if (move_selection(state.drag_origin, (int)dx, (int)dy, bounds, &next)) {
            state.selection = next;
            state.has_selection = TRUE;
        }
    } else if (state.drag_mode == DRAG_RESIZE) {
        if (resize_selection(state.drag_origin, state.active_handle, p, bounds, &next)) {
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

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
    ShaulaDragMode completed_mode = state.drag_mode;
    on_drag_update(gesture, dx, dy, data);
    gboolean should_confirm =
        CAPTURE_ON_RELEASE &&
        (completed_mode == DRAG_CREATE ||
         completed_mode == DRAG_MOVE ||
         completed_mode == DRAG_RESIZE) &&
        state.has_selection &&
        state.selection.width > 0 &&
        state.selection.height > 0;
    state.drag_mode = DRAG_NONE;
    state.active_handle = HANDLE_NONE;
    state.suppress_pointer_drag = FALSE;
    ShaulaPoint p = { .x = state.drag_start.x + (int)dx, .y = state.drag_start.y + (int)dy };
    state.hover_handle = state.has_selection ? resize_handle_at(state.selection, p) : HANDLE_NONE;
    apply_cursor(resolve_hover_cursor(p));
    queue_draw();
    if (should_confirm) {
        confirm();
    }
}

static void on_motion(GtkEventControllerMotion *controller, double x, double y, gpointer data) {
  (void)controller;
  (void)data;
  if (state.drag_mode != DRAG_NONE) return;
  ShaulaPoint p = { .x = (int)x, .y = (int)y };
  ShaulaResizeHandle next_hover = state.has_selection ? resize_handle_at(state.selection, p) : HANDLE_NONE;
  if (next_hover != state.hover_handle) {
    state.hover_handle = next_hover;
    queue_draw();
  }
  apply_cursor(resolve_hover_cursor(p));
}

static void on_motion_enter(GtkEventControllerMotion *controller, double x, double y, gpointer data) {
  on_motion(controller, x, y, data);
}

static void on_motion_leave(GtkEventControllerMotion *controller, gpointer data) {
  (void)controller;
  (void)data;
  state.hover_handle = HANDLE_NONE;
  apply_cursor(CURSOR_DEFAULT);
  queue_draw();
}

static void on_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
  (void)gesture;
  (void)n_press;
  (void)data;
  ShaulaPoint p = { .x = (int)x, .y = (int)y };

  if (CAPTURE_ON_RELEASE) {
    state.suppress_pointer_drag = FALSE;
    state.dropdown_open = FALSE;
    return;
  }

  if (state.dropdown_open) {
    int idx = dropdown_item_at(p);
    state.suppress_pointer_drag = TRUE;
    if (idx >= 0) {
      state.aspect_choice = (ShaulaAspectChoice)idx;
      apply_aspect_choice();
      queue_draw();
      return;
    }
    state.dropdown_open = FALSE;
    queue_draw();
    return;
  }

  if (state.has_toolbar && toolbar_aspect_hit(p)) {
    state.suppress_pointer_drag = TRUE;
    state.dropdown_open = !state.dropdown_open;
    queue_draw();
    return;
  }

  if (state.has_selection && toolbar_capture_hit(p)) {
    state.suppress_pointer_drag = TRUE;
    confirm();
  } else if (toolbar_cancel_hit(p)) {
    state.suppress_pointer_drag = TRUE;
    cancel();
  }
}

static void on_click_released(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
  (void)gesture;
  (void)n_press;
  (void)x;
  (void)y;
  (void)data;
  state.suppress_pointer_drag = FALSE;
}

static gboolean on_key(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType modifiers, gpointer data) {
  (void)controller;
  (void)keycode;
  (void)data;
  if (keyval == GDK_KEY_Escape) {
    if (state.dropdown_open) {
      state.dropdown_open = FALSE;
      queue_draw();
      return TRUE;
    }
    cancel();
    return TRUE;
  }
  if (keyval == GDK_KEY_q) {
    cancel();
    return TRUE;
  }
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    if (state.dropdown_open) {
      state.dropdown_open = FALSE;
      queue_draw();
      return TRUE;
    }
    confirm();
    return TRUE;
  }

    int dx = 0;
    int dy = 0;
    if (keyval == GDK_KEY_Left) dx = -1;
    if (keyval == GDK_KEY_Right) dx = 1;
    if (keyval == GDK_KEY_Up) dy = -1;
    if (keyval == GDK_KEY_Down) dy = 1;
    if ((dx != 0 || dy != 0) && state.has_selection) {
        int step = (modifiers & GDK_SHIFT_MASK) != 0 ? 10 : 1;
        ShaulaRect next;
        if (move_selection(state.selection, dx * step, dy * step, output_size(), &next)) {
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
    return FALSE;
  }
  int w = 0;
  int h = 0;
  if (sscanf(raw, "%d:%d", &w, &h) != 2 || w <= 0 || h <= 0) {
    state.has_aspect = FALSE;
    state.aspect_choice = ASPECT_FREE;
    state.aspect_custom_label[0] = '\0';
    return FALSE;
  }
  state.aspect = (ShaulaAspect){ .width = w, .height = h };
  state.has_aspect = TRUE;
  state.aspect_choice = ASPECT_FREE;
  snprintf(state.aspect_custom_label, sizeof(state.aspect_custom_label), "%d:%d", w, h);
  for (int i = 1; i < ASPECT_COUNT; i += 1) {
    if (ASPECT_WIDTHS[i] == w && ASPECT_HEIGHTS[i] == h) {
      state.aspect_choice = (ShaulaAspectChoice)i;
      state.aspect_custom_label[0] = '\0';
      break;
    }
  }
  return TRUE;
}

static void load_background(void) {
    const char *path = getenv("SHAULA_OVERLAY_BACKGROUND_PATH");
    if (path == NULL || path[0] == '\0') return;
    state.background = gdk_pixbuf_new_from_file(path, NULL);
}

static void load_initial_geometry(void) {
  const char *raw = getenv("SHAULA_OVERLAY_INITIAL_GEOMETRY");
  if (raw == NULL || raw[0] == '\0') return;

  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  if (sscanf(raw, "%d,%d,%d,%d", &x, &y, &width, &height) != 4 || width <= 0 || height <= 0) {
    return;
  }

  ShaulaRect rect;
  if (!clamp_selection((ShaulaRect){ .x = x, .y = y, .width = width, .height = height }, output_size(), &rect)) {
    return;
  }

  state.selection = rect;
  state.has_selection = TRUE;
  update_toolbar();
}

static GdkMonitor *monitor_for_output(void) {
    const char *name = getenv("SHAULA_OVERLAY_OUTPUT_NAME");
    if (name == NULL || name[0] == '\0') return NULL;
    GdkDisplay *display = gdk_display_get_default();
    if (display == NULL) return NULL;
    GListModel *monitors = gdk_display_get_monitors(display);
    guint count = g_list_model_get_n_items(monitors);
    for (guint i = 0; i < count; i += 1) {
        GObject *object = g_list_model_get_item(monitors, i);
        if (object == NULL) continue;
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
        return (ShaulaPoint){ .x = MAX(1, rect.width), .y = MAX(1, rect.height) };
    }
    return (ShaulaPoint){ .x = 1920, .y = 1080 };
}

static void on_activate(GtkApplication *app, gpointer data) {
    (void)data;
    GtkWidget *window = gtk_application_window_new(app);
    state.window = window;

    gtk_window_set_title(GTK_WINDOW(window), "shaula-overlay");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_widget_add_css_class(window, "shaula-overlay-window");

    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_namespace(GTK_WINDOW(window), "shaula-overlay");
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    GdkMonitor *monitor = monitor_for_output();
    if (monitor != NULL) {
        gtk_layer_set_monitor(GTK_WINDOW(window), monitor);
        g_object_unref(monitor);
    }
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(window), -1);

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
    gtk_window_set_child(GTK_WINDOW(window), area);
    load_initial_geometry();

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
}

int shaula_native_gtk_overlay_run(void) {
    if (getenv("SHAULA_OVERLAY_HELPER_FORCE_UNAVAILABLE") != NULL) {
        printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"forced unavailable\"}}\n");
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
            printf("{\"status\":\"ok\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}\n");
            fflush(stdout);
            return 0;
        }
        printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"gtk4-layer-shell is not supported by this compositor\"}}\n");
        fflush(stdout);
        return 36;
    }

    gtk_init();
    memset(&state, 0, sizeof(state));
    state.toolbar = (ShaulaPoint){ .x = PADDING, .y = PADDING };
    state.has_toolbar = TRUE;
    state.cursor_shape = CURSOR_UNSET;
    install_transparent_overlay_css();
    load_aspect();
    load_background();

    if (!gtk_layer_is_supported()) {
        printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"gtk4-layer-shell is not supported by this compositor\"}}\n");
        fflush(stdout);
        if (state.background != NULL) g_object_unref(state.background);
        return 36;
    }

    GtkApplication *app = gtk_application_new("dev.shaula.overlay", G_APPLICATION_DEFAULT_FLAGS);
    if (app == NULL) {
        printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"gtk application could not be created\"}}\n");
        fflush(stdout);
        if (state.background != NULL) g_object_unref(state.background);
        return 36;
    }
    state.app = app;
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int rc = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    if (state.background != NULL) g_object_unref(state.background);
    return rc > 255 ? 255 : rc;
}

#ifdef SHAULA_OVERLAY_STANDALONE
int main(void) {
    return shaula_native_gtk_overlay_run();
}
#endif
