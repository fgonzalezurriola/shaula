#include "preview_canvas.h"

#include "preview_edit_session.h"
#include <math.h>
#include <stdio.h>

#include "preview_commands.h"
#include "preview_measure.h"
#include "preview_properties_panel.h"
#include "preview_spotlight.h"
#include "preview_system_clipboard.h"
#include "preview_toolbar.h"

#define SHAULA_ERASER_PENDING_OPACITY 0.35
#define SHAULA_ERASER_TAIL_RECENT_US 300000
#define SHAULA_ERASER_TAIL_FADE_US 220000
#define SHAULA_ERASER_TAIL_SAMPLE_PX 5.0

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

static const char *cursor_name_for_tool(ShaulaTool tool) {
  switch (tool) {
  case SHAULA_TOOL_SELECT:
    return "default";
  case SHAULA_TOOL_HAND:
    return "grab";
  case SHAULA_TOOL_TEXT:
    return "text";
  case SHAULA_TOOL_ERASER:
    return "none";
  case SHAULA_TOOL_CROP:
  case SHAULA_TOOL_ARROW:
  case SHAULA_TOOL_LINE:
  case SHAULA_TOOL_MEASURE:
  case SHAULA_TOOL_RECTANGLE:
  case SHAULA_TOOL_HIGHLIGHT:
  case SHAULA_TOOL_PEN:
  case SHAULA_TOOL_SPOTLIGHT:
    return "crosshair";
  case SHAULA_TOOL_COUNT:
    return "default";
  }
  return "default";
}

static gboolean focus_is_editable_text(ShaulaPreviewState *state,
                                       GtkWidget *focus) {
  if (focus == NULL)
    return FALSE;
  return GTK_IS_EDITABLE(focus) || GTK_IS_TEXT_VIEW(focus) ||
         focus == state->text_entry ||
         (state->text_entry != NULL &&
          gtk_widget_is_ancestor(focus, state->text_entry));
}

static gboolean is_space_key(guint keyval) {
  return keyval == GDK_KEY_space || keyval == GDK_KEY_KP_Space;
}

static void eraser_start_tail_fade(ShaulaPreviewState *state);

static void restore_space_pan_tool(ShaulaPreviewState *state) {
  if (state == NULL || !state->space_pan_active)
    return;
  ShaulaTool previous = state->previous_tool_before_space_pan;
  state->space_pan_active = FALSE;
  state->space_pan_restore_pending = FALSE;
  if (previous != SHAULA_TOOL_HAND && state->active_tool == SHAULA_TOOL_HAND) {
    state->active_tool = previous;
    shaula_preview_toolbar_update_tool_state(state);
    if (state->area != NULL && state->operation != SHAULA_OPERATION_PAN)
      gtk_widget_set_cursor_from_name(state->area,
                                      cursor_name_for_tool(state->active_tool));
  }
}

static gboolean begin_space_pan_tool(ShaulaPreviewState *state,
                                     GdkModifierType modifiers) {
  if (state == NULL || state->space_pan_active)
    return TRUE;
  if ((modifiers & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) != 0)
    return FALSE;

  if (state->active_tool == SHAULA_TOOL_ERASER &&
      state->operation == SHAULA_OPERATION_ERASE_ANNOTATIONS) {
    shaula_preview_commit_eraser_pending(state);
    state->operation = SHAULA_OPERATION_NONE;
    state->eraser_drag_active = FALSE;
    eraser_start_tail_fade(state);
  }

  state->previous_tool_before_space_pan = state->active_tool;
  state->space_pan_active = TRUE;
  state->space_pan_restore_pending = FALSE;
  if (state->operation == SHAULA_OPERATION_NONE &&
      state->active_tool != SHAULA_TOOL_HAND) {
    state->active_tool = SHAULA_TOOL_HAND;
    shaula_preview_toolbar_update_tool_state(state);
    if (state->area != NULL)
      gtk_widget_set_cursor_from_name(state->area, "grab");
  }
  return TRUE;
}

static double eraser_radius_image(ShaulaPreviewState *state) {
  double radius = state != NULL ? state->tool_defaults.eraser.size
                                : SHAULA_ERASER_SIZE_DEFAULT;
  return state != NULL && state->zoom > 0.0
             ? radius / state->zoom
             : radius;
}

static void eraser_prune_trail(ShaulaPreviewState *state, gint64 now_us) {
  if (state == NULL || state->eraser_trail == NULL)
    return;
  while (state->eraser_trail->len > 0) {
    ShaulaEraserTrailPoint first =
        g_array_index(state->eraser_trail, ShaulaEraserTrailPoint, 0);
    if (now_us - first.time_us <= SHAULA_ERASER_TAIL_RECENT_US)
      break;
    g_array_remove_index(state->eraser_trail, 0);
  }
}

static void eraser_append_trail_point(ShaulaPreviewState *state, double x,
                                      double y, gint64 time_us) {
  ShaulaEraserTrailPoint point = {x, y, time_us};
  g_array_append_val(state->eraser_trail, point);
}

static void eraser_add_trail_point(ShaulaPreviewState *state, double x,
                                   double y, gint64 now_us) {
  if (state == NULL || state->eraser_trail == NULL)
    return;
  eraser_prune_trail(state, now_us);
  if (state->eraser_trail->len == 0) {
    eraser_append_trail_point(state, x, y, now_us);
    return;
  }

  ShaulaEraserTrailPoint previous = g_array_index(
      state->eraser_trail, ShaulaEraserTrailPoint, state->eraser_trail->len - 1);
  double dx = x - previous.x;
  double dy = y - previous.y;
  double distance = hypot(dx, dy);
  if (distance < 0.25)
    return;

  int steps = MAX(1, (int)ceil(distance / SHAULA_ERASER_TAIL_SAMPLE_PX));
  for (int i = 1; i <= steps; i++) {
    double t = (double)i / (double)steps;
    gint64 time =
        previous.time_us + (gint64)((double)(now_us - previous.time_us) * t);
    eraser_append_trail_point(state, previous.x + dx * t, previous.y + dy * t,
                              time);
  }
}

static gboolean eraser_tail_fade_tick(gpointer data) {
  ShaulaPreviewState *state = data;
  if (state == NULL)
    return G_SOURCE_REMOVE;
  gint64 now = g_get_monotonic_time();
  if (!state->eraser_tail_fading ||
      now - state->eraser_tail_fade_start_us >= SHAULA_ERASER_TAIL_FADE_US) {
    state->eraser_tail_fading = FALSE;
    state->eraser_tail_timeout_id = 0;
    if (state->eraser_trail != NULL)
      g_array_set_size(state->eraser_trail, 0);
    shaula_preview_queue_draw(state);
    return G_SOURCE_REMOVE;
  }
  shaula_preview_queue_draw(state);
  return G_SOURCE_CONTINUE;
}

static void eraser_start_tail_fade(ShaulaPreviewState *state) {
  if (state == NULL || state->eraser_trail == NULL ||
      state->eraser_trail->len == 0)
    return;
  state->eraser_tail_fading = TRUE;
  state->eraser_tail_fade_start_us = g_get_monotonic_time();
  eraser_prune_trail(state, state->eraser_tail_fade_start_us);
  if (state->eraser_tail_timeout_id == 0)
    state->eraser_tail_timeout_id =
        g_timeout_add(16, eraser_tail_fade_tick, state);
}

static gboolean spotlight_debug_enabled(void) {
  const char *value = g_getenv("SHAULA_DEBUG_SPOTLIGHT");
  return value != NULL && value[0] != '\0' && g_strcmp0(value, "0") != 0;
}

