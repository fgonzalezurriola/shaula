#include "capabilities/runtime.h"
#include "capture/command.h"
#include "cli/json.h"
#include "compositor/focused_output.h"
#include "compositor/runtime.h"
#include "config/config.h"
#include "config/niri_managed.h"
#include "errors/taxonomy.h"
#include "explore/inventory.h"
#include "notify/request.h"
#include "preflight/probe.h"
#include "preview/preview_result.h"
#include "runtime/process_exec.h"

#include <errno.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static ShaulaJsonSpan json_span(const char *value) {
  return (ShaulaJsonSpan){.data = (const guint8 *)value,
                          .length = value != NULL ? strlen(value) : 0};
}
static ShaulaErrorSpan error_span(const char *value) {
  return (ShaulaErrorSpan){.data = value,
                           .length = value != NULL ? strlen(value) : 0};
}

static char *json_string(const char *value) {
  ShaulaJsonOwnedBytes output = {0};
  if (shaula_json_string_escape(json_span(value != NULL ? value : ""),
                                &output) != SHAULA_JSON_STATUS_OK)
    return NULL;
  char *copy = g_strndup((const char *)output.data, output.length);
  shaula_json_owned_bytes_clear(&output);
  return copy;
}

static char *json_timestamp(void) {
  ShaulaJsonOwnedBytes output = {0};
  if (shaula_json_timestamp_from_unix_seconds((gint64)time(NULL), &output) !=
      SHAULA_JSON_STATUS_OK)
    return NULL;
  char *copy = g_strndup((const char *)output.data, output.length);
  shaula_json_owned_bytes_clear(&output);
  return copy;
}

static const char *json_bool(gboolean value) { return value ? "true" : "false"; }

static int write_error(const char *command, const char *code,
                       const char *message, const char *details_json) {
  const ShaulaErrorSpec *spec = shaula_error_taxonomy_spec_for(error_span(code));
  ShaulaJsonOwnedBytes output = {0};
  ShaulaJsonStatus status = shaula_json_basic_error_build(
      (gint64)time(NULL), json_span(command), json_span(code), json_span(message),
      spec->retryable, json_span(details_json != NULL ? details_json : "{}"),
      &output);
  if (status == SHAULA_JSON_STATUS_OK) {
    (void)fwrite(output.data, 1, output.length, stdout);
    shaula_json_owned_bytes_clear(&output);
  }
  return spec->exit_code;
}

static int write_success(const char *command, const char *result_json,
                         const char *warnings_json) {
  g_autofree char *timestamp = json_timestamp();
  if (timestamp == NULL)
    return write_error(command, "ERR_UNKNOWN_UNMAPPED",
                       "could not encode command result", "{}");
  g_print("{\"ok\":true,\"contract_version\":\"1.0.0\","
          "\"command\":\"%s\",\"timestamp\":\"%s\",\"result\":%s,"
          "\"warnings\":%s}\n",
          command, timestamp, result_json,
          warnings_json != NULL ? warnings_json : "[]");
  return 0;
}

