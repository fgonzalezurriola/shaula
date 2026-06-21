#include "preview_annotations.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <string.h>

static void rectangle_path(cairo_t *cr, ShaulaRect rect,
                           PreviewRectangleCorners corners);

static ShaulaRect rect_from_points_c(ShaulaPoint a, ShaulaPoint b) {
  double x0 = MIN(a.x, b.x);
  double y0 = MIN(a.y, b.y);
  double x1 = MAX(a.x, b.x);
  double y1 = MAX(a.y, b.y);
  return (ShaulaRect){x0, y0, x1 - x0, y1 - y0};
}

static ShaulaRect rect_expanded_c(ShaulaRect rect, double amount) {
  return (ShaulaRect){rect.x - amount, rect.y - amount,
                      rect.width + 2.0 * amount,
                      rect.height + 2.0 * amount};
}

static ShaulaRect path_bounds_c(ShaulaPenPath path) {
  if (path.len <= 0)
    return (ShaulaRect){0, 0, 0, 0};

  double min_x = path.points[0].x;
  double min_y = path.points[0].y;
  double max_x = min_x;
  double max_y = min_y;
  for (int i = 1; i < path.len; i++) {
    min_x = MIN(min_x, path.points[i].x);
    min_y = MIN(min_y, path.points[i].y);
    max_x = MAX(max_x, path.points[i].x);
    max_y = MAX(max_y, path.points[i].y);
  }
  return (ShaulaRect){min_x, min_y, max_x - min_x, max_y - min_y};
}

static ShaulaRect rect_union_c(ShaulaRect a, ShaulaRect b) {
  double x0 = MIN(a.x, b.x);
  double y0 = MIN(a.y, b.y);
  double x1 = MAX(a.x + a.width, b.x + b.width);
  double y1 = MAX(a.y + a.height, b.y + b.height);
  return (ShaulaRect){x0, y0, x1 - x0, y1 - y0};
}

static ShaulaPoint quadratic_point_c(ShaulaPoint start, ShaulaPoint control,
                                     ShaulaPoint end, double t) {
  double inv = 1.0 - t;
  return (ShaulaPoint){
      inv * inv * start.x + 2.0 * inv * t * control.x + t * t * end.x,
      inv * inv * start.y + 2.0 * inv * t * control.y + t * t * end.y};
}

static ShaulaRect quadratic_bounds_c(ShaulaPoint start, ShaulaPoint control,
                                     ShaulaPoint end) {
  ShaulaRect bounds = rect_from_points_c(start, end);
  double denominator_x = start.x - 2.0 * control.x + end.x;
  double denominator_y = start.y - 2.0 * control.y + end.y;

  if (fabs(denominator_x) > 0.000001) {
    double t = (start.x - control.x) / denominator_x;
    if (t > 0.0 && t < 1.0) {
      ShaulaPoint point = quadratic_point_c(start, control, end, t);
      bounds = rect_union_c(bounds, (ShaulaRect){point.x, point.y, 0, 0});
    }
  }
  if (fabs(denominator_y) > 0.000001) {
    double t = (start.y - control.y) / denominator_y;
    if (t > 0.0 && t < 1.0) {
      ShaulaPoint point = quadratic_point_c(start, control, end, t);
      bounds = rect_union_c(bounds, (ShaulaRect){point.x, point.y, 0, 0});
    }
  }
  return bounds;
}

static ShaulaRect arrow_head_bounds_c(ShaulaPoint direction, ShaulaPoint end,
                                      double stroke_width) {
  double head_length = MAX(12.0, stroke_width * 4.5);
  double angle = atan2(end.y - direction.y, end.x - direction.x);
  ShaulaPoint points[] = {
      end,
      {end.x + cos(angle + G_PI - 0.42) * head_length,
       end.y + sin(angle + G_PI - 0.42) * head_length},
      {end.x + cos(angle + G_PI) * head_length * 0.65,
       end.y + sin(angle + G_PI) * head_length * 0.65},
      {end.x + cos(angle + G_PI + 0.42) * head_length,
       end.y + sin(angle + G_PI + 0.42) * head_length},
  };
  return path_bounds_c((ShaulaPenPath){points, 4, 4});
}

static ShaulaRect arrow_bounds_c(const ShaulaAnnotation *annotation) {
  ShaulaPoint start = annotation->data.arrow.start;
  ShaulaPoint end = annotation->data.arrow.end;
  ShaulaRect bounds = annotation->data.arrow.is_curved
                          ? quadratic_bounds_c(start,
                                               annotation->data.arrow.control,
                                               end)
                          : rect_from_points_c(start, end);
  if (annotation->data.arrow.has_head) {
    ShaulaPoint direction = annotation->data.arrow.is_curved
                                ? annotation->data.arrow.control
                                : start;
    bounds = rect_union_c(
        bounds, arrow_head_bounds_c(direction, end, annotation->stroke_width));
  }
  return rect_expanded_c(bounds, annotation->stroke_width / 2.0 + 6.0);
}

static ShaulaAnnotation *annotation_alloc(ShaulaAnnotationType type,
                                          ShaulaColor color,
                                          double stroke_width) {
  ShaulaAnnotation *annotation = g_new0(ShaulaAnnotation, 1);
  annotation->type = type;
  annotation->color = color;
  annotation->stroke_width = stroke_width;
  return annotation;
}

static ShaulaRect line_bounds(ShaulaPoint start, ShaulaPoint end,
                              double padding) {
  return rect_expanded_c(rect_from_points_c(start, end), padding);
}

const char *shaula_text_font_family(ShaulaTextFontMode font_mode) {
  switch (font_mode) {
  case SHAULA_TEXT_FONT_SKETCH:
    return "Excalifont, Virgil, Comic Shanns, Sans";
  case SHAULA_TEXT_FONT_NORMAL:
  default:
    return "Geist, Inter, Sans";
  }
}

static PangoLayout *create_text_layout(cairo_t *cr, const char *text,
                                       double font_size,
                                       ShaulaTextFontMode font_mode,
                                       PangoFontDescription **desc_out) {
  PangoLayout *layout = pango_cairo_create_layout(cr);
  pango_layout_set_text(layout, text != NULL ? text : "", -1);
  PangoFontDescription *desc =
      pango_font_description_from_string(shaula_text_font_family(font_mode));
  gboolean sketch = font_mode == SHAULA_TEXT_FONT_SKETCH;
  pango_font_description_set_weight(desc, sketch ? PANGO_WEIGHT_NORMAL
                                                 : PANGO_WEIGHT_BOLD);
  pango_font_description_set_absolute_size(
      desc,
      font_size * PANGO_SCALE * (sketch ? 1.4 : 1.3)); /* Pango compensation */
  pango_layout_set_font_description(layout, desc);
  if (desc_out != NULL)
    *desc_out = desc;
  else
    pango_font_description_free(desc);
  return layout;
}

static double text_line_advance(PangoRectangle logical, double font_size) {
  return MAX(font_size * 1.25, (double)logical.height);
}

typedef struct {
  PangoRectangle ink;
  PangoRectangle logical;
  ShaulaRect bounds;
  double draw_x;
  double draw_y;
  double advance;
} ShaulaTextLineMetrics;

static ShaulaTextLineMetrics
text_line_metrics(PangoLayout *layout, const char *line, int line_len,
                  ShaulaPoint position, double y, double font_size,
                  ShaulaTextAlign align) {
  pango_layout_set_text(layout, line != NULL ? line : "", line_len);
  ShaulaTextLineMetrics metrics = {0};
  pango_layout_get_pixel_extents(layout, &metrics.ink, &metrics.logical);

  metrics.draw_x = position.x;
  if (align == SHAULA_TEXT_ALIGN_CENTER)
    metrics.draw_x -= (double)metrics.logical.width * 0.5;
  else if (align == SHAULA_TEXT_ALIGN_RIGHT)
    metrics.draw_x -= (double)metrics.logical.width;
  metrics.draw_y = y;

  double x0 = MIN((double)metrics.ink.x, (double)metrics.logical.x);
  double y0 = MIN((double)metrics.ink.y, (double)metrics.logical.y);
  double x1 = MAX((double)metrics.ink.x + (double)metrics.ink.width,
                  (double)metrics.logical.x + (double)metrics.logical.width);
  double y1 = MAX((double)metrics.ink.y + (double)metrics.ink.height,
                  (double)metrics.logical.y + (double)metrics.logical.height);
  metrics.bounds =
      (ShaulaRect){metrics.draw_x + x0, metrics.draw_y + y0,
                   MAX(0.0, x1 - x0), MAX(0.0, y1 - y0)};
  metrics.advance = text_line_advance(metrics.logical, font_size);
  return metrics;
}

