#include "settings_process.h"

static const char *bool_text(gboolean value) {
  return value ? "true" : "false";
}

static void append_owned(GPtrArray *argv, char *value) {
  g_ptr_array_add(argv, value);
}

static void append_literal(GPtrArray *argv, const char *value) {
  append_owned(argv, g_strdup(value));
}

static void append_pair(GPtrArray *argv, const char *flag, const char *value) {
  append_literal(argv, flag);
  append_literal(argv, value);
}

char **shaula_settings_build_save_argv(const char *shaula_bin,
                                       const ShaulaSettingsConfig *config) {
  g_return_val_if_fail(shaula_bin != NULL && shaula_bin[0] != '\0', NULL);
  g_return_val_if_fail(config != NULL, NULL);

  GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
  append_literal(argv, shaula_bin);
  append_literal(argv, "config");
  append_literal(argv, "save");
  append_literal(argv, "--json");

  append_pair(argv, "--region-mode",
              shaula_settings_region_mode_text(config->region_mode));
  append_pair(argv, "--preview-mode",
              shaula_settings_window_mode_text(config->window_mode));
  append_pair(argv, "--focused", bool_text(config->focused));
  append_pair(argv, "--close-preview-on-save",
              bool_text(config->close_preview_on_save));

  g_autofree char *width = g_strdup_printf("%d", config->width);
  g_autofree char *height = g_strdup_printf("%d", config->height);
  g_autofree char *floating_x = config->floating_x_set
                                    ? g_strdup_printf("%d", config->floating_x)
                                    : g_strdup("null");
  g_autofree char *floating_y = config->floating_y_set
                                    ? g_strdup_printf("%d", config->floating_y)
                                    : g_strdup("null");

  append_pair(argv, "--width", width);
  append_pair(argv, "--height", height);
  append_pair(argv, "--default-column-display",
              config->column_display != NULL ? config->column_display
                                             : "normal");
  append_pair(argv, "--floating-x", floating_x);
  append_pair(argv, "--floating-y", floating_y);
  append_pair(argv, "--floating-relative-to",
              config->floating_relative_to != NULL
                  ? config->floating_relative_to
                  : "top-left");

  append_pair(argv, "--after-quick-skip-preview",
              bool_text(config->quick_skip_preview));
  append_pair(argv, "--after-quick-copy", bool_text(config->quick_copy));
  append_pair(argv, "--after-quick-save", bool_text(config->quick_save));
  append_pair(argv, "--after-area-skip-preview",
              bool_text(config->area_skip_preview));
  append_pair(argv, "--after-area-copy", bool_text(config->area_copy));
  append_pair(argv, "--after-area-save", bool_text(config->area_save));
  append_pair(argv, "--after-fullscreen-skip-preview",
              bool_text(config->fullscreen_skip_preview));
  append_pair(argv, "--after-fullscreen-copy",
              bool_text(config->fullscreen_copy));
  append_pair(argv, "--after-fullscreen-save",
              bool_text(config->fullscreen_save));
  append_pair(argv, "--after-all-screens-skip-preview",
              bool_text(config->all_screens_skip_preview));
  append_pair(argv, "--after-all-screens-copy",
              bool_text(config->all_screens_copy));
  append_pair(argv, "--after-all-screens-save",
              bool_text(config->all_screens_save));
  append_pair(argv, "--save-folder",
              config->save_folder != NULL ? config->save_folder : "");
  append_pair(argv, "--notifications-success",
              bool_text(config->notifications_success));
  append_pair(argv, "--notifications-errors",
              bool_text(config->notifications_errors));
  append_pair(argv, "--notifications-thumbnails",
              bool_text(config->notifications_thumbnails));
  append_literal(argv, "--apply-niri");
  g_ptr_array_add(argv, NULL);

  return (char **)g_ptr_array_free(argv, FALSE);
}

gboolean shaula_settings_run_command(char *const *argv, gchar **stdout_text,
                                     gchar **stderr_text, int *exit_code) {
  g_return_val_if_fail(argv != NULL && argv[0] != NULL, FALSE);

  if (stdout_text != NULL)
    g_clear_pointer(stdout_text, g_free);
  if (stderr_text != NULL)
    g_clear_pointer(stderr_text, g_free);

  gint status = 1;
  g_autoptr(GError) error = NULL;
  gboolean spawned =
      g_spawn_sync(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                   stdout_text, stderr_text, &status, &error);
  if (!spawned) {
    if (stderr_text != NULL) {
      g_clear_pointer(stderr_text, g_free);
      *stderr_text = g_strdup(error != NULL ? error->message : "spawn failed");
    }
    if (exit_code != NULL)
      *exit_code = 127;
    return FALSE;
  }

  if (exit_code != NULL)
    *exit_code = g_spawn_check_wait_status(status, NULL) ? 0 : 1;
  return TRUE;
}
