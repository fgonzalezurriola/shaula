#include "capabilities/runtime.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

static ShaulaEnvSpan span_from_string(const char *value) {
  return (ShaulaEnvSpan){value, strlen(value)};
}

static void assert_span_equal(ShaulaEnvSpan actual, const char *expected) {
  if (expected == NULL) {
    g_assert_null(actual.data);
    g_assert_cmpuint(actual.length, ==, 0U);
    return;
  }
  g_assert_nonnull(actual.data);
  g_assert_cmpuint(actual.length, ==, strlen(expected));
  g_assert_cmpmem(actual.data, actual.length, expected, strlen(expected));
}

static ShaulaCapabilitiesEnvironment base_environment(const char *compositor) {
  ShaulaCapabilitiesEnvironment environment = {0};
  environment.compositor.shaula_compositor = compositor;
  environment.portal_available = "0";
  environment.portal_window_capable = "0";
  return environment;
}

static ShaulaRuntimeDecision
resolve_environment(ShaulaCapabilitiesEnvironment environment) {
  ShaulaRuntimeDecision decision = {0};
  g_assert_cmpint(shaula_capabilities_resolve(&environment, &decision), ==,
                  SHAULA_CAPABILITIES_STATUS_OK);
  return decision;
}

static void test_abi_and_backend_labels(void) {
  g_assert_cmpuint(sizeof(ShaulaBackendKind), ==, 4U);
  g_assert_cmpuint(sizeof(ShaulaCapabilitiesStatus), ==, 4U);
  g_assert_cmpint(SHAULA_BACKEND_KIND_INVALID, ==, -1);
  g_assert_cmpint(SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT, ==, 0);
  g_assert_cmpint(SHAULA_BACKEND_KIND_GRIM_WLROOTS, ==, 1);
  g_assert_cmpint(SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT, ==, 2);
  g_assert_cmpint(SHAULA_BACKEND_KIND_STUB, ==, 3);

  assert_span_equal(
      shaula_capabilities_backend_label(
          SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT),
      "niri-wayland-direct");
  assert_span_equal(
      shaula_capabilities_backend_label(SHAULA_BACKEND_KIND_GRIM_WLROOTS),
      "grim-wlroots");
  assert_span_equal(
      shaula_capabilities_backend_label(
          SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT),
      "portal-screenshot");
  assert_span_equal(
      shaula_capabilities_backend_label(SHAULA_BACKEND_KIND_STUB),
      "__stub__");
  assert_span_equal(
      shaula_capabilities_backend_label(SHAULA_BACKEND_KIND_INVALID), NULL);
  assert_span_equal(shaula_capabilities_backend_label(99), NULL);
}

