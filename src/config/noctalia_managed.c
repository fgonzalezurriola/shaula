#include "noctalia_managed.h"

#include "runtime/helper_resolution.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *PLUGIN_FILES[] = {
    "manifest.json",
    "BarWidget.qml",
    "README.md",
};

static char *config_root(void) {
  const char *xdg = g_getenv("XDG_CONFIG_HOME");
  if (xdg != NULL && xdg[0] != '\0')
    return g_strdup(xdg);
  const char *home = g_getenv("HOME");
  return home != NULL && home[0] != '\0'
             ? g_build_filename(home, ".config", NULL)
             : NULL;
}

static char *noctalia_dir(void) {
  g_autofree char *root = config_root();
  return root != NULL ? g_build_filename(root, "noctalia", NULL) : NULL;
}

static gboolean source_valid(const char *source_dir) {
  if (source_dir == NULL || source_dir[0] == '\0')
    return FALSE;
  for (guint i = 0; i < G_N_ELEMENTS(PLUGIN_FILES); i++) {
    g_autofree char *path =
        g_build_filename(source_dir, PLUGIN_FILES[i], NULL);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
      return FALSE;
  }
  return TRUE;
}

void shaula_noctalia_result_clear(ShaulaNoctaliaResult *result) {
  if (result == NULL)
    return;
  g_clear_pointer(&result->plugin_dir, g_free);
  *result = (ShaulaNoctaliaResult){0};
}

char *shaula_noctalia_plugin_source_resolve(void) {
  const char *override = g_getenv("SHAULA_NOCTALIA_PLUGIN_SOURCE");
  if (source_valid(override))
    return g_strdup(override);

  g_autofree char *self = shaula_executable_current_path();
  if (self != NULL) {
    g_autofree char *bin_dir = g_path_get_dirname(self);
    g_autofree char *prefix = g_path_get_dirname(bin_dir);
    g_autofree char *installed =
        g_build_filename(prefix, "share", "shaula", "integrations",
                         "noctalia", "shaula", NULL);
    if (source_valid(installed))
      return g_steal_pointer(&installed);
  }

  g_autofree char *repository =
      g_build_filename("integrations", "noctalia", "shaula", NULL);
  if (source_valid(repository))
    return g_steal_pointer(&repository);
  return NULL;
}

gboolean shaula_noctalia_detected(void) {
  g_autofree char *directory = noctalia_dir();
  if (directory == NULL)
    return FALSE;
  g_autofree char *plugins =
      g_build_filename(directory, "plugins.json", NULL);
  g_autofree char *settings =
      g_build_filename(directory, "settings.json", NULL);
  return g_file_test(directory, G_FILE_TEST_IS_DIR) ||
         g_file_test(plugins, G_FILE_TEST_EXISTS) ||
         g_file_test(settings, G_FILE_TEST_EXISTS);
}

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

static char *backup_path_new(const char *path) {
  const gint64 now = (gint64)time(NULL);
  for (guint attempt = 0; attempt < 1000U; attempt++) {
    g_autofree char *candidate =
        attempt == 0U
            ? g_strdup_printf("%s.shaula-backup-%" G_GINT64_FORMAT, path, now)
            : g_strdup_printf("%s.shaula-backup-%" G_GINT64_FORMAT "-%u",
                              path, now, attempt);
    if (!g_file_test(candidate, G_FILE_TEST_EXISTS))
      return g_steal_pointer(&candidate);
  }
  return NULL;
}

static gboolean atomic_write_with_backup(const char *path,
                                         const char *contents,
                                         gsize length, gboolean dry_run,
                                         gboolean *changed) {
  g_autofree char *current = NULL;
  gsize current_length = 0U;
  const gboolean existed = g_file_test(path, G_FILE_TEST_EXISTS);
  if (existed &&
      !g_file_get_contents(path, &current, &current_length, NULL))
    return FALSE;
  *changed = !existed || current_length != length ||
             memcmp(current != NULL ? current : "", contents, length) != 0;
  if (!*changed || dry_run)
    return TRUE;

  g_autofree char *parent = g_path_get_dirname(path);
  if (g_mkdir_with_parents(parent, 0700) != 0)
    return FALSE;
  if (existed) {
    g_autofree char *backup = backup_path_new(path);
    if (backup == NULL ||
        !g_file_set_contents(backup, current, (gssize)current_length, NULL))
      return FALSE;
  }
  g_autofree char *temporary =
      g_strdup_printf("%s.shaula-tmp.%ld", path, (long)getpid());
  if (!g_file_set_contents(temporary, contents, (gssize)length, NULL) ||
      g_rename(temporary, path) != 0) {
    (void)g_unlink(temporary);
    return FALSE;
  }
  return TRUE;
}

