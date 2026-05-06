#ifndef SHAULA_PREVIEW_ANNOTATIONS_H
#define SHAULA_PREVIEW_ANNOTATIONS_H

#include <cairo.h>
#include <glib.h>

#include "preview_geometry.h"

typedef enum {
  SHAULA_ANNOTATION_ARROW,
  SHAULA_ANNOTATION_TEXT,
  SHAULA_ANNOTATION_MEASURE,
  SHAULA_ANNOTATION_RECTANGLE,
  SHAULA_ANNOTATION_HIGHLIGHT,
  SHAULA_ANNOTATION_PEN
} ShaulaAnnotationType;

typedef struct {
  ShaulaPoint *points;
  int len;
  int cap;
} ShaulaPenPath;

typedef struct ShaulaAnnotation {
  int id;
  ShaulaAnnotationType type;
  gboolean selected;
  ShaulaRect bounds;
  ShaulaColor color;
  double stroke_width;
  union {
    struct {
      ShaulaPoint start;
      ShaulaPoint end;
      ShaulaPoint control;
      gboolean is_curved;
    } arrow;
    struct {
      ShaulaPoint position;
      char *text;
      double font_size;
    } text;
    struct {
      ShaulaPoint start;
      ShaulaPoint end;
      double distance_px;
    } measure;
    struct {
      ShaulaRect rect;
    } rectangle;
    struct {
      ShaulaRect rect;
    } highlight;
    ShaulaPenPath pen;
  } data;
} ShaulaAnnotation;

ShaulaAnnotation *shaula_annotation_new_arrow(ShaulaPoint start,
                                              ShaulaPoint end,
                                              ShaulaColor color,
                                              double stroke_width);
ShaulaAnnotation *shaula_annotation_new_text(ShaulaPoint position,
                                             const char *text,
                                             ShaulaColor color,
                                             double font_size);
ShaulaAnnotation *shaula_annotation_new_measure(ShaulaPoint start,
                                                ShaulaPoint end,
                                                ShaulaColor color,
                                                double stroke_width);
ShaulaAnnotation *shaula_annotation_new_rectangle(ShaulaRect rect,
                                                  ShaulaColor color,
                                                  double stroke_width);
ShaulaAnnotation *shaula_annotation_new_highlight(ShaulaRect rect,
                                                  ShaulaColor color);
ShaulaAnnotation *shaula_annotation_new_pen(const ShaulaPoint *points, int len,
                                            ShaulaColor color,
                                            double stroke_width);
ShaulaAnnotation *shaula_annotation_clone(const ShaulaAnnotation *annotation);
void shaula_annotation_free(gpointer annotation);

GPtrArray *shaula_annotations_clone_array(GPtrArray *annotations);
void shaula_annotation_update_bounds(ShaulaAnnotation *annotation);
void shaula_annotation_move(ShaulaAnnotation *annotation, double dx,
                            double dy);
void shaula_annotation_draw(cairo_t *cr, const ShaulaAnnotation *annotation);
ShaulaAnnotation *shaula_annotations_hit_test(GPtrArray *annotations,
                                              ShaulaPoint point,
                                              double tolerance);

#endif
