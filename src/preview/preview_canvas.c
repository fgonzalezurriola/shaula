#include "preview_canvas.h"

#include <math.h>
#include <stdio.h>

#include "preview_actions.h"
#include "preview_commands.h"
#include "preview_properties_panel.h"
#include "preview_spotlight.h"
#include "preview_toolbar.h"

ShaulaPoint shaula_preview_canvas_screen_to_image(ShaulaPreviewState *state,
                                                  double x, double y) {
  if (state->zoom <= 0.0)
    return (ShaulaPoint){0, 0};
  return (ShaulaPoint){(x - state->pan_x) / state->zoom,
                       (y - state->pan_y) / state->zoom};
}

ShaulaPoint shaula_preview_canvas_image_to_screen(ShaulaPreviewState *state,
                                                  ShaulaPoint point) {
  return (ShaulaPoint){state->pan_x + point.x * state->zoom,
                       state->pan_y + point.y * state->zoom};
}

static gboolean image_point_is_inside(ShaulaPreviewState *state,
                                      ShaulaPoint point) {
  return point.x >= 0.0 && point.y >= 0.0 &&
         point.x <= shaula_preview_image_width(state) &&
         point.y <= shaula_preview_image_height(state);
}

static ShaulaPoint clamped_image_point(ShaulaPreviewState *state,
                                       ShaulaPoint point) {
  return shaula_point_clamped(point, shaula_preview_image_width(state),
                              shaula_preview_image_height(state));
}

static gboolean image_point_to_pixel(ShaulaPreviewState *state,
                                     ShaulaPoint point, int *px, int *py) {
  int width = shaula_preview_image_width(state);
  int height = shaula_preview_image_height(state);
  if (width <= 0 || height <= 0 || point.x < 0.0 || point.y < 0.0 ||
      point.x >= (double)width || point.y >= (double)height)
    return FALSE;
  *px = CLAMP((int)floor(point.x), 0, width - 1);
  *py = CLAMP((int)floor(point.y), 0, height - 1);
  return TRUE;
}

/* The hover readout samples document pixels in image coordinates, not GTK
 * chrome or transient editor UI. Rendering only a translated 1x1 surface keeps
 * motion updates cheap while matching export composition for stored
 * annotations and document effects.
 */
static gboolean sample_composited_pixel(ShaulaPreviewState *state, int px,
                                        int py, ShaulaColor *color) {
  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return FALSE;
  }

  cairo_t *cr = cairo_create(surface);
  cairo_translate(cr, -(double)px, -(double)py);
  gdk_cairo_set_source_pixbuf(cr, state->image, 0, 0);
  cairo_paint(cr);

  cairo_save(cr);
  cairo_rectangle(cr, px, py, 1, 1);
  cairo_clip(cr);
  for (guint i = 0; state->annotations != NULL && i < state->annotations->len;
       i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(state->annotations, i);
    gboolean selected = annotation->selected;
    annotation->selected = FALSE;
    shaula_annotation_draw(cr, annotation);
    annotation->selected = selected;
  }
  shaula_preview_draw_spotlight_effect(state, cr);
  cairo_restore(cr);
  cairo_destroy(cr);

  cairo_surface_flush(surface);
  unsigned char *data = cairo_image_surface_get_data(surface);
  guint32 pixel = *(guint32 *)data;
  guint8 b = pixel & 0xff;
  guint8 g = (pixel >> 8) & 0xff;
  guint8 r = (pixel >> 16) & 0xff;
  guint8 a = (pixel >> 24) & 0xff;
  if (a > 0) {
    color->r = CLAMP(((double)r * 255.0) / ((double)a * 255.0), 0.0, 1.0);
    color->g = CLAMP(((double)g * 255.0) / ((double)a * 255.0), 0.0, 1.0);
    color->b = CLAMP(((double)b * 255.0) / ((double)a * 255.0), 0.0, 1.0);
  } else {
    color->r = 0.0;
    color->g = 0.0;
    color->b = 0.0;
  }
  color->a = (double)a / 255.0;
  cairo_surface_destroy(surface);
  return TRUE;
}