static ShaulaCapabilitiesEnvironment capabilities_environment(void) {
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

static int command_preflight(int argc, char **argv) {
  if (argc != 3 || !g_str_equal(argv[2], "--json"))
    return write_error("preflight", "ERR_CLI_USAGE", "--json is required",
                       "{}");
  ShaulaCapabilitiesEnvironment environment = capabilities_environment();
  ShaulaPreflightOutput output = {0};
  ShaulaPreflightStatus status = shaula_preflight_build(
      &environment, (gint64)time(NULL), json_span("portal_fallback"), &output);
  if (status != SHAULA_PREFLIGHT_STATUS_OK)
    return write_error("preflight", "ERR_UNKNOWN_UNMAPPED",
                       "preflight response could not be built", "{}");
  (void)fwrite(output.json.data, 1, output.json.length, stdout);
  int exit_code = output.exit_code;
  shaula_preflight_output_clear(&output);
  return exit_code;
}

static char *env_span_json(ShaulaEnvSpan span) {
  if (span.data == NULL)
    return g_strdup("\"\"");
  g_autofree char *text = g_strndup(span.data, span.length);
  return json_string(text);
}

static int command_capabilities(int argc, char **argv) {
  int start = 2;
  if (argc > 2 && g_str_equal(argv[2], "list"))
    start = 3;
  if (argc != start + 1 || !g_str_equal(argv[start], "--json"))
    return write_error("capabilities list", "ERR_CLI_USAGE",
                       "usage: shaula capabilities [list] --json", "{}");

  ShaulaCapabilitiesEnvironment environment = capabilities_environment();
  ShaulaRuntimeDecision runtime = {0};
  if (shaula_capabilities_resolve(&environment, &runtime) !=
      SHAULA_CAPABILITIES_STATUS_OK)
    return write_error("capabilities list", "ERR_UNKNOWN_UNMAPPED",
                       "capability resolution failed", "{}");
  g_autofree char *compositor = env_span_json(runtime.compositor.label);
  if (!runtime.compositor_supported) {
    g_autofree char *details =
        g_strdup_printf("{\"detected_compositor\":%s}", compositor);
    return write_error("capabilities list", "ERR_UNSUPPORTED_COMPOSITOR",
                       "unsupported compositor for shaula v1", details);
  }

  ShaulaEnvSpan backend_span =
      shaula_capabilities_backend_label(runtime.backend);
  g_autofree char *backend_text =
      g_strndup(backend_span.data, backend_span.length);
  g_autofree char *backend = json_string(backend_text);
  GString *fallbacks = g_string_new("[");
  size_t fallback_count =
      shaula_capabilities_fallback_count(runtime.backend);
  for (size_t i = 0; i < fallback_count; i++) {
    ShaulaEnvSpan fallback_span = shaula_capabilities_backend_label(
        shaula_capabilities_fallback_at(runtime.backend, i));
    g_autofree char *fallback_text =
        g_strndup(fallback_span.data, fallback_span.length);
    g_autofree char *fallback = json_string(fallback_text);
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

  g_autofree char *timestamp = json_timestamp();
  g_print(
      "{\"ok\":true,\"contract_version\":\"1.0.0\",\"command\":"
      "\"capabilities list\",\"timestamp\":\"%s\",\"capture\":{"
      "\"area\":%s,\"fullscreen\":%s,\"all_screens\":%s,\"window\":%s},"
      "\"backend\":%s,\"fallbacks\":%s,\"portal_window_capable\":%s,"
      "\"result\":{\"capture\":{\"area\":%s,\"fullscreen\":%s,"
      "\"all_screens\":%s,\"window\":%s},\"backend\":%s,"
      "\"fallbacks\":%s,\"compositor\":%s,\"ipc_version\":\"1.0.0\","
      "\"portal_available\":%s,\"portal_window_capable\":%s,"
      "\"overlay_supported\":%s},\"warnings\":%s}\n",
      timestamp, json_bool(runtime.capture.area),
      json_bool(runtime.capture.fullscreen),
      json_bool(runtime.capture.all_screens), json_bool(runtime.capture.window),
      backend, fallbacks->str, json_bool(runtime.portal_window_capable),
      json_bool(runtime.capture.area), json_bool(runtime.capture.fullscreen),
      json_bool(runtime.capture.all_screens), json_bool(runtime.capture.window),
      backend, fallbacks->str, compositor, json_bool(runtime.portal_available),
      json_bool(runtime.portal_window_capable),
      json_bool(runtime.overlay_supported), warnings->str);
  g_string_free(fallbacks, TRUE);
  g_string_free(warnings, TRUE);
  return 0;
}

static int command_errors(int argc, char **argv) {
  if (argc != 4 || !g_str_equal(argv[2], "list") ||
      !g_str_equal(argv[3], "--json"))
    return write_error("errors list", "ERR_CLI_USAGE",
                       "usage: shaula errors list --json", "{}");
  g_autofree char *timestamp = json_timestamp();
  GString *output = g_string_new(NULL);
  g_string_append_printf(
      output,
      "{\"ok\":true,\"contract_version\":\"1.0.0\",\"command\":"
      "\"errors list\",\"timestamp\":\"%s\",\"result\":{\"errors\":[",
      timestamp);
  for (size_t i = 0; i < shaula_error_taxonomy_count(); i++) {
    const ShaulaErrorSpec *spec = shaula_error_taxonomy_at(i);
    ShaulaErrorSpan class_token =
        shaula_failure_class_token(spec->failure_class);
    ShaulaErrorSpan action_token = shaula_recovery_action_token(spec->action);
    g_autofree char *code = g_strndup(spec->code.data, spec->code.length);
    g_autofree char *message =
        g_strndup(spec->message.data, spec->message.length);
    g_autofree char *class_text =
        g_strndup(class_token.data, class_token.length);
    g_autofree char *action =
        g_strndup(action_token.data, action_token.length);
    g_autofree char *code_json = json_string(code);
    g_autofree char *message_json = json_string(message);
    g_autofree char *class_json = json_string(class_text);
    g_autofree char *action_json = json_string(action);
    if (i > 0)
      g_string_append_c(output, ',');
    g_string_append_printf(
        output,
        "{\"code\":%s,\"message\":%s,\"retryable\":%s,\"class\":%s,"
        "\"action\":%s,\"exit_code\":%u}",
        code_json, message_json, json_bool(spec->retryable), class_json,
        action_json, spec->exit_code);
  }
  g_string_append(output, "]},\"warnings\":[]}\n");
  g_print("%s", output->str);
  g_string_free(output, TRUE);
  return 0;
}

static char *executable_path(void) {
  return g_file_read_link("/proc/self/exe", NULL);
}

static char *resolve_helper(const char *override_name, const char *binary_name) {
  const char *override = g_getenv(override_name);
  if (override != NULL && override[0] != '\0')
    return g_strdup(override);
  g_autofree char *self = executable_path();
  if (self != NULL) {
    g_autofree char *directory = g_path_get_dirname(self);
    char *sibling = g_build_filename(directory, binary_name, NULL);
    if (g_file_test(sibling, G_FILE_TEST_EXISTS))
      return sibling;
    g_free(sibling);
  }
  return g_strdup(binary_name);
}

static gboolean run_sync(char **argv, char **envp, char **stdout_text,
                         char **stderr_text, int *exit_code) {
  gint wait_status = 0;
  g_autoptr(GError) error = NULL;
  gboolean spawned =
      g_spawn_sync(NULL, argv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL, stdout_text,
                   stderr_text, &wait_status, &error);
  if (!spawned) {
    if (stderr_text != NULL) {
      g_free(*stderr_text);
      *stderr_text = g_strdup(error != NULL ? error->message : "spawn failed");
    }
    if (exit_code != NULL)
      *exit_code = 127;
    return FALSE;
  }
  if (exit_code != NULL) {
    if (WIFEXITED(wait_status))
      *exit_code = WEXITSTATUS(wait_status);
    else if (WIFSIGNALED(wait_status))
      *exit_code = 128 + WTERMSIG(wait_status);
    else
      *exit_code = 1;
  }
  return TRUE;
}

static int command_settings(int argc, char **argv) {
  if (argc == 3 && g_str_equal(argv[2], "--json")) {
    return write_success(
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
    return write_error("settings", "ERR_CLI_USAGE",
                       "usage: shaula settings [--json]", "{}");
  g_autofree char *helper =
      resolve_helper("SHAULA_SETTINGS_HELPER_BIN", "shaula-settings");
  char *helper_argv[] = {helper, NULL};
  int exit_code = 0;
  if (!run_sync(helper_argv, NULL, NULL, NULL, &exit_code) || exit_code != 0)
    return write_error("settings", "ERR_SETTINGS_UNAVAILABLE",
                       "settings helper is unavailable", "{}");
  return 0;
}

static char *config_json(const ShaulaConfig *config) {
  g_autofree char *save_folder = json_string(config->save_folder);
  g_autofree char *floating_x =
      config->floating_x_set ? g_strdup_printf("%d", config->floating_x)
                             : g_strdup("null");
  g_autofree char *floating_y =
      config->floating_y_set ? g_strdup_printf("%d", config->floating_y)
                             : g_strdup("null");
  return g_strdup_printf(
      "{\"capture\":{\"region_capture_mode\":\"%s\",\"after\":{"
      "\"save_folder\":%s,\"quick\":{\"skip_preview\":%s,"
      "\"copy_to_clipboard\":%s,\"save_to_folder\":%s},\"area\":{"
      "\"skip_preview\":%s,\"copy_to_clipboard\":%s,\"save_to_folder\":"
      "%s},\"fullscreen\":{\"skip_preview\":%s,\"copy_to_clipboard\":%s,"
      "\"save_to_folder\":%s},\"all_screens\":{\"skip_preview\":%s,"
      "\"copy_to_clipboard\":%s,\"save_to_folder\":%s}}},"
      "\"notifications\":{\"success\":%s,\"errors\":%s,\"thumbnails\":"
      "%s},\"preview\":{\"window\":{\"app_id\":\"dev.shaula.preview\","
      "\"title\":\"Shaula Preview\",\"mode\":\"%s\",\"focused\":%s,"
      "\"close_preview_on_save\":%s,\"width\":%u,\"height\":%u,"
      "\"default_column_display\":\"%s\",\"floating_position\":{\"x\":%s,"
      "\"y\":%s,\"relative_to\":\"%s\"}}}}",
      config->region_capture_mode, save_folder,
      json_bool(config->quick.skip_preview),
      json_bool(config->quick.copy_to_clipboard),
      json_bool(config->quick.save_to_folder),
      json_bool(config->area.skip_preview),
      json_bool(config->area.copy_to_clipboard),
      json_bool(config->area.save_to_folder),
      json_bool(config->fullscreen.skip_preview),
      json_bool(config->fullscreen.copy_to_clipboard),
      json_bool(config->fullscreen.save_to_folder),
      json_bool(config->all_screens.skip_preview),
      json_bool(config->all_screens.copy_to_clipboard),
      json_bool(config->all_screens.save_to_folder),
      json_bool(config->notifications_success),
      json_bool(config->notifications_errors),
      json_bool(config->notifications_thumbnails), config->preview_mode,
      json_bool(config->preview_focused),
      json_bool(config->close_preview_on_save), config->preview_width,
      config->preview_height, config->column_display,
      floating_x, floating_y,
      config->floating_relative_to);
}

static gboolean parse_bool_arg(const char *value, gboolean *out) {
  if (g_str_equal(value, "true")) {
    *out = TRUE;
    return TRUE;
  }
  if (g_str_equal(value, "false")) {
    *out = FALSE;
    return TRUE;
  }
  return FALSE;
}

static gboolean parse_uint_arg(const char *value, guint32 *out) {
  char *end = NULL;
  errno = 0;
  unsigned long parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed == 0 ||
      parsed > G_MAXUINT32)
    return FALSE;
  *out = (guint32)parsed;
  return TRUE;
}

static ShaulaAfterModeConfig *config_mode_for_flag(ShaulaConfig *config,
                                                    const char *flag) {
  if (strstr(flag, "--after-quick-") == flag)
    return &config->quick;
  if (strstr(flag, "--after-area-") == flag)
    return &config->area;
  if (strstr(flag, "--after-fullscreen-") == flag)
    return &config->fullscreen;
  if (strstr(flag, "--after-all-screens-") == flag)
    return &config->all_screens;
  return NULL;
}

static gboolean apply_config_flag(ShaulaConfig *config, const char *flag,
                                  const char *value) {
  if (g_str_equal(flag, "--region-mode")) {
    if (!g_str_equal(value, "live") && !g_str_equal(value, "frozen"))
      return FALSE;
    g_strlcpy(config->region_capture_mode, value,
              sizeof(config->region_capture_mode));
    return TRUE;
  }
  if (g_str_equal(flag, "--preview-mode")) {
    if (!g_str_equal(value, "auto") && !g_str_equal(value, "tiling") &&
        !g_str_equal(value, "floating") &&
        !g_str_equal(value, "maximized") &&
        !g_str_equal(value, "maximized-to-edges") &&
        !g_str_equal(value, "fullscreen"))
      return FALSE;
    g_strlcpy(config->preview_mode, value, sizeof(config->preview_mode));
    return TRUE;
  }
  if (g_str_equal(flag, "--focused"))
    return parse_bool_arg(value, &config->preview_focused);
  if (g_str_equal(flag, "--close-preview-on-save"))
    return parse_bool_arg(value, &config->close_preview_on_save);
  if (g_str_equal(flag, "--width"))
    return parse_uint_arg(value, &config->preview_width);
  if (g_str_equal(flag, "--height"))
    return parse_uint_arg(value, &config->preview_height);
  if (g_str_equal(flag, "--default-column-display")) {
    if (!g_str_equal(value, "normal") && !g_str_equal(value, "tabbed"))
      return FALSE;
    g_strlcpy(config->column_display, value, sizeof(config->column_display));
    return TRUE;
  }
  if (g_str_equal(flag, "--floating-x") ||
      g_str_equal(flag, "--floating-y")) {
    gboolean *is_set = g_str_equal(flag, "--floating-x")
                           ? &config->floating_x_set
                           : &config->floating_y_set;
    gint32 *target = g_str_equal(flag, "--floating-x") ? &config->floating_x
                                                        : &config->floating_y;
    if (g_str_equal(value, "null")) {
      *is_set = FALSE;
      return TRUE;
    }
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < G_MININT32 ||
        parsed > G_MAXINT32)
      return FALSE;
    *target = (gint32)parsed;
    *is_set = TRUE;
    return TRUE;
  }
  if (g_str_equal(flag, "--floating-relative-to")) {
    g_strlcpy(config->floating_relative_to, value,
              sizeof(config->floating_relative_to));
    return TRUE;
  }
  if (g_str_equal(flag, "--save-folder")) {
    g_strlcpy(config->save_folder, value, sizeof(config->save_folder));
    return TRUE;
  }
  ShaulaAfterModeConfig *mode = config_mode_for_flag(config, flag);
  if (mode != NULL) {
    gboolean parsed = FALSE;
    if (!parse_bool_arg(value, &parsed))
      return FALSE;
    if (g_str_has_suffix(flag, "-skip-preview"))
      mode->skip_preview = parsed;
    else if (g_str_has_suffix(flag, "-copy"))
      mode->copy_to_clipboard = parsed;
    else if (g_str_has_suffix(flag, "-save"))
      mode->save_to_folder = parsed;
    else
      return FALSE;
    return TRUE;
  }
  if (g_str_equal(flag, "--notifications-success"))
    return parse_bool_arg(value, &config->notifications_success);
  if (g_str_equal(flag, "--notifications-errors"))
    return parse_bool_arg(value, &config->notifications_errors);
  if (g_str_equal(flag, "--notifications-thumbnails"))
    return parse_bool_arg(value, &config->notifications_thumbnails);
  return FALSE;
}

static char *preview_rule(const ShaulaConfig *config) {
  GString *rule = g_string_new(
      "window-rule {\n    match app-id=\"^dev\\\\.shaula\\\\.preview$\"\n");
  if (g_str_equal(config->preview_mode, "floating"))
    g_string_append(rule, "    open-floating true\n");
  else if (g_str_equal(config->preview_mode, "tiling"))
    g_string_append(rule, "    open-floating false\n");
  else if (g_str_equal(config->preview_mode, "maximized"))
    g_string_append(rule,
                    "    open-floating false\n    open-maximized true\n");
  else if (g_str_equal(config->preview_mode, "maximized-to-edges"))
    g_string_append(
        rule,
        "    open-floating false\n    open-maximized-to-edges true\n");
  else if (g_str_equal(config->preview_mode, "fullscreen"))
    g_string_append(rule,
                    "    open-floating false\n    open-fullscreen true\n");
  g_string_append_printf(
      rule,
      "    open-focused %s\n    default-column-width { fixed %u; }\n"
      "    default-window-height { fixed %u; }\n"
      "    default-column-display \"%s\"\n",
      json_bool(config->preview_focused), config->preview_width,
      config->preview_height, config->column_display);
  if (g_str_equal(config->preview_mode, "floating") &&
      config->floating_x_set && config->floating_y_set)
    g_string_append_printf(
        rule,
        "    default-floating-position x=%d y=%d relative-to=\"%s\"\n",
        config->floating_x, config->floating_y,
        config->floating_relative_to);
  g_string_append(rule, "}\n");
  return g_string_free(rule, FALSE);
}

static char *render_keybinds(void) {
  g_autofree char *binary = executable_path();
  const char *path = binary != NULL ? binary : "shaula";
  return g_strdup_printf(
      "binds {\n"
      "    CTRL+Shift+1 repeat=false hotkey-overlay-title=\"Shaula: Quick Capture\" {\n        spawn \"%s\" \"capture\" \"quick\" \"--json\";\n    }\n\n"
      "    CTRL+Shift+2 repeat=false hotkey-overlay-title=\"Shaula: Capture Area\" {\n        spawn \"%s\" \"capture\" \"area\" \"--json\";\n    }\n\n"
      "    CTRL+Shift+3 repeat=false hotkey-overlay-title=\"Shaula: Capture Fullscreen\" {\n        spawn \"%s\" \"capture\" \"fullscreen\" \"--json\" \"--save\";\n    }\n\n"
      "    CTRL+Shift+4 repeat=false hotkey-overlay-title=\"Shaula: Capture All Screens\" {\n        spawn \"%s\" \"capture\" \"all-screens\" \"--json\" \"--save\";\n    }\n"
      "}\n",
      path, path, path, path);
}

static char *conflicts_json(const GPtrArray *conflicts) {
  GString *json = g_string_new("[");
  for (guint i = 0; i < conflicts->len; i++) {
    g_autofree char *context =
        json_string(g_ptr_array_index((GPtrArray *)conflicts, i));
    if (i > 0)
      g_string_append_c(json, ',');
    g_string_append_printf(json,
                           "{\"key\":\"CTRL+Shift\",\"action\":"
                           "\"existing binding\",\"context\":%s}",
                           context);
  }
  g_string_append_c(json, ']');
  return g_string_free(json, FALSE);
}

static int write_managed_result(const char *command,
                                const ManagedBlockResult *result,
                                gboolean dry_run) {
  g_autofree char *path = json_string(result->path);
  g_autofree char *backup = result->backup_path != NULL
                                ? json_string(result->backup_path)
                                : g_strdup("null");
  g_autofree char *body = g_strdup_printf(
      "{\"path\":%s,\"backup_path\":%s,\"installed\":%s,"
      "\"replaced\":%s,\"changed\":%s,\"dry_run\":%s}",
      path, backup, json_bool(result->installed), json_bool(result->replaced),
      json_bool(result->changed), json_bool(dry_run));
  return write_success(command, body, "[]");
}

static int command_config(int argc, char **argv) {
  if (argc < 4)
    return write_error("config", "ERR_CLI_USAGE",
                       "usage: shaula config <show|init|save|niri-window-rule|"
                       "niri-install|niri-keybinds|niri-keybinds-install> --json",
                       "{}");
  const char *subcommand = argv[2];
  g_autofree char *command = g_strdup_printf("config %s", subcommand);
  gboolean json_mode = FALSE;
  gboolean dry_run = FALSE;
  gboolean force = FALSE;
  gboolean apply_niri = FALSE;
  const char *path_override = NULL;
  g_autofree char *path = shaula_config_path_new();
  ShaulaConfig config;
  gboolean loaded = FALSE;
  ShaulaConfigStatus load_status = shaula_config_load(path, &config, &loaded);
  if (load_status == SHAULA_CONFIG_STATUS_INVALID)
    return write_error(command, "ERR_CONFIG_INVALID",
                       "invalid configuration file", "{}");
  if (load_status == SHAULA_CONFIG_STATUS_UNREADABLE)
    return write_error(command, "ERR_CONFIG_UNREADABLE",
                       "configuration file is unreadable", "{}");

  gboolean setting_seen = FALSE;
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--json")) {
      json_mode = TRUE;
      continue;
    }
    if (g_str_equal(argv[i], "--dry-run")) {
      dry_run = TRUE;
      continue;
    }
    if (g_str_equal(argv[i], "--force")) {
      force = TRUE;
      continue;
    }
    if (g_str_equal(argv[i], "--apply-niri")) {
      apply_niri = TRUE;
      continue;
    }
    if (g_str_equal(argv[i], "--path") && i + 1 < argc) {
      path_override = argv[++i];
      continue;
    }
    if (i + 1 < argc && apply_config_flag(&config, argv[i], argv[i + 1])) {
      setting_seen = TRUE;
      i++;
      continue;
    }
    return write_error(command, "ERR_CLI_USAGE", "unsupported flag", "{}");
  }
  if (!json_mode)
    return write_error(command, "ERR_CLI_USAGE", "--json is required", "{}");

  if (g_str_equal(subcommand, "show")) {
    g_autofree char *path_json = json_string(path != NULL ? path : "");
    g_autofree char *body = config_json(&config);
    g_autofree char *result = g_strdup_printf(
        "{\"path\":%s,\"loaded\":%s,\"config\":%s}", path_json,
        json_bool(loaded), body);
    return write_success(command, result, "[]");
  }
  if (g_str_equal(subcommand, "init")) {
    if (path == NULL)
      return write_error(command, "ERR_CONFIG_UNREADABLE",
                         "configuration path could not be resolved", "{}");
    gboolean changed = force || !g_file_test(path, G_FILE_TEST_EXISTS);
    if (changed && !dry_run &&
        shaula_config_save(path, &config) != SHAULA_CONFIG_STATUS_OK)
      return write_error(command, "ERR_CONFIG_UNREADABLE",
                         "configuration file could not be created", "{}");
    g_autofree char *path_json = json_string(path);
    g_autofree char *result = g_strdup_printf(
        "{\"path\":%s,\"created\":%s,\"changed\":%s,\"dry_run\":%s}",
        path_json, json_bool(changed), json_bool(changed), json_bool(dry_run));
    return write_success(command, result, "[]");
  }
  if (g_str_equal(subcommand, "save")) {
    if (!setting_seen)
      return write_error(command, "ERR_CLI_USAGE",
                         "config save requires at least one setting flag", "{}");
    if (!shaula_config_validate(&config))
      return write_error(command, "ERR_CLI_USAGE",
                         "skip preview requires copy or save",
                         "{\"field\":\"capture.after\"}");
    if (!dry_run &&
        shaula_config_save(path, &config) != SHAULA_CONFIG_STATUS_OK)
      return write_error(command, "ERR_CONFIG_UNREADABLE",
                         "configuration file is unreadable", "{}");
    ManagedBlockResult niri_result = {0};
    gboolean niri_invalid = FALSE;
    if (apply_niri) {
      g_autofree char *rule = preview_rule(&config);
      if (!install_managed_block(
              path_override, "// BEGIN SHAULA PREVIEW WINDOW RULE",
              "// END SHAULA PREVIEW WINDOW RULE", rule, dry_run,
              &niri_result, &niri_invalid)) {
        managed_block_result_clear(&niri_result);
        return write_error(command,
                           niri_invalid ? "ERR_CONFIG_INVALID"
                                        : "ERR_CONFIG_UNREADABLE",
                           niri_invalid
                               ? "invalid Niri configuration managed block"
                               : "Niri configuration path could not be resolved",
                           "{}");
      }
    }
    g_autofree char *path_json = json_string(path != NULL ? path : "");
    g_autofree char *result = g_strdup_printf(
        "{\"path\":%s,\"saved\":true,\"changed\":true,\"dry_run\":%s,"
        "\"niri_applied\":%s}",
        path_json, json_bool(dry_run), json_bool(apply_niri));
    int status = write_success(command, result, "[]");
    managed_block_result_clear(&niri_result);
    return status;
  }
  if (g_str_equal(subcommand, "niri-window-rule")) {
    g_autofree char *rule = preview_rule(&config);
    g_autofree char *rule_json = json_string(rule);
    g_autofree char *path_json = json_string(path != NULL ? path : "");
    g_autofree char *result = g_strdup_printf(
        "{\"path\":%s,\"loaded\":%s,\"target\":\"preview\","
        "\"app_id\":\"dev.shaula.preview\",\"title\":\"Shaula Preview\","
        "\"kdl\":%s}",
        path_json, json_bool(loaded), rule_json);
    return write_success(command, result, "[]");
  }
  if (g_str_equal(subcommand, "niri-keybinds")) {
    g_autofree char *keybinds = render_keybinds();
    g_autofree char *kdl = json_string(keybinds);
    g_autofree char *result = g_strdup_printf("{\"kdl\":%s}", kdl);
    return write_success(command, result, "[]");
  }
  if (g_str_equal(subcommand, "niri-keybinds-status")) {
    g_autofree char *niri_path = niri_config_path(path_override);
    gboolean detected =
        niri_path != NULL && g_file_test(niri_path, G_FILE_TEST_IS_REGULAR);
    g_autofree char *contents = NULL;
    if (detected)
      (void)g_file_get_contents(niri_path, &contents, NULL, NULL);
    gboolean installed =
        contents != NULL &&
        strstr(contents, "// BEGIN SHAULA MANAGED KEYBINDS") != NULL &&
        strstr(contents, "// END SHAULA MANAGED KEYBINDS") != NULL;
    g_autoptr(GPtrArray) conflicts = niri_keybind_conflicts(niri_path);
    g_autofree char *conflict_list = conflicts_json(conflicts);
    g_autofree char *path_json =
        detected ? json_string(niri_path) : g_strdup("null");
    g_autofree char *result = g_strdup_printf(
        "{\"niri_detected\":%s,\"config_path\":%s,\"installed\":%s,"
        "\"conflicts\":%s}",
        json_bool(detected), path_json, json_bool(installed), conflict_list);
    return write_success(command, result, "[]");
  }
  if (g_str_equal(subcommand, "niri-keybinds-remove")) {
    ManagedBlockResult remove_result = {0};
    gboolean invalid = FALSE;
    if (!remove_managed_keybinds(path_override, dry_run, &remove_result,
                                 &invalid)) {
      managed_block_result_clear(&remove_result);
      return write_error(command,
                         invalid ? "ERR_CONFIG_INVALID"
                                 : "ERR_CONFIG_UNREADABLE",
                         invalid ? "invalid Niri configuration managed block"
                                 : "Niri configuration path could not be resolved",
                         "{}");
    }
    g_autofree char *path_json = json_string(remove_result.path);
    g_autofree char *backup_json = remove_result.backup_path != NULL
                                       ? json_string(remove_result.backup_path)
                                       : g_strdup("null");
    g_autofree char *result = g_strdup_printf(
        "{\"path\":%s,\"backup_path\":%s,\"removed\":%s,"
        "\"changed\":%s,\"dry_run\":%s}",
        path_json, backup_json, json_bool(remove_result.changed),
        json_bool(remove_result.changed), json_bool(dry_run));
    int status = write_success(command, result, "[]");
    managed_block_result_clear(&remove_result);
    return status;
  }
  if (g_str_equal(subcommand, "niri-install") ||
      g_str_equal(subcommand, "niri-keybinds-install")) {
    g_autofree char *payload = g_str_equal(subcommand, "niri-install")
                                   ? preview_rule(&config)
                                   : render_keybinds();
    const char *begin = g_str_equal(subcommand, "niri-install")
                            ? "// BEGIN SHAULA PREVIEW WINDOW RULE"
                            : "// BEGIN SHAULA MANAGED KEYBINDS";
    const char *end = g_str_equal(subcommand, "niri-install")
                          ? "// END SHAULA PREVIEW WINDOW RULE"
                          : "// END SHAULA MANAGED KEYBINDS";
    if (g_str_equal(subcommand, "niri-keybinds-install") && !force) {
      g_autofree char *niri_path = niri_config_path(path_override);
      g_autoptr(GPtrArray) conflicts = niri_keybind_conflicts(niri_path);
      if (conflicts->len > 0) {
        g_autofree char *list = conflicts_json(conflicts);
        g_autofree char *details =
            g_strdup_printf("{\"conflicts\":%s}", list);
        return write_error(
            command, "ERR_NIRI_KEYBIND_CONFLICT",
            "existing keybind conflicts detected; use --force to overwrite",
            details);
      }
    }
    ManagedBlockResult install_result = {0};
    gboolean invalid = FALSE;
    if (!install_managed_block(path_override, begin, end, payload, dry_run,
                               &install_result, &invalid)) {
      managed_block_result_clear(&install_result);
      return write_error(command,
                         invalid ? "ERR_CONFIG_INVALID"
                                 : "ERR_CONFIG_UNREADABLE",
                         invalid ? "invalid Niri configuration managed block"
                                 : "Niri configuration path could not be resolved",
                         "{}");
    }
    int status = write_managed_result(command, &install_result, dry_run);
    managed_block_result_clear(&install_result);
    return status;
  }
  return write_error("config", "ERR_CLI_USAGE",
                     "unsupported config subcommand", "{}");
}

