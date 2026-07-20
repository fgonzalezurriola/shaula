#include "commands.h"

#include "shortcuts/shortcuts.h"
#include "support.h"

#include <glib.h>

static char *shortcuts_status_json(const ShaulaShortcutStatus *status) {
  g_autofree char *detail =
      status->detail != NULL ? shaula_command_json_string(status->detail)
                             : g_strdup("null");
  g_autofree char *error_code =
      status->error_code != NULL && status->error_code[0] != '\0'
          ? shaula_command_json_string(status->error_code)
          : g_strdup("null");
  GString *triggers = g_string_new("[");
  for (guint i = 0; i < G_N_ELEMENTS(status->triggers); i++) {
    g_autofree char *trigger = shaula_command_json_string(
        status->triggers[i] != NULL ? status->triggers[i] : "");
    if (i > 0U)
      g_string_append_c(triggers, ',');
    g_string_append(triggers, trigger);
  }
  g_string_append_c(triggers, ']');
  g_autofree char *triggers_json = g_string_free(triggers, FALSE);
  return g_strdup_printf(
      "{\"setup_completed\":%s,\"choice\":\"%s\","
      "\"enabled_requested\":%s,\"backend\":\"%s\","
      "\"state\":\"%s\",\"autostart_installed\":%s,"
      "\"provider_running\":%s,\"portal_version\":%u,"
      "\"activation_ready\":%s,\"detail\":%s,\"error_code\":%s,"
      "\"triggers\":%s}",
      shaula_command_json_bool(status->setup_completed),
      shaula_shortcut_choice_token(status->choice),
      shaula_command_json_bool(status->enabled_requested),
      shaula_shortcut_backend_token(status->backend),
      shaula_shortcut_state_token(status->state),
      shaula_command_json_bool(status->autostart_installed),
      shaula_command_json_bool(status->provider_running), status->portal_version,
      shaula_command_json_bool(status->activation_ready), detail, error_code,
      triggers_json);
}

static int shortcut_result_error(const char *command, ShaulaShortcutResult result,
                                 const ShaulaShortcutStatus *status,
                                 const char *error_text) {
  const char *code = status->error_code;
  if (code == NULL || code[0] == '\0') {
    if (result == SHAULA_SHORTCUT_RESULT_CONFLICT)
      code = "ERR_NIRI_KEYBIND_CONFLICT";
    else if (result == SHAULA_SHORTCUT_RESULT_PROVIDER_FAILED)
      code = "ERR_SHORTCUT_PROVIDER_UNAVAILABLE";
    else if (result == SHAULA_SHORTCUT_RESULT_CONFIG_INVALID)
      code = "ERR_SHORTCUT_CONFIGURATION_INVALID";
    else
      code = "ERR_CONFIG_UNREADABLE";
  }
  g_autofree char *state =
      shaula_command_json_string(shaula_shortcut_state_token(status->state));
  g_autofree char *backend =
      shaula_command_json_string(shaula_shortcut_backend_token(status->backend));
  g_autofree char *details = g_strdup_printf(
      "{\"state\":%s,\"backend\":%s}", state, backend);
  return shaula_command_write_error(
      command, code,
      error_text != NULL && error_text[0] != '\0'
          ? error_text
          : "shortcut operation failed",
      details);
}

int shaula_shortcuts_command_run(int argc, char **argv) {
  if (argc < 4)
    return shaula_command_write_error(
        "shortcuts", "ERR_CLI_USAGE",
        "usage: shaula shortcuts <status|enable|disable|repair|decline> --json",
        "{}");
  const char *operation = argv[2];
  gboolean json = FALSE;
  ShaulaShortcutOptions options = {.remember_choice = TRUE};
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--json"))
      json = TRUE;
    else if (g_str_equal(argv[i], "--force"))
      options.force = TRUE;
    else if (g_str_equal(argv[i], "--dry-run"))
      options.dry_run = TRUE;
    else
      return shaula_command_write_error("shortcuts", "ERR_CLI_USAGE",
                                        "unsupported shortcuts flag", "{}");
  }
  if (!json)
    return shaula_command_write_error("shortcuts", "ERR_CLI_USAGE",
                                      "--json is required", "{}");

  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  g_autofree char *error_text = NULL;
  ShaulaShortcutResult result;
  g_autofree char *command = g_strdup_printf("shortcuts %s", operation);
  if (g_str_equal(operation, "status")) {
    result = shaula_shortcuts_query(&status, &error_text);
  } else if (g_str_equal(operation, "enable")) {
    result = shaula_shortcuts_enable(&options, &status, &error_text);
  } else if (g_str_equal(operation, "disable")) {
    result = shaula_shortcuts_disable(&options, &status, &error_text);
  } else if (g_str_equal(operation, "repair")) {
    result = shaula_shortcuts_repair(&options, &status, &error_text);
  } else if (g_str_equal(operation, "decline")) {
    result = shaula_shortcuts_disable(&options, &status, &error_text);
  } else {
    shaula_shortcut_status_clear(&status);
    return shaula_command_write_error("shortcuts", "ERR_CLI_USAGE",
                                      "unsupported shortcuts operation", "{}");
  }

  if (result != SHAULA_SHORTCUT_RESULT_OK) {
    int exit_code =
        shortcut_result_error(command, result, &status, error_text);
    shaula_shortcut_status_clear(&status);
    return exit_code;
  }
  g_autofree char *body = shortcuts_status_json(&status);
  int exit_code = shaula_command_write_success(command, body, "[]");
  shaula_shortcut_status_clear(&status);
  return exit_code;
}
