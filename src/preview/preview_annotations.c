#include "preview_annotations.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
  ShaulaRect rect = shaula_rect_from_points(start, end);
  return shaula_rect_expanded(rect, padding);
}

static ShaulaRect text_bounds(ShaulaPoint position, const char *text,
                              double font_size) {
  double width = MAX(24.0, (double)strlen(text != NULL ? text : "") *
                              font_size * 0.58);
  return (ShaulaRect){position.x, position.y - font_size, width,
                      font_size * 1.35};
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
  shaula_annotation_update_bounds(annotation);
  return annotation;
}

ShaulaAnnotation *shaula_annotation_new_text(ShaulaPoint position,
                                             const char *text,
                                             ShaulaColor color,
                                             double font_size) {
  ShaulaAnnotation *annotation =
      annotation_alloc(SHAULA_ANNOTATION_TEXT, color, 1.0);
  annotation->data.text.position = position;
  annotation->data.text.text = g_strdup(text != NULL ? text : "");
  annotation->data.text.font_size = font_size;
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
  shaula_annotation_update_bounds(annotation);
  return annotation;
}

ShaulaAnnotation *shaula_annotation_new_rectangle(ShaulaRect rect,
                                                  ShaulaColor color,
                                                  double stroke_width) {
  ShaulaAnnotation *annotation =
      annotation_alloc(SHAULA_ANNOTATION_RECTANGLE, color, stroke_width);
  annotation->data.rectangle.rect = shaula_rect_normalized(rect);
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
  ShaulaAnnotation *clone =
      annotation_alloc(annotation->type, annotation->color,
                       annotation->stroke_width);
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
    g_ptr_array_add(clone, shaula_annotation_clone(g_ptr_array_index(annotations, i)));
  return clone;
}