static ShaulaRect text_bounds(ShaulaPoint position, const char *text,
                              double font_size, ShaulaTextAlign align,
                              ShaulaTextFontMode font_mode) {
  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t *cr = cairo_create(surface);
  PangoFontDescription *desc = NULL;
  PangoLayout *layout =
      create_text_layout(cr, "", font_size, font_mode, &desc);
  char **lines = g_strsplit(text != NULL ? text : "", "\n", -1);
  double min_x = position.x;
  double max_x = position.x;
  double y = position.y;
  double max_y = position.y;
  for (int i = 0; lines[i] != NULL; i++) {
    ShaulaTextLineMetrics metrics =
        text_line_metrics(layout, lines[i], -1, position, y, font_size, align);
    min_x = MIN(min_x, metrics.bounds.x);
    max_x = MAX(max_x, metrics.bounds.x + metrics.bounds.width);
    max_y = MAX(max_y, metrics.bounds.y + metrics.bounds.height);
    y += metrics.advance;
  }
  if (lines[0] == NULL)
    max_y = MAX(max_y, position.y + font_size * 1.25);
  g_strfreev(lines);
  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  return (ShaulaRect){min_x, position.y, max_x - min_x,
                      MAX(font_size * 1.25, max_y - position.y)};
}

ShaulaAnnotation *shaula_annotation_new_arrow(ShaulaPoint start,
                                              ShaulaPoint end,
                                              ShaulaColor color,
                                              double stroke_width) {
  ShaulaAnnotation *annotation =
      annotation_alloc(SHAULA_ANNOTATION_ARROW, color, stroke_width);
  annotation->data.arrow.start = start;
  annotation->data.arrow.end = end;
  annotation->data.arrow.stroke_style = PREVIEW_ARROW_STROKE_SOLID;
  annotation->data.arrow.is_curved = FALSE;
  annotation->data.arrow.has_head = TRUE;
  shaula_annotation_update_bounds(annotation);
  return annotation;
}

ShaulaAnnotation *
shaula_annotation_new_text(ShaulaPoint position, const char *text,
                           ShaulaColor color, double font_size,
                           ShaulaTextAlign align,
                           ShaulaTextFontMode font_mode) {
  ShaulaAnnotation *annotation =
      annotation_alloc(SHAULA_ANNOTATION_TEXT, color, 1.0);
  annotation->data.text.position = position;
  annotation->data.text.text = g_strdup(text != NULL ? text : "");
  annotation->data.text.font_size = font_size;
  annotation->data.text.align = align;
  annotation->data.text.font_mode = font_mode;
  shaula_annotation_update_bounds(annotation);
  return annotation;
}

ShaulaAnnotation *shaula_annotation_new_measure(ShaulaPoint start,
                                                ShaulaPoint end,
                                                ShaulaColor color,
                                                double stroke_width) {
  ShaulaAnnotation *annotation =
      annotation_alloc(SHAULA_ANNOTATION_MEASURE, color, stroke_width);
  annotation->data.measure.start = start;
  annotation->data.measure.end = end;
  annotation->data.measure.distance_px = shaula_point_distance(start, end);
  annotation->data.measure.rect_width = 0;
  annotation->data.measure.rect_height = 0;
  shaula_annotation_update_bounds(annotation);
  return annotation;
}

ShaulaAnnotation *shaula_annotation_new_rectangle(ShaulaRect rect,
                                                  ShaulaColor color,
                                                  double stroke_width) {
  ShaulaAnnotation *annotation =
      annotation_alloc(SHAULA_ANNOTATION_RECTANGLE, color, stroke_width);
  annotation->data.rectangle.rect = shaula_rect_normalized(rect);
  annotation->data.rectangle.stroke_style = PREVIEW_ARROW_STROKE_DASHED;
  annotation->data.rectangle.corners = PREVIEW_RECTANGLE_CORNERS_ROUNDED;
  annotation->data.rectangle.filled = FALSE;
  shaula_annotation_update_bounds(annotation);
  return annotation;
}

static void copy_path_to_annotation(ShaulaPenPath *path,
                                    const ShaulaPoint *points, int len) {
  if (len > 0) {
    path->points = g_new(ShaulaPoint, len);
    memcpy(path->points, points, sizeof(ShaulaPoint) * len);
    path->len = len;
    path->cap = len;
  }
}

ShaulaAnnotation *shaula_annotation_new_highlight(const ShaulaPoint *points,
                                                  int len, ShaulaColor color,
                                                  double stroke_width) {
  ShaulaAnnotation *annotation =
      annotation_alloc(SHAULA_ANNOTATION_HIGHLIGHT, color, stroke_width);
  copy_path_to_annotation(&annotation->data.highlight, points, len);
  shaula_annotation_update_bounds(annotation);
  return annotation;
}

ShaulaAnnotation *shaula_annotation_new_pen(const ShaulaPoint *points, int len,
                                            ShaulaColor color,
                                            double stroke_width) {
  ShaulaAnnotation *annotation =
      annotation_alloc(SHAULA_ANNOTATION_PEN, color, stroke_width);
  copy_path_to_annotation(&annotation->data.pen, points, len);
  shaula_annotation_update_bounds(annotation);
  return annotation;
}

ShaulaAnnotation *shaula_annotation_clone(const ShaulaAnnotation *annotation) {
  if (annotation == NULL)
    return NULL;
  ShaulaAnnotation *clone = annotation_alloc(
      annotation->type, annotation->color, annotation->stroke_width);
  clone->id = annotation->id;
  clone->selected = annotation->selected;
  clone->bounds = annotation->bounds;
  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    clone->data.arrow = annotation->data.arrow;
    break;
  case SHAULA_ANNOTATION_TEXT:
    clone->data.text.position = annotation->data.text.position;
    clone->data.text.font_size = annotation->data.text.font_size;
    clone->data.text.align = annotation->data.text.align;
    clone->data.text.font_mode = annotation->data.text.font_mode;
    clone->data.text.text = g_strdup(annotation->data.text.text);
    break;
  case SHAULA_ANNOTATION_MEASURE:
    clone->data.measure = annotation->data.measure;
    break;
  case SHAULA_ANNOTATION_RECTANGLE:
    clone->data.rectangle = annotation->data.rectangle;
    break;
  case SHAULA_ANNOTATION_HIGHLIGHT:
    clone->data.highlight.len = annotation->data.highlight.len;
    clone->data.highlight.cap = annotation->data.highlight.len;
    if (annotation->data.highlight.len > 0) {
      clone->data.highlight.points =
          g_new(ShaulaPoint, annotation->data.highlight.len);
      memcpy(clone->data.highlight.points, annotation->data.highlight.points,
             sizeof(ShaulaPoint) * annotation->data.highlight.len);
    }
    break;
  case SHAULA_ANNOTATION_PEN:
    clone->data.pen.len = annotation->data.pen.len;
    clone->data.pen.cap = annotation->data.pen.len;
    if (annotation->data.pen.len > 0) {
      clone->data.pen.points = g_new(ShaulaPoint, annotation->data.pen.len);
      memcpy(clone->data.pen.points, annotation->data.pen.points,
             sizeof(ShaulaPoint) * annotation->data.pen.len);
    }
    break;
  }
  return clone;
}

void shaula_annotation_free(gpointer data) {
  ShaulaAnnotation *annotation = data;
  if (annotation == NULL)
    return;
  if (annotation->type == SHAULA_ANNOTATION_TEXT)
    g_free(annotation->data.text.text);
  if (annotation->type == SHAULA_ANNOTATION_HIGHLIGHT)
    g_free(annotation->data.highlight.points);
  if (annotation->type == SHAULA_ANNOTATION_PEN)
    g_free(annotation->data.pen.points);
  g_free(annotation);
}

