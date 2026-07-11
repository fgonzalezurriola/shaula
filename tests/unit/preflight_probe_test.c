#include "preflight/probe.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

static ShaulaJsonSpan json_span(const char *value) {
  return (ShaulaJsonSpan){(const uint8_t *)value, strlen(value)};
}

static ShaulaCapabilitiesEnvironment environment_for(const char *compositor,
                                                      const char *wayland) {
  ShaulaCapabilitiesEnvironment environment = {0};
  environment.compositor.shaula_compositor = compositor;
  environment.compositor.wayland_display = wayland;
  environment.portal_available = "0";
  environment.portal_window_capable = "0";
  return environment;
}

static void assert_output_equal(const ShaulaPreflightOutput *output,
                                uint8_t expected_exit,
                                const char *expected_json) {
  g_assert_cmpuint(output->exit_code, ==, expected_exit);
  g_assert_nonnull(output->json.data);
  g_assert_cmpuint(output->json.length, ==, strlen(expected_json));
  g_assert_cmpmem(output->json.data, output->json.length, expected_json,
                  strlen(expected_json));
  g_assert_cmpuint(output->json.data[output->json.length], ==, 0U);
}

static void test_abi_init_and_clear(void) {
  ShaulaPreflightOutput output;

  g_assert_cmpuint(sizeof(ShaulaPreflightStatus), ==, 4U);
  g_assert_cmpint(SHAULA_PREFLIGHT_STATUS_OK, ==, 0);
  g_assert_cmpint(SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT, ==, 1);
  g_assert_cmpint(SHAULA_PREFLIGHT_STATUS_SIZE_OVERFLOW, ==, 2);
  g_assert_cmpint(SHAULA_PREFLIGHT_STATUS_OUT_OF_MEMORY, ==, 3);
  g_assert_cmpint(SHAULA_PREFLIGHT_STATUS_TIMESTAMP_OUT_OF_RANGE, ==, 4);
  g_assert_cmpint(SHAULA_PREFLIGHT_STATUS_INTERNAL_ERROR, ==, 5);

  memset(&output, 0x7f, sizeof(output));
  shaula_preflight_output_init(&output);
  g_assert_null(output.json.data);
  g_assert_cmpuint(output.json.length, ==, 0U);
  g_assert_cmpuint(output.exit_code, ==, 0U);

  shaula_preflight_output_clear(&output);
  shaula_preflight_output_clear(&output);
  g_assert_null(output.json.data);
  g_assert_cmpuint(output.json.length, ==, 0U);
  g_assert_cmpuint(output.exit_code, ==, 0U);
}

static void test_invalid_arguments_reset_output(void) {
  ShaulaPreflightOutput output = {0};
  ShaulaCapabilitiesEnvironment environment = environment_for("niri", "wayland-1");

  output.json.data = g_memdup2("old", 4U);
  output.json.length = 3U;
  output.exit_code = 99U;
  g_assert_cmpint(shaula_preflight_build(NULL, 0, json_span("portal_fallback"),
                                         &output),
                  ==, SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT);
  g_assert_null(output.json.data);
  g_assert_cmpuint(output.json.length, ==, 0U);
  g_assert_cmpuint(output.exit_code, ==, 0U);

  g_assert_cmpint(
      shaula_preflight_build(&environment, 0,
                             (ShaulaJsonSpan){NULL, 1U}, &output),
      ==, SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT);
  g_assert_null(output.json.data);

  g_assert_cmpint(shaula_preflight_build(&environment, 0,
                                         json_span("portal_fallback"), NULL),
                  ==, SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT);
}

static void test_unsupported_compositor_error(void) {
  ShaulaPreflightOutput output = {0};
  ShaulaCapabilitiesEnvironment environment =
      environment_for("unsupported-\"", NULL);
  const char *expected =
      "{\"ok\":false,\"contract_version\":\"1.0.0\",\"command\":\"preflight\","
      "\"timestamp\":\"1970-01-01T00:00:00Z\",\"error\":{\"code\":"
      "\"ERR_UNSUPPORTED_COMPOSITOR\",\"message\":\"unsupported compositor "
      "for shaula v1\",\"retryable\":false,\"details\":{"
      "\"detected_compositor\":\"unsupported-\\\"\"}},"
      "\"warnings\":[]}\n";

  g_assert_cmpint(shaula_preflight_build(&environment, 0,
                                         json_span("portal_fallback"), &output),
                  ==, SHAULA_PREFLIGHT_STATUS_OK);
  assert_output_equal(&output, 10U, expected);
  shaula_preflight_output_clear(&output);
}

static void test_missing_wayland_error_and_escaping(void) {
  ShaulaPreflightOutput output = {0};
  ShaulaCapabilitiesEnvironment environment = environment_for("sway", NULL);
  const char *expected =
      "{\"ok\":false,\"contract_version\":\"1.0.0\",\"command\":\"preflight\","
      "\"timestamp\":\"1970-01-01T00:00:00Z\",\"error\":{\"code\":"
      "\"ERR_PREFLIGHT_ENV_NOT_READY\",\"message\":\"wayland environment is "
      "not ready\",\"retryable\":true,\"details\":{"
      "\"detected_compositor\":\"sway\"}},\"warnings\":[]}\n";

  g_assert_cmpint(shaula_preflight_build(&environment, 0,
                                         json_span("portal_fallback"), &output),
                  ==, SHAULA_PREFLIGHT_STATUS_OK);
  assert_output_equal(&output, 11U, expected);
  shaula_preflight_output_clear(&output);
}

