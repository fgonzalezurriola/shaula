#include "explore/inventory.h"

#include "runtime/process_exec.h"

#include <json-glib/json-glib.h>
#include <string.h>

static JsonObject *node_object(JsonNode *node) {
  return node != NULL && JSON_NODE_HOLDS_OBJECT(node)
             ? json_node_get_object(node)
             : NULL;
}

static JsonArray *node_array(JsonNode *node) {
  return node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node)
                                                     : NULL;
}

static JsonNode *object_member(JsonObject *object, const char *name) {
  return object != NULL && json_object_has_member(object, name)
             ? json_object_get_member(object, name)
             : NULL;
}

static const char *object_string(JsonObject *object, const char *name) {
  JsonNode *node = object_member(object, name);
  return node != NULL && JSON_NODE_HOLDS_VALUE(node) &&
                 json_node_get_value_type(node) == G_TYPE_STRING
             ? json_node_get_string(node)
             : NULL;
}

static gboolean object_boolean(JsonObject *object, const char *name,
                               gboolean *present) {
  JsonNode *node = object_member(object, name);
  if (node == NULL || !JSON_NODE_HOLDS_VALUE(node) ||
      json_node_get_value_type(node) != G_TYPE_BOOLEAN) {
    *present = FALSE;
    return FALSE;
  }
  *present = TRUE;
  return json_node_get_boolean(node);
}

static gboolean object_integer(JsonObject *object, const char *name,
                               gint64 *value) {
  JsonNode *node = object_member(object, name);
  if (node == NULL || !JSON_NODE_HOLDS_VALUE(node))
    return FALSE;
  GType type = json_node_get_value_type(node);
  if (type != G_TYPE_INT64 && type != G_TYPE_DOUBLE)
    return FALSE;
  *value = json_node_get_int(node);
  return TRUE;
}

static gboolean object_boolean_any(JsonObject *object, const char *first,
                                   const char *second) {
  gboolean present = FALSE;
  gboolean value = object_boolean(object, first, &present);
  if (present)
    return value;
  return object_boolean(object, second, &present);
}

static JsonNode *run_niri_json(const char *command) {
  const char *arguments[] = {"niri", "msg", "-j", command, NULL};
  ShaulaProcessOutput output = {0};
  if (shaula_process_run(arguments, NULL, 1024 * 1024, 16 * 1024, &output) !=
          SHAULA_PROCESS_STATUS_OK ||
      output.term_kind != SHAULA_PROCESS_TERM_EXITED || output.term_value != 0) {
    shaula_process_output_clear(&output);
    return NULL;
  }

  g_autofree char *text =
      g_strndup((const char *)output.stdout_bytes.data, output.stdout_bytes.length);
  shaula_process_output_clear(&output);
  g_strstrip(text);
  if (text[0] == '\0')
    return NULL;

  JsonParser *parser = json_parser_new();
  gboolean parsed = json_parser_load_from_data(parser, text, -1, NULL);
  JsonNode *root = parsed ? json_node_copy(json_parser_get_root(parser)) : NULL;
  g_object_unref(parser);
  return root;
}

static void add_nullable_string(JsonBuilder *builder, const char *name,
                                const char *value) {
  json_builder_set_member_name(builder, name);
  if (value != NULL)
    json_builder_add_string_value(builder, value);
  else
    json_builder_add_null_value(builder);
}

static void add_nullable_integer(JsonBuilder *builder, const char *name,
                                 gboolean present, gint64 value) {
  json_builder_set_member_name(builder, name);
  if (present)
    json_builder_add_int_value(builder, value);
  else
    json_builder_add_null_value(builder);
}

static const char *output_name(JsonObject *object, const char *fallback) {
  const char *name = object_string(object, "name");
  if (name == NULL)
    name = object_string(object, "id");
  return name != NULL ? name : fallback;
}

static JsonObject *output_logical_object(JsonObject *object) {
  JsonObject *logical = node_object(object_member(object, "logical"));
  if (logical == NULL)
    logical = node_object(object_member(object, "geometry"));
  return logical != NULL ? logical : object;
}

static void append_geometry(JsonBuilder *builder, JsonObject *object) {
  JsonObject *logical = output_logical_object(object);
  gint64 x = 0;
  gint64 y = 0;
  gint64 width = 0;
  gint64 height = 0;
  (void)object_integer(logical, "x", &x);
  (void)object_integer(logical, "y", &y);
  gboolean has_width = object_integer(logical, "width", &width);
  gboolean has_height = object_integer(logical, "height", &height);
  json_builder_set_member_name(builder, "geometry");
  if (!has_width || !has_height) {
    json_builder_add_null_value(builder);
    return;
  }
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "x");
  json_builder_add_int_value(builder, x);
  json_builder_set_member_name(builder, "y");
  json_builder_add_int_value(builder, y);
  json_builder_set_member_name(builder, "width");
  json_builder_add_int_value(builder, width);
  json_builder_set_member_name(builder, "height");
  json_builder_add_int_value(builder, height);
  json_builder_end_object(builder);
}