GPtrArray *shaula_annotations_clone_array(GPtrArray *annotations) {
  GPtrArray *clone = g_ptr_array_new_with_free_func(shaula_annotation_free);
  if (annotations == NULL)
    return clone;
  for (guint i = 0; i < annotations->len; i++)
    g_ptr_array_add(clone,
                    shaula_annotation_clone(g_ptr_array_index(annotations, i)));
  return clone;
}

gboolean shaula_annotations_selected_bounds(GPtrArray *annotations,
                                             ShaulaRect *bounds_out) {
  if (annotations == NULL || bounds_out == NULL)
    return FALSE;

  gboolean found = FALSE;
  ShaulaRect bounds = {0};
  for (guint i = 0; i < annotations->len; i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(annotations, i);
    if (annotation == NULL || !annotation->selected)
      continue;
    bounds = found ? rect_union_c(bounds, annotation->bounds)
                   : annotation->bounds;
    found = TRUE;
  }

  if (found)
    *bounds_out = bounds;
  return found;
}

void shaula_annotation_update_bounds(ShaulaAnnotation *annotation) {
  if (annotation == NULL)
    return;
  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    annotation->bounds = arrow_bounds_c(annotation);
    break;
  case SHAULA_ANNOTATION_TEXT:
    annotation->bounds = text_bounds(
        annotation->data.text.position, annotation->data.text.text,
        annotation->data.text.font_size, annotation->data.text.align,
        annotation->data.text.font_mode);
    break;
  case SHAULA_ANNOTATION_MEASURE:
    annotation->data.measure.distance_px = shaula_point_distance(
        annotation->data.measure.start, annotation->data.measure.end);
    annotation->bounds = line_bounds(annotation->data.measure.start,
                                     annotation->data.measure.end,
                                     annotation->stroke_width + 18.0);
    break;
  case SHAULA_ANNOTATION_RECTANGLE:
    annotation->bounds = rect_expanded_c(annotation->data.rectangle.rect,
                                         annotation->stroke_width + 4.0);
    break;
  case SHAULA_ANNOTATION_HIGHLIGHT:
  case SHAULA_ANNOTATION_PEN: {
    ShaulaPenPath path = annotation->type == SHAULA_ANNOTATION_HIGHLIGHT
                             ? annotation->data.highlight
                             : annotation->data.pen;
    if (path.len <= 0) {
      annotation->bounds = (ShaulaRect){0, 0, 0, 0};
      break;
    }
    annotation->bounds = rect_expanded_c(
        path_bounds_c(path), annotation->stroke_width / 2.0 + 6.0);
    break;
  }
  }
}

void shaula_annotation_move(ShaulaAnnotation *annotation, double dx,
                            double dy) {
  if (annotation == NULL)
    return;
  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    annotation->data.arrow.start.x += dx;
    annotation->data.arrow.start.y += dy;
    annotation->data.arrow.end.x += dx;
    annotation->data.arrow.end.y += dy;
    if (annotation->data.arrow.is_curved) {
      annotation->data.arrow.control.x += dx;
      annotation->data.arrow.control.y += dy;
    }
    break;
  case SHAULA_ANNOTATION_TEXT:
    annotation->data.text.position.x += dx;
    annotation->data.text.position.y += dy;
    break;
  case SHAULA_ANNOTATION_MEASURE:
    annotation->data.measure.start.x += dx;
    annotation->data.measure.start.y += dy;
    annotation->data.measure.end.x += dx;
    annotation->data.measure.end.y += dy;
    break;
  case SHAULA_ANNOTATION_RECTANGLE:
    annotation->data.rectangle.rect.x += dx;
    annotation->data.rectangle.rect.y += dy;
    break;
  case SHAULA_ANNOTATION_HIGHLIGHT:
    for (int i = 0; i < annotation->data.highlight.len; i++) {
      annotation->data.highlight.points[i].x += dx;
      annotation->data.highlight.points[i].y += dy;
    }
    break;
  case SHAULA_ANNOTATION_PEN:
    for (int i = 0; i < annotation->data.pen.len; i++) {
      annotation->data.pen.points[i].x += dx;
      annotation->data.pen.points[i].y += dy;
    }
    break;
  }
  shaula_annotation_update_bounds(annotation);
}

static void set_annotation_color(cairo_t *cr, ShaulaColor color, double alpha) {
  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a * alpha);
}

static double screen_px_to_user(cairo_t *cr, double px) {
  double dx = px;
  double dy = 0.0;
  cairo_device_to_user_distance(cr, &dx, &dy);
  return fabs(dx);
}

void shaula_annotation_draw_selection_box(cairo_t *cr, ShaulaRect bounds) {
  bounds = shaula_rect_normalized(bounds);
  cairo_save(cr);
  cairo_set_source_rgba(cr, 0.10, 0.11, 0.12, 0.68);
  cairo_set_line_width(cr, 3.0);
  cairo_rectangle(cr, bounds.x - 3.0, bounds.y - 3.0, bounds.width + 6.0,
                  bounds.height + 6.0);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, 0.98);
  cairo_set_line_width(cr, 1.5);
  double dashes[] = {4.0, 3.0};
  cairo_set_dash(cr, dashes, 2, 0);
  cairo_rectangle(cr, bounds.x - 2.0, bounds.y - 2.0, bounds.width + 4.0,
                  bounds.height + 4.0);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_square_handle(cairo_t *cr, ShaulaPoint point) {
  double px = screen_px_to_user(cr, 1.0);
  double size = 7.0 * px;
  cairo_save(cr);
  cairo_rectangle(cr, point.x - size / 2.0, point.y - size / 2.0, size, size);
  cairo_set_source_rgba(cr, 0.98, 0.98, 0.98, 0.92);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.08, 0.09, 0.10, 0.82);
  cairo_set_line_width(cr, MAX(1.25 * px, 0.6));
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_round_handle(cairo_t *cr, ShaulaPoint point) {
  cairo_save(cr);
  cairo_arc(cr, point.x, point.y, 4.5, 0, 2 * G_PI);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.08, 0.09, 0.10, 0.9);
  cairo_set_line_width(cr, 1.5);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_rectangle_handles(cairo_t *cr, ShaulaRect rect) {
  rect = shaula_rect_normalized(rect);
  double x0 = rect.x;
  double y0 = rect.y;
  double x1 = rect.x + rect.width;
  double y1 = rect.y + rect.height;
  double cx = rect.x + rect.width / 2.0;
  double cy = rect.y + rect.height / 2.0;
  draw_square_handle(cr, (ShaulaPoint){x0, y0});
  draw_square_handle(cr, (ShaulaPoint){cx, y0});
  draw_square_handle(cr, (ShaulaPoint){x1, y0});
  draw_square_handle(cr, (ShaulaPoint){x1, cy});
  draw_square_handle(cr, (ShaulaPoint){x1, y1});
  draw_square_handle(cr, (ShaulaPoint){cx, y1});
  draw_square_handle(cr, (ShaulaPoint){x0, y1});
  draw_square_handle(cr, (ShaulaPoint){x0, cy});
}