static gboolean plugin_files_equal(const char *source, const char *target) {
  g_autofree char *marker = g_build_filename(target, ".shaula-managed", NULL);
  if (!g_file_test(marker, G_FILE_TEST_IS_REGULAR))
    return FALSE;
  for (guint i = 0; i < G_N_ELEMENTS(PLUGIN_FILES); i++) {
    g_autofree char *source_path =
        g_build_filename(source, PLUGIN_FILES[i], NULL);
    g_autofree char *target_path =
        g_build_filename(target, PLUGIN_FILES[i], NULL);
    g_autofree char *source_contents = NULL;
    g_autofree char *target_contents = NULL;
    gsize source_length = 0U;
    gsize target_length = 0U;
    if (!g_file_get_contents(source_path, &source_contents, &source_length,
                             NULL) ||
        !g_file_get_contents(target_path, &target_contents, &target_length,
                             NULL) ||
        source_length != target_length ||
        memcmp(source_contents, target_contents, source_length) != 0)
      return FALSE;
  }
  return TRUE;
}

static ShaulaNoctaliaStatus install_plugin_files(
    const char *source, const char *target, gboolean dry_run,
    gboolean *changed) {
  const gboolean exists = g_file_test(target, G_FILE_TEST_EXISTS);
  if (exists) {
    g_autofree char *marker =
        g_build_filename(target, ".shaula-managed", NULL);
    if (!g_file_test(marker, G_FILE_TEST_IS_REGULAR))
      return SHAULA_NOCTALIA_STATUS_UNMANAGED_PLUGIN;
  }
  *changed = !exists || !plugin_files_equal(source, target);
  if (!*changed || dry_run)
    return SHAULA_NOCTALIA_STATUS_OK;

  g_autofree char *parent = g_path_get_dirname(target);
  if (g_mkdir_with_parents(parent, 0700) != 0)
    return SHAULA_NOCTALIA_STATUS_IO_FAILED;
  g_autofree char *temporary =
      g_strdup_printf("%s.shaula-tmp.%ld", target, (long)getpid());
  (void)remove_tree(temporary);
  if (g_mkdir_with_parents(temporary, 0700) != 0)
    return SHAULA_NOCTALIA_STATUS_IO_FAILED;
  for (guint i = 0; i < G_N_ELEMENTS(PLUGIN_FILES); i++) {
    g_autofree char *source_path =
        g_build_filename(source, PLUGIN_FILES[i], NULL);
    g_autofree char *target_path =
        g_build_filename(temporary, PLUGIN_FILES[i], NULL);
    g_autofree char *contents = NULL;
    gsize length = 0U;
    if (!g_file_get_contents(source_path, &contents, &length, NULL) ||
        !g_file_set_contents(target_path, contents, (gssize)length, NULL)) {
      (void)remove_tree(temporary);
      return SHAULA_NOCTALIA_STATUS_IO_FAILED;
    }
  }
  g_autofree char *marker =
      g_build_filename(temporary, ".shaula-managed", NULL);
  if (!g_file_set_contents(marker, "installed-by=shaula\n", -1, NULL)) {
    (void)remove_tree(temporary);
    return SHAULA_NOCTALIA_STATUS_IO_FAILED;
  }

  g_autofree char *backup = NULL;
  if (exists) {
    backup = backup_path_new(target);
    if (backup == NULL || g_rename(target, backup) != 0) {
      (void)remove_tree(temporary);
      return SHAULA_NOCTALIA_STATUS_IO_FAILED;
    }
  }
  if (g_rename(temporary, target) != 0) {
    if (backup != NULL)
      (void)g_rename(backup, target);
    (void)remove_tree(temporary);
    return SHAULA_NOCTALIA_STATUS_IO_FAILED;
  }
  return SHAULA_NOCTALIA_STATUS_OK;
}

