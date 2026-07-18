#define _POSIX_C_SOURCE 200809L

#include "provider_handoff.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char *self_path;

static void append_log(const char *path, const char *line) {
  int fd = g_open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
  g_assert_cmpint(fd, >=, 0);
  gsize length = strlen(line);
  g_assert_cmpint(write(fd, line, length), ==, (ssize_t)length);
  g_assert_cmpint(write(fd, "\n", 1), ==, 1);
  g_assert_cmpint(close(fd), ==, 0);
}

static void touch_file(const char *path) {
  g_assert_true(g_file_set_contents(path, "ready\n", -1, NULL));
}

static gboolean wait_for_file(const char *path, guint timeout_ms) {
  const gint64 deadline =
      g_get_monotonic_time() + (gint64)timeout_ms * G_TIME_SPAN_MILLISECOND;
  do {
    if (g_file_test(path, G_FILE_TEST_EXISTS))
      return TRUE;
    g_usleep(5U * 1000U);
  } while (g_get_monotonic_time() < deadline);
  return g_file_test(path, G_FILE_TEST_EXISTS);
}

static GDBusConnection *new_private_bus_connection(const char *address) {
  g_autoptr(GError) error = NULL;
  GDBusConnection *connection = g_dbus_connection_new_for_address_sync(
      address,
      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
      NULL, NULL, &error);
  g_assert_no_error(error);
  g_assert_nonnull(connection);
  return connection;
}

typedef struct {
  GMainLoop *loop;
  const char *log_path;
  const char *ready_path;
} OldState;

static void old_prepared(const char *marker, gpointer user_data) {
  OldState *state = user_data;
  g_assert_nonnull(marker);
  append_log(state->log_path, "OLD_PREPARED");
}

static void old_committed(gpointer user_data) {
  OldState *state = user_data;
  append_log(state->log_path, "OLD_COMMITTED");
  g_main_loop_quit(state->loop);
}

static void old_aborted(const char *marker, gpointer user_data) {
  OldState *state = user_data;
  g_assert_nonnull(marker);
  append_log(state->log_path, "OLD_ABORTED");
}

static void old_timed_out(const char *marker, gpointer user_data) {
  OldState *state = user_data;
  g_assert_nonnull(marker);
  append_log(state->log_path, "OLD_TIMED_OUT");
}

static void old_name_acquired(GDBusConnection *connection, const gchar *name,
                              gpointer user_data) {
  (void)connection;
  (void)name;
  OldState *state = user_data;
  append_log(state->log_path, "OLD_NAME");
  touch_file(state->ready_path);
}

static void old_name_lost(GDBusConnection *connection, const gchar *name,
                          gpointer user_data) {
  (void)connection;
  (void)name;
  OldState *state = user_data;
  append_log(state->log_path, "OLD_NAME_LOST");
}

static int old_helper(const char *log_path, const char *ready_path) {
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusConnection) bus =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error(error);
  g_assert_nonnull(bus);

  OldState state = {
      .loop = g_main_loop_new(NULL, FALSE),
      .log_path = log_path,
      .ready_path = ready_path,
  };
  const ShaulaClipboardProviderHandoffCallbacks callbacks = {
      .prepared = old_prepared,
      .committed = old_committed,
      .aborted = old_aborted,
      .timed_out = old_timed_out,
  };
  ShaulaClipboardProviderHandoff *handoff =
      shaula_clipboard_provider_handoff_new(bus, &callbacks, &state, 5000U,
                                            &error);
  g_assert_no_error(error);
  g_assert_nonnull(handoff);

  guint owner_id = g_bus_own_name_on_connection(
      bus, SHAULA_CLIPBOARD_PROVIDER_BUS_NAME,
      G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT, old_name_acquired,
      old_name_lost, &state, NULL);
  g_assert_cmpuint(owner_id, !=, 0U);
  g_main_loop_run(state.loop);

  g_bus_unown_name(owner_id);
  shaula_clipboard_provider_handoff_free(handoff);
  g_main_loop_unref(state.loop);
  return 0;
}

typedef struct {
  GMainLoop *loop;
  GDBusConnection *bus;
  const char *log_path;
  const char *ready_path;
  const char *continue_path;
  char *previous_owner;
  char *previous_token;
} NewState;

static void new_name_acquired(GDBusConnection *connection, const gchar *name,
                              gpointer user_data) {
  (void)connection;
  (void)name;
  NewState *state = user_data;
  append_log(state->log_path, "NEW_NAME");
  append_log(state->log_path, "NEW_READY");
  touch_file(state->ready_path);
  g_assert_true(wait_for_file(state->continue_path, 3000U));

  g_autoptr(GError) error = NULL;
  g_assert_true(shaula_clipboard_provider_handoff_commit_previous(
      state->bus, state->previous_owner, state->previous_token, &error));
  g_assert_no_error(error);
  append_log(state->log_path, "NEW_COMMIT_SENT");
  g_main_loop_quit(state->loop);
}

