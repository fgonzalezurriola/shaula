#include "niri_adapter.h"

#include "config/niri_managed.h"

#include <string.h>

static const char *const preferred_triggers[] = {
    "Ctrl+Shift+1", "Ctrl+Shift+2", "Ctrl+Shift+3", "Ctrl+Shift+4"};

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

static void initialize_niri_status(ShaulaShortcutStatus *status) {
  shaula_shortcut_status_clear(status);
  status->backend = SHAULA_SHORTCUT_BACKEND_NIRI;
  for (guint i = 0; i < G_N_ELEMENTS(preferred_triggers); i++)
    status->triggers[i] = g_strdup(preferred_triggers[i]);
}

ShaulaShortcutResult
shaula_shortcut_niri_query(ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  initialize_niri_status(status);
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);

  g_autofree char *path = niri_config_path(NULL);
  if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
    status->state = SHAULA_SHORTCUT_STATE_UNSUPPORTED;
    status->error_code = g_strdup("ERR_SHORTCUTS_UNSUPPORTED");
    status->detail = g_strdup("Niri configuration was not detected.");
    return SHAULA_SHORTCUT_RESULT_OK;
  }

  g_autofree char *contents = NULL;
  if (!g_file_get_contents(path, &contents, NULL, NULL)) {
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code = g_strdup("ERR_CONFIG_UNREADABLE");
    status->detail = g_strdup(path);
    set_error(error_text, "ERR_CONFIG_UNREADABLE: Niri config could not be read");
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
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }

  g_autoptr(GPtrArray) conflicts = niri_keybind_conflicts(path);
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
  if (status->state == SHAULA_SHORTCUT_STATE_UNSUPPORTED)
    return SHAULA_SHORTCUT_RESULT_OK;
  if (status->state == SHAULA_SHORTCUT_STATE_ACTIVE)
    return SHAULA_SHORTCUT_RESULT_OK;
  if (status->state == SHAULA_SHORTCUT_STATE_CONFLICT && !options->force) {
    set_error(error_text,
              "ERR_NIRI_KEYBIND_CONFLICT: existing Ctrl+Shift+1-4 binding detected");
    return SHAULA_SHORTCUT_RESULT_CONFLICT;
  }

  g_autofree char *payload = shaula_niri_keybinds_render();
  ManagedBlockResult result = {0};
  gboolean invalid = FALSE;
  if (!install_managed_block(NULL, "// BEGIN SHAULA MANAGED KEYBINDS",
                             "// END SHAULA MANAGED KEYBINDS", payload,
                             options->dry_run, &result, &invalid)) {
    managed_block_result_clear(&result);
    initialize_niri_status(status);
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code =
        g_strdup(invalid ? "ERR_CONFIG_INVALID" : "ERR_CONFIG_UNREADABLE");
    set_error(error_text,
              invalid ? "ERR_CONFIG_INVALID: invalid Niri managed keybinding block"
                      : "ERR_CONFIG_UNREADABLE: Niri keybindings could not be written");
    return invalid ? SHAULA_SHORTCUT_RESULT_CONFIG_INVALID
                   : SHAULA_SHORTCUT_RESULT_IO_FAILED;
  }
  managed_block_result_clear(&result);
  if (options->dry_run) {
    initialize_niri_status(status);
    status->state = SHAULA_SHORTCUT_STATE_ACTIVE;
    status->activation_ready = TRUE;
    status->detail = g_strdup("Niri keybindings would be installed.");
    return SHAULA_SHORTCUT_RESULT_OK;
  }
  return shaula_shortcut_niri_query(status, error_text);
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

  g_autofree char *path = niri_config_path(NULL);
  if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
    initialize_niri_status(status);
    status->state = SHAULA_SHORTCUT_STATE_DISABLED;
    status->detail = g_strdup("No managed Niri keybindings were present.");
    return SHAULA_SHORTCUT_RESULT_OK;
  }

  ManagedBlockResult result = {0};
  gboolean invalid = FALSE;
  if (!remove_managed_keybinds(NULL, options->dry_run, &result, &invalid)) {
    managed_block_result_clear(&result);
    initialize_niri_status(status);
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code =
        g_strdup(invalid ? "ERR_CONFIG_INVALID" : "ERR_CONFIG_UNREADABLE");
    set_error(error_text,
              invalid ? "ERR_CONFIG_INVALID: invalid Niri managed keybinding block"
                      : "ERR_CONFIG_UNREADABLE: Niri keybindings could not be removed");
    return invalid ? SHAULA_SHORTCUT_RESULT_CONFIG_INVALID
                   : SHAULA_SHORTCUT_RESULT_IO_FAILED;
  }
  managed_block_result_clear(&result);
  if (options->dry_run) {
    initialize_niri_status(status);
    status->state = SHAULA_SHORTCUT_STATE_DISABLED;
    status->detail = g_strdup("Niri keybindings would be removed.");
    return SHAULA_SHORTCUT_RESULT_OK;
  }
  ShaulaShortcutResult queried =
      shaula_shortcut_niri_query(status, error_text);
  if (queried == SHAULA_SHORTCUT_RESULT_OK &&
      status->state == SHAULA_SHORTCUT_STATE_UNSUPPORTED)
    status->state = SHAULA_SHORTCUT_STATE_DISABLED;
  return queried;
}
