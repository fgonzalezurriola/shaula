#include <string.h>

#include <glib.h>

#include "settings_config.h"

static void assert_defaults(const ShaulaSettingsConfig *config) {
  g_assert_cmpint(config->region_mode, ==, REGION_FROZEN);
  g_assert_cmpint(config->window_mode, ==, WINDOW_FLOATING);
  g_assert_true(config->focused);
  g_assert_true(config->close_preview_on_save);
  g_assert_cmpint(config->width, ==, 1100);
  g_assert_cmpint(config->height, ==, 720);
  g_assert_cmpstr(config->column_display, ==, "normal");
  g_assert_false(config->floating_x_set);
  g_assert_false(config->floating_y_set);
  g_assert_cmpint(config->floating_x, ==, 0);
  g_assert_cmpint(config->floating_y, ==, 0);
  g_assert_cmpstr(config->floating_relative_to, ==, "top-left");
  g_assert_cmpint(config->position_preset, ==, POSITION_CENTERED);
  g_assert_false(config->quick_skip_preview);
  g_assert_true(config->quick_copy);
  g_assert_false(config->quick_save);
  g_assert_false(config->area_skip_preview);
  g_assert_true(config->area_copy);
  g_assert_false(config->area_save);
  g_assert_true(config->fullscreen_skip_preview);
  g_assert_true(config->fullscreen_copy);
  g_assert_true(config->fullscreen_save);
  g_assert_true(config->all_screens_skip_preview);
  g_assert_true(config->all_screens_copy);
  g_assert_true(config->all_screens_save);
  g_assert_cmpstr(config->save_folder, ==, "~/Pictures/shaula");
  g_assert_true(config->notifications_success);
  g_assert_true(config->notifications_errors);
  g_assert_true(config->notifications_thumbnails);
}

static void restore_environment(const char *name, const char *value) {
  if (value != NULL)
    g_setenv(name, value, TRUE);
  else
    g_unsetenv(name);
}

static void test_defaults_and_clear(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  assert_defaults(&config);

  shaula_settings_config_clear(&config);
  g_assert_null(config.column_display);
  g_assert_null(config.floating_relative_to);
  g_assert_null(config.save_folder);

  shaula_settings_config_clear(&config);
  g_assert_null(config.column_display);
  g_assert_null(config.floating_relative_to);
  g_assert_null(config.save_folder);
}

static void test_enum_text_and_size_presets(void) {
  g_assert_cmpint(REGION_LIVE, ==, 0);
  g_assert_cmpint(REGION_FROZEN, ==, 1);
  g_assert_cmpint(WINDOW_AUTO, ==, 0);
  g_assert_cmpint(WINDOW_TILING, ==, 1);
  g_assert_cmpint(WINDOW_FLOATING, ==, 2);
  g_assert_cmpint(WINDOW_MAXIMIZED, ==, 3);
  g_assert_cmpint(WINDOW_MAXIMIZED_TO_EDGES, ==, 4);
  g_assert_cmpint(WINDOW_FULLSCREEN, ==, 5);
  g_assert_cmpint(SIZE_SMALL, ==, 0);
  g_assert_cmpint(SIZE_MEDIUM, ==, 1);
  g_assert_cmpint(SIZE_LARGE, ==, 2);
  g_assert_cmpint(SIZE_CUSTOM, ==, 3);
  g_assert_cmpint(POSITION_CENTERED, ==, 0);
  g_assert_cmpint(POSITION_TOP_LEFT, ==, 1);
  g_assert_cmpint(POSITION_TOP_RIGHT, ==, 2);
  g_assert_cmpint(POSITION_CUSTOM, ==, 3);

  g_assert_cmpstr(shaula_settings_region_mode_text(REGION_LIVE), ==, "live");
  g_assert_cmpstr(shaula_settings_region_mode_text(REGION_FROZEN), ==,
                  "frozen");
  g_assert_cmpstr(shaula_settings_window_mode_text(WINDOW_AUTO), ==, "auto");
  g_assert_cmpstr(shaula_settings_window_mode_text(WINDOW_TILING), ==,
                  "tiling");
  g_assert_cmpstr(shaula_settings_window_mode_text(WINDOW_FLOATING), ==,
                  "floating");
  g_assert_cmpstr(shaula_settings_window_mode_text(WINDOW_MAXIMIZED), ==,
                  "maximized");
  g_assert_cmpstr(shaula_settings_window_mode_text(WINDOW_MAXIMIZED_TO_EDGES),
                  ==, "maximized-to-edges");
  g_assert_cmpstr(shaula_settings_window_mode_text(WINDOW_FULLSCREEN), ==,
                  "fullscreen");

  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  g_assert_cmpint(shaula_settings_size_preset_for_config(&config), ==,
                  SIZE_MEDIUM);

  shaula_settings_apply_size_preset(&config, SIZE_SMALL);
  g_assert_cmpint(config.width, ==, 900);
  g_assert_cmpint(config.height, ==, 600);
  g_assert_cmpint(shaula_settings_size_preset_for_config(&config), ==,
                  SIZE_SMALL);

  shaula_settings_apply_size_preset(&config, SIZE_LARGE);
  g_assert_cmpint(config.width, ==, 1400);
  g_assert_cmpint(config.height, ==, 900);
  g_assert_cmpint(shaula_settings_size_preset_for_config(&config), ==,
                  SIZE_LARGE);

  config.width = 1234;
  config.height = 777;
  shaula_settings_apply_size_preset(&config, SIZE_CUSTOM);
  g_assert_cmpint(config.width, ==, 1234);
  g_assert_cmpint(config.height, ==, 777);
  g_assert_cmpint(shaula_settings_size_preset_for_config(&config), ==,
                  SIZE_CUSTOM);
  shaula_settings_config_clear(&config);
}

