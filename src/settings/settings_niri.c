#include "settings_niri.h"

#include "settings_process.h"

#include <json-glib/json-glib.h>

void shaula_settings_niri_status_init(ShaulaSettingsNiriStatus *status) {
  g_return_if_fail(status != NULL);
  *status = (ShaulaSettingsNiriStatus){0};
}

void shaula_settings_niri_status_clear(ShaulaSettingsNiriStatus *status) {
  if (status == NULL)
    return;
  g_clear_pointer(&status->config_path, g_free);
  status->detected = FALSE;
  status->shortcuts_installed = FALSE;
  status->shortcuts_conflict = FALSE;
}

static ShaulaSettingsNiriResult run_niri_command(char *const *argv,
                                                 char **stdout_text,
                                                 char **error_text) {
  g_autofree char *out = NULL;
  g_autofree char *err = NULL;
  int exit_code = 1;
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);
  gboolean started =
      shaula_settings_run_command(argv, &out, &err, &exit_code);
  if (!started || exit_code != 0) {
    if (error_text != NULL) {
      *error_text = g_strdup_printf(
          "%s%s", out != NULL && out[0] != '\0' ? out : "",
          err != NULL && err[0] != '\0' ? err : "");
      if ((*error_text)[0] == '\0')
        g_set_str(error_text, "ERR_CONFIG_UNREADABLE: Niri command failed");
    }
    return SHAULA_SETTINGS_NIRI_COMMAND_FAILED;
  }
  if (stdout_text != NULL)
    *stdout_text = g_steal_pointer(&out);
  return SHAULA_SETTINGS_NIRI_OK;
}

static JsonObject *result_object(JsonParser *parser, const char *json) {
  g_autoptr(GError) error = NULL;
  if (json == NULL || !json_parser_load_from_data(parser, json, -1, &error))
    return NULL;
  JsonNode *root = json_parser_get_root(parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root))
    return NULL;
  JsonObject *envelope = json_node_get_object(root);
  if (!json_object_has_member(envelope, "result"))
    return NULL;
  JsonNode *result = json_object_get_member(envelope, "result");
  return result != NULL && JSON_NODE_HOLDS_OBJECT(result)
             ? json_node_get_object(result)
             : NULL;
}

static gboolean required_boolean(JsonObject *object, const char *name,
                                  gboolean *value) {
  if (!json_object_has_member(object, name))
    return FALSE;
  JsonNode *node = json_object_get_member(object, name);
  if (node == NULL || json_node_get_value_type(node) != G_TYPE_BOOLEAN)
    return FALSE;
  *value = json_node_get_boolean(node);
  return TRUE;
}

ShaulaSettingsNiriResult shaula_settings_niri_load(
    const char *shaula_bin, ShaulaSettingsNiriStatus *status,
    char **error_text) {
  g_return_val_if_fail(shaula_bin != NULL && shaula_bin[0] != '\0',
                       SHAULA_SETTINGS_NIRI_COMMAND_FAILED);
  g_return_val_if_fail(status != NULL,
                       SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID);
  char *argv[] = {(char *)shaula_bin, "config", "niri-keybinds-status",
                  "--json", NULL};
  g_autofree char *json = NULL;
  ShaulaSettingsNiriResult command =
      run_niri_command(argv, &json, error_text);
  if (command != SHAULA_SETTINGS_NIRI_OK)
    return command;

  g_autoptr(JsonParser) parser = json_parser_new();
  JsonObject *result = result_object(parser, json);
  ShaulaSettingsNiriStatus parsed = {0};
  if (result == NULL ||
      !required_boolean(result, "niri_detected", &parsed.detected) ||
      !required_boolean(result, "installed", &parsed.shortcuts_installed) ||
      !json_object_has_member(result, "conflicts")) {
    if (error_text != NULL)
      g_set_str(error_text, "ERR_CONFIG_INVALID: invalid Niri status response");
    return SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID;
  }

  JsonNode *path = json_object_get_member(result, "config_path");
  if (path != NULL && JSON_NODE_HOLDS_VALUE(path) &&
      json_node_get_value_type(path) == G_TYPE_STRING) {
    parsed.config_path = g_strdup(json_node_get_string(path));
  } else if (path != NULL && !JSON_NODE_HOLDS_NULL(path)) {
    if (error_text != NULL)
      g_set_str(error_text, "ERR_CONFIG_INVALID: invalid Niri config path");
    return SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID;
  }

  JsonNode *conflicts = json_object_get_member(result, "conflicts");
  if (conflicts == NULL || !JSON_NODE_HOLDS_ARRAY(conflicts)) {
    shaula_settings_niri_status_clear(&parsed);
    if (error_text != NULL)
      g_set_str(error_text, "ERR_CONFIG_INVALID: invalid Niri conflicts");
    return SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID;
  }
  parsed.shortcuts_conflict =
      json_array_get_length(json_node_get_array(conflicts)) > 0;

  shaula_settings_niri_status_clear(status);
  *status = parsed;
  return SHAULA_SETTINGS_NIRI_OK;
}

ShaulaSettingsNiriResult shaula_settings_niri_install(
    const char *shaula_bin, gboolean force, char **error_text) {
  g_return_val_if_fail(shaula_bin != NULL && shaula_bin[0] != '\0',
                       SHAULA_SETTINGS_NIRI_COMMAND_FAILED);
  char *argv[] = {(char *)shaula_bin,
                  "config",
                  "niri-keybinds-install",
                  "--json",
                  force ? "--force" : NULL,
                  NULL};
  return run_niri_command(argv, NULL, error_text);
}

ShaulaSettingsNiriResult shaula_settings_niri_remove(const char *shaula_bin,
                                                     char **error_text) {
  g_return_val_if_fail(shaula_bin != NULL && shaula_bin[0] != '\0',
                       SHAULA_SETTINGS_NIRI_COMMAND_FAILED);
  char *argv[] = {(char *)shaula_bin, "config", "niri-keybinds-remove",
                  "--json", NULL};
  return run_niri_command(argv, NULL, error_text);
}

ShaulaSettingsNiriResult shaula_settings_niri_rule_changed(const char *json,
                                                           gboolean *changed) {
  g_return_val_if_fail(changed != NULL,
                       SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID);
  *changed = FALSE;
  g_autoptr(JsonParser) parser = json_parser_new();
  JsonObject *result = result_object(parser, json);
  if (result == NULL || !json_object_has_member(result, "niri"))
    return SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID;
  JsonNode *niri = json_object_get_member(result, "niri");
  if (niri == NULL || !JSON_NODE_HOLDS_OBJECT(niri))
    return SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID;
  return required_boolean(json_node_get_object(niri), "changed", changed)
             ? SHAULA_SETTINGS_NIRI_OK
             : SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID;
}
