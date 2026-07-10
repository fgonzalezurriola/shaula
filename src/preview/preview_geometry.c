#include "preview_geometry.h"

#include <math.h>

static double shaula_clamp_double(double value, double minimum,
                                  double maximum) {
  if (value < minimum)
    return minimum;
  if (value > maximum)
    return maximum;
  return value;
}

static unsigned int shaula_color_channel(double value) {
  /* Avoid undefined float-to-integer conversion for malformed NaN input. */
  if (isnan(value))
    return 0;
  double clamped = shaula_clamp_double(value, 0.0, 1.0);
  return (unsigned int)(clamped * 255.0 + 0.5);
}

ShaulaColor shaula_color_default(void) {
  return (ShaulaColor){0.165, 0.290, 0.400, 1.0};
}

void shaula_color_to_hex(ShaulaColor color, char out[8]) {
  g_return_if_fail(out != NULL);

  g_snprintf(out, 8, "#%02X%02X%02X", shaula_color_channel(color.r),
             shaula_color_channel(color.g), shaula_color_channel(color.b));
}

ShaulaRect shaula_rect_from_points(ShaulaPoint a, ShaulaPoint b) {
  return shaula_rect_normalized((ShaulaRect){a.x, a.y, b.x - a.x, b.y - a.y});
}

ShaulaRect shaula_rect_normalized(ShaulaRect rect) {
  if (rect.width < 0.0) {
    rect.x += rect.width;
    rect.width = -rect.width;
  }
  if (rect.height < 0.0) {
    rect.y += rect.height;
    rect.height = -rect.height;
  }
  return rect;
}

ShaulaRect shaula_rect_clamped(ShaulaRect input, double max_width,
                               double max_height) {
  ShaulaRect rect = shaula_rect_normalized(input);
  double x1 = shaula_clamp_double(rect.x, 0.0, max_width);
  double y1 = shaula_clamp_double(rect.y, 0.0, max_height);
  double x2 = shaula_clamp_double(rect.x + rect.width, 0.0, max_width);
  double y2 = shaula_clamp_double(rect.y + rect.height, 0.0, max_height);
  return shaula_rect_from_points((ShaulaPoint){x1, y1}, (ShaulaPoint){x2, y2});
}

ShaulaRect shaula_rect_expanded(ShaulaRect input, double amount) {
  ShaulaRect rect = shaula_rect_normalized(input);
  return (ShaulaRect){rect.x - amount, rect.y - amount,
                      rect.width + amount * 2.0, rect.height + amount * 2.0};
}

ShaulaRect shaula_rect_union(ShaulaRect a_input, ShaulaRect b_input) {
  ShaulaRect a = shaula_rect_normalized(a_input);
  ShaulaRect b = shaula_rect_normalized(b_input);
  double x1 = MIN(a.x, b.x);
  double y1 = MIN(a.y, b.y);
  double x2 = MAX(a.x + a.width, b.x + b.width);
  double y2 = MAX(a.y + a.height, b.y + b.height);
  return (ShaulaRect){x1, y1, x2 - x1, y2 - y1};
}

gboolean shaula_rect_is_empty(ShaulaRect input) {
  ShaulaRect rect = shaula_rect_normalized(input);
  return rect.width <= 0.5 || rect.height <= 0.5;
}

gboolean shaula_rect_contains_point(ShaulaRect input, ShaulaPoint point) {
  ShaulaRect rect = shaula_rect_normalized(input);
  return point.x >= rect.x && point.y >= rect.y &&
         point.x <= rect.x + rect.width && point.y <= rect.y + rect.height;
}

gboolean shaula_rect_intersects(ShaulaRect a_input, ShaulaRect b_input) {
  ShaulaRect a = shaula_rect_normalized(a_input);
  ShaulaRect b = shaula_rect_normalized(b_input);
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
  double length_squared = dx * dx + dy * dy;
  if (length_squared <= 0.000001)
    return shaula_point_distance(point, a);

  double projection =
      ((point.x - a.x) * dx + (point.y - a.y) * dy) / length_squared;
  double t = shaula_clamp_double(projection, 0.0, 1.0);
  ShaulaPoint projected = {a.x + t * dx, a.y + t * dy};
  return shaula_point_distance(point, projected);
}

ShaulaPoint shaula_point_clamped(ShaulaPoint point, double max_width,
                                 double max_height) {
  return (ShaulaPoint){shaula_clamp_double(point.x, 0.0, max_width),
                       shaula_clamp_double(point.y, 0.0, max_height)};
}
