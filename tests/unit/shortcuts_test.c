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

static void reset_environment(const char *portal_state, gboolean with_niri) {
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
  g_unsetenv("NIRI_CONFIG");
  if (with_niri) {
    g_autofree char *niri_dir = g_build_filename(config, "niri", NULL);
    g_autofree char *niri_config =
        g_build_filename(niri_dir, "config.kdl", NULL);
    g_assert_cmpint(g_mkdir_with_parents(niri_dir, 0700), ==, 0);
    g_assert_true(g_file_set_contents(niri_config, "// test\n", -1, NULL));
  }
}

static void test_portal_selection_and_decline(void) {
  reset_environment("active", FALSE);
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

  ShaulaShortcutPreferences preferences;
  shaula_shortcut_preferences_init_defaults(&preferences);
  g_set_str(&preferences.triggers[0], "Super+F5");
  g_set_str(&preferences.triggers[1], "Super+F6");
  g_set_str(&preferences.triggers[2], "Super+F7");
  g_set_str(&preferences.triggers[3], "Super+F8");
  ShaulaShortcutOptions preference_options = {.remember_choice = FALSE};
  g_assert_cmpint(shaula_shortcuts_preferences_apply(
                      &preferences, &preference_options, &status, NULL),
                  ==, SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpstr(status.triggers[0], ==, "Super+F5");
  g_assert_cmpstr(status.triggers[3], ==, "Super+F8");
  shaula_shortcut_preferences_clear(&preferences);

  g_assert_cmpint(shaula_shortcuts_disable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_DISABLED);
  g_assert_cmpint(status.choice, ==, SHAULA_SHORTCUT_CHOICE_DECLINED);
  g_assert_true(status.setup_completed);
  g_assert_cmpint(shaula_shortcuts_disable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  shaula_shortcut_status_clear(&status);
}

static void test_niri_fallback_idempotence(void) {
  reset_environment("unsupported", TRUE);
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  ShaulaShortcutOptions options = {.remember_choice = TRUE};
  g_assert_cmpint(shaula_shortcuts_enable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(status.backend, ==, SHAULA_SHORTCUT_BACKEND_NIRI);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_ACTIVE);

  g_autofree char *config =
      g_build_filename(test_root, "config", "niri", "config.kdl", NULL);
  g_autofree char *first = NULL;
  g_assert_true(g_file_get_contents(config, &first, NULL, NULL));
  g_assert_nonnull(g_strstr_len(first, -1, "BEGIN SHAULA MANAGED KEYBINDS"));

  g_assert_cmpint(shaula_shortcuts_enable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_autofree char *second = NULL;
  g_assert_true(g_file_get_contents(config, &second, NULL, NULL));
  g_assert_cmpstr(first, ==, second);

  g_assert_cmpint(shaula_shortcuts_disable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_autofree char *removed = NULL;
  g_assert_true(g_file_get_contents(config, &removed, NULL, NULL));
  g_assert_null(g_strstr_len(removed, -1, "BEGIN SHAULA MANAGED KEYBINDS"));
  g_assert_cmpint(shaula_shortcuts_disable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  shaula_shortcut_status_clear(&status);
}

static void test_niri_prefix_is_not_conflict(void) {
  reset_environment("unsupported", TRUE);
  g_autofree char *config =
      g_build_filename(test_root, "config", "niri", "config.kdl", NULL);
  g_assert_true(g_file_set_contents(
      config, "binds {\n CTRL+Shift+10 { spawn \"other\"; }\n}\n", -1,
      NULL));
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  ShaulaShortcutOptions options = {.remember_choice = TRUE};
  g_assert_cmpint(shaula_shortcuts_enable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(status.backend, ==, SHAULA_SHORTCUT_BACKEND_NIRI);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_ACTIVE);
  shaula_shortcut_status_clear(&status);
}

static void test_niri_conflict_and_force(void) {
  reset_environment("unsupported", TRUE);
  g_autofree char *config =
      g_build_filename(test_root, "config", "niri", "config.kdl", NULL);
  g_assert_true(g_file_set_contents(
      config, "binds {\n CTRL+Shift+1 { spawn \"other\"; }\n}\n", -1,
      NULL));
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  ShaulaShortcutOptions options = {.remember_choice = TRUE};
  g_assert_cmpint(shaula_shortcuts_enable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_CONFLICT);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_CONFLICT);
  g_assert_cmpint(shaula_shortcuts_repair(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_CONFLICT);
  g_autofree char *conflicted = NULL;
  g_assert_true(g_file_get_contents(config, &conflicted, NULL, NULL));
  g_assert_null(
      g_strstr_len(conflicted, -1, "BEGIN SHAULA MANAGED KEYBINDS"));
  options.force = TRUE;
  g_assert_cmpint(shaula_shortcuts_enable(&options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(status.backend, ==, SHAULA_SHORTCUT_BACKEND_NIRI);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_ACTIVE);
  shaula_shortcut_status_clear(&status);
}

static void test_invalid_setup_state(void) {
  reset_environment("unsupported", FALSE);
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

static void test_preferences_validation_and_persistence(void) {
  reset_environment("unsupported", FALSE);
  ShaulaShortcutPreferences preferences;
  shaula_shortcut_preferences_init_defaults(&preferences);
  g_assert_cmpstr(preferences.triggers[0], ==, "Ctrl+Shift+1");

  g_set_str(&preferences.triggers[0], "super+f8");
  g_set_str(&preferences.triggers[1], "ctrl+alt+a");
  g_set_str(&preferences.triggers[2], "Shift+Super+Print");
  g_set_str(&preferences.triggers[3], "control+shift+9");
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  ShaulaShortcutOptions options = {.remember_choice = FALSE};
  g_autofree char *error = NULL;
  g_assert_cmpint(shaula_shortcuts_preferences_apply(
                      &preferences, &options, &status, &error),
                  ==, SHAULA_SHORTCUT_RESULT_OK);
  g_assert_null(error);

  ShaulaShortcutPreferences loaded;
  shaula_shortcut_preferences_init_defaults(&loaded);
  g_assert_cmpint(shaula_shortcuts_preferences_load(&loaded, &error), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpstr(loaded.triggers[0], ==, "Super+F8");
  g_assert_cmpstr(loaded.triggers[1], ==, "Ctrl+Alt+A");
  g_assert_cmpstr(loaded.triggers[2], ==, "Shift+Super+Print");
  g_assert_cmpstr(loaded.triggers[3], ==, "Ctrl+Shift+9");

  g_set_str(&preferences.triggers[1], "SUPER+F8");
  g_assert_cmpint(shaula_shortcuts_preferences_apply(
                      &preferences, &options, &status, &error),
                  ==, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  g_assert_nonnull(error);
  g_clear_pointer(&error, g_free);
  ShaulaShortcutPreferences unchanged;
  shaula_shortcut_preferences_init_defaults(&unchanged);
  g_assert_cmpint(shaula_shortcuts_preferences_load(&unchanged, &error), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpstr(unchanged.triggers[1], ==, "Ctrl+Alt+A");

  shaula_shortcut_preferences_clear(&unchanged);
  shaula_shortcut_preferences_clear(&loaded);
  shaula_shortcut_preferences_clear(&preferences);
  shaula_shortcut_status_clear(&status);
}

static void test_niri_custom_preferences_apply(void) {
  reset_environment("unsupported", TRUE);
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  ShaulaShortcutOptions enable_options = {.remember_choice = TRUE};
  g_assert_cmpint(shaula_shortcuts_enable(&enable_options, &status, NULL), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpint(status.backend, ==, SHAULA_SHORTCUT_BACKEND_NIRI);

  ShaulaShortcutPreferences preferences;
  shaula_shortcut_preferences_init_defaults(&preferences);
  g_set_str(&preferences.triggers[0], "Super+F5");
  g_set_str(&preferences.triggers[1], "Super+F6");
  g_set_str(&preferences.triggers[2], "Super+F7");
  g_set_str(&preferences.triggers[3], "Super+F8");
  ShaulaShortcutOptions apply_options = {.remember_choice = FALSE};
  g_autofree char *error = NULL;
  g_assert_cmpint(shaula_shortcuts_preferences_apply(
                      &preferences, &apply_options, &status, &error),
                  ==, SHAULA_SHORTCUT_RESULT_OK);
  g_assert_null(error);
  g_assert_cmpint(status.backend, ==, SHAULA_SHORTCUT_BACKEND_NIRI);
  g_assert_cmpint(status.state, ==, SHAULA_SHORTCUT_STATE_ACTIVE);
  g_assert_cmpstr(status.triggers[0], ==, "Super+F5");
  g_assert_nonnull(g_strstr_len(status.detail, -1,
                               "Live Niri config reloaded."));

  g_autofree char *config =
      g_build_filename(test_root, "config", "niri", "config.kdl", NULL);
  g_autofree char *contents = NULL;
  g_assert_true(g_file_get_contents(config, &contents, NULL, NULL));
  g_assert_nonnull(g_strstr_len(contents, -1, "Super+F5 repeat=false"));
  g_assert_nonnull(g_strstr_len(contents, -1, "Super+F8 repeat=false"));
  g_assert_null(g_strstr_len(contents, -1, "Ctrl+Shift+1 repeat=false"));

  g_assert_cmpint(shaula_shortcuts_query(&status, &error), ==,
                  SHAULA_SHORTCUT_RESULT_OK);
  g_assert_cmpstr(status.triggers[3], ==, "Super+F8");
  shaula_shortcut_preferences_clear(&preferences);
  shaula_shortcut_status_clear(&status);
}

static void test_unsupported_and_first_run(void) {
  reset_environment("unsupported", FALSE);
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
  g_test_add_func("/shortcuts/niri-fallback-idempotence",
                  test_niri_fallback_idempotence);
  g_test_add_func("/shortcuts/niri-prefix-not-conflict",
                  test_niri_prefix_is_not_conflict);
  g_test_add_func("/shortcuts/niri-conflict-force",
                  test_niri_conflict_and_force);
  g_test_add_func("/shortcuts/invalid-setup-state",
                  test_invalid_setup_state);
  g_test_add_func("/shortcuts/preferences-validation-persistence",
                  test_preferences_validation_and_persistence);
  g_test_add_func("/shortcuts/niri-custom-preferences",
                  test_niri_custom_preferences_apply);
  g_test_add_func("/shortcuts/unsupported-first-run",
                  test_unsupported_and_first_run);
  int result = g_test_run();
  if (test_root != NULL)
    g_assert_true(remove_tree(test_root));
  g_free(test_root);
  return result;
}
