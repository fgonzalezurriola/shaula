#include "compositor/runtime.h"

#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static ShaulaEnvSpan span_from_string(const char *value) {
  return (ShaulaEnvSpan){value, strlen(value)};
}

static ShaulaEnvSpan span_from_bytes(const char *value, size_t length) {
  return (ShaulaEnvSpan){value, length};
}

static void assert_span_equal(ShaulaEnvSpan actual, const char *expected,
                              size_t expected_length) {
  if (expected == NULL) {
    g_assert_null(actual.data);
  } else {
    g_assert_nonnull(actual.data);
  }
  g_assert_cmpuint(actual.length, ==, expected_length);
  if (expected_length != 0U) {
    g_assert_cmpmem(actual.data, actual.length, expected, expected_length);
  }
}

static ShaulaCompositorDetection classify(const char *value) {
  ShaulaCompositorDetection detection = {0};
  g_assert_cmpint(shaula_compositor_classify(span_from_string(value),
                                             &detection),
                  ==, SHAULA_COMPOSITOR_STATUS_OK);
  return detection;
}

static ShaulaCompositorDetection
run_detection(ShaulaCompositorEnvironment environment) {
  ShaulaCompositorDetection detection = {0};
  g_assert_cmpint(shaula_compositor_detect(&environment, &detection), ==,
                  SHAULA_COMPOSITOR_STATUS_OK);
  return detection;
}

static void test_abi_and_kind_tokens(void) {
  ShaulaEnvSpan token;

  g_assert_cmpuint(sizeof(ShaulaCompositorKind), ==, 4U);
  g_assert_cmpuint(sizeof(ShaulaCompositorStatus), ==, 4U);
  g_assert_cmpint(SHAULA_COMPOSITOR_KIND_INVALID, ==, -1);
  g_assert_cmpint(SHAULA_COMPOSITOR_KIND_NIRI, ==, 0);
  g_assert_cmpint(SHAULA_COMPOSITOR_KIND_WAYLAND, ==, 1);
  g_assert_cmpint(SHAULA_COMPOSITOR_KIND_UNSUPPORTED, ==, 2);
  g_assert_cmpint(SHAULA_COMPOSITOR_STATUS_OK, ==, 0);
  g_assert_cmpint(SHAULA_COMPOSITOR_STATUS_INVALID_ARGUMENT, ==, 1);

  token = shaula_compositor_kind_token(SHAULA_COMPOSITOR_KIND_NIRI);
  assert_span_equal(token, "niri", strlen("niri"));
  token = shaula_compositor_kind_token(SHAULA_COMPOSITOR_KIND_WAYLAND);
  assert_span_equal(token, "wayland", strlen("wayland"));
  token = shaula_compositor_kind_token(SHAULA_COMPOSITOR_KIND_UNSUPPORTED);
  assert_span_equal(token, "unsupported", strlen("unsupported"));
  token = shaula_compositor_kind_token(SHAULA_COMPOSITOR_KIND_INVALID);
  assert_span_equal(token, NULL, 0U);
  token = shaula_compositor_kind_token(99);
  assert_span_equal(token, NULL, 0U);
}

static void test_classification_tables(void) {
  static const struct {
    const char *label;
    ShaulaCompositorKind kind;
    int32_t wlroots;
  } cases[] = {
      {"wayland", SHAULA_COMPOSITOR_KIND_WAYLAND, 0},
      {"WAYLAND", SHAULA_COMPOSITOR_KIND_WAYLAND, 0},
      {"gnome", SHAULA_COMPOSITOR_KIND_WAYLAND, 0},
      {"GNOME-SHELL", SHAULA_COMPOSITOR_KIND_WAYLAND, 0},
      {"kde", SHAULA_COMPOSITOR_KIND_WAYLAND, 0},
      {"Plasma", SHAULA_COMPOSITOR_KIND_WAYLAND, 0},
      {"sway", SHAULA_COMPOSITOR_KIND_WAYLAND, 1},
      {"HYPRLAND", SHAULA_COMPOSITOR_KIND_WAYLAND, 1},
      {"river", SHAULA_COMPOSITOR_KIND_WAYLAND, 1},
      {"wayfire", SHAULA_COMPOSITOR_KIND_WAYLAND, 1},
      {"weston", SHAULA_COMPOSITOR_KIND_WAYLAND, 0},
      {"labwc", SHAULA_COMPOSITOR_KIND_WAYLAND, 1},
      {"cage", SHAULA_COMPOSITOR_KIND_WAYLAND, 1},
      {"dwl", SHAULA_COMPOSITOR_KIND_WAYLAND, 1},
      {"foo-wayland-bar", SHAULA_COMPOSITOR_KIND_WAYLAND, 0},
      {"foo-WAYLAND-bar", SHAULA_COMPOSITOR_KIND_UNSUPPORTED, 0},
      {"x11", SHAULA_COMPOSITOR_KIND_UNSUPPORTED, 0},
  };
  size_t index;

  for (index = 0U; index < G_N_ELEMENTS(cases); index += 1U) {
    const ShaulaCompositorDetection detection = classify(cases[index].label);
    g_assert_cmpint(detection.kind, ==, cases[index].kind);
    assert_span_equal(detection.label, cases[index].label,
                      strlen(cases[index].label));
    g_assert_true(detection.label.data == cases[index].label);
    g_assert_cmpint(shaula_compositor_is_wlroots(detection), ==,
                    cases[index].wlroots);
  }
}