static void debug_spotlight_rect(const char *stage, ShaulaPreviewState *state,
                                 ShaulaRect rect) {
  if (!spotlight_debug_enabled())
    return;
  g_printerr(
      "[DEBUG-spotlight] %s rect=(%.2f,%.2f %.2fx%.2f) "
      "start=(%.2f,%.2f) current=(%.2f,%.2f) pan=(%.2f,%.2f) "
      "zoom=%.4f area=%dx%d image=%dx%d\n",
      stage, rect.x, rect.y, rect.width, rect.height, state->drag_start_image.x,
      state->drag_start_image.y, state->drag_current_image.x,
      state->drag_current_image.y, state->pan_x, state->pan_y, state->zoom,
      state->area != NULL ? gtk_widget_get_width(state->area) : 0,
      state->area != NULL ? gtk_widget_get_height(state->area) : 0,
      shaula_preview_image_width(state), shaula_preview_image_height(state));
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
  gdk_cairo_set_source_pixbuf(cr, state->document.image, 0, 0);
  cairo_paint(cr);

  cairo_save(cr);
  cairo_rectangle(cr, px, py, 1, 1);
  cairo_clip(cr);
  shaula_preview_draw_spotlight_effect(state, cr);
  for (guint i = 0; state->document.annotations != NULL && i < state->document.annotations->len;
       i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(state->document.annotations, i);
    gboolean selected = annotation->selected;
    annotation->selected = FALSE;
    shaula_annotation_draw(cr, annotation);
    annotation->selected = selected;
  }
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
  if (state == NULL || state->document.image == NULL)
    return FALSE;
  ShaulaPoint image_point = shaula_preview_canvas_screen_to_image(state, x, y);
  int px = 0;
  int py = 0;
  if (!image_point_to_pixel(state, image_point, &px, &py))
    return FALSE;

  ShaulaColor color;
  if (!sample_composited_pixel(state, px, py, &color))
    return FALSE;

  gboolean changed = !state->hover_color_valid ||
                     (int)floor(state->hover_image_point.x) != px ||
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
  GdkSeat *seat =
      gdk_display_get_default_seat(gdk_surface_get_display(surface));
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
                                        surface_y - native_y, &area_x, &area_y))
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

static void draw_annotation_preview(ShaulaPreviewState *state, cairo_t *cr,
                                    ShaulaAnnotation *annotation,
                                    ShaulaAnnotationPreviewFlags flags) {
  if (annotation == NULL)
    return;

  gboolean selected = annotation->selected;
  ShaulaColor color = annotation->color;
  gboolean pending = shaula_preview_is_annotation_pending_erase(state, annotation);
  if (pending) {
    annotation->selected = FALSE;
    annotation->color.a *= SHAULA_ERASER_PENDING_OPACITY;
  }
  shaula_annotation_draw_preview(cr, annotation, flags);
  annotation->selected = selected;
  annotation->color = color;
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
  gdk_cairo_set_source_pixbuf(cr, state->document.image, 0, 0);
  cairo_paint(cr);

  cairo_rectangle(cr, 0, 0, image_w, image_h);
  cairo_clip(cr);
  shaula_preview_draw_spotlight_effect(state, cr);
  guint selected_count = shaula_annotation_editor_selected_count(state);
  ShaulaAnnotationPreviewFlags preview_flags =
      selected_count == 1
          ? SHAULA_ANNOTATION_PREVIEW_SELECTION |
                SHAULA_ANNOTATION_PREVIEW_HANDLES
          : SHAULA_ANNOTATION_PREVIEW_NONE;
  for (guint i = 0; state->document.annotations != NULL &&
                      i < state->document.annotations->len;
       i++) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, i);
    /* Hide the committed text while its string is being re-edited so the draft
     * path is the only visible copy.
     */
    if (state->text_entry != NULL && state->text_editing_id > 0 &&
        annotation != NULL && annotation->id == state->text_editing_id)
      continue;
    draw_annotation_preview(state, cr, annotation, preview_flags);
  }
  if (selected_count > 1) {
    ShaulaRect group_bounds;
    if (shaula_annotation_editor_selected_bounds(state, &group_bounds))
      shaula_annotation_draw_selection_box(cr, group_bounds);
  }
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
                            double stroke_width,
                            PreviewArrowStrokeStyle stroke_style,
                            PreviewRectangleCorners corners, gboolean fill) {
  rect = shaula_rect_normalized(rect);
  if (shaula_rect_is_empty(rect))
    return;
  ShaulaAnnotation *annotation =
      shaula_annotation_new_rectangle(rect, color, stroke_width);
  annotation->data.rectangle.stroke_style = stroke_style;
  annotation->data.rectangle.corners = corners;
  annotation->data.rectangle.filled = fill;
  shaula_annotation_draw(cr, annotation);
  shaula_annotation_free(annotation);
}

static void draw_plain_draft_rect(cairo_t *cr, ShaulaRect rect,
                                  ShaulaColor color, gboolean fill) {
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
      state->drag_start_image, state->drag_current_image,
      state->tool_defaults.arrow_line.color,
      state->tool_defaults.arrow_line.stroke_width);
  annotation->data.arrow.has_head =
      state->operation != SHAULA_OPERATION_LINE;
  annotation->data.arrow.stroke_style =
      state->tool_defaults.arrow_line.stroke_style;
  shaula_annotation_draw(cr, annotation);
  shaula_annotation_free(annotation);
}

static void draw_measure_draft(cairo_t *cr, ShaulaPreviewState *state) {
  ShaulaAnnotation *annotation = shaula_annotation_new_measure(
      state->drag_start_image, state->drag_current_image,
      state->tool_defaults.measure.color,
      state->tool_defaults.measure.stroke_width);
  shaula_annotation_draw(cr, annotation);
  shaula_annotation_free(annotation);
}

