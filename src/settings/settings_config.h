#ifndef SHAULA_SETTINGS_CONFIG_H
#define SHAULA_SETTINGS_CONFIG_H

#include <glib.h>

#include "config/config.h"

G_BEGIN_DECLS

typedef enum {
  REGION_LIVE = 0,
  REGION_FROZEN = 1,
} RegionMode;

typedef enum {
  WINDOW_AUTO = 0,
  WINDOW_TILING = 1,
  WINDOW_FLOATING = 2,
  WINDOW_MAXIMIZED = 3,
  WINDOW_MAXIMIZED_TO_EDGES = 4,
  WINDOW_FULLSCREEN = 5,
} WindowMode;

typedef enum {
  SIZE_SMALL = 0,
  SIZE_MEDIUM = 1,
  SIZE_LARGE = 2,
  SIZE_CUSTOM = 3,
} SizePreset;

typedef enum {
  POSITION_CENTERED = 0,
  POSITION_TOP_LEFT = 1,
  POSITION_TOP_RIGHT = 2,
  POSITION_CUSTOM = 3,
} PositionPreset;

/*
 * C ABI model consumed directly by native_gtk_settings.c. Field order and
 * enum values are stable. The three string fields are GLib-owned and are
 * released by shaula_settings_config_clear(). All other fields are values.
 */
typedef struct {
  RegionMode region_mode;
  WindowMode window_mode;
  gboolean focused;
  gboolean close_preview_on_save;
  int width;
  int height;
  char *column_display;
  gboolean floating_x_set;
  gboolean floating_y_set;
  int floating_x;
  int floating_y;
  char *floating_relative_to;
  PositionPreset position_preset;
  gboolean quick_skip_preview;
  gboolean quick_copy;
  gboolean quick_save;
  gboolean area_skip_preview;
  gboolean area_copy;
  gboolean area_save;
  gboolean fullscreen_skip_preview;
  gboolean fullscreen_copy;
  gboolean fullscreen_save;
  gboolean all_screens_skip_preview;
  gboolean all_screens_copy;
  gboolean all_screens_save;
  char *save_folder;
  gboolean notifications_success;
  gboolean notifications_errors;
  gboolean notifications_thumbnails;
} ShaulaSettingsConfig;

/*
 * Initializes required caller-provided storage with integrated defaults and
 * three newly allocated GLib strings. The object must not currently own live
 * fields: it may be uninitialized, zeroed, or previously cleared. Calling this
 * on an initialized but uncleared object leaks its existing owned strings.
 */
void shaula_settings_config_init_defaults(ShaulaSettingsConfig *config);

/* Releases every owned string field and sets those fields to NULL. config is
 * required. Safe after partial initialization and safe to call repeatedly.
 * Scalar fields remain unchanged. */
void shaula_settings_config_clear(ShaulaSettingsConfig *config);

/* Returned enum text is borrowed immutable process-lifetime storage. */
const char *shaula_settings_region_mode_text(RegionMode mode);
const char *shaula_settings_window_mode_text(WindowMode mode);

/* Returns a newly allocated GLib string, or NULL when no path can be resolved.
 * The caller releases a non-NULL result with g_free(). */
char *shaula_settings_resolve_config_path(void);

/*
 * Extracts the first literal `"path":"..."` value from borrowed JSON.
 * Returns a newly allocated GLib string, including an allocated empty string
 * for an empty value, or NULL for NULL/missing/unterminated input. Escapes are
 * intentionally not decoded because this preserves the existing bridge ABI.
 */
char *shaula_settings_config_path_from_show_json(const char *json);

/*
 * Applies recognized config-show fields from borrowed JSON to an initialized,
 * clearable config. NULL JSON returns FALSE without mutation. Every non-NULL
 * buffer returns TRUE, including malformed JSON; missing or malformed scalar
 * values preserve their existing values, while absent/null/invalid floating
 * coordinates clear their corresponding `_set` flags. Unknown fields are
 * ignored. No input pointer is retained.
 */
gboolean shaula_settings_config_from_show_json(const char *json,
                                               ShaulaSettingsConfig *config);

/*
 * Settings transport ownership. The Config command and GTK adapter both use
 * this module for the public JSON shape, save-flag grammar, and argv mapping.
 */
char *shaula_settings_config_public_json_new(const ShaulaConfig *config);
gboolean shaula_settings_config_apply_cli_flag(ShaulaConfig *config,
                                               const char *flag,
                                               const char *value);
char **shaula_settings_build_save_argv(const char *shaula_bin,
                                       const ShaulaSettingsConfig *config);

SizePreset
shaula_settings_size_preset_for_config(const ShaulaSettingsConfig *config);
void shaula_settings_apply_size_preset(ShaulaSettingsConfig *config,
                                       SizePreset preset);
void shaula_settings_apply_position_preset(ShaulaSettingsConfig *config,
                                           PositionPreset preset);

G_END_DECLS

#endif
