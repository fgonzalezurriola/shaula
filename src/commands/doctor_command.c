#include "commands.h"

#include "clipboard/clipboard.h"
#include "config/config.h"
#include "runtime/helper_resolution.h"
#include "shortcuts/shortcuts.h"
#include "support.h"

#include <glib.h>
#include <string.h>

static char *doctor_shortcut_status_json(void) {
  ShaulaShortcutStatus status;
  shaula_shortcut_status_init(&status);
  g_autofree char *query_error = NULL;
  ShaulaShortcutResult query = shaula_shortcuts_query(&status, &query_error);
  g_autofree char *detail = shaula_command_json_string(
      status.detail != NULL ? status.detail
                            : (query_error != NULL ? query_error : ""));
  g_autofree char *error_code = shaula_command_json_string(
      status.error_code != NULL ? status.error_code : "");
  GString *triggers = g_string_new("[");
  for (guint i = 0; i < G_N_ELEMENTS(status.triggers); i++) {
    g_autofree char *trigger = shaula_command_json_string(
        status.triggers[i] != NULL ? status.triggers[i] : "");
    if (i > 0U)
      g_string_append_c(triggers, ',');
    g_string_append(triggers, trigger);
  }
  g_string_append_c(triggers, ']');
  g_autofree char *triggers_json = g_string_free(triggers, FALSE);
  char *result = g_strdup_printf(
      "{\"query_ok\":%s,\"setup_completed\":%s,\"choice\":\"%s\","
      "\"enabled_requested\":%s,\"backend\":\"%s\",\"state\":\"%s\","
      "\"autostart_installed\":%s,\"provider_running\":%s,"
      "\"activation_ready\":%s,\"portal_version\":%u,\"detail\":%s,"
      "\"error_code\":%s,\"triggers\":%s}",
      shaula_command_json_bool(query == SHAULA_SHORTCUT_RESULT_OK),
      shaula_command_json_bool(status.setup_completed),
      shaula_shortcut_choice_token(status.choice),
      shaula_command_json_bool(status.enabled_requested),
      shaula_shortcut_backend_token(status.backend),
      shaula_shortcut_state_token(status.state),
      shaula_command_json_bool(status.autostart_installed),
      shaula_command_json_bool(status.provider_running),
      shaula_command_json_bool(status.activation_ready), status.portal_version,
      detail, error_code, triggers_json);
  shaula_shortcut_status_clear(&status);
  return result;
}

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
  g_autofree char *shortcuts_json = doctor_shortcut_status_json();

  g_autofree char *result = g_strdup_printf(
      "{\"paths\":{\"binary\":%s,\"config_file\":%s,"
      "\"config_exists\":%s},\"wayland\":{\"wayland_display\":%s},"
      "\"tools\":{\"grim\":{\"found\":%s},"
       "\"wl-copy\":{\"found\":%s}},\"noctalia\":{"
      "\"dir_exists\":%s,\"plugins_dir_exists\":%s,"
      "\"plugins_json_exists\":%s,\"settings_json_exists\":%s,"
      "\"shaula_plugin_dir_exists\":%s,\"plugin_installed\":%s},"
      "\"shortcuts\":%s}",
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
          g_file_test(noctalia_plugin, G_FILE_TEST_IS_DIR)),
      shortcuts_json);

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