static void test_niri_and_arbitrary_bytes(void) {
  static const char embedded_nul[] = {'n', 'i', 'r', 'i', '\0', 'x'};
  static const char non_ascii[] = {(char)0xc3, (char)0xb1, 'i', 'r', 'i'};
  ShaulaCompositorDetection detection = {0};
  const char mixed_case[] = "NiRi";

  g_assert_cmpint(shaula_compositor_classify(span_from_string(mixed_case),
                                             &detection),
                  ==, SHAULA_COMPOSITOR_STATUS_OK);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_NIRI);
  assert_span_equal(detection.label, "niri", strlen("niri"));
  g_assert_true(detection.label.data != mixed_case);
  g_assert_cmpint(shaula_compositor_is_wlroots(detection), ==, 0);

  g_assert_cmpint(shaula_compositor_classify(
                      span_from_bytes(embedded_nul, sizeof(embedded_nul)),
                      &detection),
                  ==, SHAULA_COMPOSITOR_STATUS_OK);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_UNSUPPORTED);
  assert_span_equal(detection.label, embedded_nul, sizeof(embedded_nul));

  g_assert_cmpint(shaula_compositor_classify(
                      span_from_bytes(non_ascii, sizeof(non_ascii)), &detection),
                  ==, SHAULA_COMPOSITOR_STATUS_OK);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_UNSUPPORTED);
  assert_span_equal(detection.label, non_ascii, sizeof(non_ascii));

  g_assert_cmpint(shaula_compositor_classify((ShaulaEnvSpan){NULL, 0U},
                                             &detection),
                  ==, SHAULA_COMPOSITOR_STATUS_OK);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_UNSUPPORTED);
  assert_span_equal(detection.label, NULL, 0U);
}

static void test_detection_precedence_and_presence(void) {
  ShaulaCompositorDetection detection;
  ShaulaCompositorEnvironment environment = {0};

  detection = run_detection(environment);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_UNSUPPORTED);
  assert_span_equal(detection.label, "unsupported", strlen("unsupported"));

  environment.shaula_compositor = "  NIRI\t";
  environment.niri_socket = "ignored";
  environment.xdg_current_desktop = "sway";
  environment.xdg_session_desktop = "kde";
  environment.wayland_display = "wayland-1";
  detection = run_detection(environment);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_NIRI);
  assert_span_equal(detection.label, "niri", strlen("niri"));

  environment.shaula_compositor = " x11 ";
  detection = run_detection(environment);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_UNSUPPORTED);
  assert_span_equal(detection.label, "x11", strlen("x11"));

  environment = (ShaulaCompositorEnvironment){0};
  environment.shaula_compositor = " \t\r\n";
  environment.niri_socket = "";
  detection = run_detection(environment);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_NIRI);
  assert_span_equal(detection.label, "niri", strlen("niri"));

  environment = (ShaulaCompositorEnvironment){0};
  environment.xdg_current_desktop = " ; sway : hyprland ";
  detection = run_detection(environment);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_WAYLAND);
  assert_span_equal(detection.label, "sway", strlen("sway"));

  environment.xdg_current_desktop = "foo;niri";
  environment.xdg_session_desktop = "kde";
  detection = run_detection(environment);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_UNSUPPORTED);
  assert_span_equal(detection.label, "foo", strlen("foo"));

  environment.xdg_current_desktop = "::;;";
  environment.xdg_session_desktop = " KDE ";
  detection = run_detection(environment);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_WAYLAND);
  assert_span_equal(detection.label, "KDE", strlen("KDE"));

  environment = (ShaulaCompositorEnvironment){0};
  environment.wayland_display = "";
  detection = run_detection(environment);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_WAYLAND);
  assert_span_equal(detection.label, "wayland", strlen("wayland"));

  environment.xdg_session_desktop = " x11 ";
  detection = run_detection(environment);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_UNSUPPORTED);
  assert_span_equal(detection.label, "x11", strlen("x11"));
}

