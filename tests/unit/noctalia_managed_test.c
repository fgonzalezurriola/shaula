#include "config/noctalia_managed.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

static gboolean remove_tree(const char *path) {
  if (!g_file_test(path, G_FILE_TEST_EXISTS))
    return TRUE;
  if (!g_file_test(path, G_FILE_TEST_IS_DIR))
    return g_unlink(path) == 0;
  g_autoptr(GDir) dir = g_dir_open(path, 0, NULL);
  if (dir == NULL)
    return FALSE;
  const char *name;
  while ((name = g_dir_read_name(dir)) != NULL) {
    g_autofree char *child = g_build_filename(path, name, NULL);
    if (!remove_tree(child))
      return FALSE;
  }
  return g_rmdir(path) == 0;
}

static void write_source(const char *source) {
  g_assert_cmpint(g_mkdir_with_parents(source, 0700), ==, 0);
  g_autofree char *manifest = g_build_filename(source, "manifest.json", NULL);
  g_autofree char *widget = g_build_filename(source, "BarWidget.qml", NULL);
  g_autofree char *readme = g_build_filename(source, "README.md", NULL);
  g_assert_true(g_file_set_contents(manifest, "{\"id\":\"shaula\"}\n", -1,
                                    NULL));
  g_assert_true(g_file_set_contents(widget, "import QtQuick\n", -1, NULL));
  g_assert_true(g_file_set_contents(readme, "# Shaula\n", -1, NULL));
}

static JsonObject *load_root(const char *path, JsonParser **out_parser) {
  JsonParser *parser = json_parser_new();
  g_assert_true(json_parser_load_from_file(parser, path, NULL));
  JsonNode *root = json_parser_get_root(parser);
  g_assert_true(JSON_NODE_HOLDS_OBJECT(root));
  *out_parser = parser;
  return json_node_get_object(root);
}

static gboolean settings_has_widget(JsonObject *root) {
  JsonObject *bar = json_object_get_object_member(root, "bar");
  JsonObject *widgets = json_object_get_object_member(bar, "widgets");
  const char *sections[] = {"left", "center", "right"};
  for (guint s = 0; s < G_N_ELEMENTS(sections); s++) {
    JsonArray *array = json_object_get_array_member(widgets, sections[s]);
    for (guint i = 0; i < json_array_get_length(array); i++) {
      JsonObject *item = json_array_get_object_element(array, i);
      if (item != NULL &&
          g_strcmp0(json_object_get_string_member(item, "id"),
                    "plugin:shaula") == 0)
        return TRUE;
    }
  }
  return FALSE;
}

