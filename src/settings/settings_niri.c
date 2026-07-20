#include "settings_niri.h"

#include <json-glib/json-glib.h>

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
