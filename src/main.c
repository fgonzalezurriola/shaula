#include "capture/command.h"
#include "commands/commands.h"
#include "commands/support.h"

#include <glib.h>

typedef int (*ShaulaCommandRun)(int argc, char **argv);

typedef struct {
  const char *family;
  ShaulaCommandRun run;
} ShaulaCommandFamily;

static const ShaulaCommandFamily COMMAND_FAMILIES[] = {
    {"preflight", shaula_preflight_command_run},
    {"capabilities", shaula_capabilities_command_run},
    {"errors", shaula_errors_command_run},
    {"settings", shaula_settings_command_run},
    {"config", shaula_config_command_run},
    {"preview", shaula_preview_command_run},
    {"directory", shaula_directory_command_run},
    {"history", shaula_history_command_run},
    {"clipboard", shaula_clipboard_command_run},
    {"explore", shaula_explore_command_run},
    {"doctor", shaula_doctor_command_run},
    {"setup", shaula_setup_command_run},
    {"notify", shaula_notify_command_run},
    {"capture", shaula_capture_command_run},
};

int main(int argc, char **argv) {
  if (argc < 2)
    return shaula_command_write_error(
        "", "ERR_CLI_USAGE",
        "usage: shaula <capture|preview|notify|config|settings|setup|directory|"
        "doctor|explore|preflight|capabilities|history|clipboard|errors> ...",
        "{}");

  for (gsize i = 0; i < G_N_ELEMENTS(COMMAND_FAMILIES); i++) {
    if (g_str_equal(argv[1], COMMAND_FAMILIES[i].family))
      return COMMAND_FAMILIES[i].run(argc, argv);
  }

  return shaula_command_write_error(argv[1], "ERR_CLI_USAGE",
                                    "unsupported command family", "{}");
}
