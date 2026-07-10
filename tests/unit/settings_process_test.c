#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "settings_config.h"
#include "settings_process.h"

static void assert_argv(char *const *actual, const char *const *expected) {
  gsize index = 0;
  while (expected[index] != NULL) {
    g_assert_nonnull(actual[index]);
    g_assert_cmpstr(actual[index], ==, expected[index]);
    index++;
  }
  g_assert_null(actual[index]);
}

static void test_build_save_argv_exact(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  config.region_mode = REGION_LIVE;
  config.window_mode = WINDOW_MAXIMIZED_TO_EDGES;
  config.focused = FALSE;
  config.close_preview_on_save = FALSE;
  config.width = 1234;
  config.height = 777;
  g_free(config.column_display);
  config.column_display = g_strdup("custom columns;$(touch nope)");
  config.floating_x_set = TRUE;
  config.floating_y_set = FALSE;
  config.floating_x = -45;
  config.floating_y = 67;
  g_free(config.floating_relative_to);
  config.floating_relative_to = g_strdup("bottom-right");
  config.quick_skip_preview = TRUE;
  config.quick_copy = FALSE;
  config.quick_save = TRUE;
  config.area_skip_preview = TRUE;
  config.area_copy = TRUE;
  config.area_save = FALSE;
  config.fullscreen_skip_preview = FALSE;
  config.fullscreen_copy = FALSE;
  config.fullscreen_save = TRUE;
  config.all_screens_skip_preview = FALSE;
  config.all_screens_copy = TRUE;
  config.all_screens_save = FALSE;
  g_free(config.save_folder);
  config.save_folder = g_strdup("~/Pictures/Screen shots/á;$(touch nope)");
  config.notifications_success = FALSE;
  config.notifications_errors = TRUE;
  config.notifications_thumbnails = FALSE;

  g_auto(GStrv) argv =
      shaula_settings_build_save_argv("/tmp/shaula binary;literal", &config);
  const char *expected[] = {
      "/tmp/shaula binary;literal",
      "config",
      "save",
      "--json",
      "--region-mode",
      "live",
      "--preview-mode",
      "maximized-to-edges",
      "--focused",
      "false",
      "--close-preview-on-save",
      "false",
      "--width",
      "1234",
      "--height",
      "777",
      "--default-column-display",
      "custom columns;$(touch nope)",
      "--floating-x",
      "-45",
      "--floating-y",
      "null",
      "--floating-relative-to",
      "bottom-right",
      "--after-quick-skip-preview",
      "true",
      "--after-quick-copy",
      "false",
      "--after-quick-save",
      "true",
      "--after-area-skip-preview",
      "true",
      "--after-area-copy",
      "true",
      "--after-area-save",
      "false",
      "--after-fullscreen-skip-preview",
      "false",
      "--after-fullscreen-copy",
      "false",
      "--after-fullscreen-save",
      "true",
      "--after-all-screens-skip-preview",
      "false",
      "--after-all-screens-copy",
      "true",
      "--after-all-screens-save",
      "false",
      "--save-folder",
      "~/Pictures/Screen shots/á;$(touch nope)",
      "--notifications-success",
      "false",
      "--notifications-errors",
      "true",
      "--notifications-thumbnails",
      "false",
      "--apply-niri",
      NULL,
  };
  assert_argv(argv, expected);
  shaula_settings_config_clear(&config);
}

static void test_build_save_argv_null_string_fallbacks(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  g_clear_pointer(&config.column_display, g_free);
  g_clear_pointer(&config.floating_relative_to, g_free);
  g_clear_pointer(&config.save_folder, g_free);

  g_auto(GStrv) argv = shaula_settings_build_save_argv("shaula", &config);
  gboolean saw_display = FALSE;
  gboolean saw_relative = FALSE;
  gboolean saw_folder = FALSE;
  for (gsize index = 0; argv[index] != NULL; index++) {
    if (g_strcmp0(argv[index], "--default-column-display") == 0) {
      g_assert_cmpstr(argv[index + 1], ==, "normal");
      saw_display = TRUE;
    } else if (g_strcmp0(argv[index], "--floating-relative-to") == 0) {
      g_assert_cmpstr(argv[index + 1], ==, "top-left");
      saw_relative = TRUE;
    } else if (g_strcmp0(argv[index], "--save-folder") == 0) {
      g_assert_cmpstr(argv[index + 1], ==, "");
      saw_folder = TRUE;
    }
  }
  g_assert_true(saw_display);
  g_assert_true(saw_relative);
  g_assert_true(saw_folder);
  shaula_settings_config_clear(&config);
}