static JsonParser *load_json_object(const char *path, JsonObject **out_object) {
  JsonParser *parser = json_parser_new();
  if (!json_parser_load_from_file(parser, path, NULL)) {
    g_object_unref(parser);
    return NULL;
  }
  JsonNode *root = json_parser_get_root(parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
    g_object_unref(parser);
    return NULL;
  }
  *out_object = json_node_get_object(root);
  return parser;
}

static char *generate_json(JsonParser *parser, gsize *out_length) {
  g_autoptr(JsonGenerator) generator = json_generator_new();
  json_generator_set_root(generator, json_parser_get_root(parser));
  json_generator_set_pretty(generator, TRUE);
  json_generator_set_indent(generator, 2U);
  gsize length = 0U;
  g_autofree char *data = json_generator_to_data(generator, &length);
  if (data == NULL)
    return NULL;
  char *with_newline = g_malloc(length + 2U);
  memcpy(with_newline, data, length);
  with_newline[length] = '\n';
  with_newline[length + 1U] = '\0';
  *out_length = length + 1U;
  return with_newline;
}

static gboolean ensure_array_member(JsonObject *object, const char *name,
                                    JsonArray **out_array) {
  if (json_object_has_member(object, name)) {
    JsonNode *node = json_object_get_member(object, name);
    if (node == NULL || !JSON_NODE_HOLDS_ARRAY(node))
      return FALSE;
    *out_array = json_node_get_array(node);
    return TRUE;
  }
  JsonArray *array = json_array_new();
  json_object_set_array_member(object, name, array);
  *out_array = array;
  return TRUE;
}

static gboolean widget_present(JsonArray *array) {
  const guint length = json_array_get_length(array);
  for (guint i = 0U; i < length; i++) {
    JsonNode *node = json_array_get_element(array, i);
    if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node))
      continue;
    JsonObject *item = json_node_get_object(node);
    if (json_object_has_member(item, "id") &&
        g_strcmp0(json_object_get_string_member(item, "id"),
                  "plugin:shaula") == 0)
      return TRUE;
  }
  return FALSE;
}

static void remove_widget(JsonArray *array) {
  for (guint i = json_array_get_length(array); i > 0U; i--) {
    JsonNode *node = json_array_get_element(array, i - 1U);
    if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node))
      continue;
    JsonObject *item = json_node_get_object(node);
    if (json_object_has_member(item, "id") &&
        g_strcmp0(json_object_get_string_member(item, "id"),
                  "plugin:shaula") == 0)
      json_array_remove_element(array, i - 1U);
  }
}

static ShaulaNoctaliaStatus update_plugins_json(const char *path,
                                                gboolean install,
                                                gboolean dry_run,
                                                gboolean *changed,
                                                gboolean *skipped) {
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    *skipped = TRUE;
    return SHAULA_NOCTALIA_STATUS_OK;
  }
  JsonObject *root = NULL;
  g_autoptr(JsonParser) parser = load_json_object(path, &root);
  if (parser == NULL || !json_object_has_member(root, "version") ||
      json_object_get_int_member(root, "version") != 2 ||
      !json_object_has_member(root, "states"))
    return SHAULA_NOCTALIA_STATUS_INVALID_STATE;
  JsonNode *states_node = json_object_get_member(root, "states");
  if (states_node == NULL || !JSON_NODE_HOLDS_OBJECT(states_node))
    return SHAULA_NOCTALIA_STATUS_INVALID_STATE;
  JsonObject *states = json_node_get_object(states_node);
  if (install) {
    JsonObject *state = NULL;
    if (json_object_has_member(states, "shaula")) {
      JsonNode *state_node = json_object_get_member(states, "shaula");
      if (state_node == NULL || !JSON_NODE_HOLDS_OBJECT(state_node))
        return SHAULA_NOCTALIA_STATUS_INVALID_STATE;
      state = json_node_get_object(state_node);
    } else {
      state = json_object_new();
      json_object_set_object_member(states, "shaula", state);
    }
    json_object_set_boolean_member(state, "enabled", TRUE);
    if (!json_object_has_member(state, "sourceUrl"))
      json_object_set_string_member(state, "sourceUrl", "local");
  } else {
    json_object_remove_member(states, "shaula");
  }
  gsize length = 0U;
  g_autofree char *generated = generate_json(parser, &length);
  if (generated == NULL)
    return SHAULA_NOCTALIA_STATUS_IO_FAILED;
  return atomic_write_with_backup(path, generated, length, dry_run, changed)
             ? SHAULA_NOCTALIA_STATUS_OK
             : SHAULA_NOCTALIA_STATUS_IO_FAILED;
}

