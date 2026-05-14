#include "preview_annotations.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <string.h>

static void rectangle_path(cairo_t *cr, ShaulaRect rect,
                           PreviewRectangleCorners corners);

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

static PangoLayout *create_text_layout(cairo_t *cr, const char *text,
                                       double font_size,
                                       PangoFontDescription **desc_out) {
  PangoLayout *layout = pango_cairo_create_layout(cr);
  pango_layout_set_text(layout, text != NULL ? text : "", -1);
  PangoFontDescription *desc = pango_font_description_from_string("Sans Bold");
  pango_font_description_set_absolute_size(desc, font_size * PANGO_SCALE);
  pango_layout_set_font_description(layout, desc);
  if (desc_out != NULL)
    *desc_out = desc;
  else
    pango_font_description_free(desc);
  return layout;
}

static ShaulaRect text_bounds(ShaulaPoint position, const char *text,
                              double font_size, ShaulaTextAlign align) {
  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t *cr = cairo_create(surface);
  PangoFontDescription *desc = NULL;
  PangoLayout *layout = create_text_layout(cr, "", font_size, &desc);
  char **lines = g_strsplit(text != NULL ? text : "", "\n", -1);
  double min_x = position.x;
  double max_x = position.x + 24.0;
  double y = position.y;
  for (int i = 0; lines[i] != NULL; i++) {
    pango_layout_set_text(layout, lines[i], -1);
    int width = 0;
    int height = 0;
    pango_layout_get_pixel_size(layout, &width, &height);
    double x = position.x;
    if (align == SHAULA_TEXT_ALIGN_CENTER)
      x -= width / 2.0;
    else if (align == SHAULA_TEXT_ALIGN_RIGHT)
      x -= width;
    min_x = MIN(min_x, x);
    max_x = MAX(max_x, x + MAX(24.0, (double)width));
    y += MAX(font_size * 1.25, (double)height);
  }
  if (lines[0] == NULL)
    y += font_size * 1.25;
  g_strfreev(lines);
  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  return (ShaulaRect){min_x, position.y, max_x - min_x,
                      MAX(font_size * 1.25, y - position.y)};
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
                                             double font_size,
                                             ShaulaTextAlign align) {
  ShaulaAnnotation *annotation =
      annotation_alloc(SHAULA_ANNOTATION_TEXT, color, 1.0);
  annotation->data.text.position = position;
  annotation->data.text.text = g_strdup(text != NULL ? text : "");
  annotation->data.text.font_size = font_size;
  annotation->data.text.align = align;
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
    clone->data.text.align = annotation->data.text.align;
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
                    annotation->data.text.font_size,
                    annotation->data.text.align);
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

static void draw_rectangle_selection(cairo_t *cr, ShaulaRect rect,
                                     PreviewRectangleCorners corners,
                                     double stroke_width) {
  rect = shaula_rect_normalized(rect);
  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

  cairo_set_source_rgba(cr, 0.10, 0.11, 0.12, 0.62);
  cairo_set_line_width(cr, MAX(stroke_width + 8.0, 10.0));
  rectangle_path(cr, rect, corners);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, 0.98);
  cairo_set_line_width(cr, MAX(stroke_width + 4.0, 6.0));
  double dashes[] = {4.0, 3.0};
  cairo_set_dash(cr, dashes, 2, 0);
  rectangle_path(cr, rect, corners);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_square_handle(cairo_t *cr, ShaulaPoint point) {
  double size = 8.0;
  cairo_save(cr);
  cairo_rectangle(cr, point.x - size / 2.0, point.y - size / 2.0, size, size);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.08, 0.09, 0.10, 0.9);
  cairo_set_line_width(cr, 1.5);
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