static gboolean update_hover_color(ShaulaPreviewState *state, double x,
                                   double y) {
  if (state == NULL || state->image == NULL)
    return FALSE;
  ShaulaPoint image_point =
      shaula_preview_canvas_screen_to_image(state, x, y);
  int px = 0;
  int py = 0;
  if (!image_point_to_pixel(state, image_point, &px, &py))
    return FALSE;

  ShaulaColor color;
  if (!sample_composited_pixel(state, px, py, &color))
    return FALSE;

  gboolean changed =
      !state->hover_color_valid || (int)floor(state->hover_image_point.x) != px ||
      (int)floor(state->hover_image_point.y) != py ||
      fabs(state->hover_color.r - color.r) > 0.0001 ||
      fabs(state->hover_color.g - color.g) > 0.0001 ||
      fabs(state->hover_color.b - color.b) > 0.0001 ||
      fabs(state->hover_color.a - color.a) > 0.0001;
  if (!changed)
    return TRUE;

  state->hover_color_valid = TRUE;
  state->hover_image_point = (ShaulaPoint){(double)px, (double)py};
  state->hover_color = color;
  shaula_color_to_hex(color, state->hover_hex);
  if (state->color_hex_label != NULL)
    gtk_label_set_text(GTK_LABEL(state->color_hex_label), state->hover_hex);
  if (state->color_swatch != NULL)
    gtk_widget_queue_draw(state->color_swatch);
  return TRUE;
}

static gboolean update_hover_color_from_pointer(ShaulaPreviewState *state) {
  if (state == NULL || state->area == NULL)
    return FALSE;

  /* Tab copy must not depend on a prior motion event. Query the current GDK
   * pointer position on the preview surface, then translate it into drawing
   * area coordinates before using the normal canvas-to-image mapping.
   */
  GtkNative *native = gtk_widget_get_native(state->area);
  if (native == NULL)
    return FALSE;
  GdkSurface *surface = gtk_native_get_surface(native);
  if (surface == NULL)
    return FALSE;
  GdkSeat *seat = gdk_display_get_default_seat(gdk_surface_get_display(surface));
  GdkDevice *pointer = seat != NULL ? gdk_seat_get_pointer(seat) : NULL;
  if (pointer == NULL)
    return FALSE;

  double surface_x = 0.0;
  double surface_y = 0.0;
  if (!gdk_surface_get_device_position(surface, pointer, &surface_x, &surface_y,
                                       NULL))
    return FALSE;

  double native_x = 0.0;
  double native_y = 0.0;
  double area_x = 0.0;
  double area_y = 0.0;
  gtk_native_get_surface_transform(native, &native_x, &native_y);
  if (!gtk_widget_translate_coordinates(GTK_WIDGET(native), state->area,
                                        surface_x - native_x,
                                        surface_y - native_y, &area_x,
                                        &area_y))
    return FALSE;
  return update_hover_color(state, area_x, area_y);
}