static void new_name_lost(GDBusConnection *connection, const gchar *name,
                          gpointer user_data) {
  (void)connection;
  (void)name;
  NewState *state = user_data;
  append_log(state->log_path, "NEW_NAME_LOST");
  g_main_loop_quit(state->loop);
}

static int new_helper(const char *mode, const char *log_path,
                      const char *ready_path, const char *continue_path) {
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusConnection) bus =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error(error);
  g_assert_nonnull(bus);

  const char *marker =
      "application/x-shaula-clipboard-provider-integration-test";
  g_autofree char *owner = NULL;
  g_autofree char *token = NULL;
  g_assert_cmpint(shaula_clipboard_provider_handoff_prepare_previous(
                      bus, marker, &owner, &token, &error),
                  ==, SHAULA_CLIPBOARD_HANDOFF_PREPARED);
  g_assert_no_error(error);
  append_log(log_path, "NEW_PREPARED");
  append_log(log_path, "NEW_CLIPBOARD_OWNED");

  if (g_str_equal(mode, "abort")) {
    g_assert_true(shaula_clipboard_provider_handoff_abort_previous(
        bus, owner, token, &error));
    g_assert_no_error(error);
    append_log(log_path, "NEW_ABORT_SENT");
    return 0;
  }

  NewState state = {
      .loop = g_main_loop_new(NULL, FALSE),
      .bus = bus,
      .log_path = log_path,
      .ready_path = ready_path,
      .continue_path = continue_path,
      .previous_owner = owner,
      .previous_token = token,
  };
  guint owner_id = g_bus_own_name_on_connection(
      bus, SHAULA_CLIPBOARD_PROVIDER_BUS_NAME,
      G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
          G_BUS_NAME_OWNER_FLAGS_REPLACE,
      new_name_acquired, new_name_lost, &state, NULL);
  g_assert_cmpuint(owner_id, !=, 0U);
  g_main_loop_run(state.loop);
  g_bus_unown_name(owner_id);
  g_main_loop_unref(state.loop);
  return 0;
}

static GPid spawn_helper(const char *role, const char *mode,
                         const char *log_path, const char *ready_path,
                         const char *continue_path) {
  char *argv[] = {self_path,          "--helper",       (char *)role,
                  (char *)mode,       (char *)log_path,  (char *)ready_path,
                  (char *)continue_path, NULL};
  g_autoptr(GError) error = NULL;
  GPid pid = 0;
  g_assert_true(g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
                              NULL, NULL, &pid, &error));
  g_assert_no_error(error);
  return pid;
}

static void assert_child_ok(GPid pid) {
  int status = 0;
  g_assert_cmpint(waitpid(pid, &status, 0), ==, pid);
  g_spawn_close_pid(pid);
  g_assert_true(WIFEXITED(status));
  g_assert_cmpint(WEXITSTATUS(status), ==, 0);
}

static gssize log_position(const char *log, const char *needle) {
  const char *found = strstr(log, needle);
  g_assert_nonnull(found);
  return found - log;
}