static JsonNode *output_scale_node(JsonObject *object) {
  JsonNode *scale = object_member(object, "scale");
  if (scale == NULL)
    scale = object_member(object, "logical_scale");
  if (scale == NULL) {
    JsonObject *logical = node_object(object_member(object, "logical"));
    scale = object_member(logical, "scale");
  }
  return scale != NULL && JSON_NODE_HOLDS_VALUE(scale) ? scale : NULL;
}

static void append_output(JsonBuilder *builder, JsonObject *object,
                          const char *fallback_name,
                          const char *focused_output_name) {
  const char *name = output_name(object, fallback_name);
  if (name == NULL)
    name = "";
  gboolean focused_present = FALSE;
  gboolean focused = object_boolean(object, "focused", &focused_present);
  if (!focused_present && focused_output_name != NULL)
    focused = g_str_equal(name, focused_output_name);

  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "id");
  json_builder_add_string_value(builder, name);
  json_builder_set_member_name(builder, "name");
  json_builder_add_string_value(builder, name);
  json_builder_set_member_name(builder, "focused");
  json_builder_add_boolean_value(builder, focused);
  append_geometry(builder, object);
  json_builder_set_member_name(builder, "scale");
  JsonNode *scale = output_scale_node(object);
  if (scale != NULL)
    json_builder_add_value(builder, json_node_copy(scale));
  else
    json_builder_add_null_value(builder);
  json_builder_end_object(builder);
}

static void append_outputs(JsonBuilder *builder, JsonNode *root,
                           const char *focused_output_name) {
  json_builder_set_member_name(builder, "outputs");
  json_builder_begin_array(builder);
  JsonArray *array = node_array(root);
  if (array != NULL) {
    guint length = json_array_get_length(array);
    for (guint i = 0; i < length; i++) {
      JsonObject *object = node_object(json_array_get_element(array, i));
      if (object != NULL)
        append_output(builder, object, NULL, focused_output_name);
    }
  } else {
    JsonObject *mapping = node_object(root);
    if (mapping != NULL) {
      GList *members = json_object_get_members(mapping);
      for (GList *item = members; item != NULL; item = item->next) {
        const char *name = item->data;
        JsonObject *object = node_object(json_object_get_member(mapping, name));
        if (object != NULL)
          append_output(builder, object, name, focused_output_name);
      }
      g_list_free(members);
    }
  }
  json_builder_end_array(builder);
}

static const char *workspace_output(JsonNode *workspaces_root,
                                    gint64 workspace_id) {
  JsonArray *array = node_array(workspaces_root);
  if (array == NULL)
    return NULL;
  guint length = json_array_get_length(array);
  for (guint i = 0; i < length; i++) {
    JsonObject *object = node_object(json_array_get_element(array, i));
    gint64 id = 0;
    if (object == NULL ||
        (!object_integer(object, "id", &id) &&
         !object_integer(object, "idx", &id)) ||
        id != workspace_id)
      continue;
    return object_string(object, "output");
  }
  return NULL;
}

static void append_workspaces(JsonBuilder *builder, JsonNode *root) {
  json_builder_set_member_name(builder, "workspaces");
  json_builder_begin_array(builder);
  JsonArray *array = node_array(root);
  if (array != NULL) {
    guint length = json_array_get_length(array);
    for (guint i = 0; i < length; i++) {
      JsonObject *object = node_object(json_array_get_element(array, i));
      if (object == NULL)
        continue;
      gint64 id = 0;
      if (!object_integer(object, "id", &id))
        (void)object_integer(object, "idx", &id);
      gboolean focused = object_boolean_any(object, "focused", "is_focused");
      gboolean active_present = FALSE;
      gboolean active = object_boolean(object, "active", &active_present);
      if (!active_present)
        active = object_boolean(object, "is_active", &active_present);
      if (!active_present)
        active = focused;

      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "id");
      json_builder_add_int_value(builder, id);
      add_nullable_string(builder, "name", object_string(object, "name"));
      add_nullable_string(builder, "output_id", object_string(object, "output"));
      json_builder_set_member_name(builder, "focused");
      json_builder_add_boolean_value(builder, focused);
      json_builder_set_member_name(builder, "active");
      json_builder_add_boolean_value(builder, active);
      json_builder_end_object(builder);
    }
  }
  json_builder_end_array(builder);
}

