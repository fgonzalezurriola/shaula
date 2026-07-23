#include "niri_adapter.h"

#include "config/niri_managed.h"

#include <string.h>

static guint count_text(const char *text, const char *needle) {
  guint count = 0;
  if (text == NULL)
    return 0;
  for (const char *cursor = text; (cursor = strstr(cursor, needle)) != NULL;
       cursor += strlen(needle))
    count++;
  return count;
}

static void set_error(char **error_text, const char *message) {
  if (error_text == NULL)
    return;
  g_clear_pointer(error_text, g_free);
  *error_text = g_strdup(message);
}

static gboolean run_niri_command(char **argv) {
  int exit_status = 0;
  g_autoptr(GError) error = NULL;
  return g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
                      NULL, &exit_status, &error) &&
         exit_status == 0;
}

/*
 * A managed-block update is complete even when live Niri IPC is unavailable.
 * Reload failure is advisory and leaves a deterministic manual recovery path.
 */
static gboolean reload_niri_config(const char *path) {
  if (g_getenv("SHAULA_SHORTCUTS_TEST_PORTAL") != NULL)
    return TRUE;
  if (path == NULL)
    return FALSE;
  char *validate_argv[] = {"niri", "validate", "-c", (char *)path, NULL};
  char *reload_argv[] = {"niri", "msg", "action", "load-config-file", NULL};
  return run_niri_command(validate_argv) && run_niri_command(reload_argv);
}

static void initialize_niri_status(
    ShaulaShortcutStatus *status,
    const ShaulaShortcutPreferences *preferences) {
  shaula_shortcut_status_clear(status);
  status->backend = SHAULA_SHORTCUT_BACKEND_NIRI;
  for (guint i = 0; i < SHAULA_SHORTCUT_COUNT; i++)
    status->triggers[i] = g_strdup(preferences->triggers[i]);
}

static ShaulaShortcutResult load_preferences(
    ShaulaShortcutPreferences *preferences, ShaulaShortcutStatus *status,
    char **error_text) {
  shaula_shortcut_preferences_init_defaults(preferences);
  ShaulaShortcutResult result =
      shaula_shortcuts_preferences_load(preferences, error_text);
  if (result == SHAULA_SHORTCUT_RESULT_OK)
    return result;
  shaula_shortcut_status_clear(status);
  status->backend = SHAULA_SHORTCUT_BACKEND_NIRI;
  status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
  status->error_code = g_strdup("ERR_SHORTCUT_CONFIGURATION_INVALID");
  status->detail = g_strdup(error_text != NULL && *error_text != NULL
                                ? *error_text
                                : "Shortcut preferences are invalid.");
  return result;
}

ShaulaShortcutResult
shaula_shortcut_niri_query(ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);

  ShaulaShortcutPreferences preferences;
  ShaulaShortcutResult preferences_result =
      load_preferences(&preferences, status, error_text);
  if (preferences_result != SHAULA_SHORTCUT_RESULT_OK)
    return preferences_result;
  initialize_niri_status(status, &preferences);

  g_autofree char *path = niri_config_path(NULL);
  if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
    status->state = SHAULA_SHORTCUT_STATE_UNSUPPORTED;
    status->error_code = g_strdup("ERR_SHORTCUTS_UNSUPPORTED");
    status->detail = g_strdup("Niri configuration was not detected.");
    shaula_shortcut_preferences_clear(&preferences);
    return SHAULA_SHORTCUT_RESULT_OK;
  }

  g_autofree char *contents = NULL;
  if (!g_file_get_contents(path, &contents, NULL, NULL)) {
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code = g_strdup("ERR_CONFIG_UNREADABLE");
    status->detail = g_strdup(path);
    set_error(error_text, "ERR_CONFIG_UNREADABLE: Niri config could not be read");
    shaula_shortcut_preferences_clear(&preferences);
    return SHAULA_SHORTCUT_RESULT_IO_FAILED;
  }

  const guint begins =
      count_text(contents, "// BEGIN SHAULA MANAGED KEYBINDS");
  const guint ends = count_text(contents, "// END SHAULA MANAGED KEYBINDS");
  status->detail = g_strdup(path);
  if (begins > 1U || ends > 1U || begins != ends) {
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code = g_strdup("ERR_CONFIG_INVALID");
    set_error(error_text,
              "ERR_CONFIG_INVALID: invalid Shaula keybinding markers in Niri config");
    shaula_shortcut_preferences_clear(&preferences);
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }

  g_autoptr(GPtrArray) conflicts = niri_keybind_conflicts(
      path, (const char *const *)preferences.triggers, SHAULA_SHORTCUT_COUNT);
  if (begins == 1U) {
    status->state = SHAULA_SHORTCUT_STATE_ACTIVE;
    status->activation_ready = TRUE;
  } else if (conflicts != NULL && conflicts->len > 0U) {
    status->state = SHAULA_SHORTCUT_STATE_CONFLICT;
    status->error_code = g_strdup("ERR_NIRI_KEYBIND_CONFLICT");
    GString *detail = g_string_new(path);
    g_string_append(detail, "\nConflicting bindings:");
    for (guint i = 0; i < conflicts->len; i++)
      g_string_append_printf(detail, "\n• %s",
                             (const char *)g_ptr_array_index(conflicts, i));
    g_free(status->detail);
    status->detail = g_string_free(detail, FALSE);
  } else {
    status->state = SHAULA_SHORTCUT_STATE_DISABLED;
  }
  shaula_shortcut_preferences_clear(&preferences);
  return SHAULA_SHORTCUT_RESULT_OK;
}

