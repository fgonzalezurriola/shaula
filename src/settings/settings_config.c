#define _GNU_SOURCE
#include "settings_config.h"

#include <stdlib.h>
#include <string.h>

void shaula_settings_config_init_defaults(ShaulaSettingsConfig *config) {
  memset(config, 0, sizeof(*config));
  config->region_mode = REGION_LIVE;
  config->window_mode = WINDOW_FLOATING;
  config->focused = TRUE;
  config->width = 1100;
  config->height = 720;
  config->column_display = g_strdup("normal");
  config->floating_relative_to = g_strdup("top-left");
  config->position_preset = POSITION_CENTERED;
}

void shaula_settings_config_clear(ShaulaSettingsConfig *config) {
  g_clear_pointer(&config->column_display, g_free);
  g_clear_pointer(&config->floating_relative_to, g_free);
}

const char *shaula_settings_region_mode_text(RegionMode mode) {
  return mode == REGION_FROZEN ? "frozen" : "live";
}

const char *shaula_settings_window_mode_text(WindowMode mode) {
  switch (mode) {
  case WINDOW_AUTO:
    return "auto";
  case WINDOW_TILING:
    return "tiling";
  case WINDOW_MAXIMIZED:
    return "maximized";
  case WINDOW_MAXIMIZED_TO_EDGES:
    return "maximized-to-edges";
  case WINDOW_FULLSCREEN:
    return "fullscreen";
  case WINDOW_FLOATING:
  default:
    return "floating";
  }
}

static RegionMode parse_region_mode(const char *value) {
  return g_strcmp0(value, "frozen") == 0 ? REGION_FROZEN : REGION_LIVE;
}

static WindowMode parse_window_mode(const char *value) {
  if (g_strcmp0(value, "auto") == 0)
    return WINDOW_AUTO;
  if (g_strcmp0(value, "tiling") == 0)
    return WINDOW_TILING;
  if (g_strcmp0(value, "maximized") == 0)
    return WINDOW_MAXIMIZED;
  if (g_strcmp0(value, "maximized-to-edges") == 0)
    return WINDOW_MAXIMIZED_TO_EDGES;
  if (g_strcmp0(value, "fullscreen") == 0)
    return WINDOW_FULLSCREEN;
  return WINDOW_FLOATING;
}

SizePreset shaula_settings_size_preset_for_config(const ShaulaSettingsConfig *config) {
  if (config->width == 900 && config->height == 600)
    return SIZE_SMALL;
  if (config->width == 1100 && config->height == 720)
    return SIZE_MEDIUM;
  if (config->width == 1400 && config->height == 900)
    return SIZE_LARGE;
  return SIZE_CUSTOM;
}

void shaula_settings_apply_size_preset(ShaulaSettingsConfig *config, SizePreset preset) {
  if (preset == SIZE_SMALL) {
    config->width = 900;
    config->height = 600;
  } else if (preset == SIZE_MEDIUM) {
    config->width = 1100;
    config->height = 720;
  } else if (preset == SIZE_LARGE) {
    config->width = 1400;
    config->height = 900;
  }
}

void shaula_settings_apply_position_preset(ShaulaSettingsConfig *config, PositionPreset preset) {
  config->position_preset = preset;
  if (preset == POSITION_CENTERED) {
    config->floating_x_set = FALSE;
    config->floating_y_set = FALSE;
    g_free(config->floating_relative_to);
    config->floating_relative_to = g_strdup("top-left");
    return;
  }
  if (preset == POSITION_TOP_LEFT || preset == POSITION_TOP_RIGHT) {
    config->floating_x_set = TRUE;
    config->floating_y_set = TRUE;
    config->floating_x = 80;
    config->floating_y = 80;
    g_free(config->floating_relative_to);
    config->floating_relative_to = g_strdup(preset == POSITION_TOP_LEFT ? "top-left" : "top-right");
  }
}

const char *shaula_settings_position_arg(const ShaulaSettingsConfig *config) {
  switch (config->position_preset) {
  case POSITION_TOP_LEFT:
    return "top-left";
  case POSITION_TOP_RIGHT:
    return "top-right";
  case POSITION_CENTERED:
  case POSITION_CUSTOM:
  default:
    return "centered";
  }
}

static char *trim_dup(const char *value) {
  char *copy = g_strdup(value == NULL ? "" : value);
  return g_strstrip(copy);
}

