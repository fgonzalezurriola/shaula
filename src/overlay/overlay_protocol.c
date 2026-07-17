#include "overlay_protocol.h"

#include <json-glib/json-glib.h>
#include <stdio.h>
#include <string.h>

void shaula_overlay_launch_init(ShaulaOverlayLaunch *launch) {
  g_return_if_fail(launch != NULL);
  *launch = (ShaulaOverlayLaunch){
      .interaction = SHAULA_OVERLAY_INTERACTION_QUICK,
  };
}

void shaula_overlay_launch_clear(ShaulaOverlayLaunch *launch) {
  if (launch == NULL)
    return;
  g_clear_pointer(&launch->background_path, g_free);
  g_clear_pointer(&launch->output_name, g_free);
  shaula_overlay_launch_init(launch);
}

static char *environment_copy(const char *name) {
  const char *value = g_getenv(name);
  return value != NULL && value[0] != '\0' ? g_strdup(value) : NULL;
}

void shaula_overlay_launch_load_environment(ShaulaOverlayLaunch *launch) {
  g_return_if_fail(launch != NULL);
  shaula_overlay_launch_clear(launch);

  const char *interaction = g_getenv("SHAULA_OVERLAY_INTERACTION_MODE");
  if (interaction != NULL && g_str_equal(interaction, "area"))
    launch->interaction = SHAULA_OVERLAY_INTERACTION_AREA;

  const char *aspect = g_getenv("SHAULA_OVERLAY_ASPECT");
  int aspect_width = 0;
  int aspect_height = 0;
  if (aspect != NULL &&
      sscanf(aspect, "%d:%d", &aspect_width, &aspect_height) == 2 &&
      aspect_width > 0 && aspect_height > 0) {
    launch->has_aspect = TRUE;
    launch->aspect_width = aspect_width;
    launch->aspect_height = aspect_height;
  }

  launch->background_path =
      environment_copy("SHAULA_OVERLAY_BACKGROUND_PATH");
  launch->output_name = environment_copy("SHAULA_OVERLAY_OUTPUT_NAME");

  const char *geometry = g_getenv("SHAULA_OVERLAY_INITIAL_GEOMETRY");
  ShaulaRect rect = {0};
  if (geometry != NULL &&
      sscanf(geometry, "%d,%d,%d,%d", &rect.x, &rect.y, &rect.width,
             &rect.height) == 4 &&
      rect.width > 0 && rect.height > 0) {
    launch->has_initial_geometry = TRUE;
    launch->initial_geometry = rect;
    launch->initial_geometry_legacy =
        g_strcmp0(g_getenv("SHAULA_OVERLAY_INITIAL_GEOMETRY_LEGACY"), "1") ==
        0;
  }
}

char **shaula_overlay_launch_environment_apply(
    char **environment, ShaulaOverlayInteraction interaction,
    const char *aspect, gboolean frozen, const char *background_path,
    const char *output_name) {
  g_return_val_if_fail(environment != NULL, NULL);
  environment = g_environ_setenv(
      environment, "SHAULA_OVERLAY_INTERACTION_MODE",
      interaction == SHAULA_OVERLAY_INTERACTION_QUICK ? "quick" : "area",
      TRUE);
  if (aspect != NULL)
    environment =
        g_environ_setenv(environment, "SHAULA_OVERLAY_ASPECT", aspect, TRUE);
  if (frozen)
    environment = g_environ_setenv(environment, "SHAULA_OVERLAY_REGION_MODE",
                                   "frozen", TRUE);
  if (background_path != NULL)
    environment = g_environ_setenv(environment,
                                   "SHAULA_OVERLAY_BACKGROUND_PATH",
                                   background_path, TRUE);
  if (output_name != NULL)
    environment = g_environ_setenv(environment, "SHAULA_OVERLAY_OUTPUT_NAME",
                                   output_name, TRUE);
  return environment;
}

void shaula_overlay_outcome_init(ShaulaOverlayOutcome *outcome) {
  g_return_if_fail(outcome != NULL);
  *outcome = (ShaulaOverlayOutcome){
      .status = SHAULA_OVERLAY_OUTCOME_INVALID,
      .action = SHAULA_OVERLAY_ACTION_CANCEL,
  };
}

