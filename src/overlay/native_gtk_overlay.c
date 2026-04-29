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
  DRAG_TOOLBAR,
} ShaulaDragMode;

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
  ShaulaPoint drag_start;
  ShaulaRect drag_origin;
  gboolean dropdown_open;
  ShaulaAspectChoice aspect_choice;
  char aspect_custom_label[32];
  gboolean suppress_pointer_drag;
} ShaulaOverlayState;

static ShaulaOverlayState state;

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

static void queue_draw(void) {
    if (state.area != NULL) gtk_widget_queue_draw(state.area);
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
    int width = abs(point.x - anchor.x);
    int height = abs(point.y - anchor.y);
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

static void draw_handles(cairo_t *cr, ShaulaRect s) {
  double hw = 6;
  double hh = 6;
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
    rounded_rect(cr, px - hw / 2, py - hh / 2, hw, hh, 2);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
  }
}

static void draw_badge(cairo_t *cr, ShaulaRect s) {
  int y = MAX(8, s.y - 32);
  char label[32];
  snprintf(label, sizeof(label), "%d x %d", s.width, s.height);
  double badge_w = 108;
  double badge_h = 26;
  double badge_x = (double)s.x;
  double badge_y = (double)y;

  rounded_rect(cr, badge_x, badge_y, badge_w, badge_h, BADGE_RADIUS);
  cairo_set_source_rgba(cr, 0.06, 0.06, 0.08, 0.82);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
  cairo_set_line_width(cr, 0.5);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, label, &extents);
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
    rounded_rect(cr, (double)s.x, (double)s.y, (double)s.width, (double)s.height, 2);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
    draw_handles(cr, s);
    draw_badge(cr, s);
  }

  ShaulaPoint t = state.has_toolbar ? state.toolbar : (ShaulaPoint){ .x = PADDING, .y = PADDING };
  draw_toolbar(cr, t, state.has_selection);

  if (state.dropdown_open) {
    draw_dropdown(cr, t);
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
    printf("{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}\n");
    fflush(stdout);
    if (state.app != NULL) g_application_quit(G_APPLICATION(state.app));
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data) {
  (void)gesture;
  (void)data;
  ShaulaPoint p = { .x = (int)x, .y = (int)y };

  if (state.suppress_pointer_drag) {
    state.drag_mode = DRAG_TOOLBAR;
    return;
  }

  if (state.dropdown_open) {
    state.drag_mode = DRAG_TOOLBAR;
    return;
  }

  if (state.has_toolbar && toolbar_aspect_hit(p)) {
    state.drag_mode = DRAG_TOOLBAR;
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
  state.drag_start = p;
  state.drag_origin = state.selection;
  if (state.has_selection && point_in_selection(state.selection, p)) {
    state.drag_mode = DRAG_MOVE;
  } else {
    state.drag_mode = DRAG_CREATE;
    state.has_selection = FALSE;
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
    }
    update_toolbar();
    queue_draw();
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
    on_drag_update(gesture, dx, dy, data);
    state.drag_mode = DRAG_NONE;
    state.suppress_pointer_drag = FALSE;
    queue_draw();
}

static void on_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
  (void)gesture;
  (void)n_press;
  (void)data;
  ShaulaPoint p = { .x = (int)x, .y = (int)y };

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
    gtk_widget_set_focusable(area, TRUE);
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), size.x);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), size.y);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);
    gtk_window_set_child(GTK_WINDOW(window), area);

    GtkGesture *drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), NULL);
    g_signal_connect(click, "released", G_CALLBACK(on_click_released), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

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