static void test_position_presets(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  config.floating_x = 27;
  config.floating_y = -31;

  shaula_settings_apply_position_preset(&config, POSITION_TOP_LEFT);
  g_assert_cmpint(config.position_preset, ==, POSITION_TOP_LEFT);
  g_assert_true(config.floating_x_set);
  g_assert_true(config.floating_y_set);
  g_assert_cmpint(config.floating_x, ==, 80);
  g_assert_cmpint(config.floating_y, ==, 80);
  g_assert_cmpstr(config.floating_relative_to, ==, "top-left");

  shaula_settings_apply_position_preset(&config, POSITION_TOP_RIGHT);
  g_assert_cmpint(config.position_preset, ==, POSITION_TOP_RIGHT);
  g_assert_cmpstr(config.floating_relative_to, ==, "top-right");

  config.floating_x = 51;
  config.floating_y = 61;
  g_free(config.floating_relative_to);
  config.floating_relative_to = g_strdup("bottom-right");
  shaula_settings_apply_position_preset(&config, POSITION_CUSTOM);
  g_assert_cmpint(config.position_preset, ==, POSITION_CUSTOM);
  g_assert_true(config.floating_x_set);
  g_assert_true(config.floating_y_set);
  g_assert_cmpint(config.floating_x, ==, 51);
  g_assert_cmpint(config.floating_y, ==, 61);
  g_assert_cmpstr(config.floating_relative_to, ==, "bottom-right");

  shaula_settings_apply_position_preset(&config, POSITION_CENTERED);
  g_assert_cmpint(config.position_preset, ==, POSITION_CENTERED);
  g_assert_false(config.floating_x_set);
  g_assert_false(config.floating_y_set);
  g_assert_cmpint(config.floating_x, ==, 51);
  g_assert_cmpint(config.floating_y, ==, 61);
  g_assert_cmpstr(config.floating_relative_to, ==, "top-left");
  shaula_settings_config_clear(&config);
}

static void test_resolve_config_path(void) {
  g_autofree char *old_explicit = g_strdup(g_getenv("SHAULA_CONFIG_FILE"));
  g_autofree char *old_xdg = g_strdup(g_getenv("XDG_CONFIG_HOME"));

  g_setenv("SHAULA_CONFIG_FILE", " \t/tmp/Shaula config/á.toml\r\n", TRUE);
  g_setenv("XDG_CONFIG_HOME", "/ignored", TRUE);
  g_autofree char *path = shaula_settings_resolve_config_path();
  g_assert_cmpstr(path, ==, "/tmp/Shaula config/á.toml");

  g_setenv("SHAULA_CONFIG_FILE", " \t\r\n", TRUE);
  g_setenv("XDG_CONFIG_HOME", "/tmp/xdg root", TRUE);
  g_clear_pointer(&path, g_free);
  path = shaula_settings_resolve_config_path();
  g_assert_cmpstr(path, ==, "/tmp/xdg root/shaula/config.toml");

  g_unsetenv("SHAULA_CONFIG_FILE");
  g_setenv("XDG_CONFIG_HOME", "/tmp/xdg/", TRUE);
  g_clear_pointer(&path, g_free);
  path = shaula_settings_resolve_config_path();
  g_assert_cmpstr(path, ==, "/tmp/xdg//shaula/config.toml");

  g_unsetenv("SHAULA_CONFIG_FILE");
  g_setenv("XDG_CONFIG_HOME", "", TRUE);
  g_clear_pointer(&path, g_free);
  path = shaula_settings_resolve_config_path();
  const char *home = g_get_home_dir();
  if (home != NULL && *home != '\0') {
    g_autofree char *expected =
        g_strconcat(home, "/.config/shaula/config.toml", NULL);
    g_assert_cmpstr(path, ==, expected);
  } else {
    g_assert_null(path);
  }

  restore_environment("SHAULA_CONFIG_FILE", old_explicit);
  restore_environment("XDG_CONFIG_HOME", old_xdg);
}