static double path_distance_to_point(ShaulaPenPath path, ShaulaPoint point) {
  if (path.len <= 0)
    return G_MAXDOUBLE;
  if (path.len == 1)
    return shaula_point_distance(point, path.points[0]);

  double best = G_MAXDOUBLE;
  for (int i = 1; i < path.len; i++) {
    double distance =
        shaula_point_distance_to_segment(point, path.points[i - 1],
                                         path.points[i]);
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
  best = MIN(best, shaula_point_distance_to_segment(point, bottom_right,
                                                    bottom_left));
  best =
      MIN(best, shaula_point_distance_to_segment(point, bottom_left, top_left));
  return best;
}

static double rectangle_visible_fill_alpha(const ShaulaAnnotation *annotation) {
  if (annotation == NULL || annotation->type != SHAULA_ANNOTATION_RECTANGLE ||
      !annotation->data.rectangle.filled)
    return 0.0;
  return annotation->color.a * 0.22;
}

static void draw_path_selection(cairo_t *cr, ShaulaPenPath path,
                                double stroke_width) {
  if (path.len <= 0)
    return;

  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_line_width(cr, MAX(stroke_width + 8.0, 10.0));
  cairo_set_source_rgba(cr, 0.10, 0.11, 0.12, 0.62);
  cairo_move_to(cr, path.points[0].x, path.points[0].y);
  for (int i = 1; i < path.len; i++)
    cairo_line_to(cr, path.points[i].x, path.points[i].y);
  cairo_stroke(cr);

  cairo_set_line_width(cr, MAX(stroke_width + 4.0, 6.0));
  cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, 0.98);
  double dashes[] = {4.0, 3.0};
  cairo_set_dash(cr, dashes, 2, 0);
  cairo_move_to(cr, path.points[0].x, path.points[0].y);
  for (int i = 1; i < path.len; i++)
    cairo_line_to(cr, path.points[i].x, path.points[i].y);
  cairo_stroke(cr);
  cairo_restore(cr);
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

static void draw_arrow_selection(cairo_t *cr, const ShaulaAnnotation *annotation) {
  ShaulaPoint start = annotation->data.arrow.start;
  ShaulaPoint end = annotation->data.arrow.end;
  ShaulaPoint control = annotation->data.arrow.control;
  double stroke_width = annotation->stroke_width;

  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

  for (int pass = 0; pass < 2; pass++) {
    if (pass == 0) {
      cairo_set_source_rgba(cr, 0.10, 0.11, 0.12, 0.62);
      cairo_set_line_width(cr, MAX(stroke_width + 8.0, 10.0));
    } else {
      cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, 0.98);
      cairo_set_line_width(cr, MAX(stroke_width + 4.0, 6.0));
      double dashes[] = {4.0, 3.0};
      cairo_set_dash(cr, dashes, 2, 0);
    }

    cairo_move_to(cr, start.x, start.y);
    if (annotation->data.arrow.is_curved) {
      for (int i = 1; i <= 24; i++) {
        ShaulaPoint p =
            arrow_curve_point(start, control, end, (double)i / 24.0);
        cairo_line_to(cr, p.x, p.y);
      }
    } else {
      cairo_line_to(cr, end.x, end.y);
    }
    cairo_stroke(cr);
  }
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

static void draw_arrow_shape(cairo_t *cr, const ShaulaAnnotation *annotation) {
  ShaulaPoint start = annotation->data.arrow.start;
  ShaulaPoint end = annotation->data.arrow.end;
  ShaulaPoint control =
      annotation->data.arrow.is_curved ? annotation->data.arrow.control : start;
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
    ShaulaPoint shaft_end_pt =
        arrow_shaft_end(dir_point, end, annotation->stroke_width);
    cairo_move_to(cr, start.x, start.y);
    cairo_line_to(cr, shaft_end_pt.x, shaft_end_pt.y);
    cairo_stroke(cr);
  }

  cairo_set_dash(cr, NULL, 0, 0);
  draw_filled_arrow_head(cr, dir_point, end, annotation->stroke_width);
}