static ShaulaNoctaliaStatus update_settings_json(const char *path,
                                                 gboolean install,
                                                 gboolean dry_run,
                                                 gboolean *changed,
                                                 gboolean *skipped) {
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    *skipped = TRUE;
    return SHAULA_NOCTALIA_STATUS_OK;
  }
  JsonObject *root = NULL;
  g_autoptr(JsonParser) parser = load_json_object(path, &root);
  if (parser == NULL || !json_object_has_member(root, "bar"))
    return SHAULA_NOCTALIA_STATUS_INVALID_STATE;
  JsonNode *bar_node = json_object_get_member(root, "bar");
  if (bar_node == NULL || !JSON_NODE_HOLDS_OBJECT(bar_node))
    return SHAULA_NOCTALIA_STATUS_INVALID_STATE;
  JsonObject *bar = json_node_get_object(bar_node);
  if (!json_object_has_member(bar, "widgets"))
    return SHAULA_NOCTALIA_STATUS_INVALID_STATE;
  JsonNode *widgets_node = json_object_get_member(bar, "widgets");
  if (widgets_node == NULL || !JSON_NODE_HOLDS_OBJECT(widgets_node))
    return SHAULA_NOCTALIA_STATUS_INVALID_STATE;
  JsonObject *widgets = json_node_get_object(widgets_node);

  JsonArray *left = NULL;
  JsonArray *center = NULL;
  JsonArray *right = NULL;
  if (!ensure_array_member(widgets, "left", &left) ||
      !ensure_array_member(widgets, "center", &center) ||
      !ensure_array_member(widgets, "right", &right))
    return SHAULA_NOCTALIA_STATUS_INVALID_STATE;
  if (install) {
    if (!widget_present(left) && !widget_present(center) &&
        !widget_present(right)) {
      JsonObject *item = json_object_new();
      json_object_set_string_member(item, "id", "plugin:shaula");
      json_array_add_object_element(right, item);
    }
  } else {
    remove_widget(left);
    remove_widget(center);
    remove_widget(right);
  }

  gsize length = 0U;
  g_autofree char *generated = generate_json(parser, &length);
  if (generated == NULL)
    return SHAULA_NOCTALIA_STATUS_IO_FAILED;
  return atomic_write_with_backup(path, generated, length, dry_run, changed)
             ? SHAULA_NOCTALIA_STATUS_OK
             : SHAULA_NOCTALIA_STATUS_IO_FAILED;
}

static ShaulaNoctaliaStatus initialize_result(ShaulaNoctaliaResult *result,
                                              char **out_directory,
                                              char **out_plugins_json,
                                              char **out_settings_json) {
  if (result == NULL)
    return SHAULA_NOCTALIA_STATUS_IO_FAILED;
  *result = (ShaulaNoctaliaResult){0};
  g_autofree char *directory = noctalia_dir();
  if (directory == NULL)
    return SHAULA_NOCTALIA_STATUS_NOT_DETECTED;
  result->detected = shaula_noctalia_detected();
  if (!result->detected)
    return SHAULA_NOCTALIA_STATUS_NOT_DETECTED;
  result->plugin_dir =
      g_build_filename(directory, "plugins", "shaula", NULL);
  *out_plugins_json = g_build_filename(directory, "plugins.json", NULL);
  *out_settings_json = g_build_filename(directory, "settings.json", NULL);
  *out_directory = g_steal_pointer(&directory);
  return SHAULA_NOCTALIA_STATUS_OK;
}