static void draw_rectangle_selection(cairo_t *cr, ShaulaRect rect) {
  rect = shaula_rect_normalized(rect);
  if (shaula_rect_is_empty(rect))
    return;

  double px = screen_px_to_user(cr, 1.0);
  double pad = MAX(3.0 * px, 1.0);
  cairo_save(cr);
  cairo_set_source_rgba(cr, 0.08, 0.09, 0.10, 0.58);
  cairo_set_line_width(cr, MAX(3.0 * px, 1.0));
  rectangle_path(cr,
                 (ShaulaRect){rect.x - pad, rect.y - pad,
                              rect.width + 2.0 * pad,
                              rect.height + 2.0 * pad},
                 PREVIEW_RECTANGLE_CORNERS_SQUARE);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, 0.96);
  cairo_set_line_width(cr, MAX(1.5 * px, 0.75));
  double dashes[] = {4.0 * px, 3.0 * px};
  cairo_set_dash(cr, dashes, 2, 0);
  rectangle_path(cr,
                 (ShaulaRect){rect.x - pad, rect.y - pad,
                              rect.width + 2.0 * pad,
                              rect.height + 2.0 * pad},
                 PREVIEW_RECTANGLE_CORNERS_SQUARE);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static double path_distance_to_point(ShaulaPenPath path, ShaulaPoint point) {
  if (path.len <= 0)
    return G_MAXDOUBLE;
  if (path.len == 1)
    return shaula_point_distance(point, path.points[0]);

  double best = G_MAXDOUBLE;
  for (int i = 1; i < path.len; i++) {
    double distance = shaula_point_distance_to_segment(
        point, path.points[i - 1], path.points[i]);
    if (distance < best)
      best = distance;
  }
  return best;
}

static double rectangle_edge_distance_to_point(ShaulaRect rect,
                                               ShaulaPoint point) {
  rect = shaula_rect_normalized(rect);
  ShaulaPoint top_left = {rect.x, rect.y};
  ShaulaPoint top_right = {rect.x + rect.width, rect.y};
  ShaulaPoint bottom_right = {rect.x + rect.width, rect.y + rect.height};
  ShaulaPoint bottom_left = {rect.x, rect.y + rect.height};
  double best = shaula_point_distance_to_segment(point, top_left, top_right);
  best = MIN(best,
             shaula_point_distance_to_segment(point, top_right, bottom_right));
  best = MIN(
      best, shaula_point_distance_to_segment(point, bottom_right, bottom_left));
  best =
      MIN(best, shaula_point_distance_to_segment(point, bottom_left, top_left));
  return best;
}

static double point_to_rect_distance(ShaulaPoint point, ShaulaRect rect) {
  rect = shaula_rect_normalized(rect);
  if (shaula_rect_contains_point(rect, point))
    return 0.0;
  double dx = MAX(MAX(rect.x - point.x, 0.0), point.x - (rect.x + rect.width));
  double dy = MAX(MAX(rect.y - point.y, 0.0), point.y - (rect.y + rect.height));
  return sqrt(dx * dx + dy * dy);
}

static double orientation(ShaulaPoint a, ShaulaPoint b, ShaulaPoint c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static gboolean value_between(double value, double a, double b) {
  return value >= MIN(a, b) - 0.0001 && value <= MAX(a, b) + 0.0001;
}

static gboolean segments_intersect(ShaulaPoint a, ShaulaPoint b,
                                   ShaulaPoint c, ShaulaPoint d) {
  double o1 = orientation(a, b, c);
  double o2 = orientation(a, b, d);
  double o3 = orientation(c, d, a);
  double o4 = orientation(c, d, b);
  if (((o1 > 0.0 && o2 < 0.0) || (o1 < 0.0 && o2 > 0.0)) &&
      ((o3 > 0.0 && o4 < 0.0) || (o3 < 0.0 && o4 > 0.0)))
    return TRUE;
  if (fabs(o1) <= 0.0001 && value_between(c.x, a.x, b.x) &&
      value_between(c.y, a.y, b.y))
    return TRUE;
  if (fabs(o2) <= 0.0001 && value_between(d.x, a.x, b.x) &&
      value_between(d.y, a.y, b.y))
    return TRUE;
  if (fabs(o3) <= 0.0001 && value_between(a.x, c.x, d.x) &&
      value_between(a.y, c.y, d.y))
    return TRUE;
  if (fabs(o4) <= 0.0001 && value_between(b.x, c.x, d.x) &&
      value_between(b.y, c.y, d.y))
    return TRUE;
  return FALSE;
}

static double segment_to_rect_distance(ShaulaPoint a, ShaulaPoint b,
                                       ShaulaRect rect) {
  rect = shaula_rect_normalized(rect);
  ShaulaPoint top_left = {rect.x, rect.y};
  ShaulaPoint top_right = {rect.x + rect.width, rect.y};
  ShaulaPoint bottom_right = {rect.x + rect.width, rect.y + rect.height};
  ShaulaPoint bottom_left = {rect.x, rect.y + rect.height};
  if (shaula_rect_contains_point(rect, a) || shaula_rect_contains_point(rect, b) ||
      segments_intersect(a, b, top_left, top_right) ||
      segments_intersect(a, b, top_right, bottom_right) ||
      segments_intersect(a, b, bottom_right, bottom_left) ||
      segments_intersect(a, b, bottom_left, top_left))
    return 0.0;

  double best = MIN(point_to_rect_distance(a, rect), point_to_rect_distance(b, rect));
  best = MIN(best, shaula_point_distance_to_segment(top_left, a, b));
  best = MIN(best, shaula_point_distance_to_segment(top_right, a, b));
  best = MIN(best, shaula_point_distance_to_segment(bottom_right, a, b));
  best = MIN(best, shaula_point_distance_to_segment(bottom_left, a, b));
  return best;
}

static double segment_to_segment_distance(ShaulaPoint a, ShaulaPoint b,
                                          ShaulaPoint c, ShaulaPoint d) {
  if (segments_intersect(a, b, c, d))
    return 0.0;
  double best = shaula_point_distance_to_segment(a, c, d);
  best = MIN(best, shaula_point_distance_to_segment(b, c, d));
  best = MIN(best, shaula_point_distance_to_segment(c, a, b));
  best = MIN(best, shaula_point_distance_to_segment(d, a, b));
  return best;
}

static double path_to_rect_distance(ShaulaPenPath path, ShaulaRect rect) {
  if (path.len <= 0)
    return G_MAXDOUBLE;
  if (path.len == 1)
    return point_to_rect_distance(path.points[0], rect);

  double best = G_MAXDOUBLE;
  for (int i = 1; i < path.len; i++)
    best = MIN(best,
               segment_to_rect_distance(path.points[i - 1], path.points[i], rect));
  return best;
}

static double path_to_segment_distance(ShaulaPenPath path, ShaulaPoint start,
                                       ShaulaPoint end) {
  if (path.len <= 0)
    return G_MAXDOUBLE;
  if (path.len == 1)
    return shaula_point_distance_to_segment(path.points[0], start, end);

  double best = G_MAXDOUBLE;
  for (int i = 1; i < path.len; i++)
    best = MIN(best, segment_to_segment_distance(path.points[i - 1],
                                                 path.points[i], start, end));
  return best;
}

static double rectangle_edge_distance_to_rect(ShaulaRect rectangle,
                                              ShaulaRect selection) {
  rectangle = shaula_rect_normalized(rectangle);
  selection = shaula_rect_normalized(selection);
  ShaulaPoint top_left = {rectangle.x, rectangle.y};
  ShaulaPoint top_right = {rectangle.x + rectangle.width, rectangle.y};
  ShaulaPoint bottom_right = {rectangle.x + rectangle.width,
                              rectangle.y + rectangle.height};
  ShaulaPoint bottom_left = {rectangle.x, rectangle.y + rectangle.height};
  double best = segment_to_rect_distance(top_left, top_right, selection);
  best = MIN(best, segment_to_rect_distance(top_right, bottom_right, selection));
  best = MIN(best, segment_to_rect_distance(bottom_right, bottom_left, selection));
  best = MIN(best, segment_to_rect_distance(bottom_left, top_left, selection));
  return best;
}

static double rectangle_edge_distance_to_segment(ShaulaRect rectangle,
                                                 ShaulaPoint start,
                                                 ShaulaPoint end) {
  rectangle = shaula_rect_normalized(rectangle);
  ShaulaPoint top_left = {rectangle.x, rectangle.y};
  ShaulaPoint top_right = {rectangle.x + rectangle.width, rectangle.y};
  ShaulaPoint bottom_right = {rectangle.x + rectangle.width,
                              rectangle.y + rectangle.height};
  ShaulaPoint bottom_left = {rectangle.x, rectangle.y + rectangle.height};
  double best = segment_to_segment_distance(top_left, top_right, start, end);
  best = MIN(best, segment_to_segment_distance(top_right, bottom_right, start, end));
  best = MIN(best, segment_to_segment_distance(bottom_right, bottom_left, start, end));
  best = MIN(best, segment_to_segment_distance(bottom_left, top_left, start, end));
  return best;
}

static void draw_pen_path(cairo_t *cr, ShaulaPenPath path) {
  if (path.len <= 0)
    return;
  cairo_move_to(cr, path.points[0].x, path.points[0].y);
  for (int i = 1; i < path.len; i++)
    cairo_line_to(cr, path.points[i].x, path.points[i].y);
  cairo_stroke(cr);
}

static ShaulaPoint arrow_curve_point(ShaulaPoint start, ShaulaPoint control,
                                     ShaulaPoint end, double t) {
  double inv = 1.0 - t;
  return (ShaulaPoint){
      inv * inv * start.x + 2.0 * inv * t * control.x + t * t * end.x,
      inv * inv * start.y + 2.0 * inv * t * control.y + t * t * end.y};
}

static double arrow_curve_distance_to_point(ShaulaPoint start,
                                            ShaulaPoint control,
                                            ShaulaPoint end,
                                            ShaulaPoint point) {
  double best = G_MAXDOUBLE;
  ShaulaPoint previous = start;
  for (int i = 1; i <= 24; i++) {
    ShaulaPoint current =
        arrow_curve_point(start, control, end, (double)i / 24.0);
    best =
        MIN(best, shaula_point_distance_to_segment(point, previous, current));
    previous = current;
  }
  return best;
}

static double arrow_curve_distance_to_rect(ShaulaPoint start,
                                           ShaulaPoint control,
                                           ShaulaPoint end,
                                           ShaulaRect rect) {
  double best = G_MAXDOUBLE;
  ShaulaPoint previous = start;
  for (int i = 1; i <= 24; i++) {
    ShaulaPoint current =
        arrow_curve_point(start, control, end, (double)i / 24.0);
    best = MIN(best, segment_to_rect_distance(previous, current, rect));
    previous = current;
  }
  return best;
}

static double arrow_curve_distance_to_segment(ShaulaPoint start,
                                              ShaulaPoint control,
                                              ShaulaPoint end,
                                              ShaulaPoint eraser_start,
                                              ShaulaPoint eraser_end) {
  double best = G_MAXDOUBLE;
  ShaulaPoint previous = start;
  for (int i = 1; i <= 24; i++) {
    ShaulaPoint current =
        arrow_curve_point(start, control, end, (double)i / 24.0);
    best = MIN(best, segment_to_segment_distance(previous, current,
                                                eraser_start, eraser_end));
    previous = current;
  }
  return best;
}

/* Draws a filled triangular arrowhead that scales with stroke width. The
 * head size is proportional to the shaft width so thicker arrows get larger
 * heads, matching Shottr-like proportions.
 */
static void draw_filled_arrow_head(cairo_t *cr, ShaulaPoint dir_point,
                                   ShaulaPoint end, double stroke_width) {
  double head_len = MAX(12.0, stroke_width * 4.5);
  double head_half = MAX(5.0, stroke_width * 2.0);
  double angle = atan2(end.y - dir_point.y, end.x - dir_point.x);
  double a1 = angle + G_PI - 0.42;
  double a2 = angle + G_PI + 0.42;
  double bx = end.x + cos(angle + G_PI) * head_len;
  double by = end.y + sin(angle + G_PI) * head_len;
  (void)bx;
  (void)by;
  cairo_move_to(cr, end.x, end.y);
  cairo_line_to(cr, end.x + cos(a1) * head_len, end.y + sin(a1) * head_len);
  cairo_line_to(cr, end.x + cos(angle + G_PI) * head_len * 0.65,
                end.y + sin(angle + G_PI) * head_len * 0.65);
  cairo_line_to(cr, end.x + cos(a2) * head_len, end.y + sin(a2) * head_len);
  cairo_close_path(cr);
  cairo_fill(cr);
  (void)head_half;
}

static void apply_arrow_stroke_style(cairo_t *cr, PreviewArrowStrokeStyle style,
                                     double stroke_width) {
  switch (style) {
  case PREVIEW_ARROW_STROKE_SOLID:
    cairo_set_dash(cr, NULL, 0, 0);
    break;
  case PREVIEW_ARROW_STROKE_DASHED: {
    double dashes[] = {stroke_width * 4.0, stroke_width * 2.4};
    cairo_set_dash(cr, dashes, 2, 0);
    break;
  }
  case PREVIEW_ARROW_STROKE_DOTTED: {
    double dots[] = {0.1, stroke_width * 2.2};
    cairo_set_dash(cr, dots, 2, 0);
    break;
  }
  }
}

static void rectangle_path(cairo_t *cr, ShaulaRect rect,
                           PreviewRectangleCorners corners) {
  rect = shaula_rect_normalized(rect);
  if (corners == PREVIEW_RECTANGLE_CORNERS_SQUARE) {
    cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
    return;
  }

  double radius = MIN(12.0, MIN(rect.width, rect.height) * 0.16);
  if (radius <= 0.5) {
    cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
    return;
  }

  double x = rect.x;
  double y = rect.y;
  double w = rect.width;
  double h = rect.height;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius, radius, -0.5 * G_PI, 0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, 0.5 * G_PI);
  cairo_arc(cr, x + radius, y + h - radius, radius, 0.5 * G_PI, G_PI);
  cairo_arc(cr, x + radius, y + radius, radius, G_PI, 1.5 * G_PI);
  cairo_close_path(cr);
}