static int command_preview(int argc, char **argv) {
  if (argc != 4 || !g_str_equal(argv[3], "--json"))
    return write_error("preview", "ERR_CLI_USAGE",
                       "usage: shaula preview <file> --json", "{}");
  const char *path = argv[2];
  if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
    return write_error("preview", "ERR_PREVIEW_INPUT_INVALID",
                       "preview input image is not readable", "{}");
  g_autofree char *helper =
      resolve_helper("SHAULA_PREVIEW_HELPER_BIN", "shaula-preview");
  g_autofree char *self = executable_path();
  g_auto(GStrv) envp = g_get_environ();
  envp = g_environ_setenv(envp, "SHAULA_BIN", self != NULL ? self : "shaula",
                          TRUE);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_COPY_ON_ACCEPT", "0", TRUE);
  ShaulaConfig config;
  gboolean loaded = FALSE;
  g_autofree char *config_path = shaula_config_path_new();
  (void)shaula_config_load(config_path, &config, &loaded);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_CLOSE_ON_SAVE",
                          config.close_preview_on_save ? "1" : "0", TRUE);
  g_autofree char *width = g_strdup_printf("%u", config.preview_width);
  g_autofree char *height = g_strdup_printf("%u", config.preview_height);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_WINDOW_WIDTH", width, TRUE);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_WINDOW_HEIGHT", height, TRUE);
  envp = g_environ_setenv(envp, "SHAULA_SAVE_FOLDER", config.save_folder, TRUE);
  char *helper_argv[] = {helper, (char *)path, NULL};
  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  int exit_code = 0;
  if (!run_sync(helper_argv, envp, &stdout_text, &stderr_text, &exit_code) ||
      exit_code != 0) {
    if (exit_code == 43)
      return write_error("preview", "ERR_PREVIEW_INPUT_INVALID",
                         "preview input image is invalid", "{}");
    return write_error("preview", "ERR_PREVIEW_UNAVAILABLE",
                       "preview helper is unavailable", "{}");
  }
  ShaulaPreviewResult parsed;
  shaula_preview_result_init(&parsed);
  ShaulaPreviewResultStatus status = shaula_preview_result_parse(
      (ShaulaPreviewResultSpan){.data = (const guint8 *)stdout_text,
                                .length = stdout_text != NULL
                                              ? strlen(stdout_text)
                                              : 0},
      &parsed);
  if (status != SHAULA_PREVIEW_RESULT_STATUS_OK) {
    shaula_preview_result_clear(&parsed);
    return write_error("preview", "ERR_PREVIEW_RESULT_INVALID",
                       "preview helper did not emit valid result JSON", "{}");
  }
  ShaulaPreviewResultSpan action_span =
      shaula_preview_action_token(parsed.action);
  g_autofree char *action_text =
      g_strndup((const char *)action_span.data, action_span.length);
  g_autofree char *path_json = json_string(path);
  g_autofree char *action_json = json_string(action_text);
  g_autofree char *saved_path = parsed.saved_path.data != NULL
                                    ? g_strndup((const char *)parsed.saved_path.data,
                                               parsed.saved_path.length)
                                    : NULL;
  g_autofree char *saved_path_json =
      saved_path != NULL ? json_string(saved_path) : g_strdup("null");
  g_autofree char *result = g_strdup_printf(
      "{\"path\":%s,\"closed\":%s,\"action\":%s,\"copied\":%s,"
      "\"saved\":%s,\"saved_path\":%s}",
      path_json, json_bool(parsed.closed), action_json, json_bool(parsed.copied),
      json_bool(parsed.saved), saved_path_json);
  shaula_preview_result_clear(&parsed);
  return write_success("preview", result, "[]");
}