static void draw_checker_background(ShaulaPreviewState *state, cairo_t *cr,
                                    int width, int height) {
  if (state->is_dark) {
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
      if (state->is_dark) {
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
}

static void draw_image_frame(ShaulaPreviewState *state, cairo_t *cr) {
  int image_w = shaula_preview_image_width(state);
  int image_h = shaula_preview_image_height(state);
  double draw_w = (double)image_w * state->zoom;
  double draw_h = (double)image_h * state->zoom;

  if (state->is_dark) {
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  } else {
    cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
  }
  cairo_rectangle(cr, state->pan_x + 2, state->pan_y + 4, draw_w, draw_h);
  cairo_fill(cr);

  cairo_save(cr);
  cairo_translate(cr, state->pan_x, state->pan_y);
  cairo_scale(cr, state->zoom, state->zoom);
  gdk_cairo_set_source_pixbuf(cr, state->image, 0, 0);
  cairo_paint(cr);

  cairo_rectangle(cr, 0, 0, image_w, image_h);
  cairo_clip(cr);
  for (guint i = 0; state->annotations != NULL && i < state->annotations->len;
       i++)
    shaula_annotation_draw(cr, g_ptr_array_index(state->annotations, i));
  shaula_preview_draw_spotlight_effect(state, cr);
  cairo_restore(cr);

  if (state->is_dark) {
    cairo_set_source_rgba(cr, 1, 1, 1, 0.14);
  } else {
    cairo_set_source_rgba(cr, 0, 0, 0, 0.12);
  }
  cairo_set_line_width(cr, 1);
  cairo_rectangle(cr, state->pan_x + 0.5, state->pan_y + 0.5, draw_w, draw_h);
  cairo_stroke(cr);
}

static void draw_draft_rect(cairo_t *cr, ShaulaRect rect, ShaulaColor color,
                            gboolean fill) {
  rect = shaula_rect_normalized(rect);
  if (shaula_rect_is_empty(rect))
    return;
  if (fill) {
    cairo_set_source_rgba(cr, color.r, color.g, color.b, 0.24);
    cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
    cairo_fill_preserve(cr);
  } else {
    cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
  }
  cairo_set_source_rgba(cr, color.r, color.g, color.b, 0.95);
  cairo_set_line_width(cr, 2.0);
  cairo_stroke(cr);
}

static void draw_arrow_draft(cairo_t *cr, ShaulaPreviewState *state) {
  ShaulaAnnotation *annotation = shaula_annotation_new_arrow(
      state->drag_start_image, state->drag_current_image, state->arrow_color,
      state->arrow_stroke_width);
  shaula_annotation_draw(cr, annotation);
  shaula_annotation_free(annotation);
}

static void draw_measure_draft(cairo_t *cr, ShaulaPreviewState *state) {
  ShaulaAnnotation *annotation = shaula_annotation_new_measure(
      state->drag_start_image, state->drag_current_image, state->current_color,
      2.0);
  shaula_annotation_draw(cr, annotation);
  shaula_annotation_free(annotation);
}

static void draw_pen_draft(cairo_t *cr, ShaulaPreviewState *state) {
  if (state->draft_pen_points == NULL || state->draft_pen_points->len < 2)
    return;
  ShaulaPoint *points = (ShaulaPoint *)state->draft_pen_points->data;
  cairo_save(cr);
  cairo_set_source_rgba(cr, state->current_color.r, state->current_color.g,
                        state->current_color.b, state->current_color.a);
  cairo_set_line_width(cr, 3.0);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_move_to(cr, points[0].x, points[0].y);
  for (guint i = 1; i < state->draft_pen_points->len; i++)
    cairo_line_to(cr, points[i].x, points[i].y);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_crop_overlay(cairo_t *cr, ShaulaPreviewState *state) {
  if (!state->has_crop_draft)
    return;
  int image_w = shaula_preview_image_width(state);
  int image_h = shaula_preview_image_height(state);
  ShaulaRect rect = shaula_rect_clamped(state->crop_draft, image_w, image_h);

  cairo_save(cr);
  cairo_rectangle(cr, 0, 0, image_w, image_h);
  cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
  cairo_fill(cr);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
  cairo_set_line_width(cr, 1.0);
  cairo_rectangle(cr, rect.x + 0.5, rect.y + 0.5, rect.width, rect.height);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_region_selection(cairo_t *cr, ShaulaPreviewState *state) {
  if (state->active_properties_panel == SHAULA_PROPERTIES_PANEL_SPOTLIGHT)
    return;
  if (!state->has_region_selection &&
      state->operation != SHAULA_OPERATION_SELECT_REGION)
    return;

  ShaulaRect rect = state->operation == SHAULA_OPERATION_SELECT_REGION
                        ? shaula_rect_from_points(state->drag_start_image,
                                                  state->drag_current_image)
                        : state->region_selection_rect;
  rect = shaula_rect_clamped(shaula_rect_normalized(rect),
                             shaula_preview_image_width(state),
                             shaula_preview_image_height(state));
  if (shaula_rect_is_empty(rect))
    return;

  cairo_save(cr);
  cairo_set_source_rgba(cr, 0.04, 0.05, 0.06, 0.16);
  cairo_rectangle(cr, 0, 0, shaula_preview_image_width(state),
                  shaula_preview_image_height(state));
  cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_fill(cr);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);

  cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, 0.96);
  cairo_set_line_width(cr, 1.5);
  double dashes[] = {6.0, 4.0};
  cairo_set_dash(cr, dashes, 2, 0);
  cairo_rectangle(cr, rect.x + 0.5, rect.y + 0.5, rect.width, rect.height);
  cairo_stroke(cr);

  cairo_set_dash(cr, NULL, 0, 0);
  cairo_set_source_rgba(cr, 0.08, 0.09, 0.10, 0.55);
  cairo_set_line_width(cr, 3.0);
  cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_drafts(ShaulaPreviewState *state, cairo_t *cr) {
  cairo_save(cr);
  cairo_translate(cr, state->pan_x, state->pan_y);
  cairo_scale(cr, state->zoom, state->zoom);
  cairo_rectangle(cr, 0, 0, shaula_preview_image_width(state),
                  shaula_preview_image_height(state));
  cairo_clip(cr);

  switch (state->operation) {
  case SHAULA_OPERATION_ARROW:
    draw_arrow_draft(cr, state);
    break;
  case SHAULA_OPERATION_RECTANGLE:
    draw_draft_rect(cr,
                    shaula_rect_from_points(state->drag_start_image,
                                            state->drag_current_image),
                    state->current_color, FALSE);
    break;
  case SHAULA_OPERATION_HIGHLIGHT:
    draw_draft_rect(cr,
                    shaula_rect_from_points(state->drag_start_image,
                                            state->drag_current_image),
                    state->current_color, TRUE);
    break;
  case SHAULA_OPERATION_MEASURE:
    draw_measure_draft(cr, state);
    break;
  case SHAULA_OPERATION_PEN:
    draw_pen_draft(cr, state);
    break;
  case SHAULA_OPERATION_SPOTLIGHT:
    draw_draft_rect(cr,
                    shaula_rect_from_points(state->drag_start_image,
                                            state->drag_current_image),
                    state->spotlight_border_color, FALSE);
    break;
  case SHAULA_OPERATION_CROP:
  case SHAULA_OPERATION_NONE:
  case SHAULA_OPERATION_PAN:
  case SHAULA_OPERATION_MOVE:
  case SHAULA_OPERATION_BEND_ARROW:
  case SHAULA_OPERATION_SELECT_REGION:
  case SHAULA_OPERATION_TEXT:
    break;
  }
  draw_region_selection(cr, state);
  draw_crop_overlay(cr, state);
  cairo_restore(cr);
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer data) {
  (void)area;
  ShaulaPreviewState *state = data;
  shaula_preview_update_theme_state(state);
  shaula_preview_update_fit_zoom(state);

  draw_checker_background(state, cr, width, height);
  if (state->image == NULL)
    return;
  draw_image_frame(state, cr);
  draw_drafts(state, cr);
}

static gboolean on_scroll(GtkEventControllerScroll *controller, double dx,
                          double dy, gpointer data) {
  (void)controller;
  (void)dx;
  ShaulaPreviewState *state = data;
  if (state->image == NULL)
    return TRUE;
  shaula_preview_zoom_by_factor(state, dy < 0 ? 1.12 : 1.0 / 1.12);
  return TRUE;
}

static void on_text_entry_activate(GtkEntry *entry, gpointer data) {
  ShaulaPreviewState *state = data;
  const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
  if (text != NULL && text[0] != '\0') {
    shaula_preview_add_annotation(
        state, shaula_annotation_new_text(state->text_anchor_image, text,
                                          state->current_color, 22.0));
  }
  shaula_preview_cancel_operation(state);
}

static gboolean on_text_entry_key(GtkEventControllerKey *controller,
                                  guint keyval, guint keycode,
                                  GdkModifierType modifiers, gpointer data) {
  (void)controller;
  (void)keycode;
  (void)modifiers;
  ShaulaPreviewState *state = data;
  if (keyval == GDK_KEY_Escape) {
    shaula_preview_cancel_operation(state);
    return TRUE;
  }
  return FALSE;
}

static void begin_text_entry(ShaulaPreviewState *state, ShaulaPoint image_point) {
  if (state->canvas_overlay == NULL)
    return;
  shaula_preview_cancel_operation(state);
  ShaulaPoint screen = shaula_preview_canvas_image_to_screen(state, image_point);
  GtkWidget *entry = gtk_entry_new();
  state->text_entry = entry;
  state->text_anchor_image = image_point;
  state->operation = SHAULA_OPERATION_TEXT;

  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Text");
  gtk_widget_set_size_request(entry, 220, -1);
  gtk_widget_set_halign(entry, GTK_ALIGN_START);
  gtk_widget_set_valign(entry, GTK_ALIGN_START);
  gtk_widget_set_margin_start(entry, MAX(0, (int)screen.x));
  gtk_widget_set_margin_top(entry, MAX(0, (int)screen.y - 18));
  gtk_overlay_add_overlay(GTK_OVERLAY(state->canvas_overlay), entry);

  g_signal_connect(entry, "activate", G_CALLBACK(on_text_entry_activate), state);

  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_text_entry_key), state);
  gtk_widget_add_controller(entry, keys);
  gtk_widget_grab_focus(entry);
}

static void start_pan(ShaulaPreviewState *state, double x, double y) {
  state->operation = SHAULA_OPERATION_PAN;
  state->drag_start_x = x;
  state->drag_start_y = y;
  state->pan_origin_x = state->pan_x;
  state->pan_origin_y = state->pan_y;
  gtk_widget_set_cursor_from_name(state->area, "grabbing");
}

static void start_operation(ShaulaPreviewState *state,
                            ShaulaPreviewOperation operation,
                            ShaulaPoint point) {
  state->operation = operation;
  state->operation_changed = FALSE;
  state->drag_start_image = point;
  state->drag_current_image = point;
  state->drag_last_image = point;
  if (operation == SHAULA_OPERATION_CROP) {
    state->has_crop_draft = TRUE;
    state->crop_draft = (ShaulaRect){point.x, point.y, 0, 0};
  }
  if (operation == SHAULA_OPERATION_SELECT_REGION ||
      operation == SHAULA_OPERATION_SPOTLIGHT) {
    state->has_region_selection = FALSE;
    state->region_selection_rect = (ShaulaRect){point.x, point.y, 0, 0};
  }
  if (operation == SHAULA_OPERATION_PEN && state->draft_pen_points != NULL) {
    g_array_set_size(state->draft_pen_points, 0);
    g_array_append_val(state->draft_pen_points, point);
  }
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y,
                          gpointer data) {
  ShaulaPreviewState *state = data;
  if (state->image == NULL)
    return;
  update_hover_color(state, x, y);

  guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  ShaulaPoint image_point =
      shaula_preview_canvas_screen_to_image(state, x, y);
  gboolean inside = image_point_is_inside(state, image_point);
  ShaulaPoint clamped = clamped_image_point(state, image_point);

  if (button == 2) {
    start_pan(state, x, y);
    return;
  }

  if (button != 1)
    return;

  switch (state->active_tool) {
  case SHAULA_TOOL_SELECT: {
    ShaulaAnnotation *hit = inside ? shaula_annotations_hit_test(
                                         state->annotations, image_point,
                                         MAX(4.0, 8.0 / state->zoom))
                                   : NULL;
    if (hit != NULL) {
      shaula_preview_select_annotation(state, hit);
      shaula_preview_begin_history_gesture(state);

      gboolean is_bend = FALSE;
      if (hit->type == SHAULA_ANNOTATION_ARROW) {
        ShaulaPoint p0 = hit->data.arrow.start;
        ShaulaPoint p2 = hit->data.arrow.end;
        ShaulaPoint p1 = hit->data.arrow.is_curved ? hit->data.arrow.control : (ShaulaPoint){(p0.x+p2.x)/2.0, (p0.y+p2.y)/2.0};
        ShaulaPoint mid = { 0.25*p0.x + 0.5*p1.x + 0.25*p2.x, 0.25*p0.y + 0.5*p1.y + 0.25*p2.y };
        if (shaula_point_distance(image_point, mid) <= MAX(8.0, 16.0 / state->zoom)) {
          is_bend = TRUE;
        }
      }

      start_operation(state, is_bend ? SHAULA_OPERATION_BEND_ARROW : SHAULA_OPERATION_MOVE, image_point);
      gtk_widget_set_cursor_from_name(state->area, "grabbing");
    } else if (inside) {
      shaula_preview_clear_selection(state);
      shaula_preview_clear_region_selection(state);
      start_operation(state, SHAULA_OPERATION_SELECT_REGION, clamped);
    } else {
      shaula_preview_clear_selection(state);
      shaula_preview_clear_region_selection(state);
      state->operation = SHAULA_OPERATION_NONE;
    }
    break;
  }
  case SHAULA_TOOL_CROP:
    start_operation(state, SHAULA_OPERATION_CROP, clamped);
    break;
  case SHAULA_TOOL_ARROW:
    if (inside) {
      shaula_preview_clear_selection(state);
      start_operation(state, SHAULA_OPERATION_ARROW, clamped);
    }
    break;
  case SHAULA_TOOL_TEXT:
    if (inside)
      begin_text_entry(state, clamped);
    break;
  case SHAULA_TOOL_MEASURE:
    if (inside)
      start_operation(state, SHAULA_OPERATION_MEASURE, clamped);
    break;
  case SHAULA_TOOL_RECTANGLE:
    if (inside)
      start_operation(state, SHAULA_OPERATION_RECTANGLE, clamped);
    break;
  case SHAULA_TOOL_HIGHLIGHT:
    if (inside)
      start_operation(state, SHAULA_OPERATION_HIGHLIGHT, clamped);
    break;
  case SHAULA_TOOL_PEN:
    if (inside)
      start_operation(state, SHAULA_OPERATION_PEN, clamped);
    break;
  case SHAULA_TOOL_SPOTLIGHT:
    if (inside) {
      shaula_preview_clear_selection(state);
      shaula_preview_clear_region_selection(state);
      start_operation(state, SHAULA_OPERATION_SPOTLIGHT, clamped);
    }
    break;
  case SHAULA_TOOL_COUNT:
    break;
  }
  shaula_preview_queue_draw(state);
}

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy,
                           gpointer data) {
  ShaulaPreviewState *state = data;
  if (state->operation == SHAULA_OPERATION_NONE ||
      state->operation == SHAULA_OPERATION_TEXT)
    return;

  double x = state->drag_start_x + dx;
  double y = state->drag_start_y + dy;
  if (state->operation != SHAULA_OPERATION_PAN) {
    x = state->pan_x + state->drag_start_image.x * state->zoom + dx;
    y = state->pan_y + state->drag_start_image.y * state->zoom + dy;
  }
  ShaulaPoint image_point = clamped_image_point(
      state, shaula_preview_canvas_screen_to_image(state, x, y));

  switch (state->operation) {
  case SHAULA_OPERATION_PAN:
    (void)gesture;
    state->fit_mode = FALSE;
    state->pan_x = state->pan_origin_x + dx;
    state->pan_y = state->pan_origin_y + dy;
    break;
  case SHAULA_OPERATION_MOVE: {
    ShaulaPoint raw = shaula_preview_canvas_screen_to_image(state, x, y);
    double mx = raw.x - state->drag_last_image.x;
    double my = raw.y - state->drag_last_image.y;
    if (!state->operation_changed &&
        (fabs(raw.x - state->drag_start_image.x) > 0.5 ||
         fabs(raw.y - state->drag_start_image.y) > 0.5))
      state->operation_changed = TRUE;
    if (state->selected_annotation != NULL && state->operation_changed) {
      shaula_annotation_move(state->selected_annotation, mx, my);
      state->modified = TRUE;
    }
    state->drag_last_image = raw;
    break;
  }
  case SHAULA_OPERATION_BEND_ARROW: {
    ShaulaPoint raw = shaula_preview_canvas_screen_to_image(state, x, y);
    if (!state->operation_changed &&
        (fabs(raw.x - state->drag_start_image.x) > 0.5 ||
         fabs(raw.y - state->drag_start_image.y) > 0.5))
      state->operation_changed = TRUE;
    if (state->selected_annotation != NULL && state->operation_changed && state->selected_annotation->type == SHAULA_ANNOTATION_ARROW) {
      ShaulaPoint p0 = state->selected_annotation->data.arrow.start;
      ShaulaPoint p2 = state->selected_annotation->data.arrow.end;
      state->selected_annotation->data.arrow.is_curved = TRUE;
      state->selected_annotation->data.arrow.control.x = 2.0 * raw.x - 0.5 * p0.x - 0.5 * p2.x;
      state->selected_annotation->data.arrow.control.y = 2.0 * raw.y - 0.5 * p0.y - 0.5 * p2.y;
      shaula_annotation_update_bounds(state->selected_annotation);
      state->modified = TRUE;
    }
    state->drag_last_image = raw;
    break;
  }
  case SHAULA_OPERATION_SELECT_REGION: {
    state->drag_current_image = image_point;
    ShaulaRect rect =
        shaula_rect_from_points(state->drag_start_image, image_point);
    state->operation_changed = rect.width >= 3.0 && rect.height >= 3.0;
    state->region_selection_rect = rect;
    state->has_region_selection = state->operation_changed;
    break;
  }
  case SHAULA_OPERATION_SPOTLIGHT: {
    state->drag_current_image = image_point;
    ShaulaRect rect =
        shaula_rect_from_points(state->drag_start_image, image_point);
    state->operation_changed = rect.width >= 3.0 && rect.height >= 3.0;
    state->region_selection_rect = rect;
    break;
  }
  case SHAULA_OPERATION_CROP:
    state->drag_current_image = image_point;
    state->crop_draft =
        shaula_rect_from_points(state->drag_start_image, image_point);
    break;
  case SHAULA_OPERATION_ARROW:
  case SHAULA_OPERATION_RECTANGLE:
  case SHAULA_OPERATION_HIGHLIGHT:
  case SHAULA_OPERATION_MEASURE:
    state->drag_current_image = image_point;
    break;
  case SHAULA_OPERATION_PEN:
    if (state->draft_pen_points != NULL &&
        shaula_point_distance(state->drag_current_image, image_point) > 0.75) {
      g_array_append_val(state->draft_pen_points, image_point);
      state->drag_current_image = image_point;
    }
    break;
  case SHAULA_OPERATION_NONE:
  case SHAULA_OPERATION_TEXT:
    break;
  }
  shaula_preview_queue_draw(state);
}