/* Computes the point on the shaft where it should stop before the arrowhead.
 * This avoids overlap artifacts between the rounded cap and the filled head.
 */
static ShaulaPoint arrow_shaft_end(ShaulaPoint dir_point, ShaulaPoint end,
                                   double stroke_width) {
  double head_len = MAX(12.0, stroke_width * 4.5);
  double dist = shaula_point_distance(dir_point, end);
  if (dist < 0.001)
    return end;
  double shorten = head_len * 0.55;
  if (shorten >= dist)
    return dir_point;
  double t = (dist - shorten) / dist;
  return (ShaulaPoint){dir_point.x + (end.x - dir_point.x) * t,
                       dir_point.y + (end.y - dir_point.y) * t};
}

static gboolean point_in_polygon(ShaulaPoint point, const ShaulaPoint *polygon,
                                 int len) {
  gboolean inside = FALSE;
  for (int i = 0, j = len - 1; i < len; j = i++) {
    gboolean crosses =
        ((polygon[i].y > point.y) != (polygon[j].y > point.y)) &&
        (point.x < (polygon[j].x - polygon[i].x) *
                           (point.y - polygon[i].y) /
                           (polygon[j].y - polygon[i].y) +
                       polygon[i].x);
    if (crosses)
      inside = !inside;
  }
  return inside;
}

static gboolean polygon_intersects_rect(const ShaulaPoint *polygon, int len,
                                        ShaulaRect rect) {
  rect = shaula_rect_normalized(rect);
  ShaulaPoint top_left = {rect.x, rect.y};
  ShaulaPoint top_right = {rect.x + rect.width, rect.y};
  ShaulaPoint bottom_right = {rect.x + rect.width, rect.y + rect.height};
  ShaulaPoint bottom_left = {rect.x, rect.y + rect.height};
  ShaulaPoint rect_points[] = {top_left, top_right, bottom_right, bottom_left};

  for (int i = 0; i < len; i++) {
    if (shaula_rect_contains_point(rect, polygon[i]))
      return TRUE;
    ShaulaPoint next = polygon[(i + 1) % len];
    for (int edge = 0; edge < 4; edge++) {
      if (segments_intersect(polygon[i], next, rect_points[edge],
                             rect_points[(edge + 1) % 4]))
        return TRUE;
    }
  }

  for (int i = 0; i < 4; i++) {
    if (point_in_polygon(rect_points[i], polygon, len))
      return TRUE;
  }
  return FALSE;
}

