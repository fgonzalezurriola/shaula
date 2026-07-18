#define _POSIX_C_SOURCE 200809L

#include "clipboard/clipboard.h"

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PROVIDER_READY "READY shaula-clipboard/1\n"

static char *self_path;

static void write_optional_file(const char *name, const char *contents) {
  const char *path = g_getenv(name);
  if (path != NULL && *path != '\0')
    (void)g_file_set_contents(path, contents, -1, NULL);
}

static int provider_main(const char *mode) {
  g_autofree char *pid_text = g_strdup_printf("%ld\n", (long)getpid());
  write_optional_file("SHAULA_TEST_PROVIDER_PID_FILE", pid_text);

  GByteArray *input = g_byte_array_new();
  if (input == NULL)
    return 99;
  for (;;) {
    guint8 buffer[4096];
    ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count > 0) {
      g_byte_array_append(input, buffer, (guint)count);
      continue;
    }
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0) {
      g_byte_array_unref(input);
      return 98;
    }
    break;
  }
  const char *capture = g_getenv("SHAULA_TEST_PROVIDER_CAPTURE");
  if (capture != NULL && *capture != '\0')
    (void)g_file_set_contents(capture, (const char *)input->data,
                              (gssize)input->len, NULL);
  g_byte_array_unref(input);

  if (g_str_equal(mode, "unavailable"))
    return 35;
  if (g_str_equal(mode, "failure"))
    return 47;
  if (g_str_equal(mode, "malformed")) {
    fputs("INVALID readiness\n", stdout);
    fflush(stdout);
    g_usleep(250U * 1000U);
    write_optional_file("SHAULA_TEST_PROVIDER_LATE_FILE", "late\n");
    return 0;
  }
  if (g_str_equal(mode, "timeout")) {
    g_usleep(250U * 1000U);
    write_optional_file("SHAULA_TEST_PROVIDER_LATE_FILE", "late\n");
    fputs(PROVIDER_READY, stdout);
    fflush(stdout);
    for (;;)
      pause();
  }
  if (g_str_equal(mode, "stderr-ready"))
    fputs("provider diagnostic remains on stderr\n", stderr);

  fputs(PROVIDER_READY, stdout);
  fflush(stdout);
  for (;;)
    pause();
}

typedef struct {
  char *root;
  char *pid_file;
  char *late_file;
  char *capture_file;
} ProviderFixture;