static void test_config_path_from_show_json(void) {
  g_assert_null(shaula_settings_config_path_from_show_json(NULL));
  g_assert_null(shaula_settings_config_path_from_show_json("{}"));

  g_autofree char *empty =
      shaula_settings_config_path_from_show_json("{\"path\":\"\"}");
  g_assert_nonnull(empty);
  g_assert_cmpstr(empty, ==, "");

  const char *json =
      "{\"ok\":true,\"result\":{\"path\":\"/tmp/config folder/á;$(touch "
      "nope).toml\"}}";
  g_autofree char *path = shaula_settings_config_path_from_show_json(json);
  g_assert_cmpstr(path, ==, "/tmp/config folder/á;$(touch nope).toml");
}

static void test_complete_config_json(void) {
  const char *json =
      "{\"ok\":true,\"result\":{\"path\":\"/tmp/config.toml\","
      "\"loaded\":true,\"config\":{"
      "\"capture\":{\"region_capture_mode\":\"live\",\"after\":{"
      "\"save_folder\":\"~/Pictures/Screen shots/á;$(touch nope)\","
      "\"quick\":{\"skip_preview\":true,\"copy_to_clipboard\":false,"
      "\"save_to_folder\":true},"
      "\"area\":{\"skip_preview\":true,\"copy_to_clipboard\":true,"
      "\"save_to_folder\":false},"
      "\"fullscreen\":{\"skip_preview\":false,"
      "\"copy_to_clipboard\":false,\"save_to_folder\":true},"
      "\"all_screens\":{\"skip_preview\":false,"
      "\"copy_to_clipboard\":true,\"save_to_folder\":false}}},"
      "\"notifications\":{\"success\":false,\"errors\":true,"
      "\"thumbnails\":false},"
      "\"preview\":{\"window\":{\"app_id\":\"dev.shaula.preview\","
      "\"title\":\"Shaula Preview\",\"mode\":\"maximized-to-edges\","
      "\"focused\":false,\"close_preview_on_save\":false,"
      "\"width\":1234,\"height\":777,"
      "\"default_column_display\":\"custom-columns\","
      "\"floating_position\":{\"x\":-45,\"y\":67,"
      "\"relative_to\":\"bottom-right\"}}}}}}}";

  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  g_assert_true(shaula_settings_config_from_show_json(json, &config));
  g_assert_cmpint(config.region_mode, ==, REGION_LIVE);
  g_assert_cmpint(config.window_mode, ==, WINDOW_MAXIMIZED_TO_EDGES);
  g_assert_false(config.focused);
  g_assert_false(config.close_preview_on_save);
  g_assert_cmpint(config.width, ==, 1234);
  g_assert_cmpint(config.height, ==, 777);
  g_assert_cmpstr(config.column_display, ==, "custom-columns");
  g_assert_true(config.floating_x_set);
  g_assert_true(config.floating_y_set);
  g_assert_cmpint(config.floating_x, ==, -45);
  g_assert_cmpint(config.floating_y, ==, 67);
  g_assert_cmpstr(config.floating_relative_to, ==, "bottom-right");
  g_assert_cmpint(config.position_preset, ==, POSITION_CUSTOM);
  g_assert_true(config.quick_skip_preview);
  g_assert_false(config.quick_copy);
  g_assert_true(config.quick_save);
  g_assert_true(config.area_skip_preview);
  g_assert_true(config.area_copy);
  g_assert_false(config.area_save);
  g_assert_false(config.fullscreen_skip_preview);
  g_assert_false(config.fullscreen_copy);
  g_assert_true(config.fullscreen_save);
  g_assert_false(config.all_screens_skip_preview);
  g_assert_true(config.all_screens_copy);
  g_assert_false(config.all_screens_save);
  g_assert_cmpstr(config.save_folder, ==,
                  "~/Pictures/Screen shots/á;$(touch nope)");
  g_assert_false(config.notifications_success);
  g_assert_true(config.notifications_errors);
  g_assert_false(config.notifications_thumbnails);
  g_assert_cmpint(shaula_settings_size_preset_for_config(&config), ==,
                  SIZE_CUSTOM);
  shaula_settings_config_clear(&config);
}