static char *saved_directory(const ShaulaConfig *config) {
  const char *home = g_getenv("HOME");
  if (home == NULL || home[0] == '\0')
    return NULL;
  if (g_str_equal(config->save_folder, "~"))
    return g_strdup(home);
  if (g_str_has_prefix(config->save_folder, "~/"))
    return g_build_filename(home, config->save_folder + 2, NULL);
  if (g_path_is_absolute(config->save_folder))
    return g_strdup(config->save_folder);
  return NULL;
}

static int command_directory(int argc, char **argv) {
  if (argc < 3 || !g_str_equal(argv[2], "screenshots"))
    return write_error("directory", "ERR_CLI_USAGE",
                       "usage: shaula directory screenshots [--open] [--json]",
                       "{}");
  gboolean open = FALSE;
  gboolean json = FALSE;
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--open"))
      open = TRUE;
    else if (g_str_equal(argv[i], "--json"))
      json = TRUE;
    else
      return write_error("directory screenshots", "ERR_CLI_USAGE",
                         "unsupported flag", "{}");
  }
  ShaulaConfig config;
  g_autofree char *config_path = shaula_config_path_new();
  (void)shaula_config_load(config_path, &config, NULL);
  g_autofree char *directory = saved_directory(&config);
  if (directory == NULL || g_mkdir_with_parents(directory, 0755) != 0)
    return write_error("directory screenshots", "ERR_OUTPUT_PATH_INVALID",
                       "screenshot directory is not writable", "{}");
  if (open) {
    char *open_argv[] = {"xdg-open", directory, NULL};
    int exit_code = 0;
    if (!run_sync(open_argv, NULL, NULL, NULL, &exit_code) || exit_code != 0)
      return write_error("directory screenshots", "ERR_OUTPUT_PATH_INVALID",
                         "could not open screenshot directory", "{}");
  }
  if (!json)
    return 0;
  g_autofree char *path_json = json_string(directory);
  g_autofree char *result =
      g_strdup_printf("{\"target\":\"screenshots\",\"path\":%s,"
                      "\"opened\":%s}",
                      path_json, json_bool(open));
  return write_success("directory screenshots", result, "[]");
}