static double polygon_to_segment_distance(const ShaulaPoint *polygon, int len,
                                          ShaulaPoint start, ShaulaPoint end) {
  if (point_in_polygon(start, polygon, len) ||
      point_in_polygon(end, polygon, len))
    return 0.0;

  double best = G_MAXDOUBLE;
  for (int i = 0; i < len; i++) {
    ShaulaPoint next = polygon[(i + 1) % len];
    best = MIN(best, segment_to_segment_distance(polygon[i], next, start, end));
  }
  return best;
}

static gboolean arrow_head_intersects_rect(const ShaulaAnnotation *annotation,
                                           ShaulaRect rect) {
  if (!annotation->data.arrow.has_head)
    return FALSE;

  ShaulaPoint end = annotation->data.arrow.end;
  ShaulaPoint dir_point = annotation->data.arrow.is_curved
                              ? annotation->data.arrow.control
                              : annotation->data.arrow.start;
  double head_len = MAX(12.0, annotation->stroke_width * 4.5);
  double angle = atan2(end.y - dir_point.y, end.x - dir_point.x);
  ShaulaPoint polygon[] = {
      end,
      {end.x + cos(angle + G_PI - 0.42) * head_len,
       end.y + sin(angle + G_PI - 0.42) * head_len},
      {end.x + cos(angle + G_PI) * head_len * 0.65,
       end.y + sin(angle + G_PI) * head_len * 0.65},
      {end.x + cos(angle + G_PI + 0.42) * head_len,
       end.y + sin(angle + G_PI + 0.42) * head_len},
  };
  return polygon_intersects_rect(polygon, 4, rect);
}

static gboolean arrow_head_intersects_segment(
    const ShaulaAnnotation *annotation, ShaulaPoint start, ShaulaPoint end,
    double radius) {
  if (!annotation->data.arrow.has_head)
    return FALSE;

  ShaulaPoint arrow_end = annotation->data.arrow.end;
  ShaulaPoint dir_point = annotation->data.arrow.is_curved
                              ? annotation->data.arrow.control
                              : annotation->data.arrow.start;
  double head_len = MAX(12.0, annotation->stroke_width * 4.5);
  double angle = atan2(arrow_end.y - dir_point.y, arrow_end.x - dir_point.x);
  ShaulaPoint polygon[] = {
      arrow_end,
      {arrow_end.x + cos(angle + G_PI - 0.42) * head_len,
       arrow_end.y + sin(angle + G_PI - 0.42) * head_len},
      {arrow_end.x + cos(angle + G_PI) * head_len * 0.65,
       arrow_end.y + sin(angle + G_PI) * head_len * 0.65},
      {arrow_end.x + cos(angle + G_PI + 0.42) * head_len,
       arrow_end.y + sin(angle + G_PI + 0.42) * head_len},
  };
  return polygon_to_segment_distance(polygon, 4, start, end) <= radius;
}

static void draw_arrow_shape(cairo_t *cr, const ShaulaAnnotation *annotation) {
  ShaulaPoint start = annotation->data.arrow.start;
  ShaulaPoint end = annotation->data.arrow.end;
  ShaulaPoint control =
      annotation->data.arrow.is_curved ? annotation->data.arrow.control : start;
  ShaulaPoint dir_point = annotation->data.arrow.is_curved ? control : start;
  apply_arrow_stroke_style(cr, annotation->data.arrow.stroke_style,
                           annotation->stroke_width);

  if (annotation->data.arrow.is_curved) {
    double shorten = annotation->data.arrow.has_head
                         ? MAX(12.0, annotation->stroke_width * 4.5) * 0.55
                         : 0.0;
    double dist12 = shaula_point_distance(control, end);
    double t_end = 1.0;
    if (dist12 > 0.001) {
      double delta_t = shorten / (2.0 * dist12);
      if (delta_t < 1.0)
        t_end = 1.0 - delta_t;
      else
        t_end = 0.0;
    }
    ShaulaPoint q0 = start;
    ShaulaPoint q1 = {(1 - t_end) * start.x + t_end * control.x,
                      (1 - t_end) * start.y + t_end * control.y};
    ShaulaPoint q2 = {
        (1 - t_end) * q1.x + t_end * ((1 - t_end) * control.x + t_end * end.x),
        (1 - t_end) * q1.y + t_end * ((1 - t_end) * control.y + t_end * end.y)};

    cairo_move_to(cr, q0.x, q0.y);
    cairo_curve_to(cr, q0.x + (q1.x - q0.x) * 2.0 / 3.0,
                   q0.y + (q1.y - q0.y) * 2.0 / 3.0,
                   q2.x + (q1.x - q2.x) * 2.0 / 3.0,
                   q2.y + (q1.y - q2.y) * 2.0 / 3.0, q2.x, q2.y);
    cairo_stroke(cr);
    dir_point = q1;
  } else {
    ShaulaPoint shaft_end_pt = annotation->data.arrow.has_head
                                   ? arrow_shaft_end(dir_point, end,
                                                     annotation->stroke_width)
                                   : end;
    cairo_move_to(cr, start.x, start.y);
    cairo_line_to(cr, shaft_end_pt.x, shaft_end_pt.y);
    cairo_stroke(cr);
  }

  cairo_set_dash(cr, NULL, 0, 0);
  if (annotation->data.arrow.has_head)
    draw_filled_arrow_head(cr, dir_point, end, annotation->stroke_width);
}

static void draw_measure_label(cairo_t *cr,
                               const ShaulaAnnotation *annotation) {
  char label[64];
  if (annotation->data.measure.rect_width > 0 &&
      annotation->data.measure.rect_height > 0) {
    snprintf(label, sizeof(label), "%d \xc3\x97 %d px",
             annotation->data.measure.rect_width,
             annotation->data.measure.rect_height);
  } else {
    snprintf(label, sizeof(label), "%.0f px",
             annotation->data.measure.distance_px);
  }
  ShaulaPoint start = annotation->data.measure.start;
  ShaulaPoint end = annotation->data.measure.end;
  double x = (start.x + end.x) / 2.0;
  double y = (start.y + end.y) / 2.0 - 8.0;

  cairo_save(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 13.0);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, label, &extents);
  cairo_move_to(cr, x - extents.width / 2.0, y - 4.0);
  cairo_show_text(cr, label);
  cairo_restore(cr);
}

