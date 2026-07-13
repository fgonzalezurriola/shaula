#include "commands.h"

#include "preflight/probe.h"
#include "support.h"

#include <glib.h>
#include <stdio.h>
#include <time.h>

int shaula_preflight_command_run(int argc, char **argv) {
  if (argc != 3 || !g_str_equal(argv[2], "--json"))
    return shaula_command_write_error("preflight", "ERR_CLI_USAGE",
                                      "--json is required", "{}");

  ShaulaCapabilitiesEnvironment environment =
      shaula_command_capabilities_environment();
  ShaulaPreflightOutput output = {0};
  ShaulaPreflightStatus status = shaula_preflight_build(
      &environment, (gint64)time(NULL),
      shaula_command_json_span("portal_fallback"), &output);
  if (status != SHAULA_PREFLIGHT_STATUS_OK)
    return shaula_command_write_error(
        "preflight", "ERR_UNKNOWN_UNMAPPED",
        "preflight response could not be built", "{}");

  (void)fwrite(output.json.data, 1, output.json.length, stdout);
  int exit_code = output.exit_code;
  shaula_preflight_output_clear(&output);
  return exit_code;
}