ShaulaShortcutResult shaula_shortcut_niri_apply_preferences(
    const ShaulaShortcutOptions *options,
    const ShaulaShortcutPreferences *preferences,
    ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(preferences != NULL,
                       SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  const ShaulaShortcutOptions defaults = {.remember_choice = TRUE};
  if (options == NULL)
    options = &defaults;
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);
  initialize_niri_status(status, preferences);

  g_autofree char *path = niri_config_path(NULL);
  if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
    status->state = SHAULA_SHORTCUT_STATE_UNSUPPORTED;
    status->error_code = g_strdup("ERR_SHORTCUTS_UNSUPPORTED");
    status->detail = g_strdup("Niri configuration was not detected.");
    return SHAULA_SHORTCUT_RESULT_OK;
  }

  g_autoptr(GPtrArray) conflicts = niri_keybind_conflicts(
      path, (const char *const *)preferences->triggers,
      SHAULA_SHORTCUT_COUNT);
  if (conflicts != NULL && conflicts->len > 0U && !options->force) {
    status->state = SHAULA_SHORTCUT_STATE_CONFLICT;
    status->error_code = g_strdup("ERR_NIRI_KEYBIND_CONFLICT");
    GString *detail = g_string_new(path);
    g_string_append(detail, "\nConflicting bindings:");
    for (guint i = 0; i < conflicts->len; i++)
      g_string_append_printf(detail, "\n• %s",
                             (const char *)g_ptr_array_index(conflicts, i));
    status->detail = g_string_free(detail, FALSE);
    set_error(error_text,
              "ERR_NIRI_KEYBIND_CONFLICT: a configured capture shortcut is already in use");
    return SHAULA_SHORTCUT_RESULT_CONFLICT;
  }

  g_autofree char *payload = shaula_niri_keybinds_render(
      (const char *const *)preferences->triggers, SHAULA_SHORTCUT_COUNT);
  ManagedBlockResult result = {0};
  gboolean invalid = FALSE;
  if (payload == NULL ||
      !install_managed_block(NULL, "// BEGIN SHAULA MANAGED KEYBINDS",
                             "// END SHAULA MANAGED KEYBINDS", payload,
                             options->dry_run, &result, &invalid)) {
    managed_block_result_clear(&result);
    initialize_niri_status(status, preferences);
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code =
        g_strdup(invalid ? "ERR_CONFIG_INVALID" : "ERR_CONFIG_UNREADABLE");
    set_error(error_text,
              invalid ? "ERR_CONFIG_INVALID: invalid Niri managed keybinding block"
                      : "ERR_CONFIG_UNREADABLE: Niri keybindings could not be written");
    return invalid ? SHAULA_SHORTCUT_RESULT_CONFIG_INVALID
                   : SHAULA_SHORTCUT_RESULT_IO_FAILED;
  }
  const gboolean changed = result.changed;
  managed_block_result_clear(&result);
  if (options->dry_run) {
    initialize_niri_status(status, preferences);
    status->state = SHAULA_SHORTCUT_STATE_ACTIVE;
    status->activation_ready = TRUE;
    status->detail = g_strdup("Niri keybindings would be installed.");
    return SHAULA_SHORTCUT_RESULT_OK;
  }
  initialize_niri_status(status, preferences);
  status->state = SHAULA_SHORTCUT_STATE_ACTIVE;
  status->activation_ready = TRUE;
  g_autofree char *config_path = niri_config_path(NULL);
  if (changed) {
    gboolean reloaded = reload_niri_config(config_path);
    status->detail = g_strdup_printf(
        "%s\n%s", config_path != NULL ? config_path : "Niri config",
        reloaded
            ? "Live Niri config reloaded."
            : "Bindings saved; run `niri validate -c <config> && niri msg action load-config-file`.");
  } else {
    status->detail = g_strdup(config_path != NULL ? config_path
                                                  : "Niri config is active.");
  }
  return SHAULA_SHORTCUT_RESULT_OK;
}