void shaula_annotation_draw_preview(cairo_t *cr,
                                    const ShaulaAnnotation *annotation,
                                    ShaulaAnnotationPreviewFlags flags) {
  if (annotation == NULL)
    return;

  gboolean show_selection =
      (flags & SHAULA_ANNOTATION_PREVIEW_SELECTION) != 0;
  gboolean show_edit_handles =
      show_selection && (flags & SHAULA_ANNOTATION_PREVIEW_HANDLES) != 0;

  cairo_save(cr);
  cairo_set_line_width(cr, annotation->stroke_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  set_annotation_color(cr, annotation->color, 1.0);

  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    draw_arrow_shape(cr, annotation);
    break;
  case SHAULA_ANNOTATION_TEXT: {
    PangoFontDescription *desc = NULL;
    PangoLayout *layout =
        create_text_layout(cr, "", annotation->data.text.font_size,
                           annotation->data.text.font_mode, &desc);
    char **lines = g_strsplit(annotation->data.text.text, "\n", -1);
    double y = annotation->data.text.position.y;
    for (int i = 0; lines[i] != NULL; i++) {
      ShaulaTextLineMetrics metrics = text_line_metrics(
          layout, lines[i], -1, annotation->data.text.position, y,
          annotation->data.text.font_size, annotation->data.text.align);
      cairo_move_to(cr, metrics.draw_x, metrics.draw_y);
      pango_cairo_show_layout(cr, layout);
      y += metrics.advance;
    }
    g_strfreev(lines);
    pango_font_description_free(desc);
    g_object_unref(layout);
    break;
  }
  case SHAULA_ANNOTATION_MEASURE:
    if (annotation->data.measure.rect_width > 0 &&
        annotation->data.measure.rect_height > 0) {
      double x0 = annotation->data.measure.start.x;
      double y0 = annotation->data.measure.start.y;
      double x1 = annotation->data.measure.end.x;
      double y1 = annotation->data.measure.end.y;
      cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
      cairo_stroke(cr);
      double foot = 6.0;
      cairo_set_line_width(cr, annotation->stroke_width);
      cairo_move_to(cr, x0, y0 - foot);
      cairo_line_to(cr, x0, y0 + foot);
      cairo_stroke(cr);
      cairo_move_to(cr, x1, y0 - foot);
      cairo_line_to(cr, x1, y0 + foot);
      cairo_stroke(cr);
      cairo_move_to(cr, x0 - foot, y0);
      cairo_line_to(cr, x0 + foot, y0);
      cairo_stroke(cr);
      cairo_move_to(cr, x0 - foot, y1);
      cairo_line_to(cr, x0 + foot, y1);
      cairo_stroke(cr);
    } else {
      cairo_move_to(cr, annotation->data.measure.start.x,
                    annotation->data.measure.start.y);
      cairo_line_to(cr, annotation->data.measure.end.x,
                    annotation->data.measure.end.y);
      cairo_stroke(cr);
    }
    draw_measure_label(cr, annotation);
    break;
  case SHAULA_ANNOTATION_RECTANGLE:
    rectangle_path(cr, annotation->data.rectangle.rect,
                   annotation->data.rectangle.corners);
    if (annotation->data.rectangle.filled) {
      set_annotation_color(cr, annotation->color, 0.22);
      cairo_fill_preserve(cr);
      set_annotation_color(cr, annotation->color, 1.0);
    }
    apply_arrow_stroke_style(cr, annotation->data.rectangle.stroke_style,
                             annotation->stroke_width);
    cairo_stroke(cr);
    break;
  case SHAULA_ANNOTATION_HIGHLIGHT:
    if (annotation->data.highlight.len > 0) {
      set_annotation_color(cr, annotation->color, 1.0);
      cairo_set_line_width(cr, annotation->stroke_width);
      draw_pen_path(cr, annotation->data.highlight);
    }
    break;
  case SHAULA_ANNOTATION_PEN:
    if (annotation->data.pen.len > 0) {
      draw_pen_path(cr, annotation->data.pen);
    }
    break;
  }

  if (annotation->selected && show_selection) {
    if (annotation->type == SHAULA_ANNOTATION_PEN) {
      shaula_annotation_draw_selection_box(cr, annotation->bounds);
    } else if (annotation->type == SHAULA_ANNOTATION_HIGHLIGHT) {
      shaula_annotation_draw_selection_box(cr, annotation->bounds);
    } else if (annotation->type == SHAULA_ANNOTATION_RECTANGLE) {
      draw_rectangle_selection(cr, annotation->data.rectangle.rect);
      set_annotation_color(cr, annotation->color, 1.0);
      cairo_set_line_width(cr, annotation->stroke_width);
      apply_arrow_stroke_style(cr, annotation->data.rectangle.stroke_style,
                               annotation->stroke_width);
      rectangle_path(cr, annotation->data.rectangle.rect,
                     annotation->data.rectangle.corners);
      cairo_stroke(cr);
      if (show_edit_handles)
        draw_rectangle_handles(cr, annotation->data.rectangle.rect);
    } else if (annotation->type != SHAULA_ANNOTATION_ARROW) {
      shaula_annotation_draw_selection_box(cr, annotation->bounds);
    }
    if (annotation->type == SHAULA_ANNOTATION_ARROW) {
      shaula_annotation_draw_selection_box(cr, annotation->bounds);
      if (show_edit_handles) {
        ShaulaPoint p0 = annotation->data.arrow.start;
        ShaulaPoint p2 = annotation->data.arrow.end;
        ShaulaPoint p1 =
            annotation->data.arrow.is_curved
                ? annotation->data.arrow.control
                : (ShaulaPoint){(p0.x + p2.x) / 2.0,
                                (p0.y + p2.y) / 2.0};
        ShaulaPoint mid = {0.25 * p0.x + 0.5 * p1.x + 0.25 * p2.x,
                           0.25 * p0.y + 0.5 * p1.y + 0.25 * p2.y};
        cairo_save(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.88);
        cairo_arc(cr, mid.x, mid.y, 4.0, 0, 2 * G_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.72);
        cairo_set_line_width(cr, 1.25);
        cairo_stroke(cr);
        cairo_restore(cr);
        draw_round_handle(cr, p0);
        draw_round_handle(cr, p2);
      }
    }
  }

  cairo_restore(cr);
}

void shaula_annotation_draw(cairo_t *cr,
                            const ShaulaAnnotation *annotation) {
  shaula_annotation_draw_preview(
      cr, annotation,
      SHAULA_ANNOTATION_PREVIEW_SELECTION | SHAULA_ANNOTATION_PREVIEW_HANDLES);
}

gboolean shaula_annotation_text_cursor_rect(const ShaulaAnnotation *annotation,
                                            int cursor_byte_index,
                                            ShaulaRect *rect) {
  if (annotation == NULL || annotation->type != SHAULA_ANNOTATION_TEXT ||
      rect == NULL)
    return FALSE;

  const char *text = annotation->data.text.text != NULL
                         ? annotation->data.text.text
                         : "";
  int text_len = (int)strlen(text);
  cursor_byte_index = CLAMP(cursor_byte_index, 0, text_len);

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t *cr = cairo_create(surface);
  PangoFontDescription *desc = NULL;
  PangoLayout *layout =
      create_text_layout(cr, "", annotation->data.text.font_size,
                         annotation->data.text.font_mode, &desc);

  const char *line_start = text;
  int line_start_byte = 0;
  double y = annotation->data.text.position.y;
  gboolean found = FALSE;
  while (TRUE) {
    const char *line_end = strchr(line_start, '\n');
    if (line_end == NULL)
      line_end = text + text_len;
    int line_len = (int)(line_end - line_start);
    int line_end_byte = line_start_byte + line_len;

    ShaulaTextLineMetrics metrics =
        text_line_metrics(layout, line_start, line_len,
                          annotation->data.text.position, y,
                          annotation->data.text.font_size,
                          annotation->data.text.align);
    if (cursor_byte_index <= line_end_byte) {
      int line_cursor_byte =
          CLAMP(cursor_byte_index - line_start_byte, 0, line_len);
      PangoRectangle strong = {0};
      pango_layout_get_cursor_pos(layout, line_cursor_byte, &strong, NULL);
      double cursor_x =
          metrics.draw_x + (double)strong.x / (double)PANGO_SCALE;
      double cursor_y =
          metrics.draw_y + (double)strong.y / (double)PANGO_SCALE;
      double height = (double)strong.height / (double)PANGO_SCALE;
      if (height <= 0.0)
        height = metrics.advance;
      *rect = (ShaulaRect){cursor_x, cursor_y, 0.0, height};
      found = TRUE;
      break;
    }

    y += metrics.advance;
    if (*line_end != '\n')
      break;
    line_start = line_end + 1;
    line_start_byte = line_end_byte + 1;
  }

  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  return found;
}

static int hit_kind_priority(ShaulaAnnotationHitKind kind) {
  switch (kind) {
  case SHAULA_ANNOTATION_HIT_HANDLE:
    return 4;
  case SHAULA_ANNOTATION_HIT_STROKE:
    return 3;
  case SHAULA_ANNOTATION_HIT_FILL:
    return 2;
  case SHAULA_ANNOTATION_HIT_TEXT_BOUNDS:
    return 1;
  case SHAULA_ANNOTATION_HIT_NONE:
    return 0;
  }
  return 0;
}

/* Selection contract: bounding boxes are only broad-phase rejection for
 * stroke-like annotations. Transparent rectangle interiors intentionally
 * return NONE so visually empty space can pass through to annotations behind.
 */