typedef struct {
  char *path;
  char *mime;
  guint width;
  guint height;
  char *backend;
  char *timestamp;
} HistoryEntry;

static void history_entry_free(gpointer data) {
  HistoryEntry *entry = data;
  if (entry == NULL)
    return;
  g_free(entry->path);
  g_free(entry->mime);
  g_free(entry->backend);
  g_free(entry->timestamp);
  g_free(entry);
}

static GPtrArray *history_entries_load(void) {
  GPtrArray *entries = g_ptr_array_new_with_free_func(history_entry_free);
  g_autofree char *contents = NULL;
  if (!g_file_get_contents("/tmp/shaula/history/latest.v1", &contents, NULL,
                           NULL))
    return entries;

  g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
  for (gsize i = 0; lines[i] != NULL && entries->len < 20; i++) {
    if (lines[i][0] == '\0')
      continue;
    g_auto(GStrv) fields = g_strsplit(lines[i], "|", 6);
    if (g_strv_length(fields) != 6)
      continue;
    char *width_end = NULL;
    char *height_end = NULL;
    guint64 width = g_ascii_strtoull(fields[2], &width_end, 10);
    guint64 height = g_ascii_strtoull(fields[3], &height_end, 10);
    if (width_end == fields[2] || *width_end != '\0' ||
        height_end == fields[3] || *height_end != '\0' || width > G_MAXUINT ||
        height > G_MAXUINT)
      continue;
    HistoryEntry *entry = g_new0(HistoryEntry, 1);
    entry->path = g_strdup(fields[0]);
    entry->mime = g_strdup(fields[1]);
    entry->width = (guint)width;
    entry->height = (guint)height;
    entry->backend = g_strdup(fields[4]);
    entry->timestamp = g_strdup(fields[5]);
    g_ptr_array_add(entries, entry);
  }
  return entries;
}