static void test_support_and_overlay_policy(void) {
  ShaulaCompositorDetection detection;

  detection = classify("niri");
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, 0),
                  ==, 1);
  g_assert_cmpint(shaula_compositor_overlay_supported(detection), ==, 1);

  detection = classify("sway");
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, 0),
                  ==, 1);
  g_assert_cmpint(shaula_compositor_overlay_supported(detection), ==, 1);

  detection = classify("gnome");
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, 0),
                  ==, 0);
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, 1),
                  ==, 1);
  g_assert_cmpint(shaula_compositor_overlay_supported(detection), ==, 0);

  detection = classify("x11");
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, 1),
                  ==, 0);
  g_assert_cmpint(shaula_compositor_overlay_supported(detection), ==, 0);

  detection = (ShaulaCompositorDetection){SHAULA_COMPOSITOR_KIND_UNSUPPORTED,
                                          span_from_string("SWAY")};
  g_assert_cmpint(shaula_compositor_is_wlroots(detection), ==, 1);
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, 0),
                  ==, 1);
  g_assert_cmpint(shaula_compositor_overlay_supported(detection), ==, 1);
}

static void test_invalid_arguments(void) {
  ShaulaCompositorDetection detection = {
      SHAULA_COMPOSITOR_KIND_NIRI, span_from_string("old")};
  ShaulaCompositorEnvironment environment = {0};

  g_assert_cmpint(shaula_compositor_classify((ShaulaEnvSpan){NULL, 1U},
                                             &detection),
                  ==, SHAULA_COMPOSITOR_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_INVALID);
  assert_span_equal(detection.label, NULL, 0U);
  g_assert_cmpint(shaula_compositor_classify(span_from_string("niri"), NULL),
                  ==, SHAULA_COMPOSITOR_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(shaula_compositor_detect(NULL, &detection), ==,
                  SHAULA_COMPOSITOR_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(detection.kind, ==, SHAULA_COMPOSITOR_KIND_INVALID);
  g_assert_cmpint(shaula_compositor_detect(&environment, NULL), ==,
                  SHAULA_COMPOSITOR_STATUS_INVALID_ARGUMENT);

  detection = (ShaulaCompositorDetection){99, span_from_string("sway")};
  g_assert_cmpint(shaula_compositor_is_wlroots(detection), ==, -1);
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, 0),
                  ==, -1);
  g_assert_cmpint(shaula_compositor_overlay_supported(detection), ==, -1);

  detection = (ShaulaCompositorDetection){SHAULA_COMPOSITOR_KIND_WAYLAND,
                                          (ShaulaEnvSpan){NULL, 2U}};
  g_assert_cmpint(shaula_compositor_is_wlroots(detection), ==, -1);
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, 0),
                  ==, -1);
  g_assert_cmpint(shaula_compositor_overlay_supported(detection), ==, -1);

  detection = classify("gnome");
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, -1),
                  ==, -1);
  g_assert_cmpint(shaula_compositor_supported_in_current_scope(detection, 2),
                  ==, -1);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/compositor/runtime/abi-kind-tokens",
                  test_abi_and_kind_tokens);
  g_test_add_func("/compositor/runtime/classification-tables",
                  test_classification_tables);
  g_test_add_func("/compositor/runtime/niri-arbitrary-bytes",
                  test_niri_and_arbitrary_bytes);
  g_test_add_func("/compositor/runtime/detection-precedence",
                  test_detection_precedence_and_presence);
  g_test_add_func("/compositor/runtime/support-overlay",
                  test_support_and_overlay_policy);
  g_test_add_func("/compositor/runtime/invalid-arguments",
                  test_invalid_arguments);
  return g_test_run();
}