static void on_motion(GtkEventControllerMotion *controller, double x, double y,
                      gpointer data) {
  (void)controller;
  update_hover_color(data, x, y);
}

static void finish_shape_annotation(ShaulaPreviewState *state) {
  ShaulaAnnotation *annotation = NULL;
  switch (state->operation) {
  case SHAULA_OPERATION_ARROW:
    if (shaula_point_distance(state->drag_start_image,
                              state->drag_current_image) >= 3.0)
      annotation = shaula_annotation_new_arrow(
          state->drag_start_image, state->drag_current_image,
          state->arrow_color, state->arrow_stroke_width);
    break;
  case SHAULA_OPERATION_RECTANGLE: {
    ShaulaRect rect =
        shaula_rect_from_points(state->drag_start_image,
                                state->drag_current_image);
    if (rect.width >= 3.0 && rect.height >= 3.0)
      annotation =
          shaula_annotation_new_rectangle(rect, state->current_color, 3.0);
    break;
  }
  case SHAULA_OPERATION_HIGHLIGHT: {
    ShaulaRect rect =
        shaula_rect_from_points(state->drag_start_image,
                                state->drag_current_image);
    if (rect.width >= 3.0 && rect.height >= 3.0)
      annotation = shaula_annotation_new_highlight(rect, state->current_color);
    break;
  }
  case SHAULA_OPERATION_MEASURE:
    if (shaula_point_distance(state->drag_start_image,
                              state->drag_current_image) >= 3.0)
      annotation = shaula_annotation_new_measure(
          state->drag_start_image, state->drag_current_image,
          state->current_color, 2.0);
    break;
  case SHAULA_OPERATION_PEN:
    if (state->draft_pen_points != NULL && state->draft_pen_points->len >= 2) {
      annotation = shaula_annotation_new_pen(
          (ShaulaPoint *)state->draft_pen_points->data,
          (int)state->draft_pen_points->len, state->current_color, 3.0);
    }
    break;
  case SHAULA_OPERATION_CROP:
  case SHAULA_OPERATION_MOVE:
  case SHAULA_OPERATION_BEND_ARROW:
  case SHAULA_OPERATION_SELECT_REGION:
  case SHAULA_OPERATION_SPOTLIGHT:
  case SHAULA_OPERATION_NONE:
  case SHAULA_OPERATION_PAN:
  case SHAULA_OPERATION_TEXT:
    break;
  }

  if (annotation != NULL) {
    shaula_preview_add_annotation(state, annotation);
    /* Open arrow HUD targeting the just-created arrow. */
    if (annotation->type == SHAULA_ANNOTATION_ARROW) {
      shaula_preview_select_annotation(state, annotation);
      state->active_arrow_index = (int)state->annotations->len - 1;
      state->active_properties_panel = SHAULA_PROPERTIES_PANEL_ARROW;
      state->active_tool = SHAULA_TOOL_SELECT;
      shaula_preview_toolbar_update_tool_state(state);
      if (state->area != NULL)
        gtk_widget_set_cursor_from_name(state->area, "default");
      shaula_preview_toolbar_update_selection_state(state);
    }
  }
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                        gpointer data) {
  (void)gesture;
  (void)dx;
  (void)dy;
  ShaulaPreviewState *state = data;
  if (state->operation == SHAULA_OPERATION_PAN ||
      state->operation == SHAULA_OPERATION_MOVE ||
      state->operation == SHAULA_OPERATION_BEND_ARROW ||
      state->operation == SHAULA_OPERATION_SELECT_REGION ||
      state->operation == SHAULA_OPERATION_SPOTLIGHT) {
    if (state->operation == SHAULA_OPERATION_MOVE || state->operation == SHAULA_OPERATION_BEND_ARROW)
      shaula_preview_commit_history_gesture(state, state->operation_changed);
    if (state->operation == SHAULA_OPERATION_SELECT_REGION) {
      if (!state->operation_changed)
        shaula_preview_clear_region_selection(state);
      else
        shaula_preview_toolbar_update_selection_state(state);
    }
    if (state->operation == SHAULA_OPERATION_SPOTLIGHT) {
      if (state->operation_changed)
        shaula_preview_spotlight_rect(state, state->region_selection_rect);
      state->has_region_selection = FALSE;
    }
    gtk_widget_set_cursor_from_name(
        state->area, state->active_tool == SHAULA_TOOL_SELECT ? "default"
                                                              : "crosshair");
  } else if (state->operation == SHAULA_OPERATION_CROP) {
    if (!shaula_preview_apply_crop(state))
      shaula_preview_cancel_operation(state);
  } else {
    finish_shape_annotation(state);
  }
  if (state->operation != SHAULA_OPERATION_CROP)
    state->operation = SHAULA_OPERATION_NONE;
  state->operation_changed = FALSE;
  if (state->draft_pen_points != NULL &&
      state->operation != SHAULA_OPERATION_PEN)
    g_array_set_size(state->draft_pen_points, 0);
  shaula_preview_queue_draw(state);
}