ShaulaShortcutResult
shaula_shortcut_niri_enable(const ShaulaShortcutOptions *options,
                            ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  const ShaulaShortcutOptions defaults = {.remember_choice = TRUE};
  if (options == NULL)
    options = &defaults;

  ShaulaShortcutResult queried =
      shaula_shortcut_niri_query(status, error_text);
  if (queried != SHAULA_SHORTCUT_RESULT_OK &&
      status->state != SHAULA_SHORTCUT_STATE_CONFLICT)
    return queried;
  if (status->state == SHAULA_SHORTCUT_STATE_UNSUPPORTED ||
      status->state == SHAULA_SHORTCUT_STATE_ACTIVE)
    return SHAULA_SHORTCUT_RESULT_OK;
  if (status->state == SHAULA_SHORTCUT_STATE_CONFLICT && !options->force) {
    set_error(error_text,
              "ERR_NIRI_KEYBIND_CONFLICT: a configured capture shortcut is already in use");
    return SHAULA_SHORTCUT_RESULT_CONFLICT;
  }

  ShaulaShortcutPreferences preferences;
  ShaulaShortcutResult preferences_result =
      load_preferences(&preferences, status, error_text);
  if (preferences_result != SHAULA_SHORTCUT_RESULT_OK)
    return preferences_result;
  ShaulaShortcutResult result = shaula_shortcut_niri_apply_preferences(
      options, &preferences, status, error_text);
  shaula_shortcut_preferences_clear(&preferences);
  return result;
}

ShaulaShortcutResult
shaula_shortcut_niri_disable(const ShaulaShortcutOptions *options,
                             ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  const ShaulaShortcutOptions defaults = {.remember_choice = TRUE};
  if (options == NULL)
    options = &defaults;
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);

  ShaulaShortcutPreferences preferences;
  ShaulaShortcutResult preferences_result =
      load_preferences(&preferences, status, error_text);
  if (preferences_result != SHAULA_SHORTCUT_RESULT_OK)
    return preferences_result;

  g_autofree char *path = niri_config_path(NULL);
  if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
    initialize_niri_status(status, &preferences);
    status->state = SHAULA_SHORTCUT_STATE_DISABLED;
    status->detail = g_strdup("No managed Niri keybindings were present.");
    shaula_shortcut_preferences_clear(&preferences);
    return SHAULA_SHORTCUT_RESULT_OK;
  }

  ManagedBlockResult result = {0};
  gboolean invalid = FALSE;
  if (!remove_managed_keybinds(NULL, options->dry_run, &result, &invalid)) {
    managed_block_result_clear(&result);
    initialize_niri_status(status, &preferences);
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code =
        g_strdup(invalid ? "ERR_CONFIG_INVALID" : "ERR_CONFIG_UNREADABLE");
    set_error(error_text,
              invalid ? "ERR_CONFIG_INVALID: invalid Niri managed keybinding block"
                      : "ERR_CONFIG_UNREADABLE: Niri keybindings could not be removed");
    shaula_shortcut_preferences_clear(&preferences);
    return invalid ? SHAULA_SHORTCUT_RESULT_CONFIG_INVALID
                   : SHAULA_SHORTCUT_RESULT_IO_FAILED;
  }
  managed_block_result_clear(&result);
  if (options->dry_run) {
    initialize_niri_status(status, &preferences);
    status->state = SHAULA_SHORTCUT_STATE_DISABLED;
    status->detail = g_strdup("Niri keybindings would be removed.");
    shaula_shortcut_preferences_clear(&preferences);
    return SHAULA_SHORTCUT_RESULT_OK;
  }
  shaula_shortcut_preferences_clear(&preferences);
  ShaulaShortcutResult queried =
      shaula_shortcut_niri_query(status, error_text);
  if (queried == SHAULA_SHORTCUT_RESULT_OK &&
      status->state == SHAULA_SHORTCUT_STATE_UNSUPPORTED)
    status->state = SHAULA_SHORTCUT_STATE_DISABLED;
  return queried;
}
