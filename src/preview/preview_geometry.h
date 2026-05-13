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

/* C-side rectangle clamp for hot GTK paths.
 *
 * The exported Zig geometry functions remain the cross-language contract, but
 * passing/returning four-double structs plus scalar bounds through C call sites
 * is brittle enough that interactive preview geometry should avoid depending on
 * that ABI for frame-by-frame clamp work.
 */
static inline ShaulaRect shaula_rect_clamped_c(ShaulaRect input,
                                               double max_width,
                                               double max_height) {
  ShaulaRect rect = input;
  if (rect.width < 0.0) {
    rect.x += rect.width;
    rect.width = -rect.width;
  }
  if (rect.height < 0.0) {
    rect.y += rect.height;
    rect.height = -rect.height;
  }
  double x1 = CLAMP(rect.x, 0.0, max_width);
  double y1 = CLAMP(rect.y, 0.0, max_height);
  double x2 = CLAMP(rect.x + rect.width, 0.0, max_width);
  double y2 = CLAMP(rect.y + rect.height, 0.0, max_height);
  return (ShaulaRect){x1, y1, MAX(0.0, x2 - x1), MAX(0.0, y2 - y1)};
}

#endif