static void history_entry_append_json(GString *output,
                                      const HistoryEntry *entry) {
  g_autofree char *path = json_string(entry->path);
  g_autofree char *mime = json_string(entry->mime);
  g_autofree char *backend = json_string(entry->backend);
  g_autofree char *timestamp = json_string(entry->timestamp);
  g_string_append_printf(
      output,
      "{\"path\":%s,\"mime\":%s,\"dimensions\":{\"width\":%u,"
      "\"height\":%u},\"backend_used\":%s,\"timestamp\":%s}",
      path, mime, entry->width, entry->height, backend, timestamp);
}

static int command_history(int argc, char **argv) {
  if (argc < 4)
    return write_error("history", "ERR_CLI_USAGE",
                       "usage: shaula history <list|show> --json", "{}");
  const char *subcommand = argv[2];
  if (!g_str_equal(subcommand, "list") && !g_str_equal(subcommand, "show"))
    return write_error("history", "ERR_CLI_USAGE",
                       "unsupported history subcommand", "{}");

  gboolean json = FALSE;
  const char *id = NULL;
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--json")) {
      json = TRUE;
      continue;
    }
    if (g_str_equal(subcommand, "show") && g_str_equal(argv[i], "--id")) {
      if (i + 1 >= argc)
        return write_error("history show", "ERR_CLI_USAGE",
                           "--id requires an entry id", "{}");
      id = argv[++i];
      continue;
    }
    return write_error(g_str_equal(subcommand, "show") ? "history show"
                                                        : "history list",
                       "ERR_CLI_USAGE", "unsupported flag", "{}");
  }
  if (!json)
    return write_error(g_str_equal(subcommand, "show") ? "history show"
                                                        : "history list",
                       "ERR_CLI_USAGE", "--json is required", "{}");

  g_autoptr(GPtrArray) entries = history_entries_load();
  if (g_str_equal(subcommand, "show")) {
    if (id == NULL)
      return write_error("history show", "ERR_CLI_USAGE", "--id is required",
                         "{}");
    if (!g_str_equal(id, "latest") || entries->len == 0)
      return write_error("history show", "ERR_HISTORY_ENTRY_NOT_FOUND",
                         "history entry was not found", "{}");
    GString *result = g_string_new("{\"id\":\"latest\",\"entry\":");
    history_entry_append_json(result, g_ptr_array_index(entries, 0));
    g_string_append_c(result, '}');
    int status = write_success("history show", result->str, "[]");
    g_string_free(result, TRUE);
    return status;
  }

  GString *result = g_string_new("{\"entries\":[");
  for (guint i = 0; i < entries->len; i++) {
    if (i > 0)
      g_string_append_c(result, ',');
    history_entry_append_json(result, g_ptr_array_index(entries, i));
  }
  g_string_append(result, "]}");
  int status = write_success("history list", result->str, "[]");
  g_string_free(result, TRUE);
  return status;
}

static gboolean clipboard_available(void) {
  const char *value = g_getenv("SHAULA_CLIPBOARD_AVAILABLE");
  if (value == NULL)
    return TRUE;
  return g_ascii_strcasecmp(value, "0") != 0 &&
         g_ascii_strcasecmp(value, "false") != 0;
}

static int command_clipboard(int argc, char **argv) {
  static const char *clipboard_dir = "/tmp/shaula/clipboard";
  static const char *clipboard_state =
      "/tmp/shaula/clipboard/current-image.path";

  if (argc < 4)
    return write_error("clipboard", "ERR_CLI_USAGE",
                       "usage: shaula clipboard <copy-image|import-image> --json",
                       "{}");
  const char *subcommand = argv[2];
  gboolean json = FALSE;
  const char *input = NULL;
  const char *output = NULL;
  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--json"))
      json = TRUE;
    else if (g_str_equal(argv[i], "--input") && i + 1 < argc)
      input = argv[++i];
    else if (g_str_equal(argv[i], "--output") && i + 1 < argc)
      output = argv[++i];
    else
      return write_error("clipboard", "ERR_CLI_USAGE", "unsupported flag",
                         "{}");
  }
  if (!json)
    return write_error("clipboard", "ERR_CLI_USAGE", "--json is required",
                       "{}");
  if (!g_str_equal(subcommand, "copy-image") &&
      !g_str_equal(subcommand, "import-image"))
    return write_error("clipboard", "ERR_CLI_USAGE",
                       "unsupported clipboard subcommand", "{}");
  if (!clipboard_available())
    return write_error(g_str_equal(subcommand, "copy-image")
                           ? "clipboard copy-image"
                           : "clipboard import-image",
                       "ERR_CLIPBOARD_UNAVAILABLE",
                       "clipboard backend is unavailable", "{}");

  if (g_str_equal(subcommand, "copy-image")) {
    if (input == NULL)
      return write_error("clipboard copy-image", "ERR_CLI_USAGE",
                         "--input is required", "{}");
    g_autofree char *bytes = NULL;
    gsize length = 0;
    if (!g_file_get_contents(input, &bytes, &length, NULL))
      return write_error("clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
                         "clipboard image copy failed", "{}");
    if (g_mkdir_with_parents(clipboard_dir, 0755) != 0) {
      return write_error("clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
                         "clipboard image copy failed", "{}");
    }
    g_autofree char *state_contents = g_strconcat(input, "\n", NULL);
    if (!g_file_set_contents(clipboard_state, state_contents, -1, NULL))
      return write_error("clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
                         "clipboard image copy failed", "{}");

    ShaulaProcessSpan arguments[] = {{.data = "wl-copy", .length = 7},
                                     {.data = "--type", .length = 6},
                                     {.data = "image/png", .length = 9}};
    ShaulaProcessTermKind term = SHAULA_PROCESS_TERM_UNKNOWN;
    guint32 value = 0;
    if (shaula_process_run_with_input(
            (ShaulaProcessArgv){.items = arguments,
                                .length = G_N_ELEMENTS(arguments)},
            (ShaulaProcessSpan){.data = bytes, .length = length}, &term,
            &value) != SHAULA_PROCESS_STATUS_OK ||
        term != SHAULA_PROCESS_TERM_EXITED || value != 0)
      return write_error("clipboard copy-image", "ERR_CLIPBOARD_COPY_FAILED",
                         "clipboard image copy failed", "{}");
    g_autofree char *input_json = json_string(input);
    g_autofree char *result = g_strdup_printf(
        "{\"input\":%s,\"copied\":true}", input_json);
    return write_success("clipboard copy-image", result, "[]");
  }
  g_autofree char *state_contents = NULL;
  if (!g_file_get_contents(clipboard_state, &state_contents, NULL, NULL))
    return write_error("clipboard import-image",
                       "ERR_CLIPBOARD_IMPORT_INVALID",
                       "clipboard image import failed", "{}");
  g_strstrip(state_contents);
  if (state_contents[0] == '\0')
    return write_error("clipboard import-image",
                       "ERR_CLIPBOARD_IMPORT_INVALID",
                       "clipboard image import failed", "{}");
  g_autofree char *source = g_strdup(state_contents);
  g_autofree char *resolved =
      output != NULL
          ? g_strdup(output)
          : g_strdup_printf("/tmp/shaula/imported-%" G_GINT64_FORMAT ".png",
                            (gint64)time(NULL));
  g_autofree char *bytes = NULL;
  gsize length = 0;
  g_autofree char *parent = g_path_get_dirname(resolved);
  if (!g_file_get_contents(source, &bytes, &length, NULL) ||
      g_mkdir_with_parents(parent, 0755) != 0 ||
      !g_file_set_contents(resolved, bytes, (gssize)length, NULL))
    return write_error("clipboard import-image",
                       "ERR_CLIPBOARD_IMPORT_INVALID",
                       "clipboard image import failed", "{}");
  g_autofree char *path_json = json_string(resolved);
  g_autofree char *result = g_strdup_printf("{\"path\":%s}", path_json);
  return write_success("clipboard import-image", result, "[]");
}