void shaula_overlay_outcome_clear(ShaulaOverlayOutcome *outcome) {
  if (outcome == NULL)
    return;
  g_clear_pointer(&outcome->output_name, g_free);
  g_clear_pointer(&outcome->aspect, g_free);
  g_clear_pointer(&outcome->error_code, g_free);
  g_clear_pointer(&outcome->error_message, g_free);
  shaula_overlay_outcome_init(outcome);
}

void shaula_overlay_outcome_set_success(
    ShaulaOverlayOutcome *outcome, ShaulaOverlayAction action,
    const char *aspect, ShaulaRect geometry, ShaulaRect local_geometry,
    ShaulaRect output_geometry, const char *output_name) {
  g_return_if_fail(outcome != NULL);
  shaula_overlay_outcome_clear(outcome);
  outcome->status = SHAULA_OVERLAY_OUTCOME_OK;
  outcome->action = action;
  outcome->has_geometry = TRUE;
  outcome->geometry = geometry;
  outcome->has_local_geometry = TRUE;
  outcome->local_geometry = local_geometry;
  outcome->has_output = TRUE;
  outcome->output_geometry = output_geometry;
  outcome->aspect = g_strdup(aspect != NULL ? aspect : "Free");
  outcome->output_name = g_strdup(output_name);
}

void shaula_overlay_outcome_set_cancel(ShaulaOverlayOutcome *outcome,
                                       const ShaulaRect *geometry) {
  g_return_if_fail(outcome != NULL);
  shaula_overlay_outcome_clear(outcome);
  outcome->status = SHAULA_OVERLAY_OUTCOME_CANCEL;
  outcome->action = SHAULA_OVERLAY_ACTION_CANCEL;
  if (geometry != NULL) {
    outcome->has_geometry = TRUE;
    outcome->geometry = *geometry;
  }
}

void shaula_overlay_outcome_set_error(ShaulaOverlayOutcome *outcome,
                                      const char *code, const char *message) {
  g_return_if_fail(outcome != NULL);
  shaula_overlay_outcome_clear(outcome);
  outcome->status = SHAULA_OVERLAY_OUTCOME_ERROR;
  outcome->action = SHAULA_OVERLAY_ACTION_CANCEL;
  outcome->error_code = g_strdup(code);
  outcome->error_message = g_strdup(message);
}

const char *shaula_overlay_action_text(ShaulaOverlayAction action) {
  switch (action) {
  case SHAULA_OVERLAY_ACTION_CAPTURE:
    return "capture";
  case SHAULA_OVERLAY_ACTION_COPY:
    return "copy";
  case SHAULA_OVERLAY_ACTION_SAVE:
    return "save";
  case SHAULA_OVERLAY_ACTION_CANCEL:
  default:
    return "cancel";
  }
}

static void builder_add_rect(JsonBuilder *builder, const char *name,
                             ShaulaRect rect) {
  json_builder_set_member_name(builder, name);
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "x");
  json_builder_add_int_value(builder, rect.x);
  json_builder_set_member_name(builder, "y");
  json_builder_add_int_value(builder, rect.y);
  json_builder_set_member_name(builder, "width");
  json_builder_add_int_value(builder, rect.width);
  json_builder_set_member_name(builder, "height");
  json_builder_add_int_value(builder, rect.height);
  json_builder_end_object(builder);
}

static void builder_add_output(JsonBuilder *builder,
                               const ShaulaOverlayOutcome *outcome) {
  json_builder_set_member_name(builder, "output");
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "name");
  if (outcome->output_name != NULL)
    json_builder_add_string_value(builder, outcome->output_name);
  else
    json_builder_add_null_value(builder);
  json_builder_set_member_name(builder, "x");
  json_builder_add_int_value(builder, outcome->output_geometry.x);
  json_builder_set_member_name(builder, "y");
  json_builder_add_int_value(builder, outcome->output_geometry.y);
  json_builder_set_member_name(builder, "width");
  json_builder_add_int_value(builder, outcome->output_geometry.width);
  json_builder_set_member_name(builder, "height");
  json_builder_add_int_value(builder, outcome->output_geometry.height);
  json_builder_end_object(builder);
}