static gboolean on_key(GtkEventControllerKey *controller, guint keyval,
                       guint keycode, GdkModifierType modifiers,
                       gpointer data) {
  (void)controller;
  (void)keycode;
  ShaulaPreviewState *state = data;
  gboolean ctrl = (modifiers & GDK_CONTROL_MASK) != 0;

  GtkWidget *focus = state->window != NULL
                         ? gtk_window_get_focus(GTK_WINDOW(state->window))
                         : NULL;
  if (focus != NULL && GTK_IS_EDITABLE(focus)) {
    if (keyval == GDK_KEY_Escape && focus == state->text_entry) {
      shaula_preview_cancel_operation(state);
      return TRUE;
    }
    return FALSE;
  }
  if (keyval == GDK_KEY_Escape) {
    if (state->operation != SHAULA_OPERATION_NONE || state->has_crop_draft) {
      shaula_preview_cancel_operation(state);
      return TRUE;
    }
    if (state->last_action == NULL)
      state->last_action = "close";
    if (state->app != NULL)
      g_application_quit(G_APPLICATION(state->app));
    return TRUE;
  }
  if (keyval == GDK_KEY_q && !ctrl) {
    if (state->last_action == NULL)
      state->last_action = "close";
    if (state->app != NULL)
      g_application_quit(G_APPLICATION(state->app));
    return TRUE;
  }
  if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) &&
      state->has_crop_draft) {
    shaula_preview_apply_crop(state);
    return TRUE;
  }

  ShaulaPreviewCommand command;
  if (shaula_preview_shortcut_command(keyval, modifiers, &command)) {
    if (command == SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR)
      update_hover_color_from_pointer(state);
    gboolean executed = shaula_preview_execute_command(state, command);
    return executed || command == SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR;
  }

  if (!ctrl && (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal)) {
    shaula_preview_action_zoom_in(state);
    return TRUE;
  }
  if (!ctrl && keyval == GDK_KEY_minus) {
    shaula_preview_action_zoom_out(state);
    return TRUE;
  }
  return FALSE;
}