static char *text_view_contents(GtkTextView *view) {
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static int text_view_cursor_byte_index(GtkTextView *view, const char *text) {
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
  GtkTextMark *insert = gtk_text_buffer_get_insert(buffer);
  GtkTextIter iter;
  gtk_text_buffer_get_iter_at_mark(buffer, &iter, insert);
  int char_offset = gtk_text_iter_get_offset(&iter);
  const char *cursor = g_utf8_offset_to_pointer(text != NULL ? text : "",
                                                char_offset);
  return (int)(cursor - (text != NULL ? text : ""));
}

static void draw_text_insertion_caret(cairo_t *cr, ShaulaPreviewState *state,
                                      ShaulaRect caret) {
  double zoom = MAX(state->zoom, 0.01);
  double line_width = MAX(1.25 / zoom, 0.8);
  double height = MAX(caret.height, state->tool_defaults.text.font_size * 1.1);
  double x = caret.x;
  double y0 = caret.y;
  double y1 = y0 + height;

  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
  cairo_set_line_width(cr, line_width + 2.0 / zoom);
  cairo_move_to(cr, x, y0);
  cairo_line_to(cr, x, y1);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, state->tool_defaults.text.color.r,
                        state->tool_defaults.text.color.g,
                        state->tool_defaults.text.color.b,
                        state->tool_defaults.text.color.a);
  cairo_set_line_width(cr, line_width);
  cairo_move_to(cr, x, y0);
  cairo_line_to(cr, x, y1);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_text_draft(cairo_t *cr, ShaulaPreviewState *state) {
  if (state->text_entry == NULL)
    return;

  char *text = text_view_contents(GTK_TEXT_VIEW(state->text_entry));
  int cursor_byte_index =
      text_view_cursor_byte_index(GTK_TEXT_VIEW(state->text_entry), text);
  ShaulaAnnotation *annotation = shaula_annotation_new_text(
      state->text_anchor_image, text, state->tool_defaults.text.color,
      state->tool_defaults.text.font_size, state->tool_defaults.text.align,
      state->tool_defaults.text.font_mode);
  shaula_annotation_draw(cr, annotation);
  ShaulaRect caret = {0};
  gboolean has_caret =
      shaula_annotation_text_cursor_rect(annotation, cursor_byte_index, &caret);
  if (has_caret)
    draw_text_insertion_caret(cr, state, caret);
  shaula_annotation_free(annotation);
  g_free(text);
}

static void draw_path_draft(cairo_t *cr, ShaulaPreviewState *state,
                            ShaulaColor color, double stroke_width) {
  if (state->draft_pen_points == NULL || state->draft_pen_points->len < 2)
    return;
  ShaulaPoint *points = (ShaulaPoint *)state->draft_pen_points->data;
  cairo_save(cr);
  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
  cairo_set_line_width(cr, stroke_width);
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
  ShaulaRect rect = shaula_rect_clamped_c(state->crop_draft, image_w, image_h);

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
  if (state->properties_hud.active_panel == SHAULA_PROPERTIES_PANEL_SPOTLIGHT)
    return;
  if (!state->has_region_selection &&
      state->operation != SHAULA_OPERATION_SELECT_REGION)
    return;

  ShaulaRect rect = state->operation == SHAULA_OPERATION_SELECT_REGION
                        ? shaula_rect_from_points(state->drag_start_image,
                                                  state->drag_current_image)
                        : state->region_selection_rect;
  rect = shaula_rect_clamped_c(rect, shaula_preview_image_width(state),
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

static void draw_live_measure(cairo_t *cr, ShaulaPreviewState *state) {
  if (!state->measure_has_live)
    return;
  ShaulaMeasureResult *r = &state->measure_result;
  double x0 = (double)r->left;
  double y0 = (double)r->top;
  double x1 = (double)r->right + 1.0;
  double y1 = (double)r->bottom + 1.0;

  cairo_save(cr);
  cairo_set_source_rgba(cr, state->tool_defaults.measure.color.r,
                        state->tool_defaults.measure.color.g,
                        state->tool_defaults.measure.color.b, 0.85);
  cairo_set_line_width(
      cr, state->tool_defaults.measure.stroke_width / state->zoom);

  if (r->width > 1 && r->height > 1) {
    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
    cairo_stroke(cr);
  } else if (r->width > 1) {
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y0);
    cairo_stroke(cr);
  } else if (r->height > 1) {
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x0, y1);
    cairo_stroke(cr);
  }

  double foot = 6.0 / state->zoom;
  cairo_set_line_width(
      cr, state->tool_defaults.measure.stroke_width / state->zoom);
  if (r->width > 1) {
    cairo_move_to(cr, x0, y0 - foot);
    cairo_line_to(cr, x0, y0 + foot);
    cairo_stroke(cr);
    cairo_move_to(cr, x1, y0 - foot);
    cairo_line_to(cr, x1, y0 + foot);
    cairo_stroke(cr);
  }
  if (r->height > 1) {
    cairo_move_to(cr, x0 - foot, y0);
    cairo_line_to(cr, x0 + foot, y0);
    cairo_stroke(cr);
    cairo_move_to(cr, x0 - foot, y1);
    cairo_line_to(cr, x0 + foot, y1);
    cairo_stroke(cr);
  }

  char label[128];
  if (r->width > 1 && r->height > 1)
    snprintf(label, sizeof(label), "%d \xc3\x97 %d px", r->width, r->height);
  else if (r->width > 1)
    snprintf(label, sizeof(label), "%d px", r->width);
  else if (r->height > 1)
    snprintf(label, sizeof(label), "%d px", r->height);
  else
    snprintf(label, sizeof(label), "1 px");

  double lx = (x0 + x1) / 2.0;
  double ly = (y0 + y1) / 2.0 - 8.0 / state->zoom;
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 13.0 / state->zoom);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, label, &extents);
  cairo_set_source_rgba(cr, 0.0, 0.1, 0.2, 0.82);
  cairo_rectangle(cr, lx - extents.width / 2.0 - 5.0 / state->zoom,
                  ly - extents.height - 5.0 / state->zoom,
                  extents.width + 10.0 / state->zoom,
                  extents.height + 8.0 / state->zoom);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_move_to(cr, lx - extents.width / 2.0, ly - 4.0 / state->zoom);
  cairo_show_text(cr, label);

  cairo_restore(cr);
}

static void draw_drafts(ShaulaPreviewState *state, cairo_t *cr) {
  cairo_save(cr);
  cairo_translate(cr, state->pan_x, state->pan_y);
  cairo_scale(cr, state->zoom, state->zoom);
  cairo_rectangle(cr, 0, 0, shaula_preview_image_width(state),
                  shaula_preview_image_height(state));
  cairo_clip(cr);

  draw_live_measure(cr, state);

  switch (state->operation) {
  case SHAULA_OPERATION_ARROW:
  case SHAULA_OPERATION_LINE:
    draw_arrow_draft(cr, state);
    break;
  case SHAULA_OPERATION_RECTANGLE:
    draw_draft_rect(cr,
                    shaula_rect_from_points(state->drag_start_image,
                                            state->drag_current_image),
                    state->tool_defaults.rectangle.color,
                    state->tool_defaults.rectangle.stroke_width,
                    state->tool_defaults.rectangle.stroke_style,
                    state->tool_defaults.rectangle.corners,
                    state->tool_defaults.rectangle.filled);
    break;
  case SHAULA_OPERATION_HIGHLIGHT:
    draw_path_draft(cr, state, state->tool_defaults.highlight.color,
                    state->tool_defaults.highlight.stroke_width);
    break;
  case SHAULA_OPERATION_MEASURE:
    draw_measure_draft(cr, state);
    break;
  case SHAULA_OPERATION_PEN:
    draw_path_draft(cr, state, state->tool_defaults.pen.color,
                    state->tool_defaults.pen.stroke_width);
    break;
  case SHAULA_OPERATION_TEXT:
    draw_text_draft(cr, state);
    break;
  case SHAULA_OPERATION_SPOTLIGHT:
    draw_plain_draft_rect(cr,
                          shaula_rect_from_points(state->drag_start_image,
                                                  state->drag_current_image),
                          state->tool_defaults.spotlight.border_color, FALSE);
    break;
  case SHAULA_OPERATION_CROP:
  case SHAULA_OPERATION_ERASE_ANNOTATIONS:
  case SHAULA_OPERATION_NONE:
  case SHAULA_OPERATION_PAN:
  case SHAULA_OPERATION_MOVE:
  case SHAULA_OPERATION_BEND_ARROW:
  case SHAULA_OPERATION_RESIZE_ANNOTATION:
  case SHAULA_OPERATION_SELECT_REGION:
    break;
  }
  draw_region_selection(cr, state);
  draw_crop_overlay(cr, state);
  cairo_restore(cr);
}