static void test_invalid_arguments_reset_output(void) {
  ShaulaRuntimeDecision decision;
  ShaulaCapabilitiesEnvironment environment = base_environment("niri");

  memset(&decision, 0x7f, sizeof(decision));
  g_assert_cmpint(shaula_capabilities_resolve(NULL, &decision), ==,
                  SHAULA_CAPABILITIES_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(decision.backend, ==, SHAULA_BACKEND_KIND_INVALID);
  g_assert_null(decision.compositor.label.data);
  g_assert_cmpuint(decision.compositor.label.length, ==, 0U);

  g_assert_cmpint(shaula_capabilities_resolve(&environment, NULL), ==,
                  SHAULA_CAPABILITIES_STATUS_INVALID_ARGUMENT);
}

static void test_niri_runtime_decision(void) {
  const ShaulaRuntimeDecision decision =
      resolve_environment(base_environment("niri"));

  g_assert_cmpint(decision.compositor.kind, ==,
                  SHAULA_COMPOSITOR_KIND_NIRI);
  assert_span_equal(decision.compositor.label, "niri");
  g_assert_cmpint(decision.compositor_supported, ==, 1);
  g_assert_cmpint(decision.overlay_supported, ==, 1);
  g_assert_cmpint(decision.backend, ==,
                  SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT);
  g_assert_cmpint(decision.capture.area, ==, 1);
  g_assert_cmpint(decision.capture.fullscreen, ==, 1);
  g_assert_cmpint(decision.capture.all_screens, ==, 1);
  g_assert_cmpint(decision.capture.window, ==, 0);
  g_assert_cmpint(decision.portal_available, ==, 0);
  g_assert_cmpint(decision.portal_window_capable, ==, 0);
}

static void test_backend_override_precedence(void) {
  static const struct {
    const char *token;
    ShaulaBackendKind expected;
  } cases[] = {
      {"__stub__", SHAULA_BACKEND_KIND_STUB},
      {" portal-screenshot\t", SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT},
      {"grim-wlroots", SHAULA_BACKEND_KIND_GRIM_WLROOTS},
      {"niri-wayland-direct", SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT},
  };
  size_t index;

  for (index = 0U; index < G_N_ELEMENTS(cases); index += 1U) {
    ShaulaCapabilitiesEnvironment environment = base_environment("niri");
    const ShaulaRuntimeDecision decision = (environment.capture_backend =
                                                cases[index].token,
                                            resolve_environment(environment));
    g_assert_cmpint(decision.backend, ==, cases[index].expected);
  }

  {
    ShaulaCapabilitiesEnvironment environment = base_environment("niri");
    environment.capture_backend = "unknown";
    g_assert_cmpint(resolve_environment(environment).backend, ==,
                    SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT);
  }
}

static void test_force_portal_and_stub_capture_modes(void) {
  ShaulaCapabilitiesEnvironment environment = base_environment("niri");
  ShaulaRuntimeDecision decision;

  environment.capture_force_portal = "YeS";
  decision = resolve_environment(environment);
  g_assert_cmpint(decision.backend, ==,
                  SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT);
  g_assert_cmpint(decision.capture.area, ==, 1);

  environment.capture_backend = "__stub__";
  decision = resolve_environment(environment);
  g_assert_cmpint(decision.backend, ==, SHAULA_BACKEND_KIND_STUB);
  g_assert_cmpint(decision.capture.area, ==, 0);
  g_assert_cmpint(decision.capture.fullscreen, ==, 0);
  g_assert_cmpint(decision.capture.all_screens, ==, 0);
  g_assert_cmpint(decision.capture.window, ==, 0);
}

static void test_generic_wayland_portal_gating(void) {
  ShaulaCapabilitiesEnvironment environment = base_environment("gnome");
  ShaulaRuntimeDecision decision = resolve_environment(environment);

  g_assert_cmpint(decision.compositor.kind, ==,
                  SHAULA_COMPOSITOR_KIND_WAYLAND);
  g_assert_cmpint(decision.compositor_supported, ==, 0);
  g_assert_cmpint(decision.overlay_supported, ==, 0);
  g_assert_cmpint(decision.backend, ==,
                  SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT);
  g_assert_cmpint(decision.capture.area, ==, 0);

  environment.portal_available = "true";
  environment.portal_window_capable = "1";
  decision = resolve_environment(environment);
  g_assert_cmpint(decision.compositor_supported, ==, 1);
  g_assert_cmpint(decision.backend, ==,
                  SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT);
  g_assert_cmpint(decision.portal_available, ==, 1);
  g_assert_cmpint(decision.portal_window_capable, ==, 1);
  g_assert_cmpint(decision.capture.area, ==, 1);
  g_assert_cmpint(decision.capture.window, ==, 0);
}

static void test_wlroots_backend_selection(void) {
  const ShaulaRuntimeDecision decision =
      resolve_environment(base_environment("sway"));

  g_assert_cmpint(decision.compositor_supported, ==, 1);
  g_assert_cmpint(decision.overlay_supported, ==, 1);
  g_assert_cmpint(decision.backend, ==,
                  SHAULA_BACKEND_KIND_GRIM_WLROOTS);
}

static void test_mode_and_fallback_policy(void) {
  const ShaulaCaptureModes capture = {1, 1, 1, 0};
  static const struct {
    const char *mode;
    int32_t expected;
  } cases[] = {
      {"quick", 1},       {"area", 1},       {"all-in-one", 1},
      {"fullscreen", 1}, {"focused", 1},    {"all-screens", 1},
      {"window", 0},      {"previous-area", 0}, {"AREA", 0},
      {" area", 0},
  };
  size_t index;

  for (index = 0U; index < G_N_ELEMENTS(cases); index += 1U) {
    g_assert_cmpint(shaula_capabilities_mode_supported(
                        capture, span_from_string(cases[index].mode)),
                    ==, cases[index].expected);
  }
  g_assert_cmpint(shaula_capabilities_mode_supported(
                      (ShaulaCaptureModes){2, 1, 1, 0},
                      span_from_string("area")),
                  ==, -1);
  g_assert_cmpint(shaula_capabilities_mode_supported(
                      capture, (ShaulaEnvSpan){NULL, 1U}),
                  ==, -1);

  g_assert_cmpuint(shaula_capabilities_fallback_count(
                       SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT),
                   ==, 1U);
  g_assert_cmpint(shaula_capabilities_fallback_at(
                      SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT, 0U),
                  ==, SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT);
  g_assert_cmpuint(shaula_capabilities_fallback_count(
                       SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT),
                   ==, 0U);
  g_assert_cmpint(shaula_capabilities_fallback_at(
                      SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT, 0U),
                  ==, SHAULA_BACKEND_KIND_INVALID);
}

static void test_decision_policy_helpers(void) {
  ShaulaRuntimeDecision decision =
      resolve_environment(base_environment("niri"));

  g_assert_cmpint(shaula_capabilities_uses_portal_backend(decision), ==, 0);
  g_assert_cmpint(shaula_capabilities_degraded_backend(decision), ==, 0);
  g_assert_cmpint(
      shaula_capabilities_should_bypass_overlay_selection(decision), ==, 0);
  g_assert_cmpint(
      shaula_capabilities_portal_selection_available(decision), ==, 0);
  g_assert_cmpint(shaula_capabilities_previous_area_supported(decision), ==,
                  1);

  g_assert_cmpint(shaula_capabilities_select_portal_fallback(&decision), ==,
                  SHAULA_CAPABILITIES_STATUS_OK);
  g_assert_cmpint(decision.backend, ==,
                  SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT);
  g_assert_cmpint(shaula_capabilities_uses_portal_backend(decision), ==, 1);
  g_assert_cmpint(shaula_capabilities_degraded_backend(decision), ==, 1);
  g_assert_cmpint(
      shaula_capabilities_should_bypass_overlay_selection(decision), ==, 1);
  g_assert_cmpint(
      shaula_capabilities_portal_selection_available(decision), ==, 1);
  g_assert_cmpint(shaula_capabilities_previous_area_supported(decision), ==,
                  0);

  decision.backend = SHAULA_BACKEND_KIND_INVALID;
  g_assert_cmpint(shaula_capabilities_uses_portal_backend(decision), ==, -1);
  g_assert_cmpint(shaula_capabilities_select_portal_fallback(&decision), ==,
                  SHAULA_CAPABILITIES_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(shaula_capabilities_select_portal_fallback(NULL), ==,
                  SHAULA_CAPABILITIES_STATUS_INVALID_ARGUMENT);
}

static void test_portal_probe_process_contract(void) {
  static const char script[] =
      "#!/bin/sh\n"
      "printf '%s\\n' \"$@\" >> \"$SHAULA_TEST_GDBUS_LOG\"\n"
      "last=\n"
      "for arg in \"$@\"; do last=$arg; done\n"
      "case \"$last\" in\n"
      "  version) printf \"('version', <uint32 1>)\\n\" ;;\n"
      "  AvailableTargets) printf \"('targets', <uint32 10>)\\n\" ;;\n"
      "  *) exit 7 ;;\n"
      "esac\n";
  g_autofree char *directory = g_dir_make_tmp("shaula-capabilities-XXXXXX", NULL);
  g_autofree char *binary = NULL;
  g_autofree char *log_path = NULL;
  g_autofree char *old_path = g_strdup(g_getenv("PATH"));
  g_autofree char *old_log = g_strdup(g_getenv("SHAULA_TEST_GDBUS_LOG"));
  g_autofree char *log_contents = NULL;
  gsize log_length = 0U;
  ShaulaCapabilitiesEnvironment environment = base_environment("gnome");
  ShaulaRuntimeDecision decision;

  g_assert_nonnull(directory);
  binary = g_build_filename(directory, "gdbus", NULL);
  log_path = g_build_filename(directory, "argv.log", NULL);
  g_assert_true(g_file_set_contents(binary, script, -1, NULL));
  g_assert_cmpint(g_chmod(binary, S_IRUSR | S_IWUSR | S_IXUSR), ==, 0);
  g_assert_true(g_setenv("PATH", directory, TRUE));
  g_assert_true(g_setenv("SHAULA_TEST_GDBUS_LOG", log_path, TRUE));

  environment.portal_available = NULL;
  environment.portal_window_capable = NULL;
  decision = resolve_environment(environment);
  g_assert_cmpint(decision.portal_available, ==, 1);
  g_assert_cmpint(decision.portal_window_capable, ==, 1);
  g_assert_cmpint(decision.compositor_supported, ==, 1);
  g_assert_true(g_file_get_contents(log_path, &log_contents, &log_length, NULL));
  g_assert_nonnull(strstr(log_contents, "org.freedesktop.DBus.Properties.Get"));
  g_assert_nonnull(strstr(log_contents, "org.freedesktop.portal.Screenshot"));
  g_assert_nonnull(strstr(log_contents, "version"));
  g_assert_nonnull(strstr(log_contents, "AvailableTargets"));

  if (old_path != NULL) {
    g_assert_true(g_setenv("PATH", old_path, TRUE));
  } else {
    g_unsetenv("PATH");
  }
  if (old_log != NULL) {
    g_assert_true(g_setenv("SHAULA_TEST_GDBUS_LOG", old_log, TRUE));
  } else {
    g_unsetenv("SHAULA_TEST_GDBUS_LOG");
  }
  g_assert_cmpint(g_remove(log_path), ==, 0);
  g_assert_cmpint(g_remove(binary), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/capabilities/abi-and-labels", test_abi_and_backend_labels);
  g_test_add_func("/capabilities/invalid-arguments",
                  test_invalid_arguments_reset_output);
  g_test_add_func("/capabilities/niri", test_niri_runtime_decision);
  g_test_add_func("/capabilities/backend-overrides",
                  test_backend_override_precedence);
  g_test_add_func("/capabilities/force-portal-and-stub",
                  test_force_portal_and_stub_capture_modes);
  g_test_add_func("/capabilities/generic-wayland",
                  test_generic_wayland_portal_gating);
  g_test_add_func("/capabilities/wlroots", test_wlroots_backend_selection);
  g_test_add_func("/capabilities/mode-and-fallback-policy",
                  test_mode_and_fallback_policy);
  g_test_add_func("/capabilities/decision-policy",
                  test_decision_policy_helpers);
  g_test_add_func("/capabilities/portal-probe-process",
                  test_portal_probe_process_contract);

  return g_test_run();
}