void shaula_annotation_update_bounds(ShaulaAnnotation *annotation) {
  if (annotation == NULL)
    return;
  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    if (annotation->data.arrow.is_curved) {
      ShaulaRect bounds = shaula_rect_union(
          line_bounds(annotation->data.arrow.start, annotation->data.arrow.end, 0.0),
          shaula_rect_from_points(annotation->data.arrow.start, annotation->data.arrow.control));
      annotation->bounds = shaula_rect_expanded(bounds, annotation->stroke_width + 8.0);
    } else {
      annotation->bounds =
          line_bounds(annotation->data.arrow.start, annotation->data.arrow.end,
                      annotation->stroke_width + 8.0);
    }
    break;
  case SHAULA_ANNOTATION_TEXT:
    annotation->bounds =
        text_bounds(annotation->data.text.position, annotation->data.text.text,
                    annotation->data.text.font_size);
    break;
  case SHAULA_ANNOTATION_MEASURE:
    annotation->data.measure.distance_px = shaula_point_distance(
        annotation->data.measure.start, annotation->data.measure.end);
    annotation->bounds =
        line_bounds(annotation->data.measure.start, annotation->data.measure.end,
                    annotation->stroke_width + 18.0);
    break;
  case SHAULA_ANNOTATION_RECTANGLE:
    annotation->bounds =
        shaula_rect_expanded(annotation->data.rectangle.rect,
                             annotation->stroke_width + 4.0);
    break;
  case SHAULA_ANNOTATION_HIGHLIGHT:
  case SHAULA_ANNOTATION_PEN:
    {
    ShaulaPenPath path = annotation->type == SHAULA_ANNOTATION_HIGHLIGHT
                             ? annotation->data.highlight
                             : annotation->data.pen;
    if (path.len <= 0) {
      annotation->bounds = (ShaulaRect){0, 0, 0, 0};
      break;
    }
    ShaulaRect bounds = {path.points[0].x, path.points[0].y, 0, 0};
    for (int i = 1; i < path.len; i++) {
      ShaulaRect point_rect = {path.points[i].x, path.points[i].y, 0, 0};
      bounds = shaula_rect_union(bounds, point_rect);
    }
    annotation->bounds =
        shaula_rect_expanded(bounds, annotation->stroke_width + 6.0);
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

static void draw_selection(cairo_t *cr, ShaulaRect bounds) {
  bounds = shaula_rect_normalized(bounds);
  cairo_save(cr);
  cairo_set_source_rgba(cr, 0.10, 0.11, 0.12, 0.68);
  cairo_set_line_width(cr, 3.0);
  cairo_rectangle(cr, bounds.x - 3.0, bounds.y - 3.0,
                  bounds.width + 6.0, bounds.height + 6.0);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, 0.98);
  cairo_set_line_width(cr, 1.5);
  double dashes[] = {4.0, 3.0};
  cairo_set_dash(cr, dashes, 2, 0);
  cairo_rectangle(cr, bounds.x - 2.0, bounds.y - 2.0,
                  bounds.width + 4.0, bounds.height + 4.0);
  cairo_stroke(cr);
  cairo_restore(cr);
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
  cairo_line_to(cr, end.x + cos(a1) * head_len,
                end.y + sin(a1) * head_len);
  cairo_line_to(cr, end.x + cos(angle + G_PI) * head_len * 0.65,
                end.y + sin(angle + G_PI) * head_len * 0.65);
  cairo_line_to(cr, end.x + cos(a2) * head_len,
                end.y + sin(a2) * head_len);
  cairo_close_path(cr);
  cairo_fill(cr);
  (void)head_half;
}

static void apply_arrow_stroke_style(cairo_t *cr,
                                     PreviewArrowStrokeStyle style,
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

static void draw_measure_label(cairo_t *cr, const ShaulaAnnotation *annotation) {
  char label[64];
  snprintf(label, sizeof(label), "%.0f px", annotation->data.measure.distance_px);
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
  cairo_set_source_rgba(cr, 0.02, 0.02, 0.025, 0.82);
  cairo_rectangle(cr, x - extents.width / 2.0 - 5.0,
                  y - extents.height - 5.0, extents.width + 10.0,
                  extents.height + 8.0);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_move_to(cr, x - extents.width / 2.0, y - 4.0);
  cairo_show_text(cr, label);
  cairo_restore(cr);
}

void shaula_annotation_draw(cairo_t *cr, const ShaulaAnnotation *annotation) {
  if (annotation == NULL)
    return;

  cairo_save(cr);
  cairo_set_line_width(cr, annotation->stroke_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  set_annotation_color(cr, annotation->color, 1.0);

  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW: {
    ShaulaPoint start = annotation->data.arrow.start;
    ShaulaPoint end = annotation->data.arrow.end;
    ShaulaPoint control =
        annotation->data.arrow.is_curved ? annotation->data.arrow.control
                                          : start;
    ShaulaPoint dir_point = annotation->data.arrow.is_curved ? control : start;
    apply_arrow_stroke_style(cr, annotation->data.arrow.stroke_style,
                             annotation->stroke_width);

    if (annotation->data.arrow.is_curved) {
      double head_len = MAX(12.0, annotation->stroke_width * 4.5);
      double shorten = head_len * 0.55;
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
          (1 - t_end) * q1.x +
              t_end * ((1 - t_end) * control.x + t_end * end.x),
          (1 - t_end) * q1.y +
              t_end * ((1 - t_end) * control.y + t_end * end.y)};

      cairo_move_to(cr, q0.x, q0.y);
      cairo_curve_to(cr, q0.x + (q1.x - q0.x) * 2.0 / 3.0,
                     q0.y + (q1.y - q0.y) * 2.0 / 3.0,
                     q2.x + (q1.x - q2.x) * 2.0 / 3.0,
                     q2.y + (q1.y - q2.y) * 2.0 / 3.0, q2.x, q2.y);
      cairo_stroke(cr);
      dir_point = q1;
    } else {
      ShaulaPoint shaft_end_pt = arrow_shaft_end(
          dir_point, end, annotation->stroke_width);
      cairo_move_to(cr, start.x, start.y);
      cairo_line_to(cr, shaft_end_pt.x, shaft_end_pt.y);
      cairo_stroke(cr);
    }

    cairo_set_dash(cr, NULL, 0, 0);
    draw_filled_arrow_head(cr, dir_point, end, annotation->stroke_width);
    break;
  }
  case SHAULA_ANNOTATION_TEXT:
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, annotation->data.text.font_size);
    cairo_move_to(cr, annotation->data.text.position.x,
                  annotation->data.text.position.y);
    cairo_show_text(cr, annotation->data.text.text);
    break;
  case SHAULA_ANNOTATION_MEASURE:
    cairo_move_to(cr, annotation->data.measure.start.x,
                  annotation->data.measure.start.y);
    cairo_line_to(cr, annotation->data.measure.end.x,
                  annotation->data.measure.end.y);
    cairo_stroke(cr);
    draw_measure_label(cr, annotation);
    break;
  case SHAULA_ANNOTATION_RECTANGLE:
    cairo_rectangle(cr, annotation->data.rectangle.rect.x,
                    annotation->data.rectangle.rect.y,
                    annotation->data.rectangle.rect.width,
                    annotation->data.rectangle.rect.height);
    cairo_stroke(cr);
    break;
  case SHAULA_ANNOTATION_HIGHLIGHT:
    if (annotation->data.highlight.len > 0) {
      set_annotation_color(cr, annotation->color, 1.0);
      cairo_set_line_width(cr, annotation->stroke_width);
      cairo_move_to(cr, annotation->data.highlight.points[0].x,
                    annotation->data.highlight.points[0].y);
      for (int i = 1; i < annotation->data.highlight.len; i++)
        cairo_line_to(cr, annotation->data.highlight.points[i].x,
                      annotation->data.highlight.points[i].y);
      cairo_stroke(cr);
    }
    break;
  case SHAULA_ANNOTATION_PEN:
    if (annotation->data.pen.len > 0) {
      cairo_move_to(cr, annotation->data.pen.points[0].x,
                    annotation->data.pen.points[0].y);
      for (int i = 1; i < annotation->data.pen.len; i++)
        cairo_line_to(cr, annotation->data.pen.points[i].x,
                      annotation->data.pen.points[i].y);
      cairo_stroke(cr);
    }
    break;
  }

  if (annotation->selected) {
    draw_selection(cr, annotation->bounds);
    if (annotation->type == SHAULA_ANNOTATION_ARROW) {
      ShaulaPoint p0 = annotation->data.arrow.start;
      ShaulaPoint p2 = annotation->data.arrow.end;
      ShaulaPoint p1 = annotation->data.arrow.is_curved
                           ? annotation->data.arrow.control
                           : (ShaulaPoint){(p0.x + p2.x) / 2.0,
                                           (p0.y + p2.y) / 2.0};
      ShaulaPoint mid = { 0.25*p0.x + 0.5*p1.x + 0.25*p2.x, 0.25*p0.y + 0.5*p1.y + 0.25*p2.y };
      cairo_save(cr);
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
      cairo_arc(cr, mid.x, mid.y, 4.0, 0, 2 * G_PI);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
      cairo_set_line_width(cr, 1.5);
      cairo_stroke(cr);
      cairo_restore(cr);
    }
  }

  cairo_restore(cr);
}

