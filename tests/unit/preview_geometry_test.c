#include "preview_geometry.h"

#include <glib.h>
#include <math.h>
#include <string.h>

#define ASSERT_DOUBLE(actual, expected)                                        \
  g_assert_cmpfloat_with_epsilon((actual), (expected), 0.000000001)

static void assert_point(ShaulaPoint actual, ShaulaPoint expected) {
  ASSERT_DOUBLE(actual.x, expected.x);
  ASSERT_DOUBLE(actual.y, expected.y);
}

static void assert_rect(ShaulaRect actual, ShaulaRect expected) {
  ASSERT_DOUBLE(actual.x, expected.x);
  ASSERT_DOUBLE(actual.y, expected.y);
  ASSERT_DOUBLE(actual.width, expected.width);
  ASSERT_DOUBLE(actual.height, expected.height);
}

static void test_color_default(void) {
  ShaulaColor color = shaula_color_default();
  ASSERT_DOUBLE(color.r, 0.165);
  ASSERT_DOUBLE(color.g, 0.290);
  ASSERT_DOUBLE(color.b, 0.400);
  ASSERT_DOUBLE(color.a, 1.0);
}

static void test_color_to_hex(void) {
  char output[8] = {0};

  shaula_color_to_hex((ShaulaColor){0.0, 0.5, 1.0, 0.25}, output);
  g_assert_cmpstr(output, ==, "#0080FF");

  shaula_color_to_hex((ShaulaColor){-1.0, 2.0, 0.1, 1.0}, output);
  g_assert_cmpstr(output, ==, "#00FF1A");
  g_assert_cmpuint(strlen(output), ==, 7);
}

static void test_rect_from_points_and_normalized(void) {
  assert_rect(
      shaula_rect_from_points((ShaulaPoint){8.0, 9.0}, (ShaulaPoint){2.0, 3.0}),
      (ShaulaRect){2.0, 3.0, 6.0, 6.0});
  assert_rect(shaula_rect_normalized((ShaulaRect){10.0, 20.0, -4.0, -8.0}),
              (ShaulaRect){6.0, 12.0, 4.0, 8.0});
  assert_rect(shaula_rect_normalized((ShaulaRect){1.0, 2.0, 0.0, 0.0}),
              (ShaulaRect){1.0, 2.0, 0.0, 0.0});
}

static void test_rect_clamped(void) {
  assert_rect(
      shaula_rect_clamped((ShaulaRect){-5.0, 8.0, 20.0, 20.0}, 12.0, 16.0),
      (ShaulaRect){0.0, 8.0, 12.0, 8.0});
  assert_rect(
      shaula_rect_clamped((ShaulaRect){8.0, 8.0, -12.0, -12.0}, 10.0, 10.0),
      (ShaulaRect){0.0, 0.0, 8.0, 8.0});
  assert_rect(
      shaula_rect_clamped((ShaulaRect){20.0, 20.0, 5.0, 5.0}, 10.0, 10.0),
      (ShaulaRect){10.0, 10.0, 0.0, 0.0});
  assert_rect(
      shaula_rect_clamped_c((ShaulaRect){-2.0, -3.0, 5.0, 7.0}, 10.0, 10.0),
      (ShaulaRect){0.0, 0.0, 3.0, 4.0});
}

static void test_rect_expanded_and_union(void) {
  assert_rect(shaula_rect_expanded((ShaulaRect){10.0, 20.0, -4.0, 6.0}, 2.0),
              (ShaulaRect){4.0, 18.0, 8.0, 10.0});
  assert_rect(shaula_rect_union((ShaulaRect){4.0, 6.0, 5.0, 4.0},
                                (ShaulaRect){12.0, 3.0, -6.0, 9.0}),
              (ShaulaRect){4.0, 3.0, 8.0, 9.0});
}

