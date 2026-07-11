#include "compositor/focused_output.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <string.h>

static gchar *test_directory;
static gchar *test_arguments_path;
static gchar *original_path;
static gchar *fixture_path;

static const char probe_script[] =
    "#!/bin/sh\n"
    "printf '%s' \"$*\" > \"$SHAULA_FOCUSED_TEST_ARGS_FILE\"\n"
    "if [ \"${SHAULA_FOCUSED_TEST_SIGNAL:-0}\" = 1 ]; then\n"
    "  kill -TERM $$\n"
    "fi\n"
    "if [ \"${SHAULA_FOCUSED_TEST_BIG:-0}\" = 1 ]; then\n"
    "  i=0\n"
    "  while [ \"$i\" -lt 9000 ]; do\n"
    "    printf x\n"
    "    i=$((i + 1))\n"
    "  done\n"
    "  exit 0\n"
    "fi\n"
    "printf '%s' \"${SHAULA_FOCUSED_TEST_PAYLOAD-}\"\n"
    "exit \"${SHAULA_FOCUSED_TEST_EXIT:-0}\"\n";

static ShaulaFocusedOutputEnvironment environment_for(const char *label) {
  ShaulaFocusedOutputEnvironment environment = {0};
  environment.compositor.shaula_compositor = label;
  return environment;
}

static void reset_probe_state(void) {
  g_unlink(test_arguments_path);
  g_unsetenv("SHAULA_FOCUSED_TEST_PAYLOAD");
  g_unsetenv("SHAULA_FOCUSED_TEST_EXIT");
  g_unsetenv("SHAULA_FOCUSED_TEST_SIGNAL");
  g_unsetenv("SHAULA_FOCUSED_TEST_BIG");
  g_setenv("PATH", fixture_path, TRUE);
}

static void set_payload(const char *payload) {
  g_assert_true(g_setenv("SHAULA_FOCUSED_TEST_PAYLOAD", payload, TRUE));
}

static ShaulaFocusedOutputResult
resolve(ShaulaFocusedOutputEnvironment environment) {
  ShaulaFocusedOutputResult result = {0};
  shaula_focused_output_result_init(&result);
  g_assert_cmpint(shaula_focused_output_resolve(&environment, &result), ==,
                  SHAULA_FOCUSED_OUTPUT_STATUS_OK);
  return result;
}

static void assert_absent(const ShaulaFocusedOutputResult *result) {
  g_assert_cmpint(result->present, ==, 0);
  g_assert_null(result->name.data);
  g_assert_cmpuint(result->name.length, ==, 0U);
}

static void assert_name(const ShaulaFocusedOutputResult *result,
                        const uint8_t *expected, size_t expected_length) {
  g_assert_cmpint(result->present, ==, 1);
  g_assert_nonnull(result->name.data);
  g_assert_cmpuint(result->name.length, ==, expected_length);
  g_assert_cmpmem(result->name.data, result->name.length, expected,
                  expected_length);
  g_assert_cmpuint(result->name.data[result->name.length], ==, 0U);
}