ShaulaAnnotation *shaula_annotations_hit_test(GPtrArray *annotations,
                                              ShaulaPoint point,
                                              double tolerance) {
  if (annotations == NULL)
    return NULL;
  for (gint i = (gint)annotations->len - 1; i >= 0; i--) {
    ShaulaAnnotation *annotation = g_ptr_array_index(annotations, (guint)i);
    if (annotation == NULL)
      continue;
    switch (annotation->type) {
    case SHAULA_ANNOTATION_ARROW:
      if (annotation->data.arrow.is_curved) {
        if (shaula_rect_contains_point(
                shaula_rect_expanded(annotation->bounds, tolerance), point))
          return annotation;
      } else {
        if (shaula_point_distance_to_segment(point, annotation->data.arrow.start,
                                             annotation->data.arrow.end) <=
            tolerance)
          return annotation;
      }
      break;
    case SHAULA_ANNOTATION_MEASURE:
      if (shaula_point_distance_to_segment(point, annotation->data.measure.start,
                                           annotation->data.measure.end) <=
          tolerance)
        return annotation;
      break;
    case SHAULA_ANNOTATION_RECTANGLE:
    case SHAULA_ANNOTATION_TEXT:
    case SHAULA_ANNOTATION_HIGHLIGHT:
    case SHAULA_ANNOTATION_PEN:
      if (shaula_rect_contains_point(
              shaula_rect_expanded(annotation->bounds, tolerance), point))
        return annotation;
      break;
    }
  }
  return NULL;
}
