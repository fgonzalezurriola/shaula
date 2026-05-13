#include "preview_canvas.h"

#include <math.h>
#include <stdio.h>

#include "preview_actions.h"
#include "preview_commands.h"
#include "preview_measure.h"
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

static gboolean spotlight_debug_enabled(void) {
  const char *value = g_getenv("SHAULA_DEBUG_SPOTLIGHT");
  return value != NULL && value[0] != '\0' && g_strcmp0(value, "0") != 0;
}

static void debug_spotlight_rect(const char *stage, ShaulaPreviewState *state,
                                 ShaulaRect rect) {
  if (!spotlight_debug_enabled())
    return;
  g_printerr("[DEBUG-spotlight] %s rect=(%.2f,%.2f %.2fx%.2f) "
             "start=(%.2f,%.2f) current=(%.2f,%.2f) pan=(%.2f,%.2f) "
             "zoom=%.4f area=%dx%d image=%dx%d\n",
             stage, rect.x, rect.y, rect.width, rect.height,
             state->drag_start_image.x, state->drag_start_image.y,
             state->drag_current_image.x, state->drag_current_image.y,
             state->pan_x, state->pan_y, state->zoom,
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
      state->drag_start_image, state->drag_current_image, state->arrow_color,
      state->arrow_stroke_width);
  shaula_annotation_draw(cr, annotation);
  shaula_annotation_free(annotation);
}