ShaulaNoctaliaStatus
shaula_noctalia_install(const char *source_dir, gboolean dry_run,
                         ShaulaNoctaliaResult *result) {
  g_autofree char *directory = NULL;
  g_autofree char *plugins_json = NULL;
  g_autofree char *settings_json = NULL;
  ShaulaNoctaliaStatus status = initialize_result(
      result, &directory, &plugins_json, &settings_json);
  if (status != SHAULA_NOCTALIA_STATUS_OK)
    return status;
  if (!source_valid(source_dir))
    return SHAULA_NOCTALIA_STATUS_SOURCE_UNAVAILABLE;

  /* Validate and calculate JSON mutations before installing plugin files. */
  status = update_plugins_json(plugins_json, TRUE, TRUE,
                               &result->plugins_json_changed,
                               &result->plugins_json_skipped);
  if (status != SHAULA_NOCTALIA_STATUS_OK)
    return status;
  status = update_settings_json(settings_json, TRUE, TRUE,
                                &result->settings_json_changed,
                                &result->settings_json_skipped);
  if (status != SHAULA_NOCTALIA_STATUS_OK)
    return status;

  status = install_plugin_files(source_dir, result->plugin_dir, dry_run,
                                &result->plugin_files_changed);
  if (status != SHAULA_NOCTALIA_STATUS_OK)
    return status;
  if (!dry_run) {
    status = update_plugins_json(plugins_json, TRUE, FALSE,
                                 &result->plugins_json_changed,
                                 &result->plugins_json_skipped);
    if (status != SHAULA_NOCTALIA_STATUS_OK)
      return status;
    status = update_settings_json(settings_json, TRUE, FALSE,
                                  &result->settings_json_changed,
                                  &result->settings_json_skipped);
    if (status != SHAULA_NOCTALIA_STATUS_OK)
      return status;
  }
  result->changed = result->plugin_files_changed ||
                    result->plugins_json_changed ||
                    result->settings_json_changed;
  return SHAULA_NOCTALIA_STATUS_OK;
}

ShaulaNoctaliaStatus shaula_noctalia_remove(gboolean dry_run,
                                            ShaulaNoctaliaResult *result) {
  g_autofree char *directory = NULL;
  g_autofree char *plugins_json = NULL;
  g_autofree char *settings_json = NULL;
  ShaulaNoctaliaStatus status = initialize_result(
      result, &directory, &plugins_json, &settings_json);
  if (status != SHAULA_NOCTALIA_STATUS_OK)
    return status;

  const gboolean plugin_exists =
      g_file_test(result->plugin_dir, G_FILE_TEST_EXISTS);
  if (plugin_exists) {
    g_autofree char *marker =
        g_build_filename(result->plugin_dir, ".shaula-managed", NULL);
    if (!g_file_test(marker, G_FILE_TEST_IS_REGULAR))
      return SHAULA_NOCTALIA_STATUS_UNMANAGED_PLUGIN;
    result->plugin_files_changed = TRUE;
  }

  status = update_plugins_json(plugins_json, FALSE, dry_run,
                               &result->plugins_json_changed,
                               &result->plugins_json_skipped);
  if (status != SHAULA_NOCTALIA_STATUS_OK)
    return status;
  status = update_settings_json(settings_json, FALSE, dry_run,
                                &result->settings_json_changed,
                                &result->settings_json_skipped);
  if (status != SHAULA_NOCTALIA_STATUS_OK)
    return status;
  if (plugin_exists && !dry_run) {
    g_autofree char *backup = backup_path_new(result->plugin_dir);
    if (backup == NULL || g_rename(result->plugin_dir, backup) != 0)
      return SHAULA_NOCTALIA_STATUS_IO_FAILED;
  }
  result->changed = result->plugin_files_changed ||
                    result->plugins_json_changed ||
                    result->settings_json_changed;
  return SHAULA_NOCTALIA_STATUS_OK;
}

const char *shaula_noctalia_status_token(ShaulaNoctaliaStatus status) {
  switch (status) {
  case SHAULA_NOCTALIA_STATUS_OK:
    return "ok";
  case SHAULA_NOCTALIA_STATUS_NOT_DETECTED:
    return "not_detected";
  case SHAULA_NOCTALIA_STATUS_SOURCE_UNAVAILABLE:
    return "source_unavailable";
  case SHAULA_NOCTALIA_STATUS_INVALID_STATE:
    return "invalid_state";
  case SHAULA_NOCTALIA_STATUS_UNMANAGED_PLUGIN:
    return "unmanaged_plugin";
  case SHAULA_NOCTALIA_STATUS_IO_FAILED:
    return "io_failed";
  default:
    return "unknown";
  }
}