static void draw_eraser_overlay(ShaulaPreviewState *state, cairo_t *cr) {
  if (state == NULL || state->area == NULL ||
      (state->active_tool != SHAULA_TOOL_ERASER &&
       !state->eraser_tail_fading))
    return;

  GdkRGBA fg;
  gtk_style_context_get_color(gtk_widget_get_style_context(state->area), &fg);
  gint64 now = g_get_monotonic_time();
  double fade = 1.0;
  if (state->eraser_tail_fading) {
    fade = 1.0 - (double)(now - state->eraser_tail_fade_start_us) /
                     (double)SHAULA_ERASER_TAIL_FADE_US;
    fade = CLAMP(fade, 0.0, 1.0);
  }

  if (state->eraser_trail != NULL && state->eraser_trail->len > 1) {
    double radius = state->tool_defaults.eraser.size;
    cairo_save(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.055 * fade);
    cairo_set_line_width(cr, radius * 0.95);
    ShaulaEraserTrailPoint first =
        g_array_index(state->eraser_trail, ShaulaEraserTrailPoint, 0);
    cairo_move_to(cr, first.x, first.y);
    for (guint i = 1; i < state->eraser_trail->len; i++) {
      ShaulaEraserTrailPoint point =
          g_array_index(state->eraser_trail, ShaulaEraserTrailPoint, i);
      cairo_line_to(cr, point.x, point.y);
    }
    cairo_stroke(cr);

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    for (guint i = 1; i < state->eraser_trail->len; i++) {
      ShaulaEraserTrailPoint a =
          g_array_index(state->eraser_trail, ShaulaEraserTrailPoint, i - 1);
      ShaulaEraserTrailPoint b =
          g_array_index(state->eraser_trail, ShaulaEraserTrailPoint, i);
      double age =
          (double)(now - b.time_us) / (double)SHAULA_ERASER_TAIL_RECENT_US;
      double freshness = 1.0 - CLAMP(age, 0.0, 1.0);
      double alpha = state->eraser_tail_fading ? freshness * fade : freshness;
      if (alpha <= 0.02)
        continue;
      double width = 2.0 + radius * 0.72 * alpha;
      cairo_set_line_width(cr, width);
      cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.16 * alpha);
      cairo_move_to(cr, a.x, a.y);
      cairo_line_to(cr, b.x, b.y);
      cairo_stroke(cr);
    }
    cairo_restore(cr);
  }

  if (state->active_tool == SHAULA_TOOL_ERASER && state->eraser_hover_valid) {
    double radius = state->tool_defaults.eraser.size;
    cairo_save(cr);
    cairo_set_line_width(cr, 1.5);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.88);
    cairo_arc(cr, state->eraser_hover_screen.x, state->eraser_hover_screen.y,
              radius, 0, 2.0 * G_PI);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.10);
    cairo_arc(cr, state->eraser_hover_screen.x, state->eraser_hover_screen.y,
              radius, 0, 2.0 * G_PI);
    cairo_fill(cr);
    cairo_restore(cr);
  }
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer data) {
  (void)area;
  ShaulaPreviewState *state = data;
  shaula_preview_update_theme_state(state);
  shaula_preview_update_fit_zoom(state);

  draw_checker_background(state, cr, width, height);
  if (state->document.image == NULL)
    return;
  draw_image_frame(state, cr);
  draw_drafts(state, cr);
  draw_eraser_overlay(state, cr);
}

static gboolean on_scroll(GtkEventControllerScroll *controller, double dx,
                          double dy, gpointer data) {
  (void)controller;
  (void)dx;
  ShaulaPreviewState *state = data;
  if (state->document.image == NULL)
    return TRUE;
  if (state->active_tool == SHAULA_TOOL_MEASURE &&
      state->operation == SHAULA_OPERATION_NONE) {
    int step = dy < 0 ? 4 : -4;
    state->measure_tolerance = CLAMP(state->measure_tolerance + step, 0, 255);
    state->measure_has_live = FALSE;
    shaula_preview_queue_draw(state);
    return TRUE;
  }
  shaula_preview_zoom_by_factor(state, dy < 0 ? 1.12 : 1.0 / 1.12);
  return TRUE;
}

static ShaulaAnnotation *annotation_by_id(ShaulaPreviewState *state, int id) {
  if (state == NULL || state->document.annotations == NULL || id <= 0)
    return NULL;
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, i);
    if (annotation != NULL && annotation->id == id)
      return annotation;
  }
  return NULL;
}

static gboolean text_style_equals_defaults(const ShaulaAnnotation *annotation,
                                           const ShaulaPreviewState *state) {
  if (annotation == NULL || state == NULL)
    return FALSE;
  const ShaulaColor *a = &annotation->color;
  const ShaulaColor *b = &state->tool_defaults.text.color;
  return fabs(a->r - b->r) <= 0.0001 && fabs(a->g - b->g) <= 0.0001 &&
         fabs(a->b - b->b) <= 0.0001 && fabs(a->a - b->a) <= 0.0001 &&
         fabs(annotation->data.text.font_size -
              state->tool_defaults.text.font_size) <= 0.0001 &&
         annotation->data.text.align == state->tool_defaults.text.align &&
         annotation->data.text.font_mode == state->tool_defaults.text.font_mode;
}

static void apply_text_tool_defaults_from_annotation(
    ShaulaPreviewState *state, const ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL ||
      annotation->type != SHAULA_ANNOTATION_TEXT)
    return;
  state->tool_defaults.text.color = annotation->color;
  state->tool_defaults.text.font_size = annotation->data.text.font_size;
  state->tool_defaults.text.align = annotation->data.text.align;
  state->tool_defaults.text.font_mode = annotation->data.text.font_mode;
}

static void select_text_after_commit(ShaulaPreviewState *state,
                                     ShaulaAnnotation *annotation) {
  if (state == NULL)
    return;
  state->active_tool = SHAULA_TOOL_SELECT;
  if (annotation != NULL)
    shaula_annotation_editor_select_only(state, annotation);
  shaula_properties_hud_set_panel(&state->properties_hud,
                                  SHAULA_PROPERTIES_PANEL_TEXT);
  if (state->area != NULL)
    gtk_widget_set_cursor_from_name(state->area, "default");
  shaula_preview_toolbar_update_tool_state(state);
  shaula_preview_toolbar_update_selection_state(state);
}

static void finish_text_entry(ShaulaPreviewState *state) {
  if (state == NULL || state->text_entry == NULL)
    return;
  char *text = text_view_contents(GTK_TEXT_VIEW(state->text_entry));
  char *trimmed = g_strdup(text != NULL ? text : "");
  g_strstrip(trimmed);
  int editing_id = state->text_editing_id;
  ShaulaAnnotation *editing = annotation_by_id(state, editing_id);
  gboolean keep_selected = FALSE;
  ShaulaAnnotation *committed = NULL;

  if (editing != NULL) {
    if (trimmed[0] == '\0') {
      /* Clearing all characters deletes the re-edited annotation. */
      shaula_annotation_editor_select_only(state, editing);
      shaula_annotation_editor_delete_selected(state);
    } else {
      const char *previous =
          editing->data.text.text != NULL ? editing->data.text.text : "";
      gboolean content_changed = g_strcmp0(previous, text) != 0;
      gboolean style_changed = !text_style_equals_defaults(editing, state);
      if (content_changed || style_changed) {
        shaula_preview_push_undo(state);
        g_free(editing->data.text.text);
        editing->data.text.text = g_strdup(text != NULL ? text : "");
        editing->color = state->tool_defaults.text.color;
        editing->color.a = 1.0;
        editing->data.text.font_size = state->tool_defaults.text.font_size;
        editing->data.text.align = state->tool_defaults.text.align;
        editing->data.text.font_mode = state->tool_defaults.text.font_mode;
        editing->data.text.position = state->text_anchor_image;
        shaula_annotation_update_bounds(editing);
        state->document.modified = TRUE;
        shaula_preview_toolbar_update_history_state(state);
      }
      committed = editing;
      keep_selected = TRUE;
    }
  } else if (trimmed[0] != '\0') {
    ShaulaAnnotation *annotation = shaula_annotation_new_text(
        state->text_anchor_image, text, state->tool_defaults.text.color,
        state->tool_defaults.text.font_size, state->tool_defaults.text.align,
        state->tool_defaults.text.font_mode);
    shaula_annotation_editor_add_annotation(state, annotation);
    committed = annotation;
    keep_selected = TRUE;
  }

  g_free(trimmed);
  g_free(text);
  shaula_preview_cancel_operation(state);
  if (keep_selected)
    select_text_after_commit(state, committed);
}

