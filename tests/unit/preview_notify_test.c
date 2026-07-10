#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "preview_notify.h"

static const char fake_notify_send_script[] =
    "#!/bin/sh\n"
    "count=0\n"
    "if [ -f \"$SHAULA_TEST_COUNT\" ]; then "
    "IFS= read -r count < \"$SHAULA_TEST_COUNT\"; fi\n"
    "count=$((count + 1))\n"
    "printf '%s' \"$count\" > \"$SHAULA_TEST_COUNT\"\n"
    "prefix=\"$SHAULA_TEST_ROOT/call-$count\"\n"
    "printf '%s\\n' \"$#\" > \"$prefix.argc\"\n"
    ": > \"$prefix.argv\"\n"
    "for arg in \"$@\"; do printf '%s\\000' \"$arg\" >> "
    "\"$prefix.argv\"; done\n"
    "if [ -n \"$SHAULA_TEST_CHILD_STDOUT\" ]; then "
    "printf '%s' \"$SHAULA_TEST_CHILD_STDOUT\"; fi\n"
    "if [ -n \"$SHAULA_TEST_CHILD_STDERR\" ]; then "
    "printf '%s' \"$SHAULA_TEST_CHILD_STDERR\" >&2; fi\n"
    "case \"$count\" in\n"
    "1) code=\"${SHAULA_TEST_EXIT_1:-0}\"; "
    "signal=\"${SHAULA_TEST_SIGNAL_1:-}\" ;;\n"
    "2) code=\"${SHAULA_TEST_EXIT_2:-0}\"; "
    "signal=\"${SHAULA_TEST_SIGNAL_2:-}\" ;;\n"
    "*) code=0; signal= ;;\n"
    "esac\n"
    "if [ -n \"$signal\" ]; then kill -\"$signal\" \"$$\"; fi\n"
    "exit \"$code\"\n";

typedef struct {
  char *tmp_dir;
  char *notify_send;
  char *count_path;
  char *old_path;
  char *old_root;
  char *old_count;
  char *old_exit_1;
  char *old_exit_2;
  char *old_signal_1;
  char *old_signal_2;
  char *old_stdout;
  char *old_stderr;
} NotifyFixture;

static void restore_environment(const char *name, const char *value) {
  if (value != NULL)
    g_setenv(name, value, TRUE);
  else
    g_unsetenv(name);
}

static void remove_tree(const char *path) {
  if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
    g_remove(path);
    return;
  }

  GDir *directory = g_dir_open(path, 0, NULL);
  if (directory != NULL) {
    const char *name = NULL;
    while ((name = g_dir_read_name(directory)) != NULL) {
      g_autofree char *child = g_build_filename(path, name, NULL);
      remove_tree(child);
    }
    g_dir_close(directory);
  }
  g_rmdir(path);
}

static void write_executable(const char *path, const char *contents) {
  GError *error = NULL;
  g_assert_true(g_file_set_contents(path, contents, -1, &error));
  g_assert_no_error(error);
  g_assert_cmpint(g_chmod(path, 0700), ==, 0);
}