static gboolean on_copy_hover_color_shortcut(GtkWidget *widget,
                                             GVariant *args, gpointer data) {
  (void)widget;
  (void)args;
  ShaulaPreviewState *state = data;
  GtkWidget *focus = state->window != NULL
                         ? gtk_window_get_focus(GTK_WINDOW(state->window))
                         : NULL;
  if (focus != NULL && GTK_IS_EDITABLE(focus))
    return FALSE;

  ShaulaPreviewCommand command;
  if (!shaula_preview_shortcut_command(GDK_KEY_Tab, 0, &command))
    return FALSE;
  update_hover_color_from_pointer(state);
  gboolean executed = shaula_preview_execute_command(state, command);
  return executed || command == SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR;
}

static void on_map(GtkWidget *widget, gpointer data) {
  (void)widget;
  ShaulaPreviewState *state = data;
  shaula_preview_update_theme_state(state);
  shaula_preview_update_zoom_label(state);
}

GtkWidget *shaula_preview_canvas_build(ShaulaPreviewState *state) {
  GtkWidget *overlay = gtk_overlay_new();
  GtkWidget *area = gtk_drawing_area_new();
  state->canvas_overlay = overlay;
  state->area = area;

  gtk_widget_set_focusable(area, TRUE);
  gtk_widget_set_hexpand(area, TRUE);
  gtk_widget_set_vexpand(area, TRUE);
  gtk_widget_set_cursor_from_name(area, "default");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, state, NULL);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), area);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_properties_panel_build(state));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_arrow_properties_panel_build(state));

  GtkGesture *drag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 0);
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), state);
  g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), state);
  g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), state);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

  GtkEventController *scroll =
      gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), state);
  gtk_widget_add_controller(area, scroll);

  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(on_motion), state);
  gtk_widget_add_controller(area, motion);

  GtkEventController *keys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), state);
  gtk_widget_add_controller(state->window, keys);

  GtkEventController *shortcut_controller = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(shortcut_controller),
                                    GTK_SHORTCUT_SCOPE_GLOBAL);
  gtk_shortcut_controller_add_shortcut(
      GTK_SHORTCUT_CONTROLLER(shortcut_controller),
      gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Tab, 0),
                       gtk_callback_action_new(on_copy_hover_color_shortcut,
                                               state, NULL)));
  gtk_widget_add_controller(state->window, shortcut_controller);

  g_signal_connect(state->window, "map", G_CALLBACK(on_map), state);

  return overlay;
}
