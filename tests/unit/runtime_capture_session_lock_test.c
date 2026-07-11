#define _POSIX_C_SOURCE 200809L

#include "capture_session_lock.h"

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static ShaulaCaptureSessionSpan span_from_string(const char *value) {
  return (ShaulaCaptureSessionSpan){value, strlen(value)};
}

static char *expected_pid_line(void) {
  return g_strdup_printf("%ld\n", (long)getpid());
}

static void test_acquire_contention_release_and_reacquire(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-capture-session-XXXXXX", &error);
  g_autofree char *capture_dir = NULL;
  g_autofree char *path = NULL;
  g_autofree char *contents = NULL;
  g_autofree char *expected = expected_pid_line();
  gsize contents_length = 0;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  capture_dir = g_build_filename(root, "nested", "capture", NULL);
  path = g_build_filename(capture_dir, "session.lock", NULL);

  g_assert_cmpint(shaula_capture_session_lock_acquire(span_from_string(path)),
                  ==, SHAULA_CAPTURE_SESSION_STATUS_OK);
  g_assert_true(
      g_file_get_contents(path, &contents, &contents_length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(contents_length, ==, strlen(expected));
  g_assert_cmpmem(contents, contents_length, expected, strlen(expected));

  g_assert_cmpint(shaula_capture_session_lock_acquire(span_from_string(path)),
                  ==, SHAULA_CAPTURE_SESSION_STATUS_BUSY);

  shaula_capture_session_lock_release(span_from_string(path));
  g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
  shaula_capture_session_lock_release(span_from_string(path));

  g_assert_cmpint(shaula_capture_session_lock_acquire(span_from_string(path)),
                  ==, SHAULA_CAPTURE_SESSION_STATUS_OK);
  shaula_capture_session_lock_release(span_from_string(path));

  g_assert_cmpint(g_rmdir(capture_dir), ==, 0);
  {
    g_autofree char *nested = g_build_filename(root, "nested", NULL);
    g_assert_cmpint(g_rmdir(nested), ==, 0);
  }
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_existing_lock_classification(void) {
  static const struct {
    const char *name;
    const char *contents;
    gssize length;
  } busy_cases[] = {
      {"current-pid", NULL, -1},
      {"empty", "", 0},
      {"whitespace", " \t\r\n", -1},
      {"invalid", "not-a-pid\n", -1},
      {"overflow", "2147483648\n", -1},
      {"leading-underscore", "_1\n", -1},
      {"trailing-underscore", "1_\n", -1},
      {"oversized", "11111111111111111111111111111111111111111111111111111111111111111",
       65},
  };
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-capture-session-existing-XXXXXX", &error);
  g_autofree char *path = NULL;
  g_autofree char *current_pid = expected_pid_line();
  size_t index;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  path = g_build_filename(root, "session.lock", NULL);

  for (index = 0; index < G_N_ELEMENTS(busy_cases); index += 1) {
    const char *contents = busy_cases[index].contents != NULL
                               ? busy_cases[index].contents
                               : current_pid;

    g_test_message("case: %s", busy_cases[index].name);
    g_assert_true(g_file_set_contents(path, contents, busy_cases[index].length,
                                      &error));
    g_assert_no_error(error);
    g_assert_cmpint(
        shaula_capture_session_lock_acquire(span_from_string(path)), ==,
        SHAULA_CAPTURE_SESSION_STATUS_BUSY);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
  }

  g_assert_cmpint(g_remove(path), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_stale_lock_is_replaced_once(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-capture-session-stale-XXXXXX", &error);
  g_autofree char *path = NULL;
  g_autofree char *contents = NULL;
  g_autofree char *expected = expected_pid_line();
  gsize contents_length = 0;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  path = g_build_filename(root, "session.lock", NULL);

  errno = 0;
  if (kill((pid_t)INT32_MAX, 0) == 0 || errno != ESRCH) {
    g_test_skip("INT32_MAX unexpectedly identifies a visible process");
    g_assert_cmpint(g_rmdir(root), ==, 0);
    return;
  }

  g_assert_true(
      g_file_set_contents(path, "  +2_147_483_647\r\n", -1, &error));
  g_assert_no_error(error);
  g_assert_cmpint(shaula_capture_session_lock_acquire(span_from_string(path)),
                  ==, SHAULA_CAPTURE_SESSION_STATUS_OK);

  g_assert_true(
      g_file_get_contents(path, &contents, &contents_length, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(contents_length, ==, strlen(expected));
  g_assert_cmpmem(contents, contents_length, expected, strlen(expected));

  shaula_capture_session_lock_release(span_from_string(path));
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_path_and_filesystem_errors(void) {
  static const char embedded_nul[] = {'a', '\0', 'b'};
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-capture-session-errors-XXXXXX", &error);
  g_autofree char *blocker = NULL;
  g_autofree char *blocked_path = NULL;

  g_assert_no_error(error);
  g_assert_nonnull(root);
  blocker = g_build_filename(root, "blocker", NULL);
  blocked_path = g_build_filename(blocker, "session.lock", NULL);
  g_assert_true(g_file_set_contents(blocker, "x", 1, &error));
  g_assert_no_error(error);

  g_assert_cmpint(
      shaula_capture_session_lock_acquire(
          (ShaulaCaptureSessionSpan){NULL, 1}),
      ==, SHAULA_CAPTURE_SESSION_STATUS_INVALID_ARGUMENT);
  g_assert_cmpint(
      shaula_capture_session_lock_acquire(
          (ShaulaCaptureSessionSpan){"x", SIZE_MAX}),
      ==, SHAULA_CAPTURE_SESSION_STATUS_OUT_OF_MEMORY);
  g_assert_cmpint(
      shaula_capture_session_lock_acquire((ShaulaCaptureSessionSpan){
          embedded_nul, sizeof(embedded_nul)}),
      ==, SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR);
  g_assert_cmpint(shaula_capture_session_lock_acquire(
                      span_from_string(blocked_path)),
                  ==, SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR);
  g_assert_cmpint(shaula_capture_session_lock_acquire(
                      (ShaulaCaptureSessionSpan){NULL, 0}),
                  ==, SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR);

  shaula_capture_session_lock_release((ShaulaCaptureSessionSpan){NULL, 1});
  shaula_capture_session_lock_release(
      (ShaulaCaptureSessionSpan){embedded_nul, sizeof(embedded_nul)});

  g_assert_cmpint(g_remove(blocker), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/runtime/capture-session/acquire-release",
                  test_acquire_contention_release_and_reacquire);
  g_test_add_func("/runtime/capture-session/existing-locks",
                  test_existing_lock_classification);
  g_test_add_func("/runtime/capture-session/stale-lock",
                  test_stale_lock_is_replaced_once);
  g_test_add_func("/runtime/capture-session/errors",
                  test_path_and_filesystem_errors);
  return g_test_run();
}
