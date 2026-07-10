#ifndef SHAULA_PREVIEW_GEOMETRY_H
#define SHAULA_PREVIEW_GEOMETRY_H

#include <glib.h>

typedef struct {
  double x;
  double y;
} ShaulaPoint;

typedef struct {
  double x;
  double y;
  double width;
  double height;
} ShaulaRect;

typedef struct {
  double r;
  double g;
  double b;
  double a;
} ShaulaColor;

ShaulaColor shaula_color_default(void);
void shaula_color_to_hex(ShaulaColor color, char out[8]);

ShaulaRect shaula_rect_from_points(ShaulaPoint a, ShaulaPoint b);
ShaulaRect shaula_rect_normalized(ShaulaRect rect);
ShaulaRect shaula_rect_clamped(ShaulaRect rect, double max_width,
                               double max_height);
ShaulaRect shaula_rect_expanded(ShaulaRect rect, double amount);
ShaulaRect shaula_rect_union(ShaulaRect a, ShaulaRect b);
gboolean shaula_rect_is_empty(ShaulaRect rect);
gboolean shaula_rect_contains_point(ShaulaRect rect, ShaulaPoint point);
gboolean shaula_rect_intersects(ShaulaRect a, ShaulaRect b);

double shaula_point_distance(ShaulaPoint a, ShaulaPoint b);
double shaula_point_distance_to_segment(ShaulaPoint point, ShaulaPoint a,
                                        ShaulaPoint b);
ShaulaPoint shaula_point_clamped(ShaulaPoint point, double max_width,
                                 double max_height);

/* Source-compatible alias retained while C callers are migrated away from the
 * old cross-language workaround. Geometry ownership now lives entirely in C.
 */
static inline ShaulaRect shaula_rect_clamped_c(ShaulaRect input,
                                               double max_width,
                                               double max_height) {
  return shaula_rect_clamped(input, max_width, max_height);
}

#endif