static void draw_measure_label(cairo_t *cr, const ShaulaAnnotation *annotation) {
  char label[64];
  if (annotation->data.measure.rect_width > 0 &&
      annotation->data.measure.rect_height > 0) {
    snprintf(label, sizeof(label), "%d \xc3\x97 %d px",
      annotation->data.measure.rect_width,
      annotation->data.measure.rect_height);
  } else {
    snprintf(label, sizeof(label), "%.0f px", annotation->data.measure.distance_px);
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

void shaula_annotation_draw(cairo_t *cr, const ShaulaAnnotation *annotation) {
  if (annotation == NULL)
    return;

  cairo_save(cr);
  cairo_set_line_width(cr, annotation->stroke_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  set_annotation_color(cr, annotation->color, 1.0);

  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    draw_arrow_shape(cr, annotation);
    break;
  case SHAULA_ANNOTATION_TEXT:
    {
    PangoFontDescription *desc = NULL;
    PangoLayout *layout =
        create_text_layout(cr, "", annotation->data.text.font_size, &desc);
    char **lines = g_strsplit(annotation->data.text.text, "\n", -1);
    double y = annotation->data.text.position.y;
    for (int i = 0; lines[i] != NULL; i++) {
      pango_layout_set_text(layout, lines[i], -1);
      int width = 0;
      int height = 0;
      pango_layout_get_pixel_size(layout, &width, &height);
      double x = annotation->data.text.position.x;
      if (annotation->data.text.align == SHAULA_TEXT_ALIGN_CENTER)
        x -= width / 2.0;
      else if (annotation->data.text.align == SHAULA_TEXT_ALIGN_RIGHT)
        x -= width;
      cairo_move_to(cr, x, y);
      pango_cairo_show_layout(cr, layout);
      y += MAX(annotation->data.text.font_size * 1.25, (double)height);
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

  if (annotation->selected) {
    if (annotation->type == SHAULA_ANNOTATION_PEN) {
      draw_path_selection(cr, annotation->data.pen, annotation->stroke_width);
      set_annotation_color(cr, annotation->color, 1.0);
      cairo_set_line_width(cr, annotation->stroke_width);
      draw_pen_path(cr, annotation->data.pen);
    } else if (annotation->type == SHAULA_ANNOTATION_HIGHLIGHT) {
      draw_path_selection(cr, annotation->data.highlight,
                          annotation->stroke_width);
      set_annotation_color(cr, annotation->color, 1.0);
      cairo_set_line_width(cr, annotation->stroke_width);
      draw_pen_path(cr, annotation->data.highlight);
    } else if (annotation->type == SHAULA_ANNOTATION_RECTANGLE) {
      draw_rectangle_selection(cr, annotation->data.rectangle.rect,
                               annotation->data.rectangle.corners,
                               annotation->stroke_width);
      set_annotation_color(cr, annotation->color, 1.0);
      cairo_set_line_width(cr, annotation->stroke_width);
      apply_arrow_stroke_style(cr, annotation->data.rectangle.stroke_style,
                               annotation->stroke_width);
      rectangle_path(cr, annotation->data.rectangle.rect,
                     annotation->data.rectangle.corners);
      cairo_stroke(cr);
      draw_rectangle_handles(cr, annotation->data.rectangle.rect);
    } else if (annotation->type != SHAULA_ANNOTATION_ARROW) {
      draw_selection(cr, annotation->bounds);
    }
    if (annotation->type == SHAULA_ANNOTATION_ARROW) {
      draw_arrow_selection(cr, annotation);
      set_annotation_color(cr, annotation->color, 1.0);
      cairo_set_line_width(cr, annotation->stroke_width);
      draw_arrow_shape(cr, annotation);
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
      draw_round_handle(cr, p0);
      draw_round_handle(cr, p2);
    }
  }

  cairo_restore(cr);
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
static ShaulaAnnotationHitKind annotation_hit_kind(
    ShaulaAnnotation *annotation, ShaulaPoint point, double tolerance) {
  if (annotation == NULL)
    return SHAULA_ANNOTATION_HIT_NONE;
  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    tolerance = MAX(tolerance, annotation->stroke_width / 2.0 + 3.0);
    if (!shaula_rect_contains_point(
            shaula_rect_expanded(annotation->bounds, tolerance), point))
      return SHAULA_ANNOTATION_HIT_NONE;
    if (annotation->data.arrow.is_curved) {
      if (arrow_curve_distance_to_point(annotation->data.arrow.start,
                                        annotation->data.arrow.control,
                                        annotation->data.arrow.end,
                                        point) <= tolerance)
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
    if (rectangle_visible_fill_alpha(annotation) >= 0.10 &&
        shaula_rect_contains_point(
            shaula_rect_normalized(annotation->data.rectangle.rect), point))
      return SHAULA_ANNOTATION_HIT_FILL;
    break;
  }
  case SHAULA_ANNOTATION_TEXT:
    if (shaula_rect_contains_point(
            shaula_rect_expanded(annotation->bounds, tolerance), point))
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