static void append_windows(JsonBuilder *builder, JsonNode *root,
                           JsonNode *workspaces_root) {
  json_builder_set_member_name(builder, "windows");
  json_builder_begin_array(builder);
  JsonArray *array = node_array(root);
  if (array != NULL) {
    guint length = json_array_get_length(array);
    for (guint i = 0; i < length; i++) {
      JsonObject *object = node_object(json_array_get_element(array, i));
      if (object == NULL)
        continue;
      gint64 id = 0;
      gint64 workspace_id = 0;
      gboolean has_workspace =
          object_integer(object, "workspace_id", &workspace_id);
      (void)object_integer(object, "id", &id);

      json_builder_begin_object(builder);
      json_builder_set_member_name(builder, "id");
      json_builder_add_int_value(builder, id);
      add_nullable_string(builder, "app_id", object_string(object, "app_id"));
      add_nullable_string(builder, "title", object_string(object, "title"));
      add_nullable_integer(builder, "workspace_id", has_workspace,
                           workspace_id);
      add_nullable_string(builder, "output_id",
                          has_workspace
                              ? workspace_output(workspaces_root, workspace_id)
                              : NULL);
      json_builder_set_member_name(builder, "focused");
      json_builder_add_boolean_value(
          builder, object_boolean_any(object, "focused", "is_focused"));
      json_builder_end_object(builder);
    }
  }
  json_builder_end_array(builder);
}

static gboolean focused_id(JsonNode *root, gint64 *id) {
  JsonArray *array = node_array(root);
  if (array == NULL)
    return FALSE;
  guint length = json_array_get_length(array);
  for (guint i = 0; i < length; i++) {
    JsonObject *object = node_object(json_array_get_element(array, i));
    if (object == NULL ||
        !object_boolean_any(object, "focused", "is_focused"))
      continue;
    if (object_integer(object, "id", id) || object_integer(object, "idx", id))
      return TRUE;
    *id = 0;
    return TRUE;
  }
  return FALSE;
}

static char *builder_to_data(JsonBuilder *builder) {
  JsonNode *root = json_builder_get_root(builder);
  JsonGenerator *generator = json_generator_new();
  json_generator_set_root(generator, root);
  char *data = json_generator_to_data(generator, NULL);
  g_object_unref(generator);
  json_node_free(root);
  return data;
}

void shaula_explore_inventory_init(ShaulaExploreInventory *inventory) {
  if (inventory == NULL)
    return;
  inventory->result_json = NULL;
  inventory->inventory_available = FALSE;
}

void shaula_explore_inventory_clear(ShaulaExploreInventory *inventory) {
  if (inventory == NULL)
    return;
  g_clear_pointer(&inventory->result_json, g_free);
  inventory->inventory_available = FALSE;
}

gboolean shaula_explore_inventory_build(const char *compositor_kind,
                                         const char *compositor_label,
                                         const char *focused_output_name,
                                         gboolean brief,
                                         ShaulaExploreInventory *inventory) {
  if (compositor_kind == NULL || compositor_label == NULL || inventory == NULL)
    return FALSE;
  shaula_explore_inventory_clear(inventory);

  JsonNode *outputs = NULL;
  JsonNode *workspaces = NULL;
  JsonNode *windows = NULL;
  if (g_str_equal(compositor_kind, "niri")) {
    outputs = run_niri_json("outputs");
    if (!brief) {
      workspaces = run_niri_json("workspaces");
      windows = run_niri_json("windows");
    }
  }
  inventory->inventory_available =
      outputs != NULL && (brief || (workspaces != NULL && windows != NULL));
  if (!inventory->inventory_available) {
    g_clear_pointer(&outputs, json_node_free);
    g_clear_pointer(&workspaces, json_node_free);
    g_clear_pointer(&windows, json_node_free);
  }

  gint64 focused_workspace = 0;
  gint64 focused_window = 0;
  gboolean has_focused_workspace = focused_id(workspaces, &focused_workspace);
  gboolean has_focused_window = focused_id(windows, &focused_window);

  JsonBuilder *builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "compositor");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "kind");
  json_builder_add_string_value(builder, compositor_kind);
  json_builder_set_member_name(builder, "label");
  json_builder_add_string_value(builder, compositor_label);
  json_builder_end_object(builder);

  json_builder_set_member_name(builder, "focused");
  json_builder_begin_object(builder);
  add_nullable_string(builder, "output_id", focused_output_name);
  add_nullable_integer(builder, "workspace_id", has_focused_workspace,
                       focused_workspace);
  add_nullable_integer(builder, "window_id", has_focused_window,
                       focused_window);
  json_builder_end_object(builder);

  if (!brief) {
    append_outputs(builder, outputs, focused_output_name);
    append_workspaces(builder, workspaces);
    append_windows(builder, windows, workspaces);
  }

  json_builder_set_member_name(builder, "recommended_capture");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "mode");
  json_builder_add_string_value(builder,
                                focused_output_name != NULL ? "output"
                                                            : "fullscreen");
  add_nullable_string(builder, "id", focused_output_name);
  json_builder_set_member_name(builder, "reason");
  json_builder_add_string_value(
      builder, focused_output_name != NULL ? "focused_output"
                                           : "focused_output_unavailable");
  json_builder_end_object(builder);
  json_builder_end_object(builder);

  inventory->result_json = builder_to_data(builder);
  g_object_unref(builder);
  g_clear_pointer(&outputs, json_node_free);
  g_clear_pointer(&workspaces, json_node_free);
  g_clear_pointer(&windows, json_node_free);
  return inventory->result_json != NULL;
}