static void notify_fixture_setup(NotifyFixture *fixture,
                                 gconstpointer user_data) {
  (void)user_data;
  GError *error = NULL;
  fixture->tmp_dir = g_dir_make_tmp("shaula-notify-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(fixture->tmp_dir);

  fixture->notify_send =
      g_build_filename(fixture->tmp_dir, "notify-send", NULL);
  fixture->count_path = g_build_filename(fixture->tmp_dir, "count", NULL);
  write_executable(fixture->notify_send, fake_notify_send_script);

  fixture->old_path = g_strdup(g_getenv("PATH"));
  fixture->old_root = g_strdup(g_getenv("SHAULA_TEST_ROOT"));
  fixture->old_count = g_strdup(g_getenv("SHAULA_TEST_COUNT"));
  fixture->old_exit_1 = g_strdup(g_getenv("SHAULA_TEST_EXIT_1"));
  fixture->old_exit_2 = g_strdup(g_getenv("SHAULA_TEST_EXIT_2"));
  fixture->old_signal_1 = g_strdup(g_getenv("SHAULA_TEST_SIGNAL_1"));
  fixture->old_signal_2 = g_strdup(g_getenv("SHAULA_TEST_SIGNAL_2"));
  fixture->old_stdout = g_strdup(g_getenv("SHAULA_TEST_CHILD_STDOUT"));
  fixture->old_stderr = g_strdup(g_getenv("SHAULA_TEST_CHILD_STDERR"));

  g_setenv("PATH", fixture->tmp_dir, TRUE);
  g_setenv("SHAULA_TEST_ROOT", fixture->tmp_dir, TRUE);
  g_setenv("SHAULA_TEST_COUNT", fixture->count_path, TRUE);
  g_unsetenv("SHAULA_TEST_EXIT_1");
  g_unsetenv("SHAULA_TEST_EXIT_2");
  g_unsetenv("SHAULA_TEST_SIGNAL_1");
  g_unsetenv("SHAULA_TEST_SIGNAL_2");
  g_unsetenv("SHAULA_TEST_CHILD_STDOUT");
  g_unsetenv("SHAULA_TEST_CHILD_STDERR");
}

static void notify_fixture_teardown(NotifyFixture *fixture,
                                    gconstpointer user_data) {
  (void)user_data;
  restore_environment("PATH", fixture->old_path);
  restore_environment("SHAULA_TEST_ROOT", fixture->old_root);
  restore_environment("SHAULA_TEST_COUNT", fixture->old_count);
  restore_environment("SHAULA_TEST_EXIT_1", fixture->old_exit_1);
  restore_environment("SHAULA_TEST_EXIT_2", fixture->old_exit_2);
  restore_environment("SHAULA_TEST_SIGNAL_1", fixture->old_signal_1);
  restore_environment("SHAULA_TEST_SIGNAL_2", fixture->old_signal_2);
  restore_environment("SHAULA_TEST_CHILD_STDOUT", fixture->old_stdout);
  restore_environment("SHAULA_TEST_CHILD_STDERR", fixture->old_stderr);

  remove_tree(fixture->tmp_dir);
  g_free(fixture->old_stderr);
  g_free(fixture->old_stdout);
  g_free(fixture->old_signal_2);
  g_free(fixture->old_signal_1);
  g_free(fixture->old_exit_2);
  g_free(fixture->old_exit_1);
  g_free(fixture->old_count);
  g_free(fixture->old_root);
  g_free(fixture->old_path);
  g_free(fixture->count_path);
  g_free(fixture->notify_send);
  g_free(fixture->tmp_dir);
}

static char *call_file_path(const NotifyFixture *fixture, guint call,
                            const char *suffix) {
  return g_strdup_printf("%s/call-%u.%s", fixture->tmp_dir, call, suffix);
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

static guint call_count(const NotifyFixture *fixture) {
  if (!g_file_test(fixture->count_path, G_FILE_TEST_EXISTS))
    return 0;

  GError *error = NULL;
  g_autofree char *contents = NULL;
  g_assert_true(
      g_file_get_contents(fixture->count_path, &contents, NULL, &error));
  g_assert_no_error(error);
  guint64 count = g_ascii_strtoull(contents, NULL, 10);
  g_assert_cmpuint(count, <=, G_MAXUINT);
  return (guint)count;
}

static void assert_call_argv(const NotifyFixture *fixture, guint call,
                             const char *const *expected) {
  g_autoptr(GByteArray) bytes = g_byte_array_new();
  guint argc = 0;
  while (expected[argc] != NULL) {
    gsize length = strlen(expected[argc]) + 1;
    g_assert_cmpuint(length, <=, G_MAXUINT);
    g_byte_array_append(bytes, (const guint8 *)expected[argc], (guint)length);
    argc++;
  }

  g_autofree char *argv_path = call_file_path(fixture, call, "argv");
  g_autofree char *argc_path = call_file_path(fixture, call, "argc");
  g_autofree char *expected_argc = g_strdup_printf("%u\n", argc);
  assert_file_bytes(argv_path, bytes->data, bytes->len);
  assert_file_bytes(argc_path, expected_argc, strlen(expected_argc));
}

static void test_null_required_inputs(NotifyFixture *fixture,
                                      gconstpointer user_data) {
  (void)user_data;
  g_assert_false(shaula_preview_notify(NULL, "body", NULL, TRUE, 2500));
  g_assert_false(shaula_preview_notify("summary", NULL, NULL, TRUE, 2500));
  g_assert_cmpuint(call_count(fixture), ==, 0);
}

static void test_empty_inputs_and_default_timeout(NotifyFixture *fixture,
                                                  gconstpointer user_data) {
  (void)user_data;
  g_assert_true(shaula_preview_notify("", "", "", FALSE, 0));
  g_assert_cmpuint(call_count(fixture), ==, 1);
  const char *expected[] = {"--app-name=Shaula",
                            "--urgency",
                            "normal",
                            "--expire-time",
                            "2500",
                            "",
                            "",
                            NULL};
  assert_call_argv(fixture, 1, expected);
}

static void test_transient_literal_arguments(NotifyFixture *fixture,
                                             gconstpointer user_data) {
  (void)user_data;
  g_autofree char *injected =
      g_build_filename(fixture->tmp_dir, "injected", NULL);
  g_autofree char *summary =
      g_strdup_printf("summary $(touch %s); 'quoted'", injected);
  const char *body = "line one\nline two | & < > 100%";

  g_assert_true(shaula_preview_notify(summary, body, NULL, TRUE, 4321));
  const char *expected[] = {"--app-name=Shaula",
                            "--urgency",
                            "normal",
                            "--expire-time",
                            "4321",
                            "--transient",
                            summary,
                            body,
                            NULL};
  assert_call_argv(fixture, 1, expected);
  g_assert_false(g_file_test(injected, G_FILE_TEST_EXISTS));
}

static void test_image_hint_uri_and_no_fallback(NotifyFixture *fixture,
                                                gconstpointer user_data) {
  (void)user_data;
  const char *image_path = "/tmp/caf\303\251 #1%.png";
  g_assert_true(shaula_preview_notify("Screenshot captured", "Saved.",
                                      image_path, TRUE, 6000));
  g_assert_cmpuint(call_count(fixture), ==, 1);
  const char *expected[] = {
      "--app-name=Shaula",
      "--urgency",
      "normal",
      "--expire-time",
      "6000",
      "--transient",
      "--hint",
      "string:image-path:file:///tmp/caf%C3%A9%20%231%25.png",
      "Screenshot captured",
      "Saved.",
      NULL,
  };
  assert_call_argv(fixture, 1, expected);
}

static void test_image_icon_fallback(NotifyFixture *fixture,
                                     gconstpointer user_data) {
  (void)user_data;
  const char *image_path = "/tmp/capture one.png";
  g_setenv("SHAULA_TEST_EXIT_1", "17", TRUE);
  g_setenv("SHAULA_TEST_EXIT_2", "0", TRUE);

  g_assert_true(
      shaula_preview_notify("Summary", "Body", image_path, FALSE, 2500));
  g_assert_cmpuint(call_count(fixture), ==, 2);
  const char *hint_expected[] = {
      "--app-name=Shaula",
      "--urgency",
      "normal",
      "--expire-time",
      "2500",
      "--hint",
      "string:image-path:file:///tmp/capture%20one.png",
      "Summary",
      "Body",
      NULL,
  };
  const char *icon_expected[] = {
      "--app-name=Shaula",
      "--urgency",
      "normal",
      "--expire-time",
      "2500",
      "-i",
      image_path,
      "Summary",
      "Body",
      NULL,
  };
  assert_call_argv(fixture, 1, hint_expected);
  assert_call_argv(fixture, 2, icon_expected);
}

static void test_image_fallback_failure(NotifyFixture *fixture,
                                        gconstpointer user_data) {
  (void)user_data;
  g_setenv("SHAULA_TEST_EXIT_1", "4", TRUE);
  g_setenv("SHAULA_TEST_EXIT_2", "5", TRUE);
  g_assert_false(
      shaula_preview_notify("Summary", "Body", "/tmp/image.png", TRUE, 2500));
  g_assert_cmpuint(call_count(fixture), ==, 2);
}

static void test_no_image_has_no_fallback(NotifyFixture *fixture,
                                          gconstpointer user_data) {
  (void)user_data;
  g_setenv("SHAULA_TEST_EXIT_1", "9", TRUE);
  g_assert_false(shaula_preview_notify("Summary", "Body", "", TRUE, -7));
  g_assert_cmpuint(call_count(fixture), ==, 1);
  const char *expected[] = {"--app-name=Shaula", "--urgency", "normal",
                            "--expire-time",     "2500",      "--transient",
                            "Summary",           "Body",      NULL};
  assert_call_argv(fixture, 1, expected);
}

static void test_spawn_failure_is_silent_false(NotifyFixture *fixture,
                                               gconstpointer user_data) {
  (void)user_data;
  g_assert_cmpint(g_remove(fixture->notify_send), ==, 0);
  g_assert_false(shaula_preview_notify("Summary", "Body", NULL, FALSE, 2500));
  g_assert_cmpuint(call_count(fixture), ==, 0);
}

static void test_signal_exit_is_failure(NotifyFixture *fixture,
                                        gconstpointer user_data) {
  (void)user_data;
  g_setenv("SHAULA_TEST_SIGNAL_1", "TERM", TRUE);
  g_assert_false(shaula_preview_notify("Summary", "Body", NULL, FALSE, 2500));
  g_assert_cmpuint(call_count(fixture), ==, 1);
}

static void test_stdio_is_suppressed(void) {
  if (g_test_subprocess()) {
    NotifyFixture fixture = {0};
    notify_fixture_setup(&fixture, NULL);
    g_setenv("SHAULA_TEST_CHILD_STDOUT", "notify-stdout", TRUE);
    g_setenv("SHAULA_TEST_CHILD_STDERR", "notify-stderr", TRUE);
    g_assert_true(shaula_preview_notify("Summary", "Body", NULL, FALSE, 2500));
    notify_fixture_teardown(&fixture, NULL);
    return;
  }

  g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
  g_test_trap_assert_passed();
  g_test_trap_assert_stdout("");
  g_test_trap_assert_stderr("");
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add("/preview/notify/null-required-inputs", NotifyFixture, NULL,
             notify_fixture_setup, test_null_required_inputs,
             notify_fixture_teardown);
  g_test_add("/preview/notify/empty-default-timeout", NotifyFixture, NULL,
             notify_fixture_setup, test_empty_inputs_and_default_timeout,
             notify_fixture_teardown);
  g_test_add("/preview/notify/transient-literal-arguments", NotifyFixture, NULL,
             notify_fixture_setup, test_transient_literal_arguments,
             notify_fixture_teardown);
  g_test_add("/preview/notify/image-hint-uri", NotifyFixture, NULL,
             notify_fixture_setup, test_image_hint_uri_and_no_fallback,
             notify_fixture_teardown);
  g_test_add("/preview/notify/image-icon-fallback", NotifyFixture, NULL,
             notify_fixture_setup, test_image_icon_fallback,
             notify_fixture_teardown);
  g_test_add("/preview/notify/image-fallback-failure", NotifyFixture, NULL,
             notify_fixture_setup, test_image_fallback_failure,
             notify_fixture_teardown);
  g_test_add("/preview/notify/no-image-no-fallback", NotifyFixture, NULL,
             notify_fixture_setup, test_no_image_has_no_fallback,
             notify_fixture_teardown);
  g_test_add("/preview/notify/spawn-failure", NotifyFixture, NULL,
             notify_fixture_setup, test_spawn_failure_is_silent_false,
             notify_fixture_teardown);
  g_test_add("/preview/notify/signal-failure", NotifyFixture, NULL,
             notify_fixture_setup, test_signal_exit_is_failure,
             notify_fixture_teardown);
  g_test_add_func("/preview/notify/stdio-suppressed", test_stdio_is_suppressed);
  return g_test_run();
}
