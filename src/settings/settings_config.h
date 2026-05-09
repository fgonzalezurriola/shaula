#pragma once

#include <glib.h>

typedef enum {
  REGION_LIVE = 0,
  REGION_FROZEN = 1,
} RegionMode;

typedef enum {
  WINDOW_AUTO = 0,
  WINDOW_TILING,
  WINDOW_FLOATING,
  WINDOW_MAXIMIZED,
  WINDOW_MAXIMIZED_TO_EDGES,
  WINDOW_FULLSCREEN,
} WindowMode;

typedef enum {
  SIZE_SMALL = 0,
  SIZE_MEDIUM,
  SIZE_LARGE,
  SIZE_CUSTOM,
} SizePreset;

typedef enum {
  POSITION_CENTERED = 0,
  POSITION_TOP_LEFT,
  POSITION_TOP_RIGHT,
  POSITION_CUSTOM,
} PositionPreset;

typedef struct {
  RegionMode region_mode;
  WindowMode window_mode;
  gboolean focused;
  int width;
  int height;
  char *column_display;
  gboolean floating_x_set;
  gboolean floating_y_set;
  int floating_x;
  int floating_y;
  char *floating_relative_to;
  PositionPreset position_preset;
} ShaulaSettingsConfig;


void shaula_settings_config_init_defaults(ShaulaSettingsConfig *config);
void shaula_settings_config_clear(ShaulaSettingsConfig *config);
const char *shaula_settings_region_mode_text(RegionMode mode);
const char *shaula_settings_window_mode_text(WindowMode mode);
char *shaula_settings_resolve_config_path(void);
gboolean shaula_settings_parse_config_file(const char *config_path, ShaulaSettingsConfig *config);
SizePreset shaula_settings_size_preset_for_config(const ShaulaSettingsConfig *config);
void shaula_settings_apply_size_preset(ShaulaSettingsConfig *config, SizePreset preset);
void shaula_settings_apply_position_preset(ShaulaSettingsConfig *config, PositionPreset preset);
gboolean shaula_settings_write_config_file(const char *config_path, const ShaulaSettingsConfig *config, gboolean force_canonical, GError **error);