static gboolean on_text_entry_key(GtkEventControllerKey *controller,
                                  guint keyval, guint keycode,
                                  GdkModifierType modifiers, gpointer data) {
  (void)controller;
  (void)keycode;
  ShaulaPreviewState *state = data;
  if (keyval == GDK_KEY_Escape) {
    finish_text_entry(state);
    return TRUE;
  }
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    if ((modifiers & GDK_CONTROL_MASK) != 0) {
      finish_text_entry(state);
      return TRUE;
    }
    return FALSE;
  }
  return FALSE;
}

static void on_text_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
  (void)buffer;
  shaula_preview_queue_draw(data);
}

static void on_text_buffer_mark_set(GtkTextBuffer *buffer, GtkTextIter *iter,
                                    GtkTextMark *mark, gpointer data) {
  (void)iter;
  if (mark == gtk_text_buffer_get_insert(buffer))
    shaula_preview_queue_draw(data);
}

static void begin_text_entry(ShaulaPreviewState *state, ShaulaPoint image_point,
                             const char *initial_text, int editing_id) {
  if (state == NULL || state->canvas_overlay == NULL)
    return;
  shaula_preview_cancel_operation(state);
  shaula_annotation_editor_clear_selection(state);
  shaula_preview_clear_region_selection(state);

  GtkWidget *entry = gtk_text_view_new();
  state->text_entry = entry;
  state->text_anchor_image = image_point;
  state->text_editing_id = editing_id > 0 ? editing_id : 0;
  state->operation = SHAULA_OPERATION_TEXT;

  /* The GtkTextView is only the input buffer. The visible draft is rendered
   * as an annotation in image coordinates so it cannot drift from commit.
   */
  gtk_widget_set_opacity(entry, 0.0);
  gtk_widget_set_size_request(entry, 1, 1);
  gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(entry), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(entry), GTK_WRAP_NONE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(entry), 0);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(entry), 0);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(entry), 0);
  gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(entry), 0);
  gtk_widget_set_hexpand(entry, FALSE);
  gtk_widget_set_vexpand(entry, FALSE);
  gtk_widget_set_halign(entry, GTK_ALIGN_START);
  gtk_widget_set_valign(entry, GTK_ALIGN_START);
  gtk_widget_set_margin_start(entry, 0);
  gtk_widget_set_margin_top(entry, 0);
  gtk_overlay_add_overlay(GTK_OVERLAY(state->canvas_overlay), entry);

  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(entry));
  if (initial_text != NULL && initial_text[0] != '\0') {
    gtk_text_buffer_set_text(buffer, initial_text, -1);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_place_cursor(buffer, &end);
  }
  g_signal_connect(buffer, "changed", G_CALLBACK(on_text_buffer_changed),
                   state);
  g_signal_connect(buffer, "mark-set", G_CALLBACK(on_text_buffer_mark_set),
                   state);
  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_text_entry_key), state);
  gtk_widget_add_controller(entry, keys);

  shaula_properties_hud_set_panel(&state->properties_hud,
                                  SHAULA_PROPERTIES_PANEL_TEXT);
  shaula_properties_hud_sync_widgets(state);
  gtk_widget_grab_focus(entry);
  shaula_preview_queue_draw(state);
}

static void begin_text_entry_for_annotation(ShaulaPreviewState *state,
                                            ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL ||
      annotation->type != SHAULA_ANNOTATION_TEXT)
    return;
  apply_text_tool_defaults_from_annotation(state, annotation);
  begin_text_entry(state, annotation->data.text.position,
                   annotation->data.text.text, annotation->id);
}

static gboolean begin_text_entry_for_id(ShaulaPreviewState *state, int id) {
  ShaulaAnnotation *annotation = annotation_by_id(state, id);
  if (annotation == NULL)
    return FALSE;
  begin_text_entry_for_annotation(state, annotation);
  return TRUE;
}

