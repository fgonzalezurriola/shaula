#include "preview_geometry.h"

#include <math.h>
#include <stdio.h>

ShaulaColor shaula_color_default(void) {
  return (ShaulaColor){0.165, 0.290, 0.400, 1.0};
}

void shaula_color_to_hex(ShaulaColor color, char out[8]) {
  int r = (int)(CLAMP(color.r, 0.0, 1.0) * 255.0 + 0.5);
  int g = (int)(CLAMP(color.g, 0.0, 1.0) * 255.0 + 0.5);
  int b = (int)(CLAMP(color.b, 0.0, 1.0) * 255.0 + 0.5);
  snprintf(out, 8, "#%02X%02X%02X", r, g, b);
}

ShaulaRect shaula_rect_from_points(ShaulaPoint a, ShaulaPoint b) {
  ShaulaRect rect = {a.x, a.y, b.x - a.x, b.y - a.y};
  return shaula_rect_normalized(rect);
}

ShaulaRect shaula_rect_normalized(ShaulaRect rect) {
  if (rect.width < 0) {
    rect.x += rect.width;
    rect.width = -rect.width;
  }
  if (rect.height < 0) {
    rect.y += rect.height;
    rect.height = -rect.height;
  }
  return rect;
}

ShaulaRect shaula_rect_clamped(ShaulaRect rect, double max_width,
                               double max_height) {
  rect = shaula_rect_normalized(rect);
  double x1 = CLAMP(rect.x, 0.0, max_width);
  double y1 = CLAMP(rect.y, 0.0, max_height);
  double x2 = CLAMP(rect.x + rect.width, 0.0, max_width);
  double y2 = CLAMP(rect.y + rect.height, 0.0, max_height);
  return shaula_rect_from_points((ShaulaPoint){x1, y1},
                                 (ShaulaPoint){x2, y2});
}

ShaulaRect shaula_rect_expanded(ShaulaRect rect, double amount) {
  rect = shaula_rect_normalized(rect);
  return (ShaulaRect){rect.x - amount, rect.y - amount,
                      rect.width + amount * 2.0,
                      rect.height + amount * 2.0};
}

ShaulaRect shaula_rect_union(ShaulaRect a, ShaulaRect b) {
  a = shaula_rect_normalized(a);
  b = shaula_rect_normalized(b);
  double x1 = MIN(a.x, b.x);
  double y1 = MIN(a.y, b.y);
  double x2 = MAX(a.x + a.width, b.x + b.width);
  double y2 = MAX(a.y + a.height, b.y + b.height);
  return (ShaulaRect){x1, y1, x2 - x1, y2 - y1};
}

gboolean shaula_rect_is_empty(ShaulaRect rect) {
  rect = shaula_rect_normalized(rect);
  return rect.width <= 0.5 || rect.height <= 0.5;
}

gboolean shaula_rect_contains_point(ShaulaRect rect, ShaulaPoint point) {
  rect = shaula_rect_normalized(rect);
  return point.x >= rect.x && point.y >= rect.y &&
         point.x <= rect.x + rect.width && point.y <= rect.y + rect.height;
}

gboolean shaula_rect_intersects(ShaulaRect a, ShaulaRect b) {
  a = shaula_rect_normalized(a);
  b = shaula_rect_normalized(b);
  return a.x <= b.x + b.width && a.x + a.width >= b.x &&
         a.y <= b.y + b.height && a.y + a.height >= b.y;
}

double shaula_point_distance(ShaulaPoint a, ShaulaPoint b) {
  double dx = a.x - b.x;
  double dy = a.y - b.y;
  return sqrt(dx * dx + dy * dy);
}

double shaula_point_distance_to_segment(ShaulaPoint point, ShaulaPoint a,
                                        ShaulaPoint b) {
  double dx = b.x - a.x;
  double dy = b.y - a.y;
  double len2 = dx * dx + dy * dy;
  if (len2 <= 0.000001)
    return shaula_point_distance(point, a);
  double t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / len2;
  t = CLAMP(t, 0.0, 1.0);
  ShaulaPoint projection = {a.x + t * dx, a.y + t * dy};
  return shaula_point_distance(point, projection);
}

ShaulaPoint shaula_point_clamped(ShaulaPoint point, double max_width,
                                 double max_height) {
  point.x = CLAMP(point.x, 0.0, max_width);
  point.y = CLAMP(point.y, 0.0, max_height);
  return point;
}