static void test_missing_wrong_and_malformed_fields(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  config.width = 777;
  config.height = 888;
  config.floating_x_set = TRUE;
  config.floating_y_set = TRUE;
  config.floating_x = 41;
  config.floating_y = 42;

  const char *wrong_types =
      "{\"preview\":{\"window\":{\"focused\":\"false\","
      "\"width\":\"wide\",\"height\":null,"
      "\"floating_position\":{\"x\":\"bad\",\"y\":null}}},"
      "\"future_field\":{\"enabled\":true}}";
  g_assert_true(shaula_settings_config_from_show_json(wrong_types, &config));
  g_assert_true(config.focused);
  g_assert_cmpint(config.width, ==, 777);
  g_assert_cmpint(config.height, ==, 888);
  g_assert_false(config.floating_x_set);
  g_assert_false(config.floating_y_set);
  g_assert_cmpint(config.floating_x, ==, 41);
  g_assert_cmpint(config.floating_y, ==, 42);
  g_assert_cmpint(config.position_preset, ==, POSITION_CENTERED);

  g_assert_true(shaula_settings_config_from_show_json(
      "malformed prefix \"width\":-42 trailing", &config));
  g_assert_cmpint(config.width, ==, -42);
  shaula_settings_config_clear(&config);
}

static void test_null_input_leaves_output_unchanged(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  config.width = 654;
  config.floating_x_set = TRUE;
  config.floating_x = 92;
  char *column_display = config.column_display;
  char *relative_to = config.floating_relative_to;
  char *save_folder = config.save_folder;

  g_assert_false(shaula_settings_config_from_show_json(NULL, &config));
  g_assert_cmpint(config.width, ==, 654);
  g_assert_true(config.floating_x_set);
  g_assert_cmpint(config.floating_x, ==, 92);
  g_assert_true(config.column_display == column_display);
  g_assert_true(config.floating_relative_to == relative_to);
  g_assert_true(config.save_folder == save_folder);
  shaula_settings_config_clear(&config);
}

static void test_unknown_mappings_ranges_and_empty_strings(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  config.width = 321;
  config.height = 654;

  const char *json = "{\"region_capture_mode\":\"future-region\","
                     "\"mode\":\"future-window\",\"width\":2147483648,"
                     "\"height\":-2147483649,\"default_column_display\":\"\","
                     "\"x\":80,\"y\":80,\"relative_to\":\"\","
                     "\"save_folder\":\"\"}";
  g_assert_true(shaula_settings_config_from_show_json(json, &config));
  g_assert_cmpint(config.region_mode, ==, REGION_LIVE);
  g_assert_cmpint(config.window_mode, ==, WINDOW_FLOATING);
  g_assert_cmpint(config.width, ==, 321);
  g_assert_cmpint(config.height, ==, 654);
  g_assert_cmpstr(config.column_display, ==, "");
  g_assert_true(config.floating_x_set);
  g_assert_true(config.floating_y_set);
  g_assert_cmpint(config.floating_x, ==, 80);
  g_assert_cmpint(config.floating_y, ==, 80);
  g_assert_cmpstr(config.floating_relative_to, ==, "");
  g_assert_cmpint(config.position_preset, ==, POSITION_CUSTOM);
  g_assert_cmpstr(config.save_folder, ==, "");
  shaula_settings_config_clear(&config);
}

static void test_first_match_and_substring_collision_contract(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);

  config.width = 700;
  g_assert_true(shaula_settings_config_from_show_json(
      "{\"note\":\"malformed \"width\":123 text\",\"width\":900}", &config));
  g_assert_cmpint(config.width, ==, 123);

  config.width = 700;
  g_assert_true(shaula_settings_config_from_show_json(
      "{\"other\":{\"width\":222},\"preview\":{\"width\":900}}", &config));
  g_assert_cmpint(config.width, ==, 222);

  config.width = 700;
  g_assert_true(shaula_settings_config_from_show_json(
      "{\"width\":321,\"width\":654}", &config));
  g_assert_cmpint(config.width, ==, 321);

  config.width = 700;
  g_assert_true(shaula_settings_config_from_show_json(
      "{\"width\":bad,\"width\":900}", &config));
  g_assert_cmpint(config.width, ==, 700);
  shaula_settings_config_clear(&config);
}