static gboolean eraser_mark_segment(ShaulaPreviewState *state,
                                    ShaulaPoint start, ShaulaPoint end) {
  if (state == NULL || state->document.annotations == NULL ||
      state->eraser_pending_annotation_ids == NULL)
    return FALSE;

  guint previous_pending_count = state->eraser_pending_annotation_ids->len;
  ShaulaPoint image_start =
      shaula_preview_canvas_screen_to_image(state, start.x, start.y);
  ShaulaPoint image_end =
      shaula_preview_canvas_screen_to_image(state, end.x, end.y);
  double radius = eraser_radius_image(state);
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, i);
    if (annotation == NULL ||
        shaula_preview_is_annotation_pending_erase(state, annotation))
      continue;
    if (shaula_preview_edit_matches(
            annotation, (ShaulaPreviewEditQuery){
                            .kind = SHAULA_PREVIEW_EDIT_QUERY_ERASER,
                            .start = image_start,
                            .end = image_end,
                            .tolerance = radius})) {
      int id = annotation->id;
      g_array_append_val(state->eraser_pending_annotation_ids, id);
    }
  }
  if (state->eraser_pending_annotation_ids->len > previous_pending_count) {
    state->operation_changed = TRUE;
    return TRUE;
  }
  return FALSE;
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y,
                          gpointer data) {
  ShaulaPreviewState *state = data;
  if (state->document.image == NULL)
    return;
  state->drag_start_x = x;
  state->drag_start_y = y;
  shaula_preview_gesture_reset(&state->gesture);
  update_hover_color(state, x, y);

  if (state->operation == SHAULA_OPERATION_TEXT && state->text_entry != NULL) {
    finish_text_entry(state);
    return;
  }

  guint button =
      gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  ShaulaPoint image_point = shaula_preview_canvas_screen_to_image(state, x, y);
  gboolean inside = image_point_is_inside(state, image_point);
  ShaulaPoint clamped = clamped_image_point(state, image_point);

  if (button == 2) {
    shaula_preview_gesture_begin_pan(state, x, y);
    gtk_widget_set_cursor_from_name(state->area, "grabbing");
    return;
  }

  if (button != 1)
    return;
  GdkModifierType modifiers =
      gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
  gboolean shift = (modifiers & GDK_SHIFT_MASK) != 0;

  switch (state->active_tool) {
  case SHAULA_TOOL_HAND:
    shaula_preview_gesture_begin_pan(state, x, y);
    gtk_widget_set_cursor_from_name(state->area, "grabbing");
    break;
  case SHAULA_TOOL_SELECT: {
    const char *cursor_name = NULL;
    ShaulaPreviewPointerEvent event = {
        .screen_x = x,
        .screen_y = y,
        .button = button,
        .shift = shift,
    };
    (void)shaula_preview_gesture_begin_selection(state, event, &cursor_name);
    if (cursor_name != NULL)
      gtk_widget_set_cursor_from_name(state->area, cursor_name);
    break;
  }
  case SHAULA_TOOL_ERASER: {
    shaula_annotation_editor_clear_selection(state);
    shaula_preview_clear_region_selection(state);
    shaula_preview_clear_eraser_pending(state);
    if (state->eraser_tail_timeout_id != 0) {
      g_source_remove(state->eraser_tail_timeout_id);
      state->eraser_tail_timeout_id = 0;
    }
    if (state->eraser_trail != NULL)
      g_array_set_size(state->eraser_trail, 0);
    state->eraser_tail_fading = FALSE;
    state->eraser_drag_active = TRUE;
    state->eraser_hover_valid = TRUE;
    state->eraser_hover_screen = (ShaulaPoint){x, y};
    state->eraser_last_screen = state->eraser_hover_screen;
    shaula_preview_gesture_begin_operation(
        state, SHAULA_OPERATION_ERASE_ANNOTATIONS,
        shaula_preview_canvas_screen_to_image(state, x, y));
    gint64 now = g_get_monotonic_time();
    eraser_add_trail_point(state, x, y, now);
    eraser_mark_segment(state, state->eraser_last_screen,
                        state->eraser_last_screen);
    gtk_widget_set_cursor_from_name(state->area, "none");
    break;
  }
  case SHAULA_TOOL_CROP:
    shaula_preview_gesture_begin_operation(state, SHAULA_OPERATION_CROP,
                                           clamped);
    break;
  case SHAULA_TOOL_ARROW:
  case SHAULA_TOOL_LINE:
    if (inside) {
      shaula_annotation_editor_clear_selection(state);
      shaula_preview_gesture_begin_operation(
          state,
          state->active_tool == SHAULA_TOOL_ARROW ? SHAULA_OPERATION_ARROW
                                                  : SHAULA_OPERATION_LINE,
          clamped);
    }
    break;
  case SHAULA_TOOL_TEXT:
    if (inside) {
      ShaulaAnnotationHit hit = shaula_preview_edit_hit_test(
          state->document.annotations, image_point,
          MAX(4.0, 8.0 / state->zoom));
      if (hit.annotation != NULL &&
          hit.annotation->type == SHAULA_ANNOTATION_TEXT)
        begin_text_entry_for_annotation(state, hit.annotation);
      else
        begin_text_entry(state, clamped, NULL, 0);
    }
    break;
  case SHAULA_TOOL_MEASURE:
    if (inside) {
      if (state->measure_has_live) {
        ShaulaMeasureResult *mr = &state->measure_result;
        ShaulaPoint ms = {(double)mr->left, (double)mr->top};
        ShaulaPoint me = {(double)mr->right + 1.0, (double)mr->bottom + 1.0};
        if (shaula_point_distance(ms, me) >= 3.0) {
          ShaulaAnnotation *annotation = shaula_annotation_new_measure(
              ms, me, state->tool_defaults.measure.color,
              state->tool_defaults.measure.stroke_width);
          annotation->data.measure.rect_width = mr->width;
          annotation->data.measure.rect_height = mr->height;
          shaula_annotation_editor_add_annotation(state, annotation);
        }
      } else {
        shaula_preview_gesture_begin_operation(
            state, SHAULA_OPERATION_MEASURE, clamped);
      }
    }
    break;
  case SHAULA_TOOL_RECTANGLE:
    if (inside)
      shaula_preview_gesture_begin_operation(
          state, SHAULA_OPERATION_RECTANGLE, clamped);
    break;
  case SHAULA_TOOL_HIGHLIGHT:
    if (inside)
      shaula_preview_gesture_begin_operation(
          state, SHAULA_OPERATION_HIGHLIGHT, clamped);
    break;
  case SHAULA_TOOL_PEN:
    if (inside)
      shaula_preview_gesture_begin_operation(state, SHAULA_OPERATION_PEN,
                                             clamped);
    break;
  case SHAULA_TOOL_SPOTLIGHT:
    if (inside) {
      shaula_annotation_editor_clear_selection(state);
      shaula_preview_clear_region_selection(state);
      shaula_preview_gesture_begin_operation(
          state, SHAULA_OPERATION_SPOTLIGHT, clamped);
      debug_spotlight_rect("begin", state,
                           (ShaulaRect){clamped.x, clamped.y, 0.0, 0.0});
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

  double start_x = state->drag_start_x;
  double start_y = state->drag_start_y;
  if (gesture != NULL)
    (void)gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
  double x = start_x + dx;
  double y = start_y + dy;
  ShaulaPreviewOperation operation = state->operation;
  if (operation == SHAULA_OPERATION_ERASE_ANNOTATIONS) {
    ShaulaPoint screen_point = {x, y};
    state->eraser_hover_valid = TRUE;
    state->eraser_hover_screen = screen_point;
    eraser_add_trail_point(state, x, y, g_get_monotonic_time());
    eraser_mark_segment(state, state->eraser_last_screen, screen_point);
    state->eraser_last_screen = screen_point;
  } else if (shaula_preview_gesture_update(state, x, y, dx, dy) &&
             operation == SHAULA_OPERATION_SPOTLIGHT) {
    ShaulaRect rect = shaula_rect_from_points(state->drag_start_image,
                                              state->drag_current_image);
    debug_spotlight_rect("update", state, rect);
  }
  shaula_preview_queue_draw(state);
}

static void on_motion(GtkEventControllerMotion *controller, double x, double y,
                      gpointer data) {
  (void)controller;
  ShaulaPreviewState *state = data;
  if (state->operation == SHAULA_OPERATION_NONE)
    update_hover_color(state, x, y);

  if (state->active_tool == SHAULA_TOOL_ERASER &&
      state->operation == SHAULA_OPERATION_NONE && state->document.image != NULL) {
    gboolean changed = !state->eraser_hover_valid ||
                       fabs(state->eraser_hover_screen.x - x) > 0.25 ||
                       fabs(state->eraser_hover_screen.y - y) > 0.25;
    state->eraser_hover_valid = TRUE;
    state->eraser_hover_screen = (ShaulaPoint){x, y};
    if (state->area != NULL)
      gtk_widget_set_cursor_from_name(state->area, "none");
    if (changed)
      shaula_preview_queue_draw(state);
    return;
  }

  if (state->active_tool == SHAULA_TOOL_SELECT &&
      state->operation == SHAULA_OPERATION_NONE && state->area != NULL &&
      state->document.image != NULL) {
    gtk_widget_set_cursor_from_name(
        state->area,
        shaula_preview_gesture_hover_cursor(state, x, y));
  }

  if (state->active_tool == SHAULA_TOOL_MEASURE &&
      state->operation == SHAULA_OPERATION_NONE && state->document.image != NULL) {
    ShaulaPoint image_point =
        shaula_preview_canvas_screen_to_image(state, x, y);
    int px = 0;
    int py = 0;
    if (image_point_to_pixel(state, image_point, &px, &py)) {
      ShaulaMeasureResult prev = state->measure_result;
      shaula_measure_detect_edges(
          state->document.image, px, py, state->measure_tolerance,
          state->measure_compare, state->measure_mode,
          state->measure_outer_bounds, &state->measure_result);
      state->measure_has_live = TRUE;
      if (memcmp(&prev, &state->measure_result, sizeof(prev)) != 0)
        shaula_preview_queue_draw(state);
    } else {
      if (state->measure_has_live) {
        state->measure_has_live = FALSE;
        shaula_preview_queue_draw(state);
      }
    }
  }
}

static void on_motion_leave(GtkEventControllerMotion *controller,
                            gpointer data) {
  (void)controller;
  ShaulaPreviewState *state = data;
  if (state->measure_has_live) {
    state->measure_has_live = FALSE;
    state->measure_mode = SHAULA_MEASURE_MODE_AUTO;
    state->measure_outer_bounds = FALSE;
    shaula_preview_queue_draw(state);
  }
  if (state->eraser_hover_valid) {
    state->eraser_hover_valid = FALSE;
    shaula_preview_queue_draw(state);
  }
}

static void finish_shape_annotation(ShaulaPreviewState *state) {
  ShaulaAnnotation *annotation = NULL;
  switch (state->operation) {
  case SHAULA_OPERATION_ARROW:
  case SHAULA_OPERATION_LINE:
    if (shaula_point_distance(state->drag_start_image,
                              state->drag_current_image) >= 3.0)
      annotation = shaula_annotation_new_arrow(
          state->drag_start_image, state->drag_current_image,
          state->tool_defaults.arrow_line.color,
          state->tool_defaults.arrow_line.stroke_width);
    if (annotation != NULL) {
      annotation->data.arrow.stroke_style =
          state->tool_defaults.arrow_line.stroke_style;
      if (state->operation == SHAULA_OPERATION_LINE)
        annotation->data.arrow.has_head = FALSE;
    }
    break;
  case SHAULA_OPERATION_RECTANGLE: {
    ShaulaRect rect = shaula_rect_from_points(state->drag_start_image,
                                              state->drag_current_image);
    if (rect.width >= 3.0 && rect.height >= 3.0)
      annotation = shaula_annotation_new_rectangle(
          rect, state->tool_defaults.rectangle.color,
          state->tool_defaults.rectangle.stroke_width);
    if (annotation != NULL) {
      annotation->data.rectangle.stroke_style =
          state->tool_defaults.rectangle.stroke_style;
      annotation->data.rectangle.corners = state->tool_defaults.rectangle.corners;
      annotation->data.rectangle.filled = state->tool_defaults.rectangle.filled;
      shaula_annotation_update_bounds(annotation);
    }
    break;
  }
  case SHAULA_OPERATION_HIGHLIGHT:
    if (state->draft_pen_points != NULL && state->draft_pen_points->len >= 2) {
      annotation = shaula_annotation_new_highlight(
          (ShaulaPoint *)state->draft_pen_points->data,
          (int)state->draft_pen_points->len,
          state->tool_defaults.highlight.color,
          state->tool_defaults.highlight.stroke_width);
    }
    break;
  case SHAULA_OPERATION_MEASURE:
    if (shaula_point_distance(state->drag_start_image,
                              state->drag_current_image) >= 3.0)
      annotation = shaula_annotation_new_measure(
          state->drag_start_image, state->drag_current_image,
          state->tool_defaults.measure.color,
          state->tool_defaults.measure.stroke_width);
    break;
  case SHAULA_OPERATION_PEN:
    if (state->draft_pen_points != NULL && state->draft_pen_points->len >= 2) {
      annotation = shaula_annotation_new_pen(
          (ShaulaPoint *)state->draft_pen_points->data,
          (int)state->draft_pen_points->len, state->tool_defaults.pen.color,
          state->tool_defaults.pen.stroke_width);
    }
    break;
  case SHAULA_OPERATION_CROP:
  case SHAULA_OPERATION_ERASE_ANNOTATIONS:
  case SHAULA_OPERATION_MOVE:
  case SHAULA_OPERATION_BEND_ARROW:
  case SHAULA_OPERATION_RESIZE_ANNOTATION:
  case SHAULA_OPERATION_SELECT_REGION:
  case SHAULA_OPERATION_SPOTLIGHT:
  case SHAULA_OPERATION_NONE:
  case SHAULA_OPERATION_PAN:
  case SHAULA_OPERATION_TEXT:
    break;
  }

  if (annotation != NULL) {
    gboolean keep_selected =
        annotation->type == SHAULA_ANNOTATION_RECTANGLE;

    if (keep_selected) {
      shaula_annotation_editor_add_annotation(state, annotation);
      state->active_tool = SHAULA_TOOL_SELECT;
      shaula_preview_toolbar_update_tool_state(state);
      if (state->area != NULL)
        gtk_widget_set_cursor_from_name(state->area, "default");
    } else {
      /* Pen, Highlight, and Measure are continuous-drawing tools: drop the
       * just-committed selection so the next stroke is unobstructed. The
       * tool-defaults HUD must stay visible; for Pen/Highlight it was opened
       * on tool activation, but Measure only opens through selection today,
       * so re-assert its panel here.
       */
      ShaulaPropertiesPanel tool_panel = SHAULA_PROPERTIES_PANEL_NONE;
      switch (annotation->type) {
      case SHAULA_ANNOTATION_PEN:
        tool_panel = SHAULA_PROPERTIES_PANEL_PEN;
        break;
      case SHAULA_ANNOTATION_HIGHLIGHT:
        tool_panel = SHAULA_PROPERTIES_PANEL_HIGHLIGHT;
        break;
      case SHAULA_ANNOTATION_MEASURE:
        tool_panel = SHAULA_PROPERTIES_PANEL_MEASURE;
        break;
      default:
        break;
      }
      shaula_annotation_editor_add_annotation_unselected(state, annotation);
      if (tool_panel != SHAULA_PROPERTIES_PANEL_NONE) {
        shaula_properties_hud_set_panel(&state->properties_hud, tool_panel);
        shaula_preview_toolbar_update_selection_state(state);
      }
    }
  }
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy,
                        gpointer data) {
  ShaulaPreviewState *state = data;
  if (state->operation != SHAULA_OPERATION_NONE &&
      state->operation != SHAULA_OPERATION_TEXT) {
    /* Use the final release offset so commit state matches the visible drag
     * endpoint even when GTK coalesces the last motion event.
     */
    on_drag_update(gesture, dx, dy, data);
  }
  ShaulaPreviewOperation completed_operation = state->operation;
  if (completed_operation == SHAULA_OPERATION_ERASE_ANNOTATIONS) {
    shaula_preview_commit_eraser_pending(state);
    state->operation = SHAULA_OPERATION_NONE;
    state->eraser_drag_active = FALSE;
    eraser_start_tail_fade(state);
    gtk_widget_set_cursor_from_name(state->area, "none");
  } else if (shaula_preview_gesture_is_selection_operation(
                 completed_operation)) {
    int text_reedit_id = 0;
    (void)shaula_preview_gesture_end_selection(state, &text_reedit_id);
    if (text_reedit_id > 0)
      begin_text_entry_for_id(state, text_reedit_id);
    else
      gtk_widget_set_cursor_from_name(state->area,
                                      cursor_name_for_tool(state->active_tool));
  } else if (completed_operation == SHAULA_OPERATION_PAN) {
    state->operation = SHAULA_OPERATION_NONE;
    gtk_widget_set_cursor_from_name(state->area,
                                    cursor_name_for_tool(state->active_tool));
  } else if (completed_operation == SHAULA_OPERATION_SPOTLIGHT) {
    if (state->operation_changed) {
      ShaulaRect rect = shaula_rect_from_points(state->drag_start_image,
                                                state->drag_current_image);
      debug_spotlight_rect("end-commit", state, rect);
      shaula_preview_spotlight_rect(state, rect);
    }
    state->has_region_selection = FALSE;
    state->operation = SHAULA_OPERATION_NONE;
    gtk_widget_set_cursor_from_name(state->area,
                                    cursor_name_for_tool(state->active_tool));
  } else if (completed_operation == SHAULA_OPERATION_CROP) {
    if (!shaula_preview_apply_crop(state))
      shaula_preview_cancel_operation(state);
  } else {
    finish_shape_annotation(state);
  }
  if (state->operation != SHAULA_OPERATION_CROP &&
      state->operation != SHAULA_OPERATION_TEXT)
    state->operation = SHAULA_OPERATION_NONE;
  state->operation_changed = FALSE;
  shaula_preview_gesture_reset(&state->gesture);
  if (state->space_pan_restore_pending)
    restore_space_pan_tool(state);
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
  /* Keep the draft editor focused while SHAULA_OPERATION_TEXT is active so
   * HUD/toolbar clicks do not strand typing on the canvas shortcut path.
   */
  if (state->operation == SHAULA_OPERATION_TEXT && state->text_entry != NULL) {
    if (keyval == GDK_KEY_Escape) {
      finish_text_entry(state);
      return TRUE;
    }
    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) && ctrl) {
      finish_text_entry(state);
      return TRUE;
    }
    if (!focus_is_editable_text(state, focus))
      gtk_widget_grab_focus(state->text_entry);
    return FALSE;
  }
  if (focus_is_editable_text(state, focus)) {
    if (keyval == GDK_KEY_Escape && focus == state->text_entry) {
      finish_text_entry(state);
      return TRUE;
    }
    return FALSE;
  }
  if (is_space_key(keyval))
    return begin_space_pan_tool(state, modifiers);
  if (keyval == GDK_KEY_Escape) {
    if (state->active_tool == SHAULA_TOOL_ERASER) {
      shaula_preview_cancel_eraser_gesture(state);
      state->active_tool = SHAULA_TOOL_SELECT;
      state->eraser_hover_valid = FALSE;
      shaula_preview_toolbar_update_tool_state(state);
      if (state->area != NULL)
        gtk_widget_set_cursor_from_name(state->area, "default");
      return TRUE;
    }
    if (state->operation != SHAULA_OPERATION_NONE || state->has_crop_draft) {
      shaula_preview_cancel_operation(state);
      return TRUE;
    }
    if (state->measure_has_live) {
      state->measure_has_live = FALSE;
      state->measure_mode = SHAULA_MEASURE_MODE_AUTO;
      state->measure_outer_bounds = FALSE;
      shaula_preview_queue_draw(state);
      return TRUE;
    }
    if (shaula_annotation_editor_has_selection(state)) {
      shaula_annotation_editor_clear_selection(state);
      return TRUE;
    }
    if (state->last_action == NULL)
      state->last_action = "close";
    shaula_system_clipboard_paste_cancel(state);
    if (state->app != NULL)
      g_application_quit(G_APPLICATION(state->app));
    return TRUE;
  }
  if (keyval == GDK_KEY_q && !ctrl) {
    if (state->last_action == NULL)
      state->last_action = "close";
    shaula_system_clipboard_paste_cancel(state);
    if (state->app != NULL)
      g_application_quit(G_APPLICATION(state->app));
    return TRUE;
  }
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    if (state->has_crop_draft) {
      shaula_preview_apply_crop(state);
      return TRUE;
    }
    if (state->operation != SHAULA_OPERATION_NONE)
      return FALSE;
    shaula_preview_execute_command(state, SHAULA_PREVIEW_COMMAND_DONE);
    return TRUE;
  }

  gboolean shift = (modifiers & GDK_SHIFT_MASK) != 0;
  if (state->active_tool == SHAULA_TOOL_MEASURE &&
      state->operation == SHAULA_OPERATION_NONE) {
    if (keyval == GDK_KEY_Left || keyval == GDK_KEY_Right) {
      state->measure_mode = SHAULA_MEASURE_MODE_HORIZONTAL;
      state->measure_outer_bounds = shift;
      shaula_preview_queue_draw(state);
      return TRUE;
    }
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down) {
      state->measure_mode = SHAULA_MEASURE_MODE_VERTICAL;
      state->measure_outer_bounds = shift;
      shaula_preview_queue_draw(state);
      return TRUE;
    }
    if (keyval == GDK_KEY_Tab) {
      state->measure_mode = SHAULA_MEASURE_MODE_AUTO;
      state->measure_outer_bounds = FALSE;
      shaula_preview_queue_draw(state);
      return TRUE;
    }
  }

  ShaulaPreviewCommand command;
  if (shaula_preview_shortcut_command(keyval, modifiers, &command)) {
    if (command == SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR)
      update_hover_color_from_pointer(state);
    gboolean executed = shaula_preview_execute_command(state, command);
    return executed || command == SHAULA_PREVIEW_COMMAND_COPY_HOVER_COLOR;
  }

  if (!ctrl && (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal)) {
    shaula_preview_execute_command(state, SHAULA_PREVIEW_COMMAND_ZOOM_IN);
    return TRUE;
  }
  if (!ctrl && keyval == GDK_KEY_minus) {
    shaula_preview_execute_command(state, SHAULA_PREVIEW_COMMAND_ZOOM_OUT);
    return TRUE;
  }
  return FALSE;
}