static char *write_fake_command(const char *directory) {
  const char script[] = "#!/bin/sh\n"
                        ": > \"$SHAULA_TEST_ARGV\"\n"
                        "for arg in \"$@\"; do printf '%s\\000' \"$arg\" >> "
                        "\"$SHAULA_TEST_ARGV\"; done\n"
                        "printf '%s' \"${SHAULA_TEST_STDOUT:-}\"\n"
                        "printf '%s' \"${SHAULA_TEST_STDERR:-}\" >&2\n"
                        "if [ -n \"${SHAULA_TEST_SIGNAL:-}\" ]; then kill "
                        "-\"$SHAULA_TEST_SIGNAL\" \"$$\"; fi\n"
                        "exit \"${SHAULA_TEST_EXIT:-0}\"\n";
  g_autofree char *path = g_build_filename(directory, "fake command", NULL);
  GError *error = NULL;
  g_assert_true(g_file_set_contents(path, script, -1, &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(path, 0700), ==, 0);
  return g_steal_pointer(&path);
}

static void assert_file_bytes(const char *path, const void *expected,
                              gsize expected_length) {
  GError *error = NULL;
  g_autofree char *actual = NULL;
  gsize actual_length = 0;
  g_assert_true(g_file_get_contents(path, &actual, &actual_length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(actual_length, ==, expected_length);
  g_assert_cmpmem(actual, actual_length, expected, expected_length);
}

static void test_run_command_success_and_exact_argv(void) {
  GError *error = NULL;
  g_autofree char *directory =
      g_dir_make_tmp("shaula-settings-process-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *command = write_fake_command(directory);
  g_autofree char *argv_log = g_build_filename(directory, "argv", NULL);

  g_setenv("SHAULA_TEST_ARGV", argv_log, TRUE);
  g_setenv("SHAULA_TEST_STDOUT", "stdout payload", TRUE);
  g_setenv("SHAULA_TEST_STDERR", "stderr payload", TRUE);
  g_setenv("SHAULA_TEST_EXIT", "0", TRUE);
  g_unsetenv("SHAULA_TEST_SIGNAL");

  char *argv[] = {command, "literal with spaces", "$(touch nope);|&", NULL};
  g_autofree gchar *stdout_text = g_strdup("stale stdout");
  g_autofree gchar *stderr_text = g_strdup("stale stderr");
  int exit_code = -1;
  g_assert_true(shaula_settings_run_command(argv, &stdout_text, &stderr_text,
                                            &exit_code));
  g_assert_cmpint(exit_code, ==, 0);
  g_assert_cmpstr(stdout_text, ==, "stdout payload");
  g_assert_cmpstr(stderr_text, ==, "stderr payload");
  static const char expected[] = "literal with spaces\0$(touch nope);|&\0";
  assert_file_bytes(argv_log, expected, sizeof(expected) - 1);

  g_unsetenv("SHAULA_TEST_ARGV");
  g_unsetenv("SHAULA_TEST_STDOUT");
  g_unsetenv("SHAULA_TEST_STDERR");
  g_unsetenv("SHAULA_TEST_EXIT");
  g_unsetenv("SHAULA_TEST_SIGNAL");
  g_assert_cmpint(g_remove(argv_log), ==, 0);
  g_assert_cmpint(g_remove(command), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void test_run_command_nonzero_and_signal(void) {
  GError *error = NULL;
  g_autofree char *directory =
      g_dir_make_tmp("shaula-settings-process-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *command = write_fake_command(directory);
  g_autofree char *argv_log = g_build_filename(directory, "argv", NULL);
  g_setenv("SHAULA_TEST_ARGV", argv_log, TRUE);
  g_setenv("SHAULA_TEST_EXIT", "23", TRUE);
  g_unsetenv("SHAULA_TEST_STDOUT");
  g_unsetenv("SHAULA_TEST_STDERR");
  g_unsetenv("SHAULA_TEST_SIGNAL");

  char *argv[] = {command, NULL};
  int exit_code = -1;
  g_assert_true(shaula_settings_run_command(argv, NULL, NULL, &exit_code));
  g_assert_cmpint(exit_code, ==, 1);

  g_setenv("SHAULA_TEST_EXIT", "0", TRUE);
  g_setenv("SHAULA_TEST_SIGNAL", "TERM", TRUE);
  exit_code = -1;
  g_assert_true(shaula_settings_run_command(argv, NULL, NULL, &exit_code));
  g_assert_cmpint(exit_code, ==, 1);

  g_unsetenv("SHAULA_TEST_ARGV");
  g_unsetenv("SHAULA_TEST_EXIT");
  g_unsetenv("SHAULA_TEST_SIGNAL");
  g_assert_cmpint(g_remove(argv_log), ==, 0);
  g_assert_cmpint(g_remove(command), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void test_run_command_spawn_failure(void) {
  char *argv[] = {"shaula-command-that-does-not-exist", NULL};
  g_autofree gchar *stderr_text = g_strdup("stale");
  int exit_code = -1;
  g_assert_false(
      shaula_settings_run_command(argv, NULL, &stderr_text, &exit_code));
  g_assert_cmpint(exit_code, ==, 127);
  g_assert_nonnull(stderr_text);
  g_assert_nonnull(strstr(stderr_text, "shaula-command-that-does-not-exist"));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/settings-process/build-save-argv-exact",
                  test_build_save_argv_exact);
  g_test_add_func("/settings-process/build-save-argv-null-fallbacks",
                  test_build_save_argv_null_string_fallbacks);
  g_test_add_func("/settings-process/run-success-exact-argv",
                  test_run_command_success_and_exact_argv);
  g_test_add_func("/settings-process/run-nonzero-signal",
                  test_run_command_nonzero_and_signal);
  g_test_add_func("/settings-process/run-spawn-failure",
                  test_run_command_spawn_failure);
  return g_test_run();
}
