#include "capture_mode.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

static ShaulaCaptureModeSpan span_from_string(const char *value) {
  return (ShaulaCaptureModeSpan){value, strlen(value)};
}

static void assert_span_equal(ShaulaCaptureModeSpan actual,
                              const char *expected) {
  g_assert_nonnull(actual.data);
  g_assert_cmpuint(actual.length, ==, strlen(expected));
  g_assert_cmpmem(actual.data, actual.length, expected, strlen(expected));
}

static void test_capture_mode_tables(void) {
  static const struct {
    ShaulaCaptureMode mode;
    const char *cli_token;
    ShaulaRuntimeCaptureMode runtime_mode;
    const char *runtime_token;
    const char *backend_token;
    int32_t interactive;
  } cases[] = {
      {SHAULA_CAPTURE_MODE_QUICK, "quick",
       SHAULA_RUNTIME_CAPTURE_MODE_AREA, "area", "area", 1},
      {SHAULA_CAPTURE_MODE_AREA, "area", SHAULA_RUNTIME_CAPTURE_MODE_AREA,
       "area", "area", 1},
      {SHAULA_CAPTURE_MODE_FULLSCREEN, "fullscreen",
       SHAULA_RUNTIME_CAPTURE_MODE_CURRENT_OUTPUT, "current-output",
       "fullscreen", 0},
      {SHAULA_CAPTURE_MODE_ALL_SCREENS, "all-screens",
       SHAULA_RUNTIME_CAPTURE_MODE_ALL_OUTPUTS, "all-outputs", "all-screens",
       0},
      {SHAULA_CAPTURE_MODE_FOCUSED, "focused",
       SHAULA_RUNTIME_CAPTURE_MODE_CURRENT_OUTPUT, "current-output", "focused",
       0},
      {SHAULA_CAPTURE_MODE_WINDOW, "window",
       SHAULA_RUNTIME_CAPTURE_MODE_WINDOW, "window", "window", 0},
      {SHAULA_CAPTURE_MODE_PREVIOUS_AREA, "previous-area",
       SHAULA_RUNTIME_CAPTURE_MODE_AREA, "area", "area", 0},
      {SHAULA_CAPTURE_MODE_ALL_IN_ONE, "all-in-one",
       SHAULA_RUNTIME_CAPTURE_MODE_AREA, "area", "area", 1},
  };
  size_t index;

  g_assert_cmpint(SHAULA_CAPTURE_MODE_QUICK, ==, 0);
  g_assert_cmpint(SHAULA_CAPTURE_MODE_AREA, ==, 1);
  g_assert_cmpint(SHAULA_CAPTURE_MODE_FULLSCREEN, ==, 2);
  g_assert_cmpint(SHAULA_CAPTURE_MODE_ALL_SCREENS, ==, 3);
  g_assert_cmpint(SHAULA_CAPTURE_MODE_FOCUSED, ==, 4);
  g_assert_cmpint(SHAULA_CAPTURE_MODE_WINDOW, ==, 5);
  g_assert_cmpint(SHAULA_CAPTURE_MODE_PREVIOUS_AREA, ==, 6);
  g_assert_cmpint(SHAULA_CAPTURE_MODE_ALL_IN_ONE, ==, 7);
  g_assert_cmpint(SHAULA_RUNTIME_CAPTURE_MODE_AREA, ==, 0);
  g_assert_cmpint(SHAULA_RUNTIME_CAPTURE_MODE_CURRENT_OUTPUT, ==, 1);
  g_assert_cmpint(SHAULA_RUNTIME_CAPTURE_MODE_ALL_OUTPUTS, ==, 2);
  g_assert_cmpint(SHAULA_RUNTIME_CAPTURE_MODE_WINDOW, ==, 3);
  g_assert_cmpint(SHAULA_REGION_CAPTURE_MODE_LIVE, ==, 0);
  g_assert_cmpint(SHAULA_REGION_CAPTURE_MODE_FROZEN, ==, 1);

  for (index = 0; index < G_N_ELEMENTS(cases); index += 1) {
    const ShaulaCaptureModeSpan cli =
        shaula_capture_mode_cli_token(cases[index].mode);
    const ShaulaCaptureModeSpan runtime =
        shaula_runtime_capture_mode_token(cases[index].runtime_mode);
    const ShaulaCaptureModeSpan backend =
        shaula_capture_mode_backend_token(cases[index].mode);

    g_assert_cmpint(
        shaula_capture_mode_parse_cli_token(
            span_from_string(cases[index].cli_token)),
        ==, cases[index].mode);
    assert_span_equal(cli, cases[index].cli_token);
    g_assert_cmpint(shaula_capture_mode_runtime_mode(cases[index].mode), ==,
                    cases[index].runtime_mode);
    assert_span_equal(runtime, cases[index].runtime_token);
    assert_span_equal(backend, cases[index].backend_token);
    g_assert_cmpint(
        shaula_capture_mode_requires_interactive_selection(cases[index].mode),
        ==, cases[index].interactive);
  }
}