static void test_install_remove_idempotent(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-noctalia-managed-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *source = g_build_filename(root, "source", NULL);
  g_autofree char *config = g_build_filename(root, "config", NULL);
  g_autofree char *noctalia = g_build_filename(config, "noctalia", NULL);
  g_autofree char *plugins_json =
      g_build_filename(noctalia, "plugins.json", NULL);
  g_autofree char *settings_json =
      g_build_filename(noctalia, "settings.json", NULL);
  g_autofree char *plugin_dir =
      g_build_filename(noctalia, "plugins", "shaula", NULL);
  write_source(source);
  g_assert_cmpint(g_mkdir_with_parents(noctalia, 0700), ==, 0);
  g_assert_true(g_file_set_contents(
      plugins_json, "{\"version\":2,\"states\":{}}\n", -1, NULL));
  g_assert_true(g_file_set_contents(
      settings_json,
      "{\"bar\":{\"widgets\":{\"left\":[],\"center\":[],\"right\":[]}}}\n",
      -1, NULL));

  g_autofree char *old_xdg = g_strdup(g_getenv("XDG_CONFIG_HOME"));
  g_setenv("XDG_CONFIG_HOME", config, TRUE);

  ShaulaNoctaliaResult result = {0};
  g_assert_cmpint(shaula_noctalia_install(source, FALSE, &result), ==,
                  SHAULA_NOCTALIA_STATUS_OK);
  g_assert_true(result.changed);
  g_assert_true(result.plugin_files_changed);
  shaula_noctalia_result_clear(&result);
  g_assert_true(g_file_test(plugin_dir, G_FILE_TEST_IS_DIR));

  g_autoptr(JsonParser) parser = NULL;
  JsonObject *plugins = load_root(plugins_json, &parser);
  JsonObject *states = json_object_get_object_member(plugins, "states");
  JsonObject *state = json_object_get_object_member(states, "shaula");
  g_assert_true(json_object_get_boolean_member(state, "enabled"));
  g_clear_object(&parser);
  JsonObject *settings = load_root(settings_json, &parser);
  g_assert_true(settings_has_widget(settings));
  g_clear_object(&parser);

  result = (ShaulaNoctaliaResult){0};
  g_assert_cmpint(shaula_noctalia_install(source, FALSE, &result), ==,
                  SHAULA_NOCTALIA_STATUS_OK);
  g_assert_false(result.changed);
  shaula_noctalia_result_clear(&result);

  result = (ShaulaNoctaliaResult){0};
  g_assert_cmpint(shaula_noctalia_remove(FALSE, &result), ==,
                  SHAULA_NOCTALIA_STATUS_OK);
  g_assert_true(result.changed);
  shaula_noctalia_result_clear(&result);
  g_assert_false(g_file_test(plugin_dir, G_FILE_TEST_EXISTS));

  plugins = load_root(plugins_json, &parser);
  states = json_object_get_object_member(plugins, "states");
  g_assert_false(json_object_has_member(states, "shaula"));
  g_clear_object(&parser);
  settings = load_root(settings_json, &parser);
  g_assert_false(settings_has_widget(settings));
  g_clear_object(&parser);

  result = (ShaulaNoctaliaResult){0};
  g_assert_cmpint(shaula_noctalia_remove(FALSE, &result), ==,
                  SHAULA_NOCTALIA_STATUS_OK);
  g_assert_false(result.changed);
  shaula_noctalia_result_clear(&result);

  if (old_xdg != NULL)
    g_setenv("XDG_CONFIG_HOME", old_xdg, TRUE);
  else
    g_unsetenv("XDG_CONFIG_HOME");
  g_assert_true(remove_tree(root));
}

static void test_invalid_and_unmanaged_are_safe(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root =
      g_dir_make_tmp("shaula-noctalia-invalid-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *source = g_build_filename(root, "source", NULL);
  g_autofree char *config = g_build_filename(root, "config", NULL);
  g_autofree char *noctalia = g_build_filename(config, "noctalia", NULL);
  g_autofree char *plugins_json =
      g_build_filename(noctalia, "plugins.json", NULL);
  g_autofree char *settings_json =
      g_build_filename(noctalia, "settings.json", NULL);
  g_autofree char *plugin_dir =
      g_build_filename(noctalia, "plugins", "shaula", NULL);
  write_source(source);
  g_assert_cmpint(g_mkdir_with_parents(plugin_dir, 0700), ==, 0);
  g_assert_true(g_file_set_contents(
      plugins_json, "{\"version\":1,\"states\":{}}\n", -1, NULL));
  g_assert_true(g_file_set_contents(
      settings_json,
      "{\"bar\":{\"widgets\":{\"left\":[],\"center\":[],\"right\":[]}}}\n",
      -1, NULL));

  g_autofree char *old_xdg = g_strdup(g_getenv("XDG_CONFIG_HOME"));
  g_setenv("XDG_CONFIG_HOME", config, TRUE);
  ShaulaNoctaliaResult result = {0};
  g_assert_cmpint(shaula_noctalia_install(source, FALSE, &result), ==,
                  SHAULA_NOCTALIA_STATUS_INVALID_STATE);
  shaula_noctalia_result_clear(&result);
  g_assert_true(g_file_test(plugin_dir, G_FILE_TEST_IS_DIR));

  g_assert_true(g_file_set_contents(
      plugins_json, "{\"version\":2,\"states\":{}}\n", -1, NULL));
  result = (ShaulaNoctaliaResult){0};
  g_assert_cmpint(shaula_noctalia_install(source, FALSE, &result), ==,
                  SHAULA_NOCTALIA_STATUS_UNMANAGED_PLUGIN);
  shaula_noctalia_result_clear(&result);

  if (old_xdg != NULL)
    g_setenv("XDG_CONFIG_HOME", old_xdg, TRUE);
  else
    g_unsetenv("XDG_CONFIG_HOME");
  g_assert_true(remove_tree(root));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/config/noctalia/install-remove-idempotent",
                  test_install_remove_idempotent);
  g_test_add_func("/config/noctalia/invalid-unmanaged-safe",
                  test_invalid_and_unmanaged_are_safe);
  return g_test_run();
}