static void test_niri_success(void) {
  ShaulaPreflightOutput output = {0};
  ShaulaCapabilitiesEnvironment environment = environment_for("niri", "wayland-1");
  const char *expected =
      "{\"ok\":true,\"contract_version\":\"1.0.0\",\"command\":\"preflight\","
      "\"timestamp\":\"1970-01-01T00:00:00Z\",\"compositor\":\"niri\","
      "\"ready\":true,\"result\":{\"compositor\":\"niri\",\"wayland\":true,"
      "\"backend\":\"niri-wayland-direct\",\"portal_available\":false},"
      "\"warnings\":[]}\n";

  g_assert_cmpint(shaula_preflight_build(&environment, 0,
                                         json_span("portal_fallback"), &output),
                  ==, SHAULA_PREFLIGHT_STATUS_OK);
  assert_output_equal(&output, 0U, expected);
  shaula_preflight_output_clear(&output);
}

static void test_present_empty_wayland_is_ready(void) {
  ShaulaPreflightOutput output = {0};
  ShaulaCapabilitiesEnvironment environment = environment_for("sway", "");

  g_assert_cmpint(shaula_preflight_build(&environment, 0,
                                         json_span("portal_fallback"), &output),
                  ==, SHAULA_PREFLIGHT_STATUS_OK);
  g_assert_cmpuint(output.exit_code, ==, 0U);
  g_assert_nonnull(g_strstr_len((const char *)output.json.data,
                                (gssize)output.json.length,
                                "\"backend\":\"grim-wlroots\""));
  shaula_preflight_output_clear(&output);
}

static void test_portal_success_and_warning(void) {
  ShaulaPreflightOutput output = {0};
  ShaulaCapabilitiesEnvironment environment = environment_for("gnome", "wayland-0");
  const char *expected =
      "{\"ok\":true,\"contract_version\":\"1.0.0\",\"command\":\"preflight\","
      "\"timestamp\":\"1970-01-01T00:00:00Z\",\"compositor\":\"gnome\","
      "\"ready\":true,\"result\":{\"compositor\":\"gnome\",\"wayland\":true,"
      "\"backend\":\"portal-screenshot\",\"portal_available\":true},"
      "\"warnings\":[\"portal_fallback\"]}\n";

  environment.portal_available = "1";
  g_assert_cmpint(shaula_preflight_build(&environment, 0,
                                         json_span("portal_fallback"), &output),
                  ==, SHAULA_PREFLIGHT_STATUS_OK);
  assert_output_equal(&output, 0U, expected);
  shaula_preflight_output_clear(&output);
}

static void test_output_replacement_and_timestamp_failure(void) {
  ShaulaPreflightOutput output = {0};
  ShaulaCapabilitiesEnvironment environment = environment_for("niri", "wayland-1");

  g_assert_cmpint(shaula_preflight_build(&environment, 0,
                                         json_span("portal_fallback"), &output),
                  ==, SHAULA_PREFLIGHT_STATUS_OK);
  g_assert_nonnull(output.json.data);

  environment.compositor.shaula_compositor = "unsupported";
  environment.compositor.wayland_display = NULL;
  g_assert_cmpint(shaula_preflight_build(&environment, 0,
                                         json_span("portal_fallback"), &output),
                  ==, SHAULA_PREFLIGHT_STATUS_OK);
  g_assert_cmpuint(output.exit_code, ==, 10U);

  environment = environment_for("niri", "wayland-1");
  g_assert_cmpint(shaula_preflight_build(&environment, INT64_MAX,
                                         json_span("portal_fallback"), &output),
                  ==, SHAULA_PREFLIGHT_STATUS_TIMESTAMP_OUT_OF_RANGE);
  g_assert_null(output.json.data);
  g_assert_cmpuint(output.json.length, ==, 0U);
  g_assert_cmpuint(output.exit_code, ==, 0U);
  shaula_preflight_output_clear(&output);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/preflight/abi-init-clear", test_abi_init_and_clear);
  g_test_add_func("/preflight/invalid-arguments", test_invalid_arguments_reset_output);
  g_test_add_func("/preflight/unsupported", test_unsupported_compositor_error);
  g_test_add_func("/preflight/missing-wayland", test_missing_wayland_error_and_escaping);
  g_test_add_func("/preflight/niri-success", test_niri_success);
  g_test_add_func("/preflight/present-empty-wayland", test_present_empty_wayland_is_ready);
  g_test_add_func("/preflight/portal-success", test_portal_success_and_warning);
  g_test_add_func("/preflight/replacement-timestamp", test_output_replacement_and_timestamp_failure);
  return g_test_run();
}
