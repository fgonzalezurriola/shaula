#include "support.h"

#include "errors/taxonomy.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static ShaulaErrorSpan error_span(const char *value) {
  return (ShaulaErrorSpan){.data = value,
                           .length = value != NULL ? strlen(value) : 0};
}

ShaulaJsonSpan shaula_command_json_span(const char *value) {
  return (ShaulaJsonSpan){.data = (const guint8 *)value,
                          .length = value != NULL ? strlen(value) : 0};
}

char *shaula_command_json_string(const char *value) {
  ShaulaJsonOwnedBytes output = {0};
  if (shaula_json_string_escape(
          shaula_command_json_span(value != NULL ? value : ""), &output) !=
      SHAULA_JSON_STATUS_OK)
    return NULL;
  char *copy = g_strndup((const char *)output.data, output.length);
  shaula_json_owned_bytes_clear(&output);
  return copy;
}

char *shaula_command_json_timestamp(void) {
  ShaulaJsonOwnedBytes output = {0};
  if (shaula_json_timestamp_from_unix_seconds((gint64)time(NULL), &output) !=
      SHAULA_JSON_STATUS_OK)
    return NULL;
  char *copy = g_strndup((const char *)output.data, output.length);
  shaula_json_owned_bytes_clear(&output);
  return copy;
}

const char *shaula_command_json_bool(gboolean value) {
  return value ? "true" : "false";
}

int shaula_command_write_error(const char *command, const char *code,
                               const char *message,
                               const char *details_json) {
  const ShaulaErrorSpec *spec = shaula_error_taxonomy_spec_for(error_span(code));
  ShaulaJsonOwnedBytes output = {0};
  ShaulaJsonStatus status = shaula_json_basic_error_build(
      (gint64)time(NULL), shaula_command_json_span(command),
      shaula_command_json_span(code), shaula_command_json_span(message),
      spec->retryable,
      shaula_command_json_span(details_json != NULL ? details_json : "{}"),
      &output);
  if (status == SHAULA_JSON_STATUS_OK) {
    (void)fwrite(output.data, 1, output.length, stdout);
    shaula_json_owned_bytes_clear(&output);
  }
  return spec->exit_code;
}

int shaula_command_write_success(const char *command,
                                 const char *result_json,
                                 const char *warnings_json) {
  g_autofree char *timestamp = shaula_command_json_timestamp();
  if (timestamp == NULL)
    return shaula_command_write_error(command, "ERR_UNKNOWN_UNMAPPED",
                                      "could not encode command result", "{}");
  g_print("{\"ok\":true,\"contract_version\":\"1.0.0\","
          "\"command\":\"%s\",\"timestamp\":\"%s\",\"result\":%s,"
          "\"warnings\":%s}\n",
          command, timestamp, result_json,
          warnings_json != NULL ? warnings_json : "[]");
  return 0;
}

ShaulaCapabilitiesEnvironment shaula_command_capabilities_environment(void) {
  return (ShaulaCapabilitiesEnvironment){
      .compositor =
          {
              .shaula_compositor = g_getenv("SHAULA_COMPOSITOR"),
              .niri_socket = g_getenv("NIRI_SOCKET"),
              .xdg_current_desktop = g_getenv("XDG_CURRENT_DESKTOP"),
              .xdg_session_desktop = g_getenv("XDG_SESSION_DESKTOP"),
              .wayland_display = g_getenv("WAYLAND_DISPLAY"),
          },
      .capture_backend = g_getenv("SHAULA_CAPTURE_BACKEND"),
      .capture_force_portal = g_getenv("SHAULA_CAPTURE_FORCE_PORTAL"),
      .portal_available = g_getenv("SHAULA_PORTAL_AVAILABLE"),
      .portal_window_capable = g_getenv("SHAULA_PORTAL_WINDOW_CAPABLE"),
  };
}

char *shaula_command_env_span_json(ShaulaEnvSpan span) {
  if (span.data == NULL)
    return g_strdup("\"\"");
  g_autofree char *text = g_strndup(span.data, span.length);
  return shaula_command_json_string(text);
}