static void test_string_escape_non_decoding_contract(void) {
  g_autofree char *path =
      shaula_settings_config_path_from_show_json("{\"path\":\"one\\\"two\"}");
  g_assert_cmpstr(path, ==, "one\\");

  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  g_assert_true(shaula_settings_config_from_show_json(
      "{\"default_column_display\":\"alpha\\\"beta\","
      "\"save_folder\":\"folder\\name\"}",
      &config));
  g_assert_cmpstr(config.column_display, ==, "alpha\\");
  g_assert_cmpstr(config.save_folder, ==, "folder\\name");
  shaula_settings_config_clear(&config);
}

static void test_integer_prefix_and_boundary_contract(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);

  g_assert_true(shaula_settings_config_from_show_json(
      "{\"width\":+123abc,\"height\":-2147483648,\"x\":2147483647,"
      "\"y\":-2147483648}",
      &config));
  g_assert_cmpint(config.width, ==, 123);
  g_assert_cmpint(config.height, ==, G_MININT);
  g_assert_true(config.floating_x_set);
  g_assert_true(config.floating_y_set);
  g_assert_cmpint(config.floating_x, ==, G_MAXINT);
  g_assert_cmpint(config.floating_y, ==, G_MININT);

  config.width = 456;
  config.height = 789;
  g_assert_true(shaula_settings_config_from_show_json(
      "{\"width\":2147483648,\"height\":-2147483649,\"x\":+}", &config));
  g_assert_cmpint(config.width, ==, 456);
  g_assert_cmpint(config.height, ==, 789);
  g_assert_false(config.floating_x_set);
  shaula_settings_config_clear(&config);
}

static void test_incomplete_after_mode_preserves_values(void) {
  ShaulaSettingsConfig config = {0};
  shaula_settings_config_init_defaults(&config);
  config.quick_skip_preview = FALSE;
  config.quick_copy = TRUE;
  config.quick_save = FALSE;

  g_assert_true(shaula_settings_config_from_show_json(
      "{\"quick\":{\"skip_preview\":true,\"copy_to_clipboard\":false",
      &config));
  g_assert_false(config.quick_skip_preview);
  g_assert_true(config.quick_copy);
  g_assert_false(config.quick_save);

  g_assert_true(shaula_settings_config_from_show_json(
      "{\"quick\":{\"skip_preview\":true},"
      "\"quick\":{\"skip_preview\":false}}",
      &config));
  g_assert_true(config.quick_skip_preview);
  shaula_settings_config_clear(&config);
}

static void test_repeated_init_clear_cycles(void) {
  for (guint index = 0; index < 100; index++) {
    ShaulaSettingsConfig config = {0};
    shaula_settings_config_init_defaults(&config);
    assert_defaults(&config);
    g_assert_true(
        shaula_settings_config_from_show_json("{\"width\":900}", &config));
    g_assert_cmpint(config.width, ==, 900);
    shaula_settings_config_clear(&config);
    shaula_settings_config_clear(&config);
  }
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/settings-config/defaults-and-clear",
                  test_defaults_and_clear);
  g_test_add_func("/settings-config/enum-text-and-size-presets",
                  test_enum_text_and_size_presets);
  g_test_add_func("/settings-config/position-presets", test_position_presets);
  g_test_add_func("/settings-config/resolve-config-path",
                  test_resolve_config_path);
  g_test_add_func("/settings-config/config-path-from-show-json",
                  test_config_path_from_show_json);
  g_test_add_func("/settings-config/complete-config-json",
                  test_complete_config_json);
  g_test_add_func("/settings-config/missing-wrong-malformed-fields",
                  test_missing_wrong_and_malformed_fields);
  g_test_add_func("/settings-config/null-input-unchanged",
                  test_null_input_leaves_output_unchanged);
  g_test_add_func("/settings-config/unknown-ranges-empty-strings",
                  test_unknown_mappings_ranges_and_empty_strings);
  g_test_add_func("/settings-config/first-match-substring-collisions",
                  test_first_match_and_substring_collision_contract);
  g_test_add_func("/settings-config/string-escape-non-decoding",
                  test_string_escape_non_decoding_contract);
  g_test_add_func("/settings-config/integer-prefix-boundaries",
                  test_integer_prefix_and_boundary_contract);
  g_test_add_func("/settings-config/incomplete-after-mode",
                  test_incomplete_after_mode_preserves_values);
  g_test_add_func("/settings-config/repeated-init-clear",
                  test_repeated_init_clear_cycles);
  return g_test_run();
}