char *shaula_overlay_outcome_json_new(const ShaulaOverlayOutcome *outcome) {
  g_return_val_if_fail(outcome != NULL, NULL);
  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "status");
  const char *status = outcome->status == SHAULA_OVERLAY_OUTCOME_OK
                           ? "ok"
                           : outcome->status == SHAULA_OVERLAY_OUTCOME_CANCEL
                                 ? "cancel"
                                 : "error";
  json_builder_add_string_value(builder, status);
  json_builder_set_member_name(builder, "action");
  json_builder_add_string_value(builder,
                                shaula_overlay_action_text(outcome->action));
  if (outcome->aspect != NULL) {
    json_builder_set_member_name(builder, "aspect");
    json_builder_add_string_value(builder, outcome->aspect);
  }
  if (outcome->has_geometry)
    builder_add_rect(builder, "geometry", outcome->geometry);
  else {
    json_builder_set_member_name(builder, "geometry");
    json_builder_add_null_value(builder);
  }
  if (outcome->has_local_geometry)
    builder_add_rect(builder, "local_geometry", outcome->local_geometry);
  if (outcome->has_output)
    builder_add_output(builder, outcome);
  json_builder_set_member_name(builder, "error");
  if (outcome->status == SHAULA_OVERLAY_OUTCOME_ERROR) {
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "code");
    json_builder_add_string_value(builder, outcome->error_code != NULL
                                               ? outcome->error_code
                                               : "ERR_OVERLAY_UNAVAILABLE");
    json_builder_set_member_name(builder, "message");
    json_builder_add_string_value(builder, outcome->error_message != NULL
                                               ? outcome->error_message
                                               : "overlay unavailable");
    json_builder_end_object(builder);
  } else {
    json_builder_add_null_value(builder);
  }
  json_builder_end_object(builder);

  g_autoptr(JsonGenerator) generator = json_generator_new();
  g_autoptr(JsonNode) root = json_builder_get_root(builder);
  json_generator_set_root(generator, root);
  return json_generator_to_data(generator, NULL);
}

static gboolean object_rect(JsonObject *object, const char *name,
                            gboolean required, ShaulaRect *rect,
                            gboolean *present) {
  *present = FALSE;
  if (!json_object_has_member(object, name))
    return !required;
  JsonNode *node = json_object_get_member(object, name);
  if (node == NULL || JSON_NODE_HOLDS_NULL(node))
    return !required;
  if (!JSON_NODE_HOLDS_OBJECT(node))
    return FALSE;
  JsonObject *value = json_node_get_object(node);
  const char *members[] = {"x", "y", "width", "height"};
  for (guint i = 0; i < G_N_ELEMENTS(members); i += 1) {
    if (!json_object_has_member(value, members[i]) ||
        json_node_get_value_type(json_object_get_member(value, members[i])) !=
            G_TYPE_INT64)
      return FALSE;
  }
  gint64 x = json_object_get_int_member(value, "x");
  gint64 y = json_object_get_int_member(value, "y");
  gint64 width = json_object_get_int_member(value, "width");
  gint64 height = json_object_get_int_member(value, "height");
  if (x < G_MININT || x > G_MAXINT || y < G_MININT || y > G_MAXINT ||
      width <= 0 || width > G_MAXINT || height <= 0 || height > G_MAXINT)
    return FALSE;
  *rect = (ShaulaRect){.x = (int)x,
                       .y = (int)y,
                       .width = (int)width,
                       .height = (int)height};
  *present = TRUE;
  return TRUE;
}

static gboolean optional_string(JsonObject *object, const char *name,
                                char **value) {
  if (!json_object_has_member(object, name))
    return TRUE;
  JsonNode *node = json_object_get_member(object, name);
  if (node == NULL || JSON_NODE_HOLDS_NULL(node))
    return TRUE;
  if (!JSON_NODE_HOLDS_VALUE(node) ||
      json_node_get_value_type(node) != G_TYPE_STRING)
    return FALSE;
  *value = g_strdup(json_node_get_string(node));
  return TRUE;
}

