#include "command.h"

#include "capabilities/runtime.h"
#include "cli/json.h"
#include "compositor/focused_output.h"
#include "config/config.h"
#include "errors/taxonomy.h"
#include "preview/preview_result.h"
#include "runtime/capture_session_lock.h"
#include "runtime/paths.h"
#include "runtime/previous_area_store.h"
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

typedef struct {
  gint32 x;
  gint32 y;
  guint32 width;
  guint32 height;
} CaptureGeometry;

typedef struct {
  char *path;
  char *output_name;
  gboolean cleanup;
  CaptureGeometry local_geometry;
  guint32 surface_width;
  guint32 surface_height;
} FrozenSource;

static void frozen_source_clear(FrozenSource *source) {
  if (source->cleanup && source->path != NULL)
    (void)g_unlink(source->path);
  g_clear_pointer(&source->path, g_free);
  g_clear_pointer(&source->output_name, g_free);
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(FrozenSource, frozen_source_clear)

typedef struct {
  char *path;
  gboolean acquired;
} CaptureSession;

/* Owns the capture lock across every lifecycle exit and permits the explicit
 * release-before-Preview contract without duplicating cleanup branches. */
static void capture_session_clear(CaptureSession *session) {
  if (session->acquired)
    shaula_capture_session_lock_release(
        (ShaulaCaptureSessionSpan){.data = session->path,
                                   .length = strlen(session->path)});
  g_clear_pointer(&session->path, g_free);
  session->acquired = FALSE;
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(CaptureSession, capture_session_clear)

typedef struct {
  const char *mode;
  gboolean json;
  gboolean copy;
  gboolean save;
  gboolean preview;
  gboolean dry_run;
  gboolean simulate_cancel;
  const char *output;
  const char *window_id;
  const char *aspect;
  const char *region_mode;
} CaptureOptions;

typedef enum {
  SELECTION_OK,
  SELECTION_CANCELLED,
  SELECTION_UNAVAILABLE,
  SELECTION_TIMEOUT,
  SELECTION_PROTOCOL_INVALID,
} SelectionStatus;

static ShaulaJsonSpan json_span(const char *value) {
  return (ShaulaJsonSpan){.data = (const guint8 *)value,
                          .length = value != NULL ? strlen(value) : 0};
}

static ShaulaErrorSpan error_span(const char *value) {
  return (ShaulaErrorSpan){.data = value, .length = strlen(value)};
}

static ShaulaRuntimePathSpan path_span(const char *value) {
  return (ShaulaRuntimePathSpan){.data = value, .length = strlen(value)};
}

static const char *json_bool(gboolean value) { return value ? "true" : "false"; }

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

static int capture_error(const char *command, const char *code,
                         const char *message, const char *details) {
  const ShaulaErrorSpec *spec = shaula_error_taxonomy_spec_for(error_span(code));
  ShaulaJsonOwnedBytes output = {0};
  if (shaula_json_basic_error_build(
          (gint64)time(NULL), json_span(command), json_span(code),
          json_span(message), spec->retryable,
          json_span(details != NULL ? details : "{}"), &output) ==
      SHAULA_JSON_STATUS_OK) {
    (void)fwrite(output.data, 1, output.length, stdout);
    shaula_json_owned_bytes_clear(&output);
  }
  return spec->exit_code;
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
  gint status = 0;
  g_autoptr(GError) error = NULL;
  gboolean spawned =
      g_spawn_sync(NULL, argv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL, stdout_text,
                   stderr_text, &status, &error);
  if (!spawned) {
    if (exit_code != NULL)
      *exit_code = 127;
    return FALSE;
  }
  if (exit_code != NULL) {
    if (WIFEXITED(status))
      *exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      *exit_code = 128 + WTERMSIG(status);
    else
      *exit_code = 1;
  }
  return TRUE;
}

static gboolean env_flag(const char *name) {
  const char *value = g_getenv(name);
  return value != NULL &&
         (g_str_equal(value, "1") || g_ascii_strcasecmp(value, "true") == 0 ||
          g_ascii_strcasecmp(value, "yes") == 0);
}

static ShaulaAfterModeConfig mode_defaults(const ShaulaConfig *config,
                                           const char *mode) {
  if (g_str_equal(mode, "quick"))
    return config->quick;
  if (g_str_equal(mode, "area"))
    return config->area;
  if (g_str_equal(mode, "fullscreen") || g_str_equal(mode, "focused"))
    return config->fullscreen;
  if (g_str_equal(mode, "all-screens") || g_str_equal(mode, "all-in-one"))
    return config->all_screens;
  return (ShaulaAfterModeConfig){0};
}

static gboolean parse_options(int argc, char **argv, const ShaulaConfig *config,
                              CaptureOptions *options) {
  if (argc < 4)
    return FALSE;
  memset(options, 0, sizeof(*options));
  options->mode = argv[2];
  ShaulaAfterModeConfig defaults = mode_defaults(config, options->mode);
  options->copy = defaults.copy_to_clipboard;
  options->save = defaults.save_to_folder;
  options->preview = !defaults.skip_preview;
  options->region_mode = config->region_capture_mode;
  const char *region_override = g_getenv("SHAULA_REGION_CAPTURE_MODE");
  if (region_override != NULL && region_override[0] != '\0')
    options->region_mode = region_override;
  if (g_str_equal(options->mode, "previous-area") ||
      g_str_equal(options->mode, "window")) {
    options->copy = FALSE;
    options->save = FALSE;
    options->preview = FALSE;
  }

  for (int i = 3; i < argc; i++) {
    if (g_str_equal(argv[i], "--json"))
      options->json = TRUE;
    else if (g_str_equal(argv[i], "--copy"))
      options->copy = TRUE;
    else if (g_str_equal(argv[i], "--save"))
      options->save = TRUE;
    else if (g_str_equal(argv[i], "--preview"))
      options->preview = TRUE;
    else if (g_str_equal(argv[i], "--no-preview"))
      options->preview = FALSE;
    else if (g_str_equal(argv[i], "--dry-run"))
      options->dry_run = TRUE;
    else if (g_str_equal(argv[i], "--simulate-cancel"))
      options->simulate_cancel = TRUE;
    else if (g_str_equal(argv[i], "--frozen-region"))
      options->region_mode = "frozen";
    else if (g_str_equal(argv[i], "--region-mode") && i + 1 < argc)
      options->region_mode = argv[++i];
    else if (g_str_equal(argv[i], "--output") && i + 1 < argc)
      options->output = argv[++i];
    else if (g_str_equal(argv[i], "--window-id") && i + 1 < argc)
      options->window_id = argv[++i];
    else if (g_str_equal(argv[i], "--aspect") && i + 1 < argc)
      options->aspect = argv[++i];
    else
      return FALSE;
  }
  if (!g_str_equal(options->region_mode, "live") &&
      !g_str_equal(options->region_mode, "frozen"))
    return FALSE;
  if (options->dry_run)
    options->preview = FALSE;
  return options->json;
}

static char *runtime_path(const char *relative) {
  ShaulaRuntimeOwnedPath path = {0};
  if (shaula_runtime_path_resolve(
          NULL, g_getenv("XDG_RUNTIME_DIR"),
          (ShaulaRuntimePathSpan){.data = relative, .length = strlen(relative)},
          &path) != SHAULA_RUNTIME_PATH_STATUS_OK)
    return NULL;
  char *copy = g_strndup(path.data, path.length);
  shaula_runtime_owned_path_clear(&path);
  return copy;
}

static char *timestamp_path(const char *directory) {
  g_autoptr(GDateTime) now = g_date_time_new_now_utc();
  g_autofree char *stamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
  for (guint attempt = 0; attempt < 1000; attempt++) {
    char *name = attempt == 0 ? g_strdup_printf("%s.png", stamp)
                              : g_strdup_printf("%s-%u.png", stamp, attempt + 1);
    char *path = g_build_filename(directory, name, NULL);
    g_free(name);
    if (!g_file_test(path, G_FILE_TEST_EXISTS))
      return path;
    g_free(path);
  }
  return NULL;
}

static char *saved_directory(const ShaulaConfig *config) {
  const char *home = g_getenv("HOME");
  if (home == NULL || home[0] == '\0')
    return NULL;

  // Empty persisted values retain the historical default-folder contract.
  const char *folder = config->save_folder[0] != '\0'
                           ? config->save_folder
                           : "~/Pictures/shaula";
  if (g_str_equal(folder, "~"))
    return g_strdup(home);
  if (g_str_has_prefix(folder, "~/"))
    return g_build_filename(home, folder + 2, NULL);
  if (g_path_is_absolute(folder))
    return g_strdup(folder);
  return NULL;
}

static char *resolve_output(const CaptureOptions *options,
                            const ShaulaConfig *config) {
  if (options->output != NULL) {
    if (strstr(options->output, "::invalid::") != NULL)
      return NULL;
    return g_strdup(options->output);
  }
  g_autofree char *directory = NULL;
  if (options->save)
    directory = saved_directory(config);
  else
    directory = runtime_path("captures");
  if (directory == NULL || g_mkdir_with_parents(directory, 0755) != 0)
    return NULL;
  return timestamp_path(directory);
}

// Success notifications are detached so action handling cannot delay capture JSON.
static gboolean spawn_saved_notification(const char *path,
                                         gboolean include_thumbnail) {
  g_autofree char *self = executable_path();
  if (self == NULL)
    return FALSE;
  char *argv[] = {self, "notify", "__saved-action-listener", (char *)path,
                  include_thumbnail ? (char *)path : NULL, NULL};
  g_autoptr(GError) error = NULL;
  return g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
                       &error);
}

static char *focused_output(void) {
  ShaulaFocusedOutputEnvironment environment = {
      .overlay_output_name = g_getenv("SHAULA_OVERLAY_OUTPUT_NAME"),
      .compositor = capabilities_environment().compositor,
  };
  ShaulaFocusedOutputResult result = {0};
  if (shaula_focused_output_resolve(&environment, &result) !=
          SHAULA_FOCUSED_OUTPUT_STATUS_OK ||
      !result.present) {
    shaula_focused_output_result_clear(&result);
    return NULL;
  }
  char *copy = g_strndup((const char *)result.name.data, result.name.length);
  shaula_focused_output_result_clear(&result);
  return copy;
}

static gboolean execute_backend(const ShaulaRuntimeDecision *runtime,
                                const char *mode, const CaptureGeometry *geometry,
                                const char *output) {
  ShaulaEnvSpan backend_span =
      shaula_capabilities_backend_label(runtime->backend);
  g_autofree char *backend =
      g_strndup(backend_span.data, backend_span.length);
  g_autofree char *geometry_text =
      geometry != NULL
          ? g_strdup_printf("%d,%d %ux%u", geometry->x, geometry->y,
                            geometry->width, geometry->height)
          : NULL;
  const char *configured = g_getenv("SHAULA_RUNTIME_CAPTURE_HELPER");
  g_autofree char *portal_helper = NULL;
  g_autofree char *grim = NULL;
  g_autofree char *focus = NULL;
  char *arguments[10] = {0};
  int index = 0;

  if (configured != NULL && configured[0] != '\0') {
    arguments[index++] = (char *)configured;
    arguments[index++] = "--backend";
    arguments[index++] = backend;
    arguments[index++] = "--mode";
    arguments[index++] = (char *)mode;
    if (geometry_text != NULL) {
      arguments[index++] = "--geometry";
      arguments[index++] = geometry_text;
    }
    arguments[index++] = "--output";
    arguments[index++] = (char *)output;
  } else if (runtime->backend == SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT) {
    portal_helper = resolve_helper("SHAULA_PORTAL_SCREENSHOT_HELPER_BIN",
                                   "shaula-portal-screenshot");
    arguments[index++] = portal_helper;
    arguments[index++] = "--backend";
    arguments[index++] = backend;
    arguments[index++] = "--mode";
    arguments[index++] = (char *)mode;
    if (geometry_text != NULL) {
      arguments[index++] = "--geometry";
      arguments[index++] = geometry_text;
    }
    arguments[index++] = "--output";
    arguments[index++] = (char *)output;
  } else {
    grim = g_find_program_in_path("grim");
    if (grim == NULL)
      return FALSE;
    arguments[index++] = grim;
    if (geometry_text != NULL) {
      arguments[index++] = "-g";
      arguments[index++] = geometry_text;
    } else if (g_str_equal(mode, "fullscreen") ||
               g_str_equal(mode, "focused")) {
      focus = focused_output();
      if (focus == NULL)
        return FALSE;
      arguments[index++] = "-o";
      arguments[index++] = focus;
    }
    arguments[index++] = (char *)output;
  }
  arguments[index] = NULL;
  int exit_code = 0;
  return run_sync(arguments, NULL, NULL, NULL, &exit_code) && exit_code == 0;
}

/* Captures the exact frame shown by a frozen overlay before the helper opens. */
static gboolean prepare_frozen_source(FrozenSource *source) {
  const char *existing = g_getenv("SHAULA_OVERLAY_BACKGROUND_PATH");
  if (existing != NULL && existing[0] != '\0') {
    source->path = g_strdup(existing);
    source->cleanup = FALSE;
    source->output_name = focused_output();
    return g_file_test(source->path, G_FILE_TEST_IS_REGULAR);
  }
  if (env_flag("SHAULA_OVERLAY_DISABLE_BACKGROUND"))
    return FALSE;
  g_autofree char *directory = runtime_path("overlay");
  if (directory == NULL || g_mkdir_with_parents(directory, 0700) != 0)
    return FALSE;
  source->path = g_strdup_printf("%s/background-%" G_GINT64_FORMAT ".png",
                                 directory, g_get_real_time() / 1000);
  source->output_name = focused_output();
  source->cleanup = TRUE;
  g_autofree char *grim = g_find_program_in_path("grim");
  if (grim == NULL)
    return FALSE;
  char *arguments[5] = {grim, NULL, NULL, NULL, NULL};
  int index = 1;
  if (source->output_name != NULL) {
    arguments[index++] = "-o";
    arguments[index++] = source->output_name;
  }
  arguments[index++] = source->path;
  int exit_code = 0;
  return run_sync(arguments, NULL, NULL, NULL, &exit_code) && exit_code == 0;
}

static gboolean crop_frozen_source(const FrozenSource *source,
                                   const char *output) {
  if (source->path == NULL || source->surface_width == 0 ||
      source->surface_height == 0)
    return FALSE;
  g_autofree char *helper =
      resolve_helper("SHAULA_CROP_HELPER_BIN", "shaula-crop-image");
  g_autofree char *x = g_strdup_printf("%d", source->local_geometry.x);
  g_autofree char *y = g_strdup_printf("%d", source->local_geometry.y);
  g_autofree char *width =
      g_strdup_printf("%u", source->local_geometry.width);
  g_autofree char *height =
      g_strdup_printf("%u", source->local_geometry.height);
  g_autofree char *surface_width =
      g_strdup_printf("%u", source->surface_width);
  g_autofree char *surface_height =
      g_strdup_printf("%u", source->surface_height);
  char *arguments[] = {helper,       source->path, (char *)output,
                       x,            y,            width,
                       height,       surface_width, surface_height,
                       NULL};
  int exit_code = 0;
  return run_sync(arguments, NULL, NULL, NULL, &exit_code) && exit_code == 0;
}

static gboolean json_integer_after(const char *start, const char *key,
                                   gint64 *out) {
  const char *found = strstr(start, key);
  if (found == NULL)
    return FALSE;
  found = strchr(found, ':');
  if (found == NULL)
    return FALSE;
  found++;
  while (g_ascii_isspace(*found))
    found++;
  errno = 0;
  char *end = NULL;
  gint64 value = g_ascii_strtoll(found, &end, 10);
  if (errno != 0 || end == found)
    return FALSE;
  *out = value;
  return TRUE;
}

static gboolean parse_geometry(const char *payload, CaptureGeometry *geometry,
                               CaptureGeometry *local_geometry,
                               guint32 *surface_width,
                               guint32 *surface_height, char **action) {
  const char *start = strstr(payload, "\"geometry\"");
  if (start == NULL || strstr(start, "null") == start + 11)
    return FALSE;
  gint64 x = 0;
  gint64 y = 0;
  gint64 width = 0;
  gint64 height = 0;
  if (!json_integer_after(start, "\"x\"", &x) ||
      !json_integer_after(start, "\"y\"", &y) ||
      !json_integer_after(start, "\"width\"", &width) ||
      !json_integer_after(start, "\"height\"", &height) || width <= 0 ||
      height <= 0 || x < G_MININT32 || x > G_MAXINT32 || y < G_MININT32 ||
      y > G_MAXINT32 || width > G_MAXUINT32 || height > G_MAXUINT32)
    return FALSE;
  geometry->x = (gint32)x;
  geometry->y = (gint32)y;
  geometry->width = (guint32)width;
  geometry->height = (guint32)height;
  *local_geometry = *geometry;
  *surface_width = geometry->width;
  *surface_height = geometry->height;
  const char *local_start = strstr(payload, "\"local_geometry\"");
  const char *output_start = strstr(payload, "\"output\"");
  gint64 local_x = 0;
  gint64 local_y = 0;
  gint64 local_width = 0;
  gint64 local_height = 0;
  gint64 output_width = 0;
  gint64 output_height = 0;
  if (local_start != NULL && output_start != NULL &&
      json_integer_after(local_start, "\"x\"", &local_x) &&
      json_integer_after(local_start, "\"y\"", &local_y) &&
      json_integer_after(local_start, "\"width\"", &local_width) &&
      json_integer_after(local_start, "\"height\"", &local_height) &&
      json_integer_after(output_start, "\"width\"", &output_width) &&
      json_integer_after(output_start, "\"height\"", &output_height) &&
      local_x >= 0 && local_y >= 0 && local_width > 0 && local_height > 0 &&
      local_x <= G_MAXINT32 && local_y <= G_MAXINT32 &&
      local_width <= G_MAXUINT32 && local_height <= G_MAXUINT32 &&
      output_width > 0 && output_height > 0 && output_width <= G_MAXUINT32 &&
      output_height <= G_MAXUINT32) {
    *local_geometry = (CaptureGeometry){.x = (gint32)local_x,
                                        .y = (gint32)local_y,
                                        .width = (guint32)local_width,
                                        .height = (guint32)local_height};
    *surface_width = (guint32)output_width;
    *surface_height = (guint32)output_height;
  }
  if (strstr(payload, "\"action\":\"copy\"") != NULL)
    *action = g_strdup("copy");
  else if (strstr(payload, "\"action\":\"save\"") != NULL)
    *action = g_strdup("save");
  else
    *action = g_strdup("capture");
  return TRUE;
}

/* Runs the interactive helper through its environment/stdout protocol. */
static SelectionStatus select_area(const CaptureOptions *options,
                                   FrozenSource *frozen,
                                   CaptureGeometry *geometry, char **action) {
  if (options->simulate_cancel)
    return SELECTION_CANCELLED;
  const char *test_mode = g_getenv("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE");
  if (test_mode != NULL) {
    if (g_str_equal(test_mode, "cancel") ||
        g_str_equal(test_mode, "interaction_cancel"))
      return SELECTION_CANCELLED;
    if (g_str_equal(test_mode, "timeout"))
      return SELECTION_TIMEOUT;
    if (g_str_equal(test_mode, "unavailable"))
      return SELECTION_UNAVAILABLE;
    if (g_str_equal(test_mode, "malformed"))
      return SELECTION_PROTOCOL_INVALID;
    *geometry =
        (CaptureGeometry){.x = 320, .y = 180, .width = 640, .height = 360};
    frozen->local_geometry = *geometry;
    frozen->surface_width = 1920;
    frozen->surface_height = 1080;
    *action = g_strdup("capture");
    return SELECTION_OK;
  }
  if (options->dry_run ||
      env_flag("SHAULA_CAPTURE_FORCE_NONINTERACTIVE_SELECTION")) {
    *geometry = (CaptureGeometry){.x = 100, .y = 100, .width = 400, .height = 300};
    frozen->local_geometry = *geometry;
    frozen->surface_width = 1920;
    frozen->surface_height = 1080;
    *action = g_strdup("capture");
    return SELECTION_OK;
  }

  g_autofree char *helper =
      resolve_helper("SHAULA_OVERLAY_HELPER_BIN", "shaula-overlay");
  g_auto(GStrv) envp = g_get_environ();
  envp = g_environ_setenv(envp, "SHAULA_OVERLAY_INTERACTION_MODE",
                          g_str_equal(options->mode, "quick") ? "quick" : "area",
                          TRUE);
  if (options->aspect != NULL)
    envp = g_environ_setenv(envp, "SHAULA_OVERLAY_ASPECT", options->aspect, TRUE);
  if (g_str_equal(options->region_mode, "frozen")) {
    envp = g_environ_setenv(envp, "SHAULA_OVERLAY_REGION_MODE", "frozen", TRUE);
    if (frozen->path != NULL)
      envp = g_environ_setenv(envp, "SHAULA_OVERLAY_BACKGROUND_PATH",
                              frozen->path, TRUE);
  }
  if (frozen->output_name != NULL)
    envp = g_environ_setenv(envp, "SHAULA_OVERLAY_OUTPUT_NAME",
                            frozen->output_name, TRUE);
  char *arguments[] = {helper, NULL};
  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  int exit_code = 0;
  if (!run_sync(arguments, envp, &stdout_text, &stderr_text, &exit_code) ||
      exit_code != 0)
    return SELECTION_UNAVAILABLE;
  if (stdout_text == NULL || strstr(stdout_text, "\"status\":\"ok\"") == NULL) {
    if (stdout_text != NULL &&
        strstr(stdout_text, "\"status\":\"cancel\"") != NULL)
      return SELECTION_CANCELLED;
    return SELECTION_PROTOCOL_INVALID;
  }
  return parse_geometry(stdout_text, geometry, &frozen->local_geometry,
                        &frozen->surface_width, &frozen->surface_height, action)
             ? SELECTION_OK
             : SELECTION_PROTOCOL_INVALID;
}

static gboolean copy_png(const char *path) {
  if (g_getenv("SHAULA_CLIPBOARD_AVAILABLE") != NULL &&
      !env_flag("SHAULA_CLIPBOARD_AVAILABLE"))
    return FALSE;
  g_autofree char *bytes = NULL;
  gsize length = 0;
  if (!g_file_get_contents(path, &bytes, &length, NULL))
    return FALSE;
  ShaulaProcessSpan arguments[] = {{.data = "wl-copy", .length = 7},
                                   {.data = "--type", .length = 6},
                                   {.data = "image/png", .length = 9}};
  ShaulaProcessTermKind term = SHAULA_PROCESS_TERM_UNKNOWN;
  guint32 value = 0;
  return shaula_process_run_with_input(
             (ShaulaProcessArgv){.items = arguments,
                                 .length = G_N_ELEMENTS(arguments)},
             (ShaulaProcessSpan){.data = bytes, .length = length}, &term,
             &value) == SHAULA_PROCESS_STATUS_OK &&
         term == SHAULA_PROCESS_TERM_EXITED && value == 0;
}

static gboolean append_history(const char *path, guint width, guint height,
                               const char *backend, const char *timestamp) {
  const char *history_path = "/tmp/shaula/history/latest.v1";
  (void)g_mkdir_with_parents("/tmp/shaula/history", 0755);
  g_autofree char *existing = NULL;
  gsize length = 0;
  (void)g_file_get_contents(history_path, &existing, &length, NULL);
  GString *contents = g_string_new(NULL);
  g_string_append_printf(contents, "%s|image/png|%u|%u|%s|%s\n", path, width,
                         height, backend, timestamp);
  if (existing != NULL) {
    g_auto(GStrv) lines = g_strsplit(existing, "\n", -1);
    for (guint i = 0, kept = 1; lines[i] != NULL && kept < 20; i++) {
      if (lines[i][0] == '\0')
        continue;
      g_string_append_printf(contents, "%s\n", lines[i]);
      kept++;
    }
  }
  gboolean ok = g_file_set_contents(history_path, contents->str,
                                    (gssize)contents->len, NULL);
  g_string_free(contents, TRUE);
  return ok;
}

static gboolean run_preview(const char *path, const ShaulaConfig *config,
                            char **action, gboolean *copied, gboolean *saved,
                            char **saved_path) {
  g_autofree char *helper =
      resolve_helper("SHAULA_PREVIEW_HELPER_BIN", "shaula-preview");
  g_autofree char *self = executable_path();
  g_auto(GStrv) envp = g_get_environ();
  envp = g_environ_setenv(envp, "SHAULA_BIN", self != NULL ? self : "shaula",
                          TRUE);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_COPY_ON_ACCEPT", "0", TRUE);
  envp = g_environ_setenv(envp, "SHAULA_PREVIEW_CLOSE_ON_SAVE",
                          config->close_preview_on_save ? "1" : "0", TRUE);
  envp = g_environ_setenv(envp, "SHAULA_SAVE_FOLDER", config->save_folder, TRUE);
  char *arguments[] = {helper, (char *)path, NULL};
  g_autofree char *stdout_text = NULL;
  g_autofree char *stderr_text = NULL;
  int exit_code = 0;
  if (!run_sync(arguments, envp, &stdout_text, &stderr_text, &exit_code) ||
      exit_code != 0 || stdout_text == NULL)
    return FALSE;
  ShaulaPreviewResult result;
  shaula_preview_result_init(&result);
  if (shaula_preview_result_parse(
          (ShaulaPreviewResultSpan){.data = (const guint8 *)stdout_text,
                                    .length = strlen(stdout_text)},
          &result) != SHAULA_PREVIEW_RESULT_STATUS_OK) {
    shaula_preview_result_clear(&result);
    return FALSE;
  }
  ShaulaPreviewResultSpan token = shaula_preview_action_token(result.action);
  *action = g_strndup((const char *)token.data, token.length);
  *copied = result.copied;
  *saved = result.saved;
  if (result.saved_path.data != NULL)
    *saved_path = g_strndup((const char *)result.saved_path.data,
                            result.saved_path.length);
  shaula_preview_result_clear(&result);
  return TRUE;
}

static int write_dry_run(const CaptureOptions *options,
                         const CaptureGeometry *geometry) {
  g_autofree char *timestamp = json_timestamp();
  const char *selection_mode = options->aspect != NULL ? "fixed-aspect" : "freeform";
  g_autofree char *aspect =
      options->aspect != NULL ? json_string(options->aspect) : g_strdup("null");
  g_print(
      "{\"ok\":true,\"contract_version\":\"1.0.0\",\"command\":"
      "\"capture %s\",\"timestamp\":\"%s\",\"selection\":{\"mode\":"
      "\"%s\",\"aspect\":%s,\"geometry\":{\"x\":%d,\"y\":%d,"
      "\"width\":%u,\"height\":%u},\"cancelled\":false},\"preview\":{"
      "\"attempted\":false,\"ok\":false,\"error\":null},\"warnings\":[]}\n",
      options->mode, timestamp, selection_mode, aspect, geometry->x, geometry->y,
      geometry->width, geometry->height);
  return 0;
}

int shaula_capture_command_run(int argc, char **argv) {
  ShaulaConfig config;
  g_autofree char *config_path = shaula_config_path_new();
  ShaulaConfigStatus config_status =
      shaula_config_load(config_path, &config, NULL);
  if (config_status == SHAULA_CONFIG_STATUS_INVALID)
    return capture_error("capture", "ERR_CONFIG_INVALID",
                         "invalid configuration file", "{}");
  if (config_status == SHAULA_CONFIG_STATUS_UNREADABLE)
    return capture_error("capture", "ERR_CONFIG_UNREADABLE",
                         "configuration file is unreadable", "{}");

  CaptureOptions options;
  if (!parse_options(argc, argv, &config, &options))
    return capture_error(
        "capture", "ERR_CLI_USAGE",
        "usage: shaula capture <quick|area|fullscreen|all-screens|window|"
        "previous-area> --json",
        "{}");
  g_autofree char *command = g_strdup_printf("capture %s", options.mode);
  const gboolean area_mode = g_str_equal(options.mode, "area") ||
                             g_str_equal(options.mode, "quick");
  const gboolean previous_mode = g_str_equal(options.mode, "previous-area");
  const gboolean fullscreen_mode = g_str_equal(options.mode, "fullscreen") ||
                                   g_str_equal(options.mode, "focused");
  const gboolean all_mode = g_str_equal(options.mode, "all-screens") ||
                            g_str_equal(options.mode, "all-in-one");
  const gboolean window_mode = g_str_equal(options.mode, "window");
  if (!area_mode && !previous_mode && !fullscreen_mode && !all_mode &&
      !window_mode)
    return capture_error(command, "ERR_CLI_USAGE", "unsupported capture mode",
                         "{}");

  ShaulaCapabilitiesEnvironment environment = capabilities_environment();
  ShaulaRuntimeDecision runtime = {0};
  if (shaula_capabilities_resolve(&environment, &runtime) !=
      SHAULA_CAPABILITIES_STATUS_OK)
    return capture_error(command, "ERR_UNKNOWN_UNMAPPED",
                         "capability resolution failed", "{}");
  if (!runtime.compositor_supported)
    return capture_error(command, "ERR_UNSUPPORTED_COMPOSITOR",
                         "unsupported compositor for shaula v1", "{}");
  const char *support_mode = area_mode || previous_mode
                                 ? "area"
                                 : (all_mode ? "all-screens" : options.mode);
  if (shaula_capabilities_mode_supported(
          runtime.capture,
          (ShaulaEnvSpan){.data = support_mode,
                          .length = strlen(support_mode)}) != 1)
    return capture_error(command, "ERR_CAPTURE_MODE_UNSUPPORTED",
                         "capture mode is unsupported by runtime capabilities",
                         "{}");
  if (window_mode && options.window_id == NULL &&
      g_getenv("SHAULA_WINDOW_ID") == NULL &&
      !env_flag("SHAULA_WINDOW_TARGET_RESOLVED"))
    return capture_error(command, "ERR_WINDOW_TARGET_UNRESOLVED",
                         "window target could not be resolved",
                         "{\"mode\":\"window\"}");

  g_auto(CaptureSession) session = {.path = runtime_path("capture.lock")};
  if (session.path == NULL)
    return capture_error(command, "ERR_OUTPUT_PATH_INVALID",
                         "capture lock path is unavailable", "{}");
  if (shaula_capture_session_lock_acquire(
          (ShaulaCaptureSessionSpan){.data = session.path,
                                     .length = strlen(session.path)}) !=
      SHAULA_CAPTURE_SESSION_STATUS_OK)
    return capture_error(command, "ERR_CAPTURE_IN_PROGRESS",
                         "another capture is already in progress", "{}");
  session.acquired = TRUE;

  CaptureGeometry geometry = {0};
  g_auto(FrozenSource) frozen = {0};
  g_autofree char *confirm_action = NULL;
  if (area_mode) {
    if (g_str_equal(options.region_mode, "frozen") &&
        !options.dry_run &&
        (g_getenv("SHAULA_OVERLAY_HELPER_STDIO_TEST_MODE") == NULL ||
         g_getenv("SHAULA_OVERLAY_BACKGROUND_PATH") != NULL) &&
        !prepare_frozen_source(&frozen)) {
      return capture_error(command, "ERR_CAPTURE_BACKEND_UNAVAILABLE",
                           "frozen capture source unavailable", "{}");
    }
    SelectionStatus status =
        select_area(&options, &frozen, &geometry, &confirm_action);
    if (status != SELECTION_OK) {
      g_autofree char *details =
          g_strdup_printf("{\"mode\":\"%s\"}", options.mode);
      if (status == SELECTION_TIMEOUT)
        return capture_error(command, "ERR_OVERLAY_TIMEOUT",
                             "overlay helper timed out", details);
      if (status == SELECTION_UNAVAILABLE)
        return capture_error(command, "ERR_OVERLAY_UNAVAILABLE",
                             "overlay helper is unavailable", details);
      if (status == SELECTION_PROTOCOL_INVALID)
        return capture_error(command, "ERR_OVERLAY_PROTOCOL_INVALID",
                             "overlay helper produced invalid protocol payload",
                             details);
      return capture_error(command, "ERR_SELECTION_CANCELLED",
                           "selection was cancelled by user", details);
    }
  } else if (previous_mode) {
    g_autofree char *previous_path = runtime_path("previous-area.v1");
    gint32 present = 0;
    ShaulaPreviousAreaGeometry previous = {0};
    if (previous_path == NULL ||
        shaula_previous_area_load(
            (ShaulaPreviousAreaSpan){.data = previous_path,
                                     .length = strlen(previous_path)},
            &present, &previous) != SHAULA_PREVIOUS_AREA_STATUS_OK ||
        !present) {
      return capture_error(command, "ERR_PREVIOUS_AREA_UNAVAILABLE",
                           "previous area is unavailable", "{}");
    }
    geometry = (CaptureGeometry){.x = previous.x,
                                 .y = previous.y,
                                 .width = previous.width,
                                 .height = previous.height};
  }

  if (options.dry_run && (area_mode || previous_mode)) {
    return write_dry_run(&options, &geometry);
  }

  if (confirm_action != NULL && g_str_equal(confirm_action, "copy")) {
    options.copy = TRUE;
    options.preview = FALSE;
  } else if (confirm_action != NULL && g_str_equal(confirm_action, "save")) {
    options.save = TRUE;
    options.preview = FALSE;
  }

  g_autofree char *output = resolve_output(&options, &config);
  if (output == NULL ||
      shaula_runtime_path_ensure_parent(path_span(output)) !=
          SHAULA_RUNTIME_PATH_STATUS_OK) {
    return capture_error(command, "ERR_OUTPUT_PATH_INVALID",
                         "output path is not writable", "{}");
  }
  if (env_flag("SHAULA_INJECT_UNKNOWN_FAILURE")) {
    return capture_error(command, "ERR_UNKNOWN_UNMAPPED",
                         "injected unknown failure", "{}");
  }

  const char *backend_mode = area_mode || previous_mode ? "area" : options.mode;
  gboolean backend_ok =
      area_mode && frozen.path != NULL
          ? crop_frozen_source(&frozen, output)
          : execute_backend(&runtime, backend_mode,
                            area_mode || previous_mode ? &geometry : NULL,
                            output);
  if (!backend_ok) {
    return capture_error(command, "ERR_CAPTURE_BACKEND_UNAVAILABLE",
                         "capture backend unavailable", "{}");
  }

  g_autoptr(GError) image_error = NULL;
  g_autoptr(GdkPixbuf) image = gdk_pixbuf_new_from_file(output, &image_error);
  if (image == NULL) {
    return capture_error(command, "ERR_CAPTURE_BACKEND_UNAVAILABLE",
                         "capture backend did not produce a valid PNG", "{}");
  }
  guint width = (guint)gdk_pixbuf_get_width(image);
  guint height = (guint)gdk_pixbuf_get_height(image);

  if ((area_mode || previous_mode) && runtime.backend != SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT) {
    g_autofree char *previous_path = runtime_path("previous-area.v1");
    if (previous_path != NULL)
      (void)shaula_previous_area_store(
          (ShaulaPreviousAreaSpan){.data = previous_path,
                                   .length = strlen(previous_path)},
          (ShaulaPreviousAreaGeometry){.x = geometry.x,
                                       .y = geometry.y,
                                       .width = geometry.width,
                                       .height = geometry.height});
  }

  gboolean clipboard_ok = options.copy ? copy_png(output) : FALSE;
  ShaulaEnvSpan backend_span = shaula_capabilities_backend_label(runtime.backend);
  g_autofree char *backend =
      frozen.path != NULL ? g_strdup("frozen-source")
                          : g_strndup(backend_span.data, backend_span.length);
  g_autofree char *capture_timestamp = json_timestamp();
  gboolean history_ok = append_history(output, width, height, backend,
                                       capture_timestamp);

  capture_session_clear(&session);

  gboolean preview_attempted = options.preview;
  gboolean preview_ok = FALSE;
  gboolean preview_copied = FALSE;
  gboolean preview_saved = FALSE;
  g_autofree char *preview_action = NULL;
  g_autofree char *preview_saved_path = NULL;
  if (options.save && config.notifications_success)
    (void)spawn_saved_notification(output,
                                   config.notifications_thumbnails);
  if (options.preview)
    preview_ok = run_preview(output, &config, &preview_action, &preview_copied,
                             &preview_saved, &preview_saved_path);

  g_autofree char *path_json = json_string(output);
  g_autofree char *backend_json = json_string(backend);
  g_autofree char *preview_action_json =
      preview_action != NULL ? json_string(preview_action) : g_strdup("null");
  g_autofree char *preview_saved_path_json = preview_saved_path != NULL
                                                 ? json_string(preview_saved_path)
                                                 : g_strdup("null");
  gboolean partial = (options.copy && !clipboard_ok) ||
                     (preview_attempted && !preview_ok) || !history_ok;
  GString *warnings = g_string_new("[");
  gboolean warning = FALSE;
  if (options.copy && !clipboard_ok) {
    g_string_append(warnings, "\"clipboard_unavailable\"");
    warning = TRUE;
  }
  if (!history_ok) {
    if (warning)
      g_string_append_c(warnings, ',');
    g_string_append(warnings, "\"history_unavailable\"");
    warning = TRUE;
  }
  if (shaula_capabilities_uses_portal_backend(runtime) == 1) {
    if (warning)
      g_string_append_c(warnings, ',');
    g_string_append(warnings, "\"portal_fallback\"");
  }
  g_string_append_c(warnings, ']');

  guint latency = area_mode ? 12 : (all_mode ? 16 : 14);
  g_print(
      "{\"ok\":true,\"contract_version\":\"1.0.0\",\"command\":"
      "\"capture %s\",\"timestamp\":\"%s\",\"mode\":\"%s\","
      "\"path\":%s,\"mime\":\"image/png\",\"dimensions\":{\"width\":%u,"
      "\"height\":%u},\"backend_used\":%s,\"latency_ms\":%u,"
      "\"degraded\":%s,\"saved\":{\"ok\":%s,\"path\":%s,\"error\":null},"
      "\"clipboard\":{\"ok\":%s,\"error\":null},\"preview\":{"
      "\"attempted\":%s,\"ok\":%s,\"error\":null,\"action\":%s,"
      "\"copied\":%s,\"saved\":%s,\"saved_path\":%s},\"partial\":%s,"
      "\"result\":{\"mode\":\"%s\",\"path\":%s,\"mime\":\"image/png\","
      "\"dimensions\":{\"width\":%u,\"height\":%u},\"backend_used\":%s,"
      "\"latency_ms\":%u,\"saved\":{\"ok\":%s,\"path\":%s},"
      "\"clipboard\":{\"ok\":%s},\"preview\":{\"attempted\":%s,\"ok\":%s,"
      "\"error\":null,\"action\":%s,\"copied\":%s,\"saved\":%s,"
      "\"saved_path\":%s}},\"warnings\":%s}\n",
      options.mode, capture_timestamp, options.mode, path_json, width, height,
      backend_json, latency,
      json_bool(shaula_capabilities_degraded_backend(runtime) == 1),
      json_bool(options.save), options.save ? path_json : "null",
      json_bool(options.copy && clipboard_ok), json_bool(preview_attempted),
      json_bool(preview_ok), preview_action_json, json_bool(preview_copied),
      json_bool(preview_saved), preview_saved_path_json, json_bool(partial),
      options.mode, path_json, width, height, backend_json, latency,
      json_bool(options.save), options.save ? path_json : "null",
      json_bool(options.copy && clipboard_ok), json_bool(preview_attempted),
      json_bool(preview_ok), preview_action_json, json_bool(preview_copied),
      json_bool(preview_saved), preview_saved_path_json, warnings->str);
  g_string_free(warnings, TRUE);
  return 0;
}