static ShaulaAnnotationHitKind annotation_hit_kind(ShaulaAnnotation *annotation,
                                                   ShaulaPoint point,
                                                   double tolerance) {
  if (annotation == NULL)
    return SHAULA_ANNOTATION_HIT_NONE;
  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    tolerance = MAX(tolerance, annotation->stroke_width / 2.0 + 3.0);
    if (!shaula_rect_contains_point(
            shaula_rect_expanded(annotation->bounds, tolerance), point))
      return SHAULA_ANNOTATION_HIT_NONE;
    if (annotation->data.arrow.is_curved) {
      if (arrow_curve_distance_to_point(
              annotation->data.arrow.start, annotation->data.arrow.control,
              annotation->data.arrow.end, point) <= tolerance)
        return SHAULA_ANNOTATION_HIT_STROKE;
    } else if (shaula_point_distance_to_segment(
                   point, annotation->data.arrow.start,
                   annotation->data.arrow.end) <= tolerance) {
      return SHAULA_ANNOTATION_HIT_STROKE;
    }
    if (shaula_point_distance(point, annotation->data.arrow.end) <=
        MAX(tolerance, annotation->stroke_width * 2.5))
      return SHAULA_ANNOTATION_HIT_STROKE;
    break;
  case SHAULA_ANNOTATION_MEASURE:
    if (shaula_point_distance_to_segment(point, annotation->data.measure.start,
                                         annotation->data.measure.end) <=
        tolerance)
      return SHAULA_ANNOTATION_HIT_STROKE;
    break;
  case SHAULA_ANNOTATION_RECTANGLE: {
    double stroke_tolerance =
        MAX(tolerance, annotation->stroke_width / 2.0 + 3.0);
    if (!shaula_rect_contains_point(
            shaula_rect_expanded(annotation->bounds, stroke_tolerance), point))
      return SHAULA_ANNOTATION_HIT_NONE;
    if (rectangle_edge_distance_to_point(annotation->data.rectangle.rect,
                                         point) <= stroke_tolerance)
      return SHAULA_ANNOTATION_HIT_STROKE;
    if (annotation->data.rectangle.filled &&
        shaula_rect_contains_point(
            shaula_rect_normalized(annotation->data.rectangle.rect), point))
      return SHAULA_ANNOTATION_HIT_FILL;
    break;
  }
  case SHAULA_ANNOTATION_TEXT:
    /* Text selection should match the visible dashed selection bounds. Extra
     * hit slop makes adjacent labels steal clicks from each other.
     */
    if (shaula_rect_contains_point(annotation->bounds, point))
      return SHAULA_ANNOTATION_HIT_TEXT_BOUNDS;
    break;
  case SHAULA_ANNOTATION_HIGHLIGHT:
    if (path_distance_to_point(annotation->data.highlight, point) <=
        MAX(tolerance, annotation->stroke_width / 2.0 + 3.0))
      return SHAULA_ANNOTATION_HIT_STROKE;
    break;
  case SHAULA_ANNOTATION_PEN:
    if (path_distance_to_point(annotation->data.pen, point) <=
        MAX(tolerance, annotation->stroke_width / 2.0 + 3.0))
      return SHAULA_ANNOTATION_HIT_STROKE;
    break;
  }
  return SHAULA_ANNOTATION_HIT_NONE;
}

ShaulaAnnotationHit shaula_annotations_hit_test_ranked(GPtrArray *annotations,
                                                       ShaulaPoint point,
                                                       double tolerance) {
  ShaulaAnnotationHit best = {NULL, SHAULA_ANNOTATION_HIT_NONE};
  if (annotations == NULL)
    return best;
  for (gint i = (gint)annotations->len - 1; i >= 0; i--) {
    ShaulaAnnotation *annotation = g_ptr_array_index(annotations, (guint)i);
    ShaulaAnnotationHitKind kind =
        annotation_hit_kind(annotation, point, tolerance);
    if (hit_kind_priority(kind) > hit_kind_priority(best.kind))
      best = (ShaulaAnnotationHit){annotation, kind};
  }
  return best;
}

ShaulaAnnotation *shaula_annotations_hit_test(GPtrArray *annotations,
                                              ShaulaPoint point,
                                              double tolerance) {
  return shaula_annotations_hit_test_ranked(annotations, point, tolerance)
      .annotation;
}

gboolean shaula_annotation_intersects_selection_rect(
    const ShaulaAnnotation *annotation, ShaulaRect rect) {
  if (annotation == NULL)
    return FALSE;
  rect = shaula_rect_normalized(rect);
  if (shaula_rect_is_empty(rect))
    return FALSE;

  double stroke_tolerance = MAX(1.0, annotation->stroke_width / 2.0);

  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW: {
    double shaft_distance = annotation->data.arrow.is_curved
                                ? arrow_curve_distance_to_rect(
                                      annotation->data.arrow.start,
                                      annotation->data.arrow.control,
                                      annotation->data.arrow.end, rect)
                                : segment_to_rect_distance(
                                      annotation->data.arrow.start,
                                      annotation->data.arrow.has_head
                                          ? arrow_shaft_end(
                                                annotation->data.arrow.start,
                                                annotation->data.arrow.end,
                                                annotation->stroke_width)
                                          : annotation->data.arrow.end,
                                      rect);
    return shaft_distance <= stroke_tolerance ||
           arrow_head_intersects_rect(annotation, rect);
  }
  case SHAULA_ANNOTATION_MEASURE:
    return segment_to_rect_distance(annotation->data.measure.start,
                                    annotation->data.measure.end,
                                    rect) <= stroke_tolerance;
  case SHAULA_ANNOTATION_RECTANGLE: {
    ShaulaRect rectangle = shaula_rect_normalized(annotation->data.rectangle.rect);
    if (annotation->data.rectangle.filled &&
        shaula_rect_intersects(rectangle, rect))
      return TRUE;
    return rectangle_edge_distance_to_rect(rectangle, rect) <= stroke_tolerance;
  }
  case SHAULA_ANNOTATION_HIGHLIGHT:
    return path_to_rect_distance(annotation->data.highlight, rect) <=
           stroke_tolerance;
  case SHAULA_ANNOTATION_PEN:
    return path_to_rect_distance(annotation->data.pen, rect) <= stroke_tolerance;
  case SHAULA_ANNOTATION_TEXT:
    return shaula_rect_intersects(annotation->bounds, rect);
  }

  return FALSE;
}

gboolean shaula_annotation_intersects_eraser_segment(
    const ShaulaAnnotation *annotation, ShaulaPoint start, ShaulaPoint end,
    double radius) {
  if (annotation == NULL || radius < 0.0)
    return FALSE;

  double stroke_tolerance = MAX(radius, annotation->stroke_width / 2.0 + radius);

  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW: {
    ShaulaPoint shaft_end =
        annotation->data.arrow.has_head
            ? arrow_shaft_end(annotation->data.arrow.start,
                              annotation->data.arrow.end,
                              annotation->stroke_width)
            : annotation->data.arrow.end;
    double shaft_distance =
        annotation->data.arrow.is_curved
            ? arrow_curve_distance_to_segment(annotation->data.arrow.start,
                                              annotation->data.arrow.control,
                                              shaft_end, start, end)
            : segment_to_segment_distance(annotation->data.arrow.start,
                                          shaft_end, start, end);
    return shaft_distance <= stroke_tolerance ||
           arrow_head_intersects_segment(annotation, start, end, radius);
  }
  case SHAULA_ANNOTATION_MEASURE:
    return segment_to_segment_distance(annotation->data.measure.start,
                                       annotation->data.measure.end, start,
                                       end) <= stroke_tolerance;
  case SHAULA_ANNOTATION_RECTANGLE: {
    ShaulaRect rect = shaula_rect_normalized(annotation->data.rectangle.rect);
    if (annotation->data.rectangle.filled &&
        segment_to_rect_distance(start, end, rect) <= radius)
      return TRUE;
    return rectangle_edge_distance_to_segment(rect, start, end) <=
           stroke_tolerance;
  }
  case SHAULA_ANNOTATION_TEXT:
    return segment_to_rect_distance(start, end, annotation->bounds) <= radius;
  case SHAULA_ANNOTATION_HIGHLIGHT:
    return path_to_segment_distance(annotation->data.highlight, start, end) <=
           MAX(radius, annotation->stroke_width / 2.0 + radius);
  case SHAULA_ANNOTATION_PEN:
    return path_to_segment_distance(annotation->data.pen, start, end) <=
           MAX(radius, annotation->stroke_width / 2.0 + radius);
  }
  return FALSE;
}