static const char *required_string(JsonObject *object, const char *name) {
  if (!json_object_has_member(object, name))
    return NULL;
  JsonNode *node = json_object_get_member(object, name);
  if (node == NULL || !JSON_NODE_HOLDS_VALUE(node) ||
      json_node_get_value_type(node) != G_TYPE_STRING)
    return NULL;
  return json_node_get_string(node);
}

static gboolean output_name(JsonObject *object, char **value) {
  if (!json_object_has_member(object, "output"))
    return TRUE;
  JsonNode *node = json_object_get_member(object, "output");
  if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node))
    return FALSE;
  return optional_string(json_node_get_object(node), "name", value);
}

gboolean shaula_overlay_outcome_parse(const char *json,
                                      ShaulaOverlayOutcome *outcome) {
  g_return_val_if_fail(outcome != NULL, FALSE);
  shaula_overlay_outcome_clear(outcome);
  g_autoptr(JsonParser) parser = json_parser_new();
  g_autoptr(GError) error = NULL;
  if (json == NULL || !json_parser_load_from_data(parser, json, -1, &error))
    return FALSE;
  JsonNode *root = json_parser_get_root(parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root))
    return FALSE;
  JsonObject *object = json_node_get_object(root);
  const char *status = required_string(object, "status");
  const char *action = required_string(object, "action");
  if (status == NULL || action == NULL)
    return FALSE;
  if (g_strcmp0(status, "error") == 0) {
    if (g_strcmp0(action, "cancel") != 0 ||
        !json_object_has_member(object, "error"))
      goto invalid;
    if (json_object_has_member(object, "geometry")) {
      JsonNode *geometry = json_object_get_member(object, "geometry");
      if (geometry == NULL || !JSON_NODE_HOLDS_NULL(geometry))
        goto invalid;
    }
    JsonNode *error_node = json_object_get_member(object, "error");
    if (error_node == NULL || !JSON_NODE_HOLDS_OBJECT(error_node))
      goto invalid;
    JsonObject *error_object = json_node_get_object(error_node);
    const char *code = required_string(error_object, "code");
    const char *message = required_string(error_object, "message");
    if (code == NULL || message == NULL || code[0] == '\0' ||
        message[0] == '\0')
      goto invalid;
    outcome->status = SHAULA_OVERLAY_OUTCOME_ERROR;
    outcome->action = SHAULA_OVERLAY_ACTION_CANCEL;
    outcome->error_code = g_strdup(code);
    outcome->error_message = g_strdup(message);
    return TRUE;
  }
  if (g_strcmp0(status, "cancel") == 0) {
    outcome->status = SHAULA_OVERLAY_OUTCOME_CANCEL;
    outcome->action = SHAULA_OVERLAY_ACTION_CANCEL;
    if (g_strcmp0(action, "cancel") != 0 ||
        !object_rect(object, "geometry", FALSE, &outcome->geometry,
                     &outcome->has_geometry))
      goto invalid;
    return TRUE;
  }
  if (g_strcmp0(status, "ok") != 0)
    goto invalid;
  outcome->status = SHAULA_OVERLAY_OUTCOME_OK;
  if (g_strcmp0(action, "capture") == 0)
    outcome->action = SHAULA_OVERLAY_ACTION_CAPTURE;
  else if (g_strcmp0(action, "copy") == 0)
    outcome->action = SHAULA_OVERLAY_ACTION_COPY;
  else if (g_strcmp0(action, "save") == 0)
    outcome->action = SHAULA_OVERLAY_ACTION_SAVE;
  else
    goto invalid;
  if (!object_rect(object, "geometry", TRUE, &outcome->geometry,
                   &outcome->has_geometry) ||
      !object_rect(object, "local_geometry", FALSE,
                   &outcome->local_geometry,
                   &outcome->has_local_geometry) ||
      !object_rect(object, "output", FALSE, &outcome->output_geometry,
                   &outcome->has_output) ||
      !optional_string(object, "aspect", &outcome->aspect) ||
      !output_name(object, &outcome->output_name))
    goto invalid;
  if (!outcome->has_local_geometry)
    outcome->local_geometry = outcome->geometry;
  if (!outcome->has_output)
    outcome->output_geometry = outcome->geometry;
  return TRUE;

invalid:
  shaula_overlay_outcome_clear(outcome);
  return FALSE;
}