static void test_exact_cli_parsing(void) {
  static const char embedded_nul[] = {'a', 'r', 'e', 'a', '\0', 'x'};
  static const char non_ascii[] = {(char)0xc3, (char)0xa1, 'r', 'e', 'a'};

  g_assert_cmpint(shaula_capture_mode_parse_cli_token(span_from_string("")), ==,
                  SHAULA_CAPTURE_MODE_INVALID);
  g_assert_cmpint(
      shaula_capture_mode_parse_cli_token(span_from_string("AREA")), ==,
      SHAULA_CAPTURE_MODE_INVALID);
  g_assert_cmpint(
      shaula_capture_mode_parse_cli_token(span_from_string(" area")), ==,
      SHAULA_CAPTURE_MODE_INVALID);
  g_assert_cmpint(
      shaula_capture_mode_parse_cli_token(span_from_string("area ")), ==,
      SHAULA_CAPTURE_MODE_INVALID);
  g_assert_cmpint(
      shaula_capture_mode_parse_cli_token(span_from_string("all-screen")), ==,
      SHAULA_CAPTURE_MODE_INVALID);
  g_assert_cmpint(
      shaula_capture_mode_parse_cli_token(span_from_string("all-screens-x")),
      ==, SHAULA_CAPTURE_MODE_INVALID);
  g_assert_cmpint(shaula_capture_mode_parse_cli_token(
                      (ShaulaCaptureModeSpan){embedded_nul,
                                              sizeof(embedded_nul)}),
                  ==, SHAULA_CAPTURE_MODE_INVALID);
  g_assert_cmpint(shaula_capture_mode_parse_cli_token(
                      (ShaulaCaptureModeSpan){non_ascii, sizeof(non_ascii)}),
                  ==, SHAULA_CAPTURE_MODE_INVALID);
  g_assert_cmpint(shaula_capture_mode_parse_cli_token(
                      (ShaulaCaptureModeSpan){NULL, 0}),
                  ==, SHAULA_CAPTURE_MODE_INVALID);
  g_assert_cmpint(shaula_capture_mode_parse_cli_token(
                      (ShaulaCaptureModeSpan){NULL, 1}),
                  ==, SHAULA_CAPTURE_MODE_INVALID);
}

static void test_region_modes(void) {
  static const char embedded_nul[] = {'l', 'i', 'v', 'e', '\0'};
  ShaulaCaptureModeSpan first;
  ShaulaCaptureModeSpan second;

  g_assert_cmpint(
      shaula_region_capture_mode_parse(span_from_string("live")), ==,
      SHAULA_REGION_CAPTURE_MODE_LIVE);
  g_assert_cmpint(
      shaula_region_capture_mode_parse(span_from_string("frozen")), ==,
      SHAULA_REGION_CAPTURE_MODE_FROZEN);
  g_assert_cmpint(
      shaula_region_capture_mode_parse(span_from_string("LIVE")), ==,
      SHAULA_REGION_CAPTURE_MODE_INVALID);
  g_assert_cmpint(
      shaula_region_capture_mode_parse(span_from_string(" frozen")), ==,
      SHAULA_REGION_CAPTURE_MODE_INVALID);
  g_assert_cmpint(shaula_region_capture_mode_parse(
                      (ShaulaCaptureModeSpan){embedded_nul,
                                              sizeof(embedded_nul)}),
                  ==, SHAULA_REGION_CAPTURE_MODE_INVALID);
  g_assert_cmpint(shaula_region_capture_mode_parse(
                      (ShaulaCaptureModeSpan){NULL, 1}),
                  ==, SHAULA_REGION_CAPTURE_MODE_INVALID);

  first = shaula_region_capture_mode_token(SHAULA_REGION_CAPTURE_MODE_LIVE);
  second =
      shaula_region_capture_mode_token(SHAULA_REGION_CAPTURE_MODE_FROZEN);
  assert_span_equal(first, "live");
  assert_span_equal(second, "frozen");
  assert_span_equal(first, "live");
}

static void test_invalid_enum_values(void) {
  ShaulaCaptureModeSpan span;

  span = shaula_capture_mode_cli_token(SHAULA_CAPTURE_MODE_INVALID);
  g_assert_null(span.data);
  g_assert_cmpuint(span.length, ==, 0);
  span = shaula_capture_mode_backend_token(99);
  g_assert_null(span.data);
  g_assert_cmpuint(span.length, ==, 0);
  span = shaula_runtime_capture_mode_token(-9);
  g_assert_null(span.data);
  g_assert_cmpuint(span.length, ==, 0);
  span = shaula_region_capture_mode_token(7);
  g_assert_null(span.data);
  g_assert_cmpuint(span.length, ==, 0);

  g_assert_cmpint(shaula_capture_mode_runtime_mode(-1), ==,
                  SHAULA_RUNTIME_CAPTURE_MODE_INVALID);
  g_assert_cmpint(shaula_capture_mode_runtime_mode(8), ==,
                  SHAULA_RUNTIME_CAPTURE_MODE_INVALID);
  g_assert_cmpint(shaula_capture_mode_requires_interactive_selection(-1), ==,
                  -1);
  g_assert_cmpint(shaula_capture_mode_requires_interactive_selection(8), ==,
                  -1);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/core/capture-mode/tables", test_capture_mode_tables);
  g_test_add_func("/core/capture-mode/exact-cli", test_exact_cli_parsing);
  g_test_add_func("/core/capture-mode/region", test_region_modes);
  g_test_add_func("/core/capture-mode/invalid-enums",
                  test_invalid_enum_values);
  return g_test_run();
}