static void draw_measure_draft(cairo_t *cr, ShaulaPreviewState *state) {
 ShaulaAnnotation *annotation = shaula_annotation_new_measure(
 state->drag_start_image, state->drag_current_image, state->measure_color,
 state->measure_stroke_width);
 shaula_annotation_draw(cr, annotation);
 shaula_annotation_free(annotation);
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
  if (state->active_properties_panel == SHAULA_PROPERTIES_PANEL_SPOTLIGHT)
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
 cairo_set_source_rgba(cr, state->measure_color.r, state->measure_color.g,
 state->measure_color.b, 0.85);
 cairo_set_line_width(cr, state->measure_stroke_width / state->zoom);

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
 cairo_set_line_width(cr, state->measure_stroke_width / state->zoom);
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
    draw_arrow_draft(cr, state);
    break;
  case SHAULA_OPERATION_RECTANGLE:
    draw_draft_rect(cr,
                    shaula_rect_from_points(state->drag_start_image,
                                            state->drag_current_image),
                    state->rectangle_color, state->rectangle_stroke_width,
                    state->rectangle_stroke_style, state->rectangle_corners,
                    state->rectangle_filled);
    break;
  case SHAULA_OPERATION_HIGHLIGHT:
    draw_path_draft(cr, state, state->highlight_color,
                    state->highlight_stroke_width);
    break;
  case SHAULA_OPERATION_MEASURE:
    draw_measure_draft(cr, state);
    break;
  case SHAULA_OPERATION_PEN:
    draw_path_draft(cr, state, state->pen_color, state->pen_stroke_width);
    break;
  case SHAULA_OPERATION_SPOTLIGHT:
    draw_plain_draft_rect(cr,
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

static char *text_view_contents(GtkTextView *view) {
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void finish_text_entry(ShaulaPreviewState *state) {
  if (state == NULL || state->text_entry == NULL)
    return;
  char *text = text_view_contents(GTK_TEXT_VIEW(state->text_entry));
  char *trimmed = g_strdup(text != NULL ? text : "");
  g_strstrip(trimmed);
  gboolean created = FALSE;
  if (trimmed[0] != '\0') {
    ShaulaAnnotation *annotation = shaula_annotation_new_text(
        state->text_anchor_image, text, state->text_color,
        state->text_font_size, state->text_align);
    shaula_preview_add_annotation(state, annotation);
    shaula_preview_select_annotation(state, annotation);
    state->active_properties_panel = SHAULA_PROPERTIES_PANEL_TEXT;
    created = TRUE;
  }
  g_free(trimmed);
  g_free(text);
  shaula_preview_cancel_operation(state);
  if (created) {
    state->active_tool = SHAULA_TOOL_SELECT;
    state->active_properties_panel = SHAULA_PROPERTIES_PANEL_TEXT;
    if (state->area != NULL)
      gtk_widget_set_cursor_from_name(state->area, "default");
    shaula_preview_toolbar_update_tool_state(state);
    shaula_preview_toolbar_update_selection_state(state);
  }
}

static gboolean on_text_entry_key(GtkEventControllerKey *controller,
                                  guint keyval, guint keycode,
                                  GdkModifierType modifiers, gpointer data) {
  (void)controller;
  (void)keycode;
  ShaulaPreviewState *state = data;
  if (keyval == GDK_KEY_Escape) {
    shaula_preview_cancel_operation(state);
    return TRUE;
  }
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    gboolean copy_to_clipboard = (modifiers & GDK_SHIFT_MASK) != 0;
    finish_text_entry(state);
    shaula_preview_action_accept(state, copy_to_clipboard);
    return TRUE;
  }
  return FALSE;
}

static void apply_text_entry_style(ShaulaPreviewState *state,
                                   GtkWidget *entry) {
  GtkCssProvider *provider = gtk_css_provider_new();
  gchar *css = g_strdup_printf(
      ".shaula-text-editor {"
      "  color: rgba(%d,%d,%d,%.3f);"
      "  font-family: Sans;"
      "  font-size: %.1fpx;"
      "  font-weight: 700;"
      "  background: alpha(@theme_bg_color, 0.12);"
      "  border: 1px solid alpha(@theme_fg_color, 0.18);"
      "  border-radius: 6px;"
      "  padding: 0;"
      "}"
      ".shaula-text-editor text {"
      "  color: rgba(%d,%d,%d,%.3f);"
      "  background: transparent;"
      "  padding: 0;"
      "}",
      (int)round(state->text_color.r * 255.0),
      (int)round(state->text_color.g * 255.0),
      (int)round(state->text_color.b * 255.0), state->text_color.a,
      state->text_font_size * state->zoom,
      (int)round(state->text_color.r * 255.0),
      (int)round(state->text_color.g * 255.0),
      (int)round(state->text_color.b * 255.0), state->text_color.a);
  gtk_css_provider_load_from_data(provider, css, -1);
  gtk_style_context_add_provider(gtk_widget_get_style_context(entry),
                                 GTK_STYLE_PROVIDER(provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_free(css);
  g_object_unref(provider);
}

static void begin_text_entry(ShaulaPreviewState *state, ShaulaPoint image_point) {
  if (state->canvas_overlay == NULL)
    return;
  shaula_preview_cancel_operation(state);
  ShaulaPoint screen = shaula_preview_canvas_image_to_screen(state, image_point);
  GtkWidget *entry = gtk_text_view_new();
  state->text_entry = entry;
  state->text_anchor_image = image_point;
  state->operation = SHAULA_OPERATION_TEXT;

  gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(entry), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(entry), GTK_WRAP_NONE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(entry), 0);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(entry), 0);
  GtkJustification justification = GTK_JUSTIFY_LEFT;
  if (state->text_align == SHAULA_TEXT_ALIGN_CENTER)
    justification = GTK_JUSTIFY_CENTER;
  else if (state->text_align == SHAULA_TEXT_ALIGN_RIGHT)
    justification = GTK_JUSTIFY_RIGHT;
  gtk_text_view_set_justification(GTK_TEXT_VIEW(entry), justification);
  gtk_widget_add_css_class(entry, "shaula-text-editor");
  apply_text_entry_style(state, entry);

  int image_w = shaula_preview_image_width(state);
  double available_image_width = (double)image_w - image_point.x;
  if (state->text_align == SHAULA_TEXT_ALIGN_CENTER)
    available_image_width = 2.0 * MIN(image_point.x,
                                      (double)image_w - image_point.x);
  else if (state->text_align == SHAULA_TEXT_ALIGN_RIGHT)
    available_image_width = image_point.x;
  int editor_width =
      MAX(80, (int)floor(MAX(1.0, available_image_width) * state->zoom));
  gtk_widget_set_size_request(entry, editor_width, -1);
  gtk_widget_set_halign(entry, GTK_ALIGN_START);
  gtk_widget_set_valign(entry, GTK_ALIGN_START);
  double x = screen.x;
  if (state->text_align == SHAULA_TEXT_ALIGN_CENTER)
    x -= editor_width / 2.0;
  else if (state->text_align == SHAULA_TEXT_ALIGN_RIGHT)
    x -= editor_width;
  gtk_widget_set_margin_start(entry, MAX(0, (int)x));
  gtk_widget_set_margin_top(entry, MAX(0, (int)screen.y));
  gtk_overlay_add_overlay(GTK_OVERLAY(state->canvas_overlay), entry);

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
  if (operation == SHAULA_OPERATION_SELECT_REGION) {
    state->has_region_selection = FALSE;
    state->region_selection_rect = (ShaulaRect){point.x, point.y, 0, 0};
  }
  if ((operation == SHAULA_OPERATION_PEN ||
       operation == SHAULA_OPERATION_HIGHLIGHT) &&
      state->draft_pen_points != NULL) {
    g_array_set_size(state->draft_pen_points, 0);
    g_array_append_val(state->draft_pen_points, point);
  }
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y,
                          gpointer data) {
  ShaulaPreviewState *state = data;
  if (state->image == NULL)
    return;
  state->drag_start_x = x;
  state->drag_start_y = y;
  update_hover_color(state, x, y);

  if (state->operation == SHAULA_OPERATION_TEXT &&
      state->text_entry != NULL) {
    finish_text_entry(state);
    return;
  }

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
 if (inside) {
 if (state->measure_has_live) {
  ShaulaMeasureResult *mr = &state->measure_result;
  ShaulaPoint ms = {(double)mr->left, (double)mr->top};
  ShaulaPoint me = {(double)mr->right + 1.0, (double)mr->bottom + 1.0};
  if (shaula_point_distance(ms, me) >= 3.0) {
  ShaulaAnnotation *annotation =
  shaula_annotation_new_measure(ms, me,
  state->measure_color, state->measure_stroke_width);
  annotation->data.measure.rect_width = mr->width;
  annotation->data.measure.rect_height = mr->height;
  shaula_preview_add_annotation(state, annotation);
  }
 } else {
 start_operation(state, SHAULA_OPERATION_MEASURE, clamped);
 }
 }
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
      debug_spotlight_rect(
          "begin", state,
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
    debug_spotlight_rect("update", state, rect);
    break;
  }
  case SHAULA_OPERATION_CROP:
    state->drag_current_image = image_point;
    state->crop_draft =
        shaula_rect_from_points(state->drag_start_image, image_point);
    break;
  case SHAULA_OPERATION_ARROW:
  case SHAULA_OPERATION_RECTANGLE:
  case SHAULA_OPERATION_MEASURE:
    state->drag_current_image = image_point;
    break;
  case SHAULA_OPERATION_HIGHLIGHT:
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
	ShaulaPreviewState *state = data;
	update_hover_color(state, x, y);

	if (state->active_tool == SHAULA_TOOL_MEASURE &&
		state->operation == SHAULA_OPERATION_NONE &&
		state->image != NULL) {
		ShaulaPoint image_point =
			shaula_preview_canvas_screen_to_image(state, x, y);
		int px = 0;
		int py = 0;
		if (image_point_to_pixel(state, image_point, &px, &py)) {
			ShaulaMeasureResult prev = state->measure_result;
			shaula_measure_detect_edges(state->image, px, py,
				state->measure_tolerance, state->measure_compare,
				state->measure_mode, state->measure_outer_bounds,
				&state->measure_result);
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
      annotation = shaula_annotation_new_rectangle(
          rect, state->rectangle_color, state->rectangle_stroke_width);
    if (annotation != NULL) {
      annotation->data.rectangle.stroke_style = state->rectangle_stroke_style;
      annotation->data.rectangle.corners = state->rectangle_corners;
      annotation->data.rectangle.filled = state->rectangle_filled;
      shaula_annotation_update_bounds(annotation);
    }
    break;
  }
  case SHAULA_OPERATION_HIGHLIGHT:
    if (state->draft_pen_points != NULL && state->draft_pen_points->len >= 2) {
      annotation = shaula_annotation_new_highlight(
          (ShaulaPoint *)state->draft_pen_points->data,
          (int)state->draft_pen_points->len, state->highlight_color,
          state->highlight_stroke_width);
    }
    break;
 case SHAULA_OPERATION_MEASURE:
 if (shaula_point_distance(state->drag_start_image,
 state->drag_current_image) >= 3.0)
 annotation = shaula_annotation_new_measure(
 state->drag_start_image, state->drag_current_image,
 state->measure_color, state->measure_stroke_width);
    break;
  case SHAULA_OPERATION_PEN:
    if (state->draft_pen_points != NULL && state->draft_pen_points->len >= 2) {
      annotation = shaula_annotation_new_pen(
          (ShaulaPoint *)state->draft_pen_points->data,
          (int)state->draft_pen_points->len, state->pen_color,
          state->pen_stroke_width);
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
    } else if (annotation->type == SHAULA_ANNOTATION_RECTANGLE) {
      shaula_preview_select_annotation(state, annotation);
      state->active_rectangle_index = (int)state->annotations->len - 1;
      state->active_properties_panel = SHAULA_PROPERTIES_PANEL_RECTANGLE;
      state->active_tool = SHAULA_TOOL_SELECT;
      shaula_preview_toolbar_update_tool_state(state);
      if (state->area != NULL)
        gtk_widget_set_cursor_from_name(state->area, "default");
      shaula_preview_toolbar_update_selection_state(state);
    } else if (annotation->type == SHAULA_ANNOTATION_PEN) {
      shaula_preview_select_annotation(state, annotation);
      state->active_properties_panel = SHAULA_PROPERTIES_PANEL_PEN;
    } else if (annotation->type == SHAULA_ANNOTATION_HIGHLIGHT) {
      shaula_preview_select_annotation(state, annotation);
      state->active_properties_panel = SHAULA_PROPERTIES_PANEL_HIGHLIGHT;
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
      if (state->operation_changed) {
        ShaulaRect rect =
            shaula_rect_from_points(state->drag_start_image,
                                    state->drag_current_image);
        debug_spotlight_rect("end-commit", state, rect);
        shaula_preview_spotlight_rect(state, rect);
      }
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
  if (focus != NULL &&
      (GTK_IS_EDITABLE(focus) || focus == state->text_entry ||
       (state->text_entry != NULL &&
        gtk_widget_is_ancestor(focus, state->text_entry)))) {
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
 if (state->measure_has_live) {
 state->measure_has_live = FALSE;
 state->measure_mode = SHAULA_MEASURE_MODE_AUTO;
 state->measure_outer_bounds = FALSE;
 shaula_preview_queue_draw(state);
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
  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
    if (state->has_crop_draft) {
      shaula_preview_apply_crop(state);
      return TRUE;
    }
 shaula_preview_action_accept(state, (modifiers & GDK_SHIFT_MASK) != 0);
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
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_rectangle_properties_panel_build(
                              state));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_pen_properties_panel_build(state));
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
                          shaula_preview_highlight_properties_panel_build(state));
 gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
 shaula_preview_text_properties_panel_build(state));
 gtk_overlay_add_overlay(GTK_OVERLAY(overlay),
 shaula_preview_measure_properties_panel_build(state));

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