static void test_replacement_order_and_lifetime(void) {
  g_autoptr(GTestDBus) test_bus = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(test_bus);

  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-handoff-order-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *log_path = g_build_filename(root, "events.log", NULL);
  g_autofree char *old_ready = g_build_filename(root, "old.ready", NULL);
  g_autofree char *new_ready = g_build_filename(root, "new.ready", NULL);
  g_autofree char *continue_path =
      g_build_filename(root, "continue", NULL);

  GPid old_pid =
      spawn_helper("old", "serve", log_path, old_ready, continue_path);
  g_assert_true(wait_for_file(old_ready, 3000U));
  GPid new_pid =
      spawn_helper("new", "success", log_path, new_ready, continue_path);
  g_assert_true(wait_for_file(new_ready, 3000U));

  g_assert_cmpint(kill(old_pid, 0), ==, 0);
  g_autofree char *before_commit = NULL;
  g_assert_true(g_file_get_contents(log_path, &before_commit, NULL, NULL));
  g_assert_null(strstr(before_commit, "OLD_COMMITTED"));
  touch_file(continue_path);

  assert_child_ok(new_pid);
  assert_child_ok(old_pid);

  g_autofree char *log = NULL;
  g_assert_true(g_file_get_contents(log_path, &log, NULL, NULL));
  g_assert_cmpint(log_position(log, "NEW_PREPARED"), <,
                  log_position(log, "NEW_CLIPBOARD_OWNED"));
  g_assert_cmpint(log_position(log, "NEW_CLIPBOARD_OWNED"), <,
                  log_position(log, "NEW_NAME"));
  g_assert_cmpint(log_position(log, "NEW_NAME"), <,
                  log_position(log, "NEW_READY"));
  g_assert_cmpint(log_position(log, "NEW_READY"), <,
                  log_position(log, "OLD_COMMITTED"));

  g_assert_cmpint(g_remove(continue_path), ==, 0);
  g_assert_cmpint(g_remove(new_ready), ==, 0);
  g_assert_cmpint(g_remove(old_ready), ==, 0);
  g_assert_cmpint(g_remove(log_path), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
  g_test_dbus_down(test_bus);
}

static void test_legacy_provider_without_handoff_is_replaceable(void) {
  g_autoptr(GTestDBus) test_bus = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(test_bus);
  const char *address = g_test_dbus_get_bus_address(test_bus);

  g_autoptr(GDBusConnection) legacy = new_private_bus_connection(address);
  g_autoptr(GDBusConnection) replacement =
      new_private_bus_connection(address);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) request_reply = g_dbus_connection_call_sync(
      legacy, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "RequestName",
      g_variant_new("(su)", SHAULA_CLIPBOARD_PROVIDER_BUS_NAME, 1U),
      G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &error);
  g_assert_no_error(error);
  g_assert_nonnull(request_reply);
  guint32 request_result = 0U;
  g_variant_get(request_reply, "(u)", &request_result);
  g_assert_cmpuint(request_result, ==, 1U);

  g_autofree char *owner = NULL;
  g_autofree char *token = NULL;
  g_assert_cmpint(shaula_clipboard_provider_handoff_prepare_previous(
                      replacement,
                      "application/x-shaula-clipboard-provider-legacy-test",
                      &owner, &token, &error),
                  ==, SHAULA_CLIPBOARD_HANDOFF_NONE);
  g_assert_no_error(error);
  g_assert_null(owner);
  g_assert_null(token);

  g_assert_true(g_dbus_connection_close_sync(replacement, NULL, &error));
  g_assert_no_error(error);
  g_assert_true(g_dbus_connection_close_sync(legacy, NULL, &error));
  g_assert_no_error(error);
  g_test_dbus_down(test_bus);
}

static void test_failed_replacement_keeps_old_provider(void) {
  g_autoptr(GTestDBus) test_bus = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(test_bus);

  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-handoff-abort-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *log_path = g_build_filename(root, "events.log", NULL);
  g_autofree char *old_ready = g_build_filename(root, "old.ready", NULL);
  g_autofree char *unused_ready = g_build_filename(root, "unused.ready", NULL);
  g_autofree char *unused_continue =
      g_build_filename(root, "unused.continue", NULL);

  GPid old_pid =
      spawn_helper("old", "serve", log_path, old_ready, unused_continue);
  g_assert_true(wait_for_file(old_ready, 3000U));
  GPid new_pid = spawn_helper("new", "abort", log_path, unused_ready,
                              unused_continue);
  assert_child_ok(new_pid);

  g_usleep(50U * 1000U);
  g_assert_cmpint(kill(old_pid, 0), ==, 0);
  g_autofree char *log = NULL;
  g_assert_true(g_file_get_contents(log_path, &log, NULL, NULL));
  g_assert_nonnull(strstr(log, "OLD_ABORTED"));
  g_assert_null(strstr(log, "OLD_COMMITTED"));

  g_assert_cmpint(kill(old_pid, SIGTERM), ==, 0);
  int status = 0;
  g_assert_cmpint(waitpid(old_pid, &status, 0), ==, old_pid);
  g_spawn_close_pid(old_pid);
  g_assert_true(WIFSIGNALED(status));

  g_assert_cmpint(g_remove(old_ready), ==, 0);
  g_assert_cmpint(g_remove(log_path), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
  g_test_dbus_down(test_bus);
}

int main(int argc, char **argv) {
  if (argc == 7 && g_str_equal(argv[1], "--helper")) {
    if (g_str_equal(argv[2], "old"))
      return old_helper(argv[4], argv[5]);
    if (g_str_equal(argv[2], "new"))
      return new_helper(argv[3], argv[4], argv[5], argv[6]);
    return 2;
  }

  self_path = g_file_read_link("/proc/self/exe", NULL);
  if (self_path == NULL)
    return 132;
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/clipboard/handoff/replacement-order",
                  test_replacement_order_and_lifetime);
  g_test_add_func("/clipboard/handoff/failed-replacement",
                  test_failed_replacement_keeps_old_provider);
  g_test_add_func("/clipboard/handoff/legacy-provider",
                  test_legacy_provider_without_handoff_is_replaceable);
  int result = g_test_run();
  g_free(self_path);
  return result;
}