static void assert_arguments(const char *expected) {
  gchar *contents = NULL;
  gsize length = 0U;
  GError *error = NULL;

  g_assert_true(
      g_file_get_contents(test_arguments_path, &contents, &length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(length, ==, strlen(expected));
  g_assert_cmpmem(contents, length, expected, strlen(expected));
  g_free(contents);
}

static void test_abi_init_clear_and_invalid_arguments(void) {
  ShaulaFocusedOutputResult result = {
      .present = 1,
      .name = {.data = g_memdup2("old", 4U), .length = 3U},
  };
  ShaulaFocusedOutputEnvironment environment = environment_for("x11");

  g_assert_cmpuint(sizeof(ShaulaFocusedOutputStatus), ==, 4U);
  g_assert_cmpint(SHAULA_FOCUSED_OUTPUT_STATUS_OK, ==, 0);
  g_assert_cmpint(SHAULA_FOCUSED_OUTPUT_STATUS_INVALID_ARGUMENT, ==, 1);
  g_assert_cmpint(SHAULA_FOCUSED_OUTPUT_STATUS_OUT_OF_MEMORY, ==, 2);

  shaula_focused_output_result_clear(&result);
  assert_absent(&result);
  shaula_focused_output_result_clear(&result);
  assert_absent(&result);

  g_assert_cmpint(shaula_focused_output_resolve(NULL, &result), ==,
                  SHAULA_FOCUSED_OUTPUT_STATUS_INVALID_ARGUMENT);
  assert_absent(&result);
  g_assert_cmpint(shaula_focused_output_resolve(&environment, NULL), ==,
                  SHAULA_FOCUSED_OUTPUT_STATUS_INVALID_ARGUMENT);

  shaula_focused_output_result_init(NULL);
  shaula_focused_output_result_clear(NULL);
}

static void test_override_precedence_and_replacement(void) {
  static const uint8_t expected[] = "output $name;[]";
  ShaulaFocusedOutputEnvironment environment = environment_for("niri");
  ShaulaFocusedOutputResult result = {0};

  reset_probe_state();
  environment.overlay_output_name = " \toutput $name;[]\r\n";
  g_assert_cmpint(shaula_focused_output_resolve(&environment, &result), ==,
                  SHAULA_FOCUSED_OUTPUT_STATUS_OK);
  assert_name(&result, expected, sizeof(expected) - 1U);
  g_assert_false(g_file_test(test_arguments_path, G_FILE_TEST_EXISTS));

  environment.overlay_output_name = NULL;
  environment.compositor.shaula_compositor = "x11";
  g_assert_cmpint(shaula_focused_output_resolve(&environment, &result), ==,
                  SHAULA_FOCUSED_OUTPUT_STATUS_OK);
  assert_absent(&result);
  shaula_focused_output_result_clear(&result);
}

static void test_empty_override_falls_through(void) {
  static const uint8_t expected[] = "DP-1";
  ShaulaFocusedOutputEnvironment environment = environment_for("niri");
  ShaulaFocusedOutputResult result;

  reset_probe_state();
  environment.overlay_output_name = " \t\r\n";
  set_payload("{\"name\":\"DP-1\"}");
  result = resolve(environment);
  assert_name(&result, expected, sizeof(expected) - 1U);
  assert_arguments("msg -j focused-output");
  shaula_focused_output_result_clear(&result);
}

static void test_unsupported_compositor_does_not_probe(void) {
  ShaulaFocusedOutputEnvironment environment = environment_for("gnome");
  ShaulaFocusedOutputResult result;

  reset_probe_state();
  result = resolve(environment);
  assert_absent(&result);
  g_assert_false(g_file_test(test_arguments_path, G_FILE_TEST_EXISTS));
  shaula_focused_output_result_clear(&result);
}

static void test_niri_valid_and_unknown_fields(void) {
  static const uint8_t expected[] = "DP-2";
  ShaulaFocusedOutputEnvironment environment = environment_for("niri");
  ShaulaFocusedOutputResult result;

  reset_probe_state();
  set_payload(
      "{\"unknown\":{\"duplicate\":1,\"duplicate\":2},"
      "\"name\":\"DP-2\",\"array\":[true,null,1.25e+2]}");
  result = resolve(environment);
  assert_name(&result, expected, sizeof(expected) - 1U);
  assert_arguments("msg -j focused-output");
  shaula_focused_output_result_clear(&result);
}

static void test_niri_escaped_key_unicode_and_embedded_nul(void) {
  static const uint8_t expected_nul[] = {'A', 0U, 'B'};
  static const uint8_t expected_unicode[] = {0xf0U, 0x9fU, 0x98U, 0x80U};
  ShaulaFocusedOutputEnvironment environment = environment_for("NIRI");
  ShaulaFocusedOutputResult result;

  reset_probe_state();
  set_payload("{\"na\\u006de\":\"A\\u0000B\"}");
  result = resolve(environment);
  assert_name(&result, expected_nul, sizeof(expected_nul));
  shaula_focused_output_result_clear(&result);

  set_payload("{\"name\":\"\\uD83D\\uDE00\"}");
  result = resolve(environment);
  assert_name(&result, expected_unicode, sizeof(expected_unicode));
  shaula_focused_output_result_clear(&result);
}

static void test_niri_invalid_documents_are_absent(void) {
  static const char *const cases[] = {
      "",
      "[]",
      "{}",
      "{\"name\":1}",
      "{\"name\":null}",
      "{\"name\":\"\"}",
      "{\"name\":\"A\",\"name\":\"B\"}",
      "{\"name\":\"A\",\"na\\u006de\":\"B\"}",
      "{\"name\":\"\\uD800\"}",
      "{\"name\":\"A\"} trailing",
      "{\"unknown\":[1,] ,\"name\":\"A\"}",
  };
  ShaulaFocusedOutputEnvironment environment = environment_for("niri");
  size_t index;

  reset_probe_state();
  for (index = 0U; index < G_N_ELEMENTS(cases); index += 1U) {
    ShaulaFocusedOutputResult result;
    set_payload(cases[index]);
    result = resolve(environment);
    assert_absent(&result);
    shaula_focused_output_result_clear(&result);
  }
}

static void test_sway_first_focused_output(void) {
  static const uint8_t expected[] = "HDMI-A-1";
  ShaulaFocusedOutputEnvironment environment = environment_for("SWAY");
  ShaulaFocusedOutputResult result;

  reset_probe_state();
  set_payload(
      "[{\"name\":\"DP-1\",\"focused\":false,\"extra\":{}},"
      "{\"name\":\"HDMI-A-1\",\"focused\":true},"
      "{\"name\":\"DP-3\",\"focused\":true}]");
  result = resolve(environment);
  assert_name(&result, expected, sizeof(expected) - 1U);
  assert_arguments("-t get_outputs -r");
  shaula_focused_output_result_clear(&result);
}

static void test_sway_defaults_and_no_match(void) {
  static const char *const cases[] = {
      "[]",
      "[{\"name\":\"DP-1\"}]",
      "[{\"name\":\"DP-1\",\"focused\":false}]",
      "[{\"name\":\"\",\"focused\":true}]",
  };
  ShaulaFocusedOutputEnvironment environment = environment_for("sway");
  size_t index;

  reset_probe_state();
  for (index = 0U; index < G_N_ELEMENTS(cases); index += 1U) {
    ShaulaFocusedOutputResult result;
    set_payload(cases[index]);
    result = resolve(environment);
    assert_absent(&result);
    shaula_focused_output_result_clear(&result);
  }
}

static void test_sway_invalid_documents_are_absent(void) {
  static const char *const cases[] = {
      "{}",
      "[{}]",
      "[{\"focused\":true}]",
      "[{\"name\":1,\"focused\":true}]",
      "[{\"name\":\"A\",\"focused\":1}]",
      "[{\"name\":\"A\",\"name\":\"B\",\"focused\":true}]",
      "[{\"name\":\"A\",\"focused\":true,\"focused\":false}]",
      "[{\"name\":\"A\",\"focused\":true},{\"name\":1}]",
      "[{\"name\":\"A\",\"focused\":true}] trailing",
  };
  ShaulaFocusedOutputEnvironment environment = environment_for("sway");
  size_t index;

  reset_probe_state();
  for (index = 0U; index < G_N_ELEMENTS(cases); index += 1U) {
    ShaulaFocusedOutputResult result;
    set_payload(cases[index]);
    result = resolve(environment);
    assert_absent(&result);
    shaula_focused_output_result_clear(&result);
  }
}

static void test_process_failures_are_advisory_absence(void) {
  ShaulaFocusedOutputEnvironment environment = environment_for("niri");
  ShaulaFocusedOutputResult result;
  gchar *empty_path;
  GError *error = NULL;

  reset_probe_state();
  g_assert_true(g_setenv("SHAULA_FOCUSED_TEST_EXIT", "7", TRUE));
  result = resolve(environment);
  assert_absent(&result);
  shaula_focused_output_result_clear(&result);

  reset_probe_state();
  g_assert_true(g_setenv("SHAULA_FOCUSED_TEST_SIGNAL", "1", TRUE));
  result = resolve(environment);
  assert_absent(&result);
  shaula_focused_output_result_clear(&result);

  reset_probe_state();
  g_assert_true(g_setenv("SHAULA_FOCUSED_TEST_BIG", "1", TRUE));
  result = resolve(environment);
  assert_absent(&result);
  shaula_focused_output_result_clear(&result);

  reset_probe_state();
  empty_path = g_build_filename(test_directory, "empty", NULL);
  g_assert_cmpint(g_mkdir(empty_path, 0700), ==, 0);
  g_assert_true(g_setenv("PATH", empty_path, TRUE));
  result = resolve(environment);
  assert_absent(&result);
  shaula_focused_output_result_clear(&result);
  g_assert_cmpint(g_rmdir(empty_path), ==, 0);
  g_free(empty_path);
  g_clear_error(&error);
}

static void create_probe(const char *name) {
  gchar *path = g_build_filename(test_directory, name, NULL);
  GError *error = NULL;

  g_assert_true(g_file_set_contents(path, probe_script, -1, &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(path, 0700), ==, 0);
  g_free(path);
}

static void setup_fixtures(void) {
  GError *error = NULL;
  const char *path = g_getenv("PATH");

  test_directory = g_dir_make_tmp("shaula-focused-output-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(test_directory);
  test_arguments_path =
      g_build_filename(test_directory, "arguments.txt", NULL);
  original_path = g_strdup(path != NULL ? path : "");
  fixture_path = g_strdup_printf("%s:%s", test_directory, original_path);
  create_probe("niri");
  create_probe("swaymsg");
  g_assert_true(g_setenv("SHAULA_FOCUSED_TEST_ARGS_FILE", test_arguments_path,
                         TRUE));
  reset_probe_state();
}

static void teardown_fixtures(void) {
  gchar *niri = g_build_filename(test_directory, "niri", NULL);
  gchar *swaymsg = g_build_filename(test_directory, "swaymsg", NULL);

  g_unsetenv("SHAULA_FOCUSED_TEST_PAYLOAD");
  g_unsetenv("SHAULA_FOCUSED_TEST_EXIT");
  g_unsetenv("SHAULA_FOCUSED_TEST_SIGNAL");
  g_unsetenv("SHAULA_FOCUSED_TEST_BIG");
  g_unsetenv("SHAULA_FOCUSED_TEST_ARGS_FILE");
  g_setenv("PATH", original_path, TRUE);
  g_unlink(niri);
  g_unlink(swaymsg);
  g_unlink(test_arguments_path);
  g_rmdir(test_directory);
  g_free(niri);
  g_free(swaymsg);
  g_free(fixture_path);
  g_free(original_path);
  g_free(test_arguments_path);
  g_free(test_directory);
}

int main(int argc, char **argv) {
  int result;

  g_test_init(&argc, &argv, NULL);
  setup_fixtures();
  g_test_add_func("/compositor/focused-output/abi-init-clear-invalid",
                  test_abi_init_clear_and_invalid_arguments);
  g_test_add_func("/compositor/focused-output/override-precedence",
                  test_override_precedence_and_replacement);
  g_test_add_func("/compositor/focused-output/empty-override",
                  test_empty_override_falls_through);
  g_test_add_func("/compositor/focused-output/unsupported-no-probe",
                  test_unsupported_compositor_does_not_probe);
  g_test_add_func("/compositor/focused-output/niri-valid-unknown",
                  test_niri_valid_and_unknown_fields);
  g_test_add_func("/compositor/focused-output/niri-escaped-unicode-nul",
                  test_niri_escaped_key_unicode_and_embedded_nul);
  g_test_add_func("/compositor/focused-output/niri-invalid",
                  test_niri_invalid_documents_are_absent);
  g_test_add_func("/compositor/focused-output/sway-first-focused",
                  test_sway_first_focused_output);
  g_test_add_func("/compositor/focused-output/sway-defaults-no-match",
                  test_sway_defaults_and_no_match);
  g_test_add_func("/compositor/focused-output/sway-invalid",
                  test_sway_invalid_documents_are_absent);
  g_test_add_func("/compositor/focused-output/process-fallbacks",
                  test_process_failures_are_advisory_absence);
  result = g_test_run();
  teardown_fixtures();
  return result;
}
