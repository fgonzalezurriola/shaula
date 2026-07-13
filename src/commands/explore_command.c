#include "commands.h"

#include "capabilities/runtime.h"
#include "compositor/focused_output.h"
#include "compositor/runtime.h"
#include "explore/inventory.h"
#include "support.h"

#include <glib.h>

int shaula_explore_command_run(int argc, char **argv) {
  gboolean json = FALSE;
  gboolean brief = FALSE;
  for (int i = 2; i < argc; i++) {
    if (g_str_equal(argv[i], "--json"))
      json = TRUE;
    else if (g_str_equal(argv[i], "--brief"))
      brief = TRUE;
    else
      return shaula_command_write_error("explore", "ERR_CLI_USAGE",
                                        "unsupported flag", "{}");
  }
  if (!json)
    return shaula_command_write_error("explore", "ERR_CLI_USAGE",
                                      "--json is required", "{}");

  ShaulaCapabilitiesEnvironment environment =
      shaula_command_capabilities_environment();
  ShaulaRuntimeDecision runtime = {0};
  if (shaula_capabilities_resolve(&environment, &runtime) !=
      SHAULA_CAPABILITIES_STATUS_OK)
    return shaula_command_write_error(
        "explore", "ERR_UNKNOWN_UNMAPPED",
        "desktop inventory resolution failed", "{}");

  ShaulaEnvSpan kind_span = shaula_compositor_kind_token(runtime.compositor.kind);
  g_autofree char *kind = g_strndup(kind_span.data, kind_span.length);
  g_autofree char *label =
      g_strndup(runtime.compositor.label.data, runtime.compositor.label.length);

  ShaulaFocusedOutputResult focused;
  shaula_focused_output_result_init(&focused);
  ShaulaFocusedOutputEnvironment focused_environment = {
      .overlay_output_name = g_getenv("SHAULA_OVERLAY_OUTPUT_NAME"),
      .compositor = environment.compositor,
  };
  ShaulaFocusedOutputStatus focused_status =
      shaula_focused_output_resolve(&focused_environment, &focused);
  if (focused_status == SHAULA_FOCUSED_OUTPUT_STATUS_OUT_OF_MEMORY) {
    shaula_focused_output_result_clear(&focused);
    return shaula_command_write_error(
        "explore", "ERR_UNKNOWN_UNMAPPED",
        "focused output resolution failed", "{}");
  }
  g_autofree char *focused_name =
      focused.present
          ? g_strndup((const char *)focused.name.data, focused.name.length)
          : NULL;

  ShaulaExploreInventory inventory;
  shaula_explore_inventory_init(&inventory);
  gboolean built = shaula_explore_inventory_build(
      kind, label, focused_name, brief, &inventory);
  shaula_focused_output_result_clear(&focused);
  if (!built) {
    shaula_explore_inventory_clear(&inventory);
    return shaula_command_write_error(
        "explore", "ERR_UNKNOWN_UNMAPPED",
        "desktop inventory response could not be built", "{}");
  }

  int status = shaula_command_write_success(
      "explore", inventory.result_json,
      inventory.inventory_available ? "[]"
                                    : "[\"explore_inventory_unavailable\"]");
  shaula_explore_inventory_clear(&inventory);
  return status;
}
