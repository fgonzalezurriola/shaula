#include "commands.h"

#include "capabilities/runtime.h"
#include "clipboard/clipboard.h"
#include "support.h"

#include <glib.h>

int shaula_capabilities_command_run(int argc, char **argv) {
  int start = 2;
  if (argc > 2 && g_str_equal(argv[2], "list"))
    start = 3;
  if (argc != start + 1 || !g_str_equal(argv[start], "--json"))
    return shaula_command_write_error(
        "capabilities list", "ERR_CLI_USAGE",
        "usage: shaula capabilities [list] --json", "{}");

  ShaulaCapabilitiesEnvironment environment =
      shaula_command_capabilities_environment();
  ShaulaRuntimeDecision runtime = {0};
  if (shaula_capabilities_resolve(&environment, &runtime) !=
      SHAULA_CAPABILITIES_STATUS_OK)
    return shaula_command_write_error("capabilities list",
                                      "ERR_UNKNOWN_UNMAPPED",
                                      "capability resolution failed", "{}");

  g_autofree char *compositor =
      shaula_command_env_span_json(runtime.compositor.label);
  if (!runtime.compositor_supported) {
    g_autofree char *details =
        g_strdup_printf("{\"detected_compositor\":%s}", compositor);
    return shaula_command_write_error(
        "capabilities list", "ERR_UNSUPPORTED_COMPOSITOR",
        "unsupported compositor for shaula v1", details);
  }
  if (!runtime.capture_route_available) {
    g_autofree char *details = g_strdup_printf(
        "{\"detected_compositor\":%s,\"grim_available\":%s,"
        "\"portal_available\":%s}",
        compositor, shaula_command_json_bool(runtime.grim_available),
        shaula_command_json_bool(runtime.portal_available));
    return shaula_command_write_error(
        "capabilities list", "ERR_CAPTURE_BACKEND_UNAVAILABLE",
        "no usable Wayland capture route is available", details);
  }

  ShaulaEnvSpan backend_span =
      shaula_capabilities_backend_label(runtime.backend);
  g_autofree char *backend_text =
      g_strndup(backend_span.data, backend_span.length);
  g_autofree char *backend = shaula_command_json_string(backend_text);
  GString *fallbacks = g_string_new("[");
  size_t fallback_count =
      shaula_capabilities_fallback_count(runtime);
  for (size_t i = 0; i < fallback_count; i++) {
    ShaulaEnvSpan fallback_span = shaula_capabilities_backend_label(
        shaula_capabilities_fallback_at(runtime, i));
    g_autofree char *fallback_text =
        g_strndup(fallback_span.data, fallback_span.length);
    g_autofree char *fallback =
        shaula_command_json_string(fallback_text);
    if (i > 0)
      g_string_append_c(fallbacks, ',');
    g_string_append(fallbacks, fallback);
  }
  g_string_append_c(fallbacks, ']');

  GString *warnings = g_string_new("[");
  gboolean has_warning = FALSE;
  if (!runtime.capture.window) {
    g_string_append(warnings, "\"window_capture_degraded\"");
    has_warning = TRUE;
  }
  if (shaula_capabilities_uses_portal_backend(runtime) == 1) {
    if (has_warning)
      g_string_append_c(warnings, ',');
    g_string_append(warnings, "\"portal_fallback\"");
  }
  g_string_append_c(warnings, ']');

  g_autofree char *timestamp = shaula_command_json_timestamp();
  g_print(
      "{\"ok\":true,\"contract_version\":\"1.0.0\",\"command\":"
      "\"capabilities list\",\"timestamp\":\"%s\",\"capture\":{"
      "\"area\":%s,\"fullscreen\":%s,\"all_screens\":%s,\"window\":%s},"
      "\"backend\":%s,\"fallbacks\":%s,\"portal_window_capable\":%s,"
      "\"grim_available\":%s,\"capture_route_available\":%s,"
      "\"clipboard_provider_available\":%s,\"result\":{\"capture\":{"
      "\"area\":%s,\"fullscreen\":%s,\"all_screens\":%s,\"window\":%s},"
      "\"backend\":%s,\"fallbacks\":%s,\"compositor\":%s,"
      "\"ipc_version\":\"1.0.0\",\"portal_available\":%s,"
      "\"portal_window_capable\":%s,\"overlay_supported\":%s,"
      "\"grim_available\":%s,\"capture_route_available\":%s,"
      "\"clipboard_provider_available\":%s},\"warnings\":%s}\n",
      timestamp, shaula_command_json_bool(runtime.capture.area),
      shaula_command_json_bool(runtime.capture.fullscreen),
      shaula_command_json_bool(runtime.capture.all_screens),
      shaula_command_json_bool(runtime.capture.window), backend, fallbacks->str,
      shaula_command_json_bool(runtime.portal_window_capable),
      shaula_command_json_bool(runtime.grim_available),
      shaula_command_json_bool(runtime.capture_route_available),
      shaula_command_json_bool(shaula_clipboard_provider_available()),
      shaula_command_json_bool(runtime.capture.area),
      shaula_command_json_bool(runtime.capture.fullscreen),
      shaula_command_json_bool(runtime.capture.all_screens),
      shaula_command_json_bool(runtime.capture.window), backend, fallbacks->str,
      compositor, shaula_command_json_bool(runtime.portal_available),
      shaula_command_json_bool(runtime.portal_window_capable),
      shaula_command_json_bool(runtime.overlay_supported),
      shaula_command_json_bool(runtime.grim_available),
      shaula_command_json_bool(runtime.capture_route_available),
      shaula_command_json_bool(shaula_clipboard_provider_available()),
      warnings->str);
  g_string_free(fallbacks, TRUE);
  g_string_free(warnings, TRUE);
  return 0;
}