char *shaula_settings_resolve_config_path(void) {
  const char *explicit_path = g_getenv("SHAULA_CONFIG_FILE");
  if (explicit_path != NULL) {
    char *trimmed = trim_dup(explicit_path);
    if (trimmed[0] != '\0')
      return trimmed;
    g_free(trimmed);
  }

  const char *xdg = g_getenv("XDG_CONFIG_HOME");
  if (xdg != NULL && *xdg != '\0')
    return g_build_filename(xdg, "shaula", "config.toml", NULL);

  const char *home = g_get_home_dir();
  if (home != NULL && *home != '\0')
    return g_build_filename(home, ".config", "shaula", "config.toml", NULL);

  return NULL;
}

static char *json_string_after(const char *json, const char *needle) {
  const char *p = strstr(json, needle);
  if (p == NULL)
    return NULL;
  p += strlen(needle);
  const char *end = strchr(p, '"');
  if (end == NULL)
    return NULL;
  return g_strndup(p, end - p);
}

static gboolean json_bool_after(const char *json, const char *needle, gboolean fallback) {
  const char *p = strstr(json, needle);
  if (p == NULL)
    return fallback;
  p += strlen(needle);
  if (g_str_has_prefix(p, "true"))
    return TRUE;
  if (g_str_has_prefix(p, "false"))
    return FALSE;
  return fallback;
}

static int json_int_after(const char *json, const char *needle, int fallback) {
  const char *p = strstr(json, needle);
  if (p == NULL)
    return fallback;
  p += strlen(needle);
  if (g_str_has_prefix(p, "null"))
    return fallback;
  char *end = NULL;
  long parsed = strtol(p, &end, 10);
  if (end == p || parsed < G_MININT || parsed > G_MAXINT)
    return fallback;
  return (int)parsed;
}

static gboolean json_nullable_int_after(const char *json, const char *needle, int *out) {
  const char *p = strstr(json, needle);
  if (p == NULL)
    return FALSE;
  p += strlen(needle);
  if (g_str_has_prefix(p, "null"))
    return FALSE;
  char *end = NULL;
  long parsed = strtol(p, &end, 10);
  if (end == p || parsed < G_MININT || parsed > G_MAXINT)
    return FALSE;
  *out = (int)parsed;
  return TRUE;
}

char *shaula_settings_config_path_from_show_json(const char *json) {
  return json_string_after(json, "\"path\":\"");
}

gboolean shaula_settings_config_from_show_json(const char *json, ShaulaSettingsConfig *config) {
  if (json == NULL)
    return FALSE;
  char *region = json_string_after(json, "\"region_capture_mode\":\"");
  if (region != NULL) {
    config->region_mode = parse_region_mode(region);
    g_free(region);
  }

  char *mode = json_string_after(json, "\"mode\":\"");
  if (mode != NULL) {
    config->window_mode = parse_window_mode(mode);
    g_free(mode);
  }

  config->focused = json_bool_after(json, "\"focused\":", config->focused);
  config->width = json_int_after(json, "\"width\":", config->width);
  config->height = json_int_after(json, "\"height\":", config->height);

  char *display = json_string_after(json, "\"default_column_display\":\"");
  if (display != NULL) {
    g_free(config->column_display);
    config->column_display = display;
  }

  int x = 0;
  int y = 0;
  config->floating_x_set = json_nullable_int_after(json, "\"x\":", &x);
  config->floating_y_set = json_nullable_int_after(json, "\"y\":", &y);
  if (config->floating_x_set)
    config->floating_x = x;
  if (config->floating_y_set)
    config->floating_y = y;

  char *relative = json_string_after(json, "\"relative_to\":\"");
  if (relative != NULL) {
    g_free(config->floating_relative_to);
    config->floating_relative_to = relative;
  }

  if (!config->floating_x_set || !config->floating_y_set) {
    config->position_preset = POSITION_CENTERED;
  } else if (config->floating_x == 80 && config->floating_y == 80 &&
             g_strcmp0(config->floating_relative_to, "top-left") == 0) {
    config->position_preset = POSITION_TOP_LEFT;
  } else if (config->floating_x == 80 && config->floating_y == 80 &&
             g_strcmp0(config->floating_relative_to, "top-right") == 0) {
    config->position_preset = POSITION_TOP_RIGHT;
  } else {
    config->position_preset = POSITION_CUSTOM;
  }
  return TRUE;
}
