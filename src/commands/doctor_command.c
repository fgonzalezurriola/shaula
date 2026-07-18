#include "commands.h"

#include "clipboard/clipboard.h"
#include "config/config.h"
#include "runtime/helper_resolution.h"
#include "support.h"

#include <glib.h>
#include <string.h>

int shaula_doctor_command_run(int argc, char **argv) {
  if (argc != 3 || !g_str_equal(argv[2], "--json"))
    return shaula_command_write_error("doctor", "ERR_CLI_USAGE",
                                      "--json is required", "{}");

  g_autofree char *self = shaula_executable_current_path();
  g_autofree char *self_json =
      shaula_command_json_string(self != NULL ? self : "shaula");
  g_autofree char *config_path = shaula_config_path_new();
  g_autofree char *config_json_value =
      shaula_command_json_string(config_path != NULL ? config_path : "");

  const char *xdg = g_getenv("XDG_CONFIG_HOME");
  g_autofree char *xdg_fallback = NULL;
  if (xdg == NULL || xdg[0] == '\0') {
    const char *home = g_getenv("HOME");
    if (home != NULL && home[0] != '\0')
      xdg_fallback = g_build_filename(home, ".config", NULL);
    xdg = xdg_fallback;
  }

  g_autofree char *noctalia_dir =
      xdg != NULL ? g_build_filename(xdg, "noctalia", NULL) : NULL;
  g_autofree char *noctalia_plugins =
      noctalia_dir != NULL ? g_build_filename(noctalia_dir, "plugins", NULL)
                           : NULL;
  g_autofree char *noctalia_plugin =
      noctalia_plugins != NULL
          ? g_build_filename(noctalia_plugins, "shaula", NULL)
          : NULL;
  g_autofree char *plugins_json =
      noctalia_dir != NULL
          ? g_build_filename(noctalia_dir, "plugins.json", NULL)
          : NULL;
  g_autofree char *settings_json =
      noctalia_dir != NULL
          ? g_build_filename(noctalia_dir, "settings.json", NULL)
          : NULL;
  g_autofree char *grim_path = shaula_executable_find_grim();

  g_autofree char *result = g_strdup_printf(
      "{\"paths\":{\"binary\":%s,\"config_file\":%s,"
      "\"config_exists\":%s},\"wayland\":{\"wayland_display\":%s},"
      "\"tools\":{\"grim\":{\"found\":%s},"
      "\"shaula-clipboard-provider\":{\"found\":%s}},\"noctalia\":{"
      "\"dir_exists\":%s,\"plugins_dir_exists\":%s,"
      "\"plugins_json_exists\":%s,\"settings_json_exists\":%s,"
      "\"shaula_plugin_dir_exists\":%s,\"plugin_installed\":%s}}",
      self_json, config_json_value,
      shaula_command_json_bool(config_path != NULL &&
                               g_file_test(config_path, G_FILE_TEST_EXISTS)),
      g_getenv("WAYLAND_DISPLAY") != NULL ? "\"present\"" : "null",
      shaula_command_json_bool(grim_path != NULL),
      shaula_command_json_bool(shaula_clipboard_provider_available()),
      shaula_command_json_bool(noctalia_dir != NULL &&
                               g_file_test(noctalia_dir, G_FILE_TEST_IS_DIR)),
      shaula_command_json_bool(
          noctalia_plugins != NULL &&
          g_file_test(noctalia_plugins, G_FILE_TEST_IS_DIR)),
      shaula_command_json_bool(
          plugins_json != NULL &&
          g_file_test(plugins_json, G_FILE_TEST_IS_REGULAR)),
      shaula_command_json_bool(
          settings_json != NULL &&
          g_file_test(settings_json, G_FILE_TEST_IS_REGULAR)),
      shaula_command_json_bool(
          noctalia_plugin != NULL &&
          g_file_test(noctalia_plugin, G_FILE_TEST_IS_DIR)),
      shaula_command_json_bool(
          noctalia_plugin != NULL &&
          g_file_test(noctalia_plugin, G_FILE_TEST_IS_DIR)));

  g_autofree char *timestamp = shaula_command_json_timestamp();
  if (timestamp == NULL)
    return shaula_command_write_error("doctor", "ERR_UNKNOWN_UNMAPPED",
                                      "could not encode doctor result", "{}");
  g_print("{\"ok\":true,\"contract_version\":\"1.0.0\","
          "\"command\":\"doctor\",\"timestamp\":\"%s\",%.*s,"
          "\"warnings\":[]}\n",
          timestamp, (int)strlen(result) - 2, result + 1);
  return 0;
}
