#include "commands.h"

#include "runtime/helper_resolution.h"
#include "runtime/process_exec.h"
#include "support.h"

#include <glib.h>

int shaula_launch_command_run(int argc, char **argv) {
  if (argc != 2)
    return shaula_command_write_error("launch", "ERR_CLI_USAGE",
                                      "usage: shaula launch", "{}");
  g_autofree char *helper = shaula_executable_resolve_helper(
      "SHAULA_LAUNCHER_HELPER_BIN", "shaula-launcher");
  if (helper == NULL)
    return shaula_command_write_error("launch", "ERR_SETTINGS_UNAVAILABLE",
                                      "launcher helper is unavailable", "{}");

  const char *helper_argv[] = {helper, argv[0], NULL};
  int exit_code = 0;
  if (shaula_process_run_sync(helper_argv, NULL, NULL, NULL, &exit_code) !=
          SHAULA_PROCESS_STATUS_OK ||
      exit_code != 0)
    return shaula_command_write_error("launch", "ERR_SETTINGS_UNAVAILABLE",
                                      "launcher helper is unavailable", "{}");
  return 0;
}