static void fixture_init(ProviderFixture *fixture, const char *mode) {
  g_autoptr(GError) error = NULL;
  fixture->root = g_dir_make_tmp("shaula-clipboard-life-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(fixture->root);
  fixture->pid_file = g_build_filename(fixture->root, "provider.pid", NULL);
  fixture->late_file = g_build_filename(fixture->root, "late", NULL);
  fixture->capture_file = g_build_filename(fixture->root, "capture", NULL);

  g_setenv("SHAULA_CLIPBOARD_PROVIDER_BIN", self_path, TRUE);
  g_setenv("SHAULA_TEST_PROVIDER_MODE", mode, TRUE);
  g_setenv("SHAULA_TEST_PROVIDER_PID_FILE", fixture->pid_file, TRUE);
  g_setenv("SHAULA_TEST_PROVIDER_LATE_FILE", fixture->late_file, TRUE);
  g_setenv("SHAULA_TEST_PROVIDER_CAPTURE", fixture->capture_file, TRUE);
  g_unsetenv("SHAULA_CLIPBOARD_AVAILABLE");
  g_unsetenv("SHAULA_CLIPBOARD_READY_TIMEOUT_MS");
}

static void fixture_clear(ProviderFixture *fixture) {
  g_unsetenv("SHAULA_CLIPBOARD_PROVIDER_BIN");
  g_unsetenv("SHAULA_TEST_PROVIDER_MODE");
  g_unsetenv("SHAULA_TEST_PROVIDER_PID_FILE");
  g_unsetenv("SHAULA_TEST_PROVIDER_LATE_FILE");
  g_unsetenv("SHAULA_TEST_PROVIDER_CAPTURE");
  g_unsetenv("SHAULA_CLIPBOARD_READY_TIMEOUT_MS");

  (void)g_remove(fixture->pid_file);
  (void)g_remove(fixture->late_file);
  (void)g_remove(fixture->capture_file);
  g_assert_cmpint(g_rmdir(fixture->root), ==, 0);
  g_clear_pointer(&fixture->pid_file, g_free);
  g_clear_pointer(&fixture->late_file, g_free);
  g_clear_pointer(&fixture->capture_file, g_free);
  g_clear_pointer(&fixture->root, g_free);
}

static gboolean wait_for_path(const char *path, guint timeout_ms) {
  const gint64 deadline =
      g_get_monotonic_time() + (gint64)timeout_ms * G_TIME_SPAN_MILLISECOND;
  do {
    if (g_file_test(path, G_FILE_TEST_EXISTS))
      return TRUE;
    g_usleep(5U * 1000U);
  } while (g_get_monotonic_time() < deadline);
  return g_file_test(path, G_FILE_TEST_EXISTS);
}

static pid_t read_provider_pid(const char *path) {
  g_assert_true(wait_for_path(path, 1000U));
  g_autofree char *contents = NULL;
  g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
  char *end = NULL;
  gint64 parsed = g_ascii_strtoll(contents, &end, 10);
  g_assert_true(end != contents && parsed > 1 && parsed <= G_MAXINT);
  return (pid_t)parsed;
}

static gboolean process_alive(pid_t pid) {
  return kill(pid, 0) == 0 || errno == EPERM;
}

static void assert_process_gone(pid_t pid) {
  const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
  while (process_alive(pid) && g_get_monotonic_time() < deadline)
    g_usleep(5U * 1000U);
  g_assert_false(process_alive(pid));
}

static void stop_provider(pid_t pid) {
  if (kill(pid, SIGTERM) < 0)
    g_assert_cmpint(errno, ==, ESRCH);
  assert_process_gone(pid);
}

static void test_unavailable_exit_maps_and_reaps(void) {
  ProviderFixture fixture = {0};
  fixture_init(&fixture, "unavailable");

  g_assert_cmpint(shaula_clipboard_publish_text("payload", 7), ==,
                  SHAULA_CLIPBOARD_STATUS_UNAVAILABLE);
  pid_t pid = read_provider_pid(fixture.pid_file);
  assert_process_gone(pid);
  fixture_clear(&fixture);
}

static void test_malformed_readiness_cannot_act_later(void) {
  ProviderFixture fixture = {0};
  fixture_init(&fixture, "malformed");

  g_assert_cmpint(shaula_clipboard_publish_text("payload", 7), ==,
                  SHAULA_CLIPBOARD_STATUS_PROTOCOL_INVALID);
  pid_t pid = read_provider_pid(fixture.pid_file);
  assert_process_gone(pid);
  g_usleep(300U * 1000U);
  g_assert_false(g_file_test(fixture.late_file, G_FILE_TEST_EXISTS));
  fixture_clear(&fixture);
}

static void test_timeout_terminates_before_late_readiness(void) {
  ProviderFixture fixture = {0};
  fixture_init(&fixture, "timeout");
  g_setenv("SHAULA_CLIPBOARD_READY_TIMEOUT_MS", "30", TRUE);

  g_assert_cmpint(shaula_clipboard_publish_text("payload", 7), ==,
                  SHAULA_CLIPBOARD_STATUS_TIMEOUT);
  if (wait_for_path(fixture.pid_file, 1000U)) {
    pid_t pid = read_provider_pid(fixture.pid_file);
    assert_process_gone(pid);
  }
  g_usleep(300U * 1000U);
  g_assert_false(g_file_test(fixture.late_file, G_FILE_TEST_EXISTS));
  fixture_clear(&fixture);
}

static void test_generic_provider_failure_is_deterministic(void) {
  ProviderFixture fixture = {0};
  fixture_init(&fixture, "failure");

  g_assert_cmpint(shaula_clipboard_publish_text("payload", 7), ==,
                  SHAULA_CLIPBOARD_STATUS_PROVIDER_FAILED);
  pid_t pid = read_provider_pid(fixture.pid_file);
  assert_process_gone(pid);
  fixture_clear(&fixture);
}

static void test_spawn_failure_maps_unavailable(void) {
  g_setenv("SHAULA_CLIPBOARD_PROVIDER_BIN",
           "/nonexistent/shaula-clipboard-provider", TRUE);
  g_assert_cmpint(shaula_clipboard_publish_text("payload", 7), ==,
                  SHAULA_CLIPBOARD_STATUS_UNAVAILABLE);
  g_unsetenv("SHAULA_CLIPBOARD_PROVIDER_BIN");
}

static void test_ready_provider_survives_caller_exit(void) {
  ProviderFixture fixture = {0};
  fixture_init(&fixture, "ready");

  pid_t caller = fork();
  g_assert_cmpint(caller, >=, 0);
  if (caller == 0) {
    ShaulaClipboardStatus status =
        shaula_clipboard_publish_text("detached", strlen("detached"));
    _exit(status == SHAULA_CLIPBOARD_STATUS_OK ? 0 : 1);
  }

  int wait_status = 0;
  g_assert_cmpint(waitpid(caller, &wait_status, 0), ==, caller);
  g_assert_true(WIFEXITED(wait_status));
  g_assert_cmpint(WEXITSTATUS(wait_status), ==, 0);

  pid_t provider = read_provider_pid(fixture.pid_file);
  g_assert_true(process_alive(provider));
  stop_provider(provider);
  fixture_clear(&fixture);
}

static void test_provider_stdout_is_private(void) {
  ProviderFixture fixture = {0};
  fixture_init(&fixture, "stderr-ready");

  int output_pipe[2];
  g_assert_cmpint(pipe(output_pipe), ==, 0);
  fflush(stdout);
  int saved_stdout = dup(STDOUT_FILENO);
  g_assert_cmpint(saved_stdout, >=, 0);
  g_assert_cmpint(dup2(output_pipe[1], STDOUT_FILENO), ==, STDOUT_FILENO);
  g_assert_cmpint(close(output_pipe[1]), ==, 0);

  ShaulaClipboardStatus status =
      shaula_clipboard_publish_text("payload", strlen("payload"));

  fflush(stdout);
  g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), ==, STDOUT_FILENO);
  g_assert_cmpint(close(saved_stdout), ==, 0);

  char byte = '\0';
  ssize_t count = read(output_pipe[0], &byte, 1);
  g_assert_cmpint(count, ==, 0);
  g_assert_cmpint(close(output_pipe[0]), ==, 0);
  g_assert_cmpint(status, ==, SHAULA_CLIPBOARD_STATUS_OK);

  pid_t provider = read_provider_pid(fixture.pid_file);
  stop_provider(provider);
  fixture_clear(&fixture);
}

int main(int argc, char **argv) {
  const char *provider_mode = g_getenv("SHAULA_TEST_PROVIDER_MODE");
  if (provider_mode != NULL && *provider_mode != '\0')
    return provider_main(provider_mode);

  self_path = g_file_read_link("/proc/self/exe", NULL);
  if (self_path == NULL)
    return 132;

  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/clipboard/lifecycle/unavailable-exit",
                  test_unavailable_exit_maps_and_reaps);
  g_test_add_func("/clipboard/lifecycle/malformed-readiness",
                  test_malformed_readiness_cannot_act_later);
  g_test_add_func("/clipboard/lifecycle/timeout-cleanup",
                  test_timeout_terminates_before_late_readiness);
  g_test_add_func("/clipboard/lifecycle/generic-failure",
                  test_generic_provider_failure_is_deterministic);
  g_test_add_func("/clipboard/lifecycle/spawn-unavailable",
                  test_spawn_failure_maps_unavailable);
  g_test_add_func("/clipboard/lifecycle/detached-lifetime",
                  test_ready_provider_survives_caller_exit);
  g_test_add_func("/clipboard/lifecycle/stdout-isolation",
                  test_provider_stdout_is_private);

  int result = g_test_run();
  g_free(self_path);
  return result;
}