static void test_rect_empty_contains_and_intersects(void) {
  g_assert_true(shaula_rect_is_empty((ShaulaRect){0.0, 0.0, 0.5, 10.0}));
  g_assert_true(shaula_rect_is_empty((ShaulaRect){0.0, 0.0, -0.4, 10.0}));
  g_assert_false(shaula_rect_is_empty((ShaulaRect){0.0, 0.0, 0.5001, 1.0}));

  ShaulaRect rect = {10.0, 20.0, -5.0, -10.0};
  g_assert_true(shaula_rect_contains_point(rect, (ShaulaPoint){5.0, 10.0}));
  g_assert_true(shaula_rect_contains_point(rect, (ShaulaPoint){10.0, 20.0}));
  g_assert_false(shaula_rect_contains_point(rect, (ShaulaPoint){10.1, 20.0}));

  g_assert_true(shaula_rect_intersects((ShaulaRect){0.0, 0.0, 5.0, 5.0},
                                       (ShaulaRect){5.0, 5.0, 2.0, 2.0}));
  g_assert_true(shaula_rect_intersects((ShaulaRect){10.0, 10.0, -5.0, -5.0},
                                       (ShaulaRect){4.0, 4.0, 2.0, 2.0}));
  g_assert_false(shaula_rect_intersects((ShaulaRect){0.0, 0.0, 4.0, 4.0},
                                        (ShaulaRect){5.0, 5.0, 1.0, 1.0}));
}

static void test_point_distance(void) {
  ASSERT_DOUBLE(
      shaula_point_distance((ShaulaPoint){0.0, 0.0}, (ShaulaPoint){3.0, 4.0}),
      5.0);
  ASSERT_DOUBLE(shaula_point_distance((ShaulaPoint){-2.0, -2.0},
                                      (ShaulaPoint){-2.0, -2.0}),
                0.0);
}

static void test_point_distance_to_segment(void) {
  ASSERT_DOUBLE(shaula_point_distance_to_segment((ShaulaPoint){5.0, 4.0},
                                                 (ShaulaPoint){0.0, 0.0},
                                                 (ShaulaPoint){10.0, 0.0}),
                4.0);
  ASSERT_DOUBLE(shaula_point_distance_to_segment((ShaulaPoint){-3.0, 4.0},
                                                 (ShaulaPoint){0.0, 0.0},
                                                 (ShaulaPoint){10.0, 0.0}),
                5.0);
  ASSERT_DOUBLE(shaula_point_distance_to_segment((ShaulaPoint){4.0, 6.0},
                                                 (ShaulaPoint){1.0, 2.0},
                                                 (ShaulaPoint){1.0, 2.0}),
                5.0);
}

static void test_point_clamped(void) {
  assert_point(shaula_point_clamped((ShaulaPoint){-1.0, 15.0}, 10.0, 12.0),
               (ShaulaPoint){0.0, 12.0});
  assert_point(shaula_point_clamped((ShaulaPoint){4.0, 5.0}, 10.0, 12.0),
               (ShaulaPoint){4.0, 5.0});
}

static void test_exceptional_floating_point_values(void) {
  char output[8] = {0};
  shaula_color_to_hex((ShaulaColor){NAN, INFINITY, -INFINITY, 1.0}, output);
  g_assert_cmpstr(output, ==, "#00FF00");

  ShaulaRect normalized =
      shaula_rect_normalized((ShaulaRect){NAN, 2.0, NAN, -INFINITY});
  g_assert_true(isnan(normalized.x));
  g_assert_true(isnan(normalized.width));
  g_assert_true(isinf(normalized.y));
  g_assert_true(isinf(normalized.height));
  g_assert_cmpfloat(normalized.height, >, 0.0);

  ShaulaPoint clamped =
      shaula_point_clamped((ShaulaPoint){NAN, INFINITY}, INFINITY, 12.0);
  g_assert_true(isnan(clamped.x));
  ASSERT_DOUBLE(clamped.y, 12.0);

  g_assert_true(isnan(
      shaula_point_distance((ShaulaPoint){NAN, 0.0}, (ShaulaPoint){0.0, 0.0})));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/preview/geometry/color/default", test_color_default);
  g_test_add_func("/preview/geometry/color/to-hex", test_color_to_hex);
  g_test_add_func("/preview/geometry/rect/normalize",
                  test_rect_from_points_and_normalized);
  g_test_add_func("/preview/geometry/rect/clamp", test_rect_clamped);
  g_test_add_func("/preview/geometry/rect/expand-union",
                  test_rect_expanded_and_union);
  g_test_add_func("/preview/geometry/rect/predicates",
                  test_rect_empty_contains_and_intersects);
  g_test_add_func("/preview/geometry/point/distance", test_point_distance);
  g_test_add_func("/preview/geometry/point/segment-distance",
                  test_point_distance_to_segment);
  g_test_add_func("/preview/geometry/point/clamp", test_point_clamped);
  g_test_add_func("/preview/geometry/exceptional-floating-point",
                  test_exceptional_floating_point_values);
  return g_test_run();
}
