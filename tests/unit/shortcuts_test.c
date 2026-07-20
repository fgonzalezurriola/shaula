#include "shortcuts/shortcuts.h"

#include <glib.h>
#include <glib/gstdio.h>

static char *test_root;

static gboolean remove_tree(const char *path) {
  if (!g_file_test(path, G_FILE_TEST_EXISTS))
    return TRUE;
  if (!g_file_test(path, G_FILE_TEST_IS_DIR))
    return g_unlink(path) == 0;
  g_autoptr(GDir) directory = g_dir_open(path, 0, NULL);
  if (directory == NULL)
    return FALSE;
  const char *name;
  while ((name = g_dir_read_name(directory)) != NULL) {
    g_autofree char *child = g_build_filename(path, name, NULL);
    if (!remove_tree(child))
      return FALSE;
  }
  return g_rmdir(path) == 0;
}

static void reset_environment(const char *portal_state) {
  if (test_root != NULL) {
    g_assert_true(remove_tree(test_root));
    g_clear_pointer(&test_root, g_free);
  }
  test_root = g_dir_make_tmp("shaula-shortcuts-test-XXXXXX", NULL);
  g_assert_nonnull(test_root);
  g_autofree char *config = g_build_filename(test_root, "config", NULL);
  g_autofree char *state = g_build_filename(test_root, "state", NULL);
  g_assert_cmpint(g_mkdir_with_parents(config, 0700), ==, 0);
  g_assert_cmpint(g_mkdir_with_parents(state, 0700), ==, 0);
  g_setenv("HOME", test_root, TRUE);
  g_setenv("XDG_CONFIG_HOME", config, TRUE);
  g_setenv("XDG_STATE_HOME", state, TRUE);
  g_setenv("SHAULA_SHORTCUTS_TEST_PORTAL", portal_state, TRUE);
}

static void test_portal_selection_and_decline(void) {
  reset_environment("active");
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  ShaulaShortcutOptions options = {.remember_choice = TRUE};
  g_assert_cmpint(shaula_shortcuts_enable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(status.backend, ==, SHAULA_SHORTCUT_BACKEND_PORTAL);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_ACTIVE);
  g_assert_true(status.setup_completed);
  g_assert_cmpint(status.choice, ==, SHAULA_SHORTCUT_CHOICE_ENABLED);

  g_assert_cmpint(shaula_shortcuts_query(&status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(status.backend, ==, SHAULA_SHORTCUT_BACKEND_PORTAL);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_ACTIVE);

  g_assert_cmpint(shaula_shortcuts_disable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_DISABLED);
  g_assert_cmpint(status.choice, ==, SHAULA_SHORTCUT_CHOICE_DECLINED);
  g_assert_true(status.setup_completed);
  g_assert_cmpint(shaula_shortcuts_disable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  shaula_shortcut_status_clear(&status);
}

static void test_invalid_setup_state(void) {
  reset_environment("unsupported");
  g_autofree char *directory =
      g_build_filename(test_root, "config", "shaula", NULL);
  g_autofree char *path =
      g_build_filename(directory, "setup-state.ini", NULL);
  g_assert_cmpint(g_mkdir_with_parents(directory, 0700), ==, 0);
  g_assert_true(g_file_set_contents(path, "[setup]\ncompleted=maybe\n", -1,
                                    NULL));
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  g_autofree char *error = NULL;
  g_assert_cmpint(shaula_shortcuts_query(&status, &error), ==,
                  SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_CONFIG_INVALID);
  g_assert_cmpstr(status.error_code, ==,
                  "ERR_SHORTCUT_CONFIGURATION_INVALID");
  g_assert_nonnull(error);
  shaula_shortcut_status_clear(&status);
}

static void test_unsupported_and_first_run(void) {
  reset_environment("unsupported");
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  g_assert_cmpint(shaula_shortcuts_query(&status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_false(status.setup_completed);

  ShaulaShortcutOptions options = {.remember_choice = TRUE};
  g_assert_cmpint(shaula_shortcuts_enable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(status.backend, ==, SHAULA_SHORTCUT_BACKEND_NONE);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_UNSUPPORTED);
  g_assert_true(status.setup_completed);
  g_assert_true(status.enabled_requested);

  g_assert_cmpint(shaula_shortcuts_disable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(shaula_shortcuts_query(&status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_true(status.setup_completed);
  g_assert_cmpint(status.choice, ==, SHAULA_SHORTCUT_CHOICE_DECLINED);
  shaula_shortcut_status_clear(&status);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/shortcuts/portal-selection-decline",
                  test_portal_selection_and_decline);
  g_test_add_func("/shortcuts/invalid-setup-state",
                  test_invalid_setup_state);
  g_test_add_func("/shortcuts/unsupported-first-run",
                  test_unsupported_and_first_run);
  int result = g_test_run();
  if (test_root != NULL)
    g_assert_true(remove_tree(test_root));
  g_free(test_root);
  return result;
}