static void on_key_released(GtkEventControllerKey *controller, guint keyval,
                            guint keycode, GdkModifierType modifiers,
                            gpointer data) {
  (void)controller;
  (void)keycode;
  (void)modifiers;
  ShaulaPreviewState *state = data;
  if (!is_space_key(keyval) || !state->space_pan_active)
    return;
  if (state->operation == SHAULA_OPERATION_PAN) {
    state->space_pan_restore_pending = TRUE;
    return;
  }
  restore_space_pan_tool(state);
}

static gboolean on_copy_hover_color_shortcut(GtkWidget *widget, GVariant *args,
                                             gpointer data) {
  (void)widget;
  (void)args;
  ShaulaPreviewState *state = data;
  GtkWidget *focus = state->window != NULL
                         ? gtk_window_get_focus(GTK_WINDOW(state->window))
                         : NULL;
  if (focus_is_editable_text(state, focus))
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
                          shaula_preview_select_properties_panel_build(state));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_properties_panel_build(state));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_arrow_properties_panel_build(state));
  gtk_overlay_add_overlay(
      GTK_OVERLAY(overlay),
      shaula_preview_rectangle_properties_panel_build(state));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_pen_properties_panel_build(state));
  gtk_overlay_add_overlay(
      GTK_OVERLAY(overlay),
      shaula_preview_highlight_properties_panel_build(state));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_text_properties_panel_build(state));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_measure_properties_panel_build(state));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_eraser_properties_panel_build(state));

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
  g_signal_connect(motion, "leave", G_CALLBACK(on_motion_leave), state);
  gtk_widget_add_controller(area, motion);

  GtkEventController *keys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
  g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), state);
  g_signal_connect(keys, "key-released", G_CALLBACK(on_key_released), state);
  gtk_widget_add_controller(state->window, keys);

  GtkEventController *shortcut_controller = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(
      GTK_SHORTCUT_CONTROLLER(shortcut_controller), GTK_SHORTCUT_SCOPE_GLOBAL);
  gtk_shortcut_controller_add_shortcut(
      GTK_SHORTCUT_CONTROLLER(shortcut_controller),
      gtk_shortcut_new(
          gtk_keyval_trigger_new(GDK_KEY_Tab, 0),
          gtk_callback_action_new(on_copy_hover_color_shortcut, state, NULL)));
  gtk_widget_add_controller(state->window, shortcut_controller);

  g_signal_connect(state->window, "map", G_CALLBACK(on_map), state);

  return overlay;
}