static int command_explore(int argc, char **argv) {
  gboolean json = FALSE;
  gboolean brief = FALSE;
  for (int i = 2; i < argc; i++) {
    if (g_str_equal(argv[i], "--json"))
      json = TRUE;
    else if (g_str_equal(argv[i], "--brief"))
      brief = TRUE;
    else
      return write_error("explore", "ERR_CLI_USAGE", "unsupported flag", "{}");
  }
  if (!json)
    return write_error("explore", "ERR_CLI_USAGE", "--json is required", "{}");

  ShaulaCapabilitiesEnvironment environment = capabilities_environment();
  ShaulaRuntimeDecision runtime = {0};
  if (shaula_capabilities_resolve(&environment, &runtime) !=
      SHAULA_CAPABILITIES_STATUS_OK)
    return write_error("explore", "ERR_UNKNOWN_UNMAPPED",
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
    return write_error("explore", "ERR_UNKNOWN_UNMAPPED",
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
    return write_error("explore", "ERR_UNKNOWN_UNMAPPED",
                       "desktop inventory response could not be built", "{}");
  }
  int status = write_success(
      "explore", inventory.result_json,
      inventory.inventory_available ? "[]"
                                    : "[\"explore_inventory_unavailable\"]");
  shaula_explore_inventory_clear(&inventory);
  return status;
}

static int command_doctor(int argc, char **argv) {
  if (argc != 3 || !g_str_equal(argv[2], "--json"))
    return write_error("doctor", "ERR_CLI_USAGE", "--json is required", "{}");
  g_autofree char *self = executable_path();
  g_autofree char *self_json = json_string(self != NULL ? self : "shaula");
  g_autofree char *config_path = shaula_config_path_new();
  g_autofree char *config_json_value =
      json_string(config_path != NULL ? config_path : "");
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
  g_autofree char *result = g_strdup_printf(
      "{\"paths\":{\"binary\":%s,\"config_file\":%s,"
      "\"config_exists\":%s},\"wayland\":{\"wayland_display\":%s},"
      "\"tools\":{\"grim\":{\"found\":%s},\"wl-copy\":{\"found\":%s},"
      "\"wl-paste\":{\"found\":%s}},\"noctalia\":{"
      "\"dir_exists\":%s,\"plugins_dir_exists\":%s,"
      "\"plugins_json_exists\":%s,\"settings_json_exists\":%s,"
      "\"shaula_plugin_dir_exists\":%s,\"plugin_installed\":%s}}",
      self_json, config_json_value,
      json_bool(config_path != NULL && g_file_test(config_path, G_FILE_TEST_EXISTS)),
      g_getenv("WAYLAND_DISPLAY") != NULL ? "\"present\"" : "null",
      json_bool(g_find_program_in_path("grim") != NULL),
      json_bool(g_find_program_in_path("wl-copy") != NULL),
      json_bool(g_find_program_in_path("wl-paste") != NULL),
      json_bool(noctalia_dir != NULL &&
                g_file_test(noctalia_dir, G_FILE_TEST_IS_DIR)),
      json_bool(noctalia_plugins != NULL &&
                g_file_test(noctalia_plugins, G_FILE_TEST_IS_DIR)),
      json_bool(plugins_json != NULL &&
                g_file_test(plugins_json, G_FILE_TEST_IS_REGULAR)),
      json_bool(settings_json != NULL &&
                g_file_test(settings_json, G_FILE_TEST_IS_REGULAR)),
      json_bool(noctalia_plugin != NULL &&
                g_file_test(noctalia_plugin, G_FILE_TEST_IS_DIR)),
      json_bool(noctalia_plugin != NULL &&
                g_file_test(noctalia_plugin, G_FILE_TEST_IS_DIR)));
  g_autofree char *timestamp = json_timestamp();
  if (timestamp == NULL)
    return write_error("doctor", "ERR_UNKNOWN_UNMAPPED",
                       "could not encode doctor result", "{}");
  g_print("{\"ok\":true,\"contract_version\":\"1.0.0\","
          "\"command\":\"doctor\",\"timestamp\":\"%s\",%.*s,"
          "\"warnings\":[]}\n",
          timestamp, (int)strlen(result) - 2, result + 1);
  return 0;
}

static int command_setup(int argc, char **argv) {
  gboolean integrations = TRUE;
  gboolean niri = TRUE;
  gboolean install_keybinds = FALSE;
  gboolean skip_keybinds = FALSE;
  gboolean dry_run = FALSE;
  for (int i = 2; i < argc; i++) {
    if (g_str_equal(argv[i], "--help")) {
      g_print("Usage: shaula setup [--yes] [--no-integrations] [--no-niri] "
              "[--no-noctalia] [--niri-keybinds] [--skip-niri-keybinds] "
              "[--force] [--dry-run]\n");
      return 0;
    }
    if (g_str_equal(argv[i], "--no-integrations"))
      integrations = FALSE;
    else if (g_str_equal(argv[i], "--no-niri"))
      niri = FALSE;
    else if (g_str_equal(argv[i], "--niri-keybinds") ||
             g_str_equal(argv[i], "--force"))
      install_keybinds = TRUE;
    else if (g_str_equal(argv[i], "--skip-niri-keybinds"))
      skip_keybinds = TRUE;
    else if (g_str_equal(argv[i], "--dry-run"))
      dry_run = TRUE;
    else if (!g_str_equal(argv[i], "--yes") &&
             !g_str_equal(argv[i], "--no-noctalia"))
      return write_error("setup", "ERR_CLI_USAGE", "unsupported setup flag",
                         "{}");
  }
  ShaulaConfig config;
  g_autofree char *path = shaula_config_path_new();
  gboolean loaded = FALSE;
  if (path == NULL ||
      shaula_config_load(path, &config, &loaded) != SHAULA_CONFIG_STATUS_OK ||
      (!loaded && !dry_run &&
       shaula_config_save(path, &config) != SHAULA_CONFIG_STATUS_OK))
    return write_error("setup", "ERR_CONFIG_UNREADABLE",
                       "configuration path could not be resolved", "{}");
  g_print("Shaula setup\n\nok: %s config %s\n",
          loaded ? "kept" : dry_run ? "would create" : "created", path);

  g_autofree char *niri_path = niri_config_path(NULL);
  if (integrations && niri && niri_path != NULL &&
      g_file_test(niri_path, G_FILE_TEST_EXISTS)) {
    g_autofree char *rule = preview_rule(&config);
    ManagedBlockResult result = {0};
    gboolean invalid = FALSE;
    if (install_managed_block(niri_path,
                              "// BEGIN SHAULA PREVIEW WINDOW RULE",
                              "// END SHAULA PREVIEW WINDOW RULE", rule,
                              dry_run, &result, &invalid))
      g_print("ok: %s Niri preview rule in %s\n",
              result.changed ? dry_run ? "would install" : "installed"
                             : "kept",
              result.path);
    else
      g_printerr("warning: skipped Niri preview rule (%s)\n",
                 invalid ? "invalid managed block" : "unreadable config");
    managed_block_result_clear(&result);

    if (install_keybinds && !skip_keybinds) {
      g_autofree char *keybinds = render_keybinds();
      result = (ManagedBlockResult){0};
      invalid = FALSE;
      if (install_managed_block(niri_path,
                                "// BEGIN SHAULA MANAGED KEYBINDS",
                                "// END SHAULA MANAGED KEYBINDS", keybinds,
                                dry_run, &result, &invalid))
        g_print("ok: %s Niri keybinds in %s\n",
                result.changed ? dry_run ? "would install" : "installed"
                               : "kept",
                result.path);
      else
        g_printerr("warning: skipped Niri keybinds (%s)\n",
                   invalid ? "invalid managed block" : "unreadable config");
      managed_block_result_clear(&result);
    }
  } else if (integrations && niri) {
    g_print("info: Niri config was not detected; skipped Niri integration.\n");
  }
  g_print("\nok: setup complete\n");
  return 0;
}

static ShaulaNotifySpan notify_span(const char *value) {
  return (ShaulaNotifySpan){.data = (const guint8 *)value,
                            .length = value != NULL ? strlen(value) : 0};
}

static char **notify_argv_new(const ShaulaNotifySendArgs *args) {
  char **argv = g_new0(char *, args->length + 1);
  for (gsize i = 0; i < args->length; i++)
    argv[i] = g_strndup((const char *)args->items[i].data,
                        args->items[i].length);
  return argv;
}

static gboolean notify_request_run(const char *summary, const char *body,
                                   const char *image_path, guint timeout_ms,
                                   gboolean transient, gboolean with_action,
                                   char **action_output) {
  ShaulaNotifyRequest request;
  shaula_notify_request_init(&request);
  request.summary = notify_span(summary);
  request.body = notify_span(body);
  request.urgency = SHAULA_NOTIFY_URGENCY_NORMAL;
  request.timeout_ms = timeout_ms;
  request.transient = transient ? 1 : 0;
  if (image_path != NULL) {
    request.has_image_path = 1;
    request.image_path = notify_span(image_path);
  }
  if (with_action) {
    request.has_action = 1;
    request.action_id = notify_span("default");
    request.action_label = notify_span("Show in folder");
  }

  for (guint attempt = 0; attempt < (image_path != NULL ? 2U : 1U);
       attempt++) {
    ShaulaNotifySendArgs args;
    shaula_notify_send_args_init(&args);
    ShaulaNotifyImageMode image_mode =
        attempt == 0 ? SHAULA_NOTIFY_IMAGE_MODE_HINT
                     : SHAULA_NOTIFY_IMAGE_MODE_ICON;
    if (shaula_notify_send_args_build(&request, image_mode, &args) !=
        SHAULA_NOTIFY_STATUS_OK) {
      shaula_notify_send_args_clear(&args);
      return FALSE;
    }
    g_auto(GStrv) argv = notify_argv_new(&args);
    g_autofree char *stdout_text = NULL;
    int exit_code = 0;
    gboolean spawned =
        run_sync(argv, NULL, action_output != NULL ? &stdout_text : NULL, NULL,
                 &exit_code);
    shaula_notify_send_args_clear(&args);
    if (spawned && exit_code == 0) {
      if (action_output != NULL) {
        g_strstrip(stdout_text);
        *action_output = g_steal_pointer(&stdout_text);
      }
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean reveal_file(const char *path) {
  g_autofree char *absolute = g_canonicalize_filename(path, NULL);
  ShaulaNotifyOwnedBytes uri = {0};
  if (shaula_notify_file_uri_build(notify_span(absolute), &uri) !=
      SHAULA_NOTIFY_STATUS_OK)
    return FALSE;
  g_autofree char *uri_text =
      g_strndup((const char *)uri.data, uri.length);
  shaula_notify_owned_bytes_clear(&uri);
  g_autofree char *items = g_strdup_printf("['%s']", uri_text);
  char *gdbus_argv[] = {
      "gdbus",
      "call",
      "--session",
      "--dest",
      "org.freedesktop.FileManager1",
      "--object-path",
      "/org/freedesktop/FileManager1",
      "--method",
      "org.freedesktop.FileManager1.ShowItems",
      items,
      "",
      NULL,
  };
  int exit_code = 0;
  if (run_sync(gdbus_argv, NULL, NULL, NULL, &exit_code) && exit_code == 0)
    return TRUE;

  g_autofree char *parent = g_path_get_dirname(absolute);
  char *open_argv[] = {"xdg-open", parent, NULL};
  return run_sync(open_argv, NULL, NULL, NULL, &exit_code) && exit_code == 0;
}

static int notify_test(const char *kind) {
  const char *summary = "Screenshot captured";
  const char *body = "You can paste the image from the clipboard.";
  const char *image_path = NULL;
  guint timeout_ms = 2500;
  gboolean transient = TRUE;
  if (g_str_equal(kind, "saved")) {
    body = "Saved to screenshots folder.";
    image_path = "/tmp/shaula-notify-test.png";
    timeout_ms = 6000;
  } else if (g_str_equal(kind, "error")) {
    summary = "Could not copy screenshot";
    body = "Copy failed";
    timeout_ms = 5000;
    transient = FALSE;
  }
  gboolean delivered = notify_request_run(summary, body, image_path, timeout_ms,
                                           transient, FALSE, NULL);
  g_autofree char *kind_json = json_string(kind);
  g_autofree char *result =
      g_strdup_printf("{\"kind\":%s,\"delivered\":%s}", kind_json,
                      json_bool(delivered));
  g_autofree char *timestamp = json_timestamp();
  g_print("{\"ok\":%s,\"contract_version\":\"1.0.0\","
          "\"command\":\"notify test\",\"timestamp\":\"%s\","
          "\"result\":%s,\"warnings\":[]}\n",
          json_bool(delivered), timestamp, result);
  return 0;
}

static int command_notify(int argc, char **argv) {
  if (argc >= 4 && g_str_equal(argv[2], "__saved-action-listener")) {
    g_autofree char *absolute = g_canonicalize_filename(argv[3], NULL);
    const char *image_path = argc >= 5 ? argv[4] : NULL;
    g_autofree char *action = NULL;
    if (notify_request_run("Screenshot captured",
                           "Saved to screenshots folder.", image_path, 6000,
                           TRUE, TRUE, &action) &&
        action != NULL &&
        (g_str_equal(action, "default") ||
         g_str_equal(action, "show-in-folder") ||
         g_str_equal(action, "reveal-file")))
      (void)reveal_file(absolute);
    return 0;
  }
  if (argc >= 4 && g_str_equal(argv[2], "reveal-file")) {
    (void)reveal_file(argv[3]);
    return 0;
  }
  if (argc < 3 || !g_str_equal(argv[2], "test"))
    return write_error("notify test", "ERR_CLI_USAGE",
                       "usage: shaula notify test [--kind copied|saved|error]",
                       "{}");

  const char *kind = "copied";
  for (int i = 3; i < argc; i++) {
    if (!g_str_equal(argv[i], "--kind"))
      return write_error("notify test", "ERR_CLI_USAGE", "unsupported flag",
                         "{}");
    if (i + 1 >= argc)
      return write_error("notify test", "ERR_CLI_USAGE",
                         "--kind requires copied, saved, or error", "{}");
    kind = argv[++i];
    if (!g_str_equal(kind, "copied") && !g_str_equal(kind, "saved") &&
        !g_str_equal(kind, "error"))
      return write_error("notify test", "ERR_CLI_USAGE",
                         "--kind must be copied, saved, or error", "{}");
  }
  return notify_test(kind);
}

int main(int argc, char **argv) {
  if (argc < 2)
    return write_error(
        "", "ERR_CLI_USAGE",
        "usage: shaula <capture|preview|notify|config|settings|setup|directory|"
        "doctor|explore|preflight|capabilities|history|clipboard|errors> ...",
        "{}");
  if (g_str_equal(argv[1], "preflight"))
    return command_preflight(argc, argv);
  if (g_str_equal(argv[1], "capabilities"))
    return command_capabilities(argc, argv);
  if (g_str_equal(argv[1], "errors"))
    return command_errors(argc, argv);
  if (g_str_equal(argv[1], "settings"))
    return command_settings(argc, argv);
  if (g_str_equal(argv[1], "config"))
    return command_config(argc, argv);
  if (g_str_equal(argv[1], "preview"))
    return command_preview(argc, argv);
  if (g_str_equal(argv[1], "directory"))
    return command_directory(argc, argv);
  if (g_str_equal(argv[1], "history"))
    return command_history(argc, argv);
  if (g_str_equal(argv[1], "clipboard"))
    return command_clipboard(argc, argv);
  if (g_str_equal(argv[1], "explore"))
    return command_explore(argc, argv);
  if (g_str_equal(argv[1], "doctor"))
    return command_doctor(argc, argv);
  if (g_str_equal(argv[1], "setup"))
    return command_setup(argc, argv);
  if (g_str_equal(argv[1], "notify"))
    return command_notify(argc, argv);
  if (g_str_equal(argv[1], "capture"))
    return shaula_capture_command_run(argc, argv);
  return write_error(argv[1], "ERR_CLI_USAGE", "unsupported command family",
                     "{}");
}
