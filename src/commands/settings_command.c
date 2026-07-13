#include "commands.h"

#include "runtime/helper_resolution.h"
#include "runtime/process_exec.h"
#include "support.h"

#include <glib.h>

int shaula_settings_command_run(int argc, char **argv) {
  if (argc == 3 && g_str_equal(argv[2], "--json")) {
    return shaula_command_write_success(
        "settings",
        "{\"purpose\":\"shaula_agent_discovery\",\"human_ui\":\"shaula "
        "settings\",\"privacy\":{\"explore_captures_pixels\":false,"
        "\"capture_captures_pixels\":true,\"window_titles_may_be_sensitive\":"
        "true,\"screenshots_stay_local_by_default\":true},"
        "\"recommended_flow\":[\"shaula settings --json\",\"shaula doctor "
        "--json\",\"shaula capabilities list --json\",\"shaula explore "
        "--json --brief\",\"shaula capture fullscreen --json "
        "--no-preview\"],\"commands\":{\"discover\":\"shaula settings "
        "--json\",\"health\":\"shaula doctor --json\",\"capabilities\":"
        "\"shaula capabilities list --json\",\"desktop_inventory\":\"shaula "
        "explore --json [--brief]\",\"capture_current_output\":\"shaula "
        "capture fullscreen --json --no-preview\",\"capture_all_outputs\":"
        "\"shaula capture all-screens --json --no-preview\","
        "\"capture_area\":\"shaula capture area --json --no-preview\","
        "\"open_settings_ui\":\"shaula settings\"},\"json_contract\":{"
        "\"success_path\":\".result\",\"error_code_path\":\".error.code\","
        "\"warnings_path\":\".warnings\",\"capture_path\":\".result.path "
        "// .path\",\"recommended_capture_path\":"
        "\".result.recommended_capture\"}}",
        "[]");
  }
  if (argc != 2)
    return shaula_command_write_error("settings", "ERR_CLI_USAGE",
                                      "usage: shaula settings [--json]",
                                      "{}");

  g_autofree char *helper =
      shaula_executable_resolve_helper("SHAULA_SETTINGS_HELPER_BIN",
                                       "shaula-settings");
  if (helper == NULL)
    return shaula_command_write_error("settings", "ERR_SETTINGS_UNAVAILABLE",
                                      "settings helper is unavailable", "{}");

  const char *helper_argv[] = {helper, NULL};
  int exit_code = 0;
  if (shaula_process_run_sync((const char *const *)helper_argv, NULL, NULL,
                              NULL, &exit_code) != SHAULA_PROCESS_STATUS_OK ||
      exit_code != 0)
    return shaula_command_write_error("settings", "ERR_SETTINGS_UNAVAILABLE",
                                      "settings helper is unavailable", "{}");
  return 0;
}
