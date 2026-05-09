#define _GNU_SOURCE
#include "settings_config.h"

#include <glib/gstdio.h>
#include <errno.h>
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

static char *trim_dup(const char *value) {
  char *copy = g_strdup(value == NULL ? "" : value);
  return g_strstrip(copy);
}

static char *strip_comment(const char *line) {
  gboolean in_string = FALSE;
  gboolean escaped = FALSE;
  GString *out = g_string_new(NULL);
  for (const char *p = line; *p != '\0'; p++) {
    if (escaped) {
      escaped = FALSE;
      g_string_append_c(out, *p);
      continue;
    }
    if (*p == '\\' && in_string) {
      escaped = TRUE;
      g_string_append_c(out, *p);
      continue;
    }
    if (*p == '"') {
      in_string = !in_string;
      g_string_append_c(out, *p);
      continue;
    }
    if (*p == '#' && !in_string)
      break;
    g_string_append_c(out, *p);
  }
  return g_string_free(out, FALSE);
}

static gboolean parse_string_value(const char *value, char **out) {
  char *trimmed = trim_dup(value);
  size_t len = strlen(trimmed);
  if (len < 2 || trimmed[0] != '"' || trimmed[len - 1] != '"') {
    g_free(trimmed);
    return FALSE;
  }
  trimmed[len - 1] = '\0';
  *out = g_strdup(trimmed + 1);
  g_free(trimmed);
  return TRUE;
}

static gboolean parse_bool_value(const char *value, gboolean *out) {
  char *trimmed = trim_dup(value);
  if (g_strcmp0(trimmed, "true") == 0) {
    *out = TRUE;
    g_free(trimmed);
    return TRUE;
  }
  if (g_strcmp0(trimmed, "false") == 0) {
    *out = FALSE;
    g_free(trimmed);
    return TRUE;
  }
  g_free(trimmed);
  return FALSE;
}

static gboolean parse_int_value(const char *value, int *out) {
  char *trimmed = trim_dup(value);
  char *end = NULL;
  long parsed = strtol(trimmed, &end, 10);
  gboolean ok = end != trimmed && *end == '\0' && parsed >= G_MININT && parsed <= G_MAXINT;
  if (ok)
    *out = (int)parsed;
  g_free(trimmed);
  return ok;
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


gboolean shaula_settings_parse_config_file(const char *config_path, ShaulaSettingsConfig *config) {
  char *contents = NULL;
  gsize len = 0;
  if (!g_file_get_contents(config_path, &contents, &len, NULL))
    return TRUE;

  enum { SECTION_ROOT, SECTION_CAPTURE, SECTION_PREVIEW, SECTION_FLOATING } section = SECTION_ROOT;
  char **lines = g_strsplit(contents, "\n", -1);
  for (int i = 0; lines[i] != NULL; i++) {
    char *without_comment = strip_comment(lines[i]);
    char *line = g_strstrip(without_comment);
    if (*line == '\0') {
      g_free(without_comment);
      continue;
    }
    if (line[0] == '[') {
      if (g_strcmp0(line, "[capture]") == 0)
        section = SECTION_CAPTURE;
      else if (g_strcmp0(line, "[preview.window]") == 0)
        section = SECTION_PREVIEW;
      else if (g_strcmp0(line, "[preview.window.floating_position]") == 0)
        section = SECTION_FLOATING;
      else
        section = SECTION_ROOT;
      g_free(without_comment);
      continue;
    }

    char **parts = g_strsplit(line, "=", 2);
    if (parts[0] == NULL || parts[1] == NULL) {
      g_strfreev(parts);
      g_free(without_comment);
      continue;
    }
    char *key = g_strstrip(parts[0]);
    char *value = g_strstrip(parts[1]);
    char *text = NULL;
    int number = 0;
    gboolean flag = FALSE;

    if (section == SECTION_CAPTURE && g_strcmp0(key, "region_capture_mode") == 0 &&
        parse_string_value(value, &text)) {
      config->region_mode = parse_region_mode(text);
      g_free(text);
    } else if (section == SECTION_PREVIEW && g_strcmp0(key, "mode") == 0 &&
               parse_string_value(value, &text)) {
      config->window_mode = parse_window_mode(text);
      g_free(text);
    } else if (section == SECTION_PREVIEW && g_strcmp0(key, "focused") == 0 &&
               parse_bool_value(value, &flag)) {
      config->focused = flag;
    } else if (section == SECTION_PREVIEW && g_strcmp0(key, "width") == 0 &&
               parse_int_value(value, &number)) {
      config->width = number;
    } else if (section == SECTION_PREVIEW && g_strcmp0(key, "height") == 0 &&
               parse_int_value(value, &number)) {
      config->height = number;
    } else if (section == SECTION_PREVIEW && g_strcmp0(key, "default_column_display") == 0 &&
               parse_string_value(value, &text)) {
      g_free(config->column_display);
      config->column_display = text;
    } else if (section == SECTION_FLOATING && g_strcmp0(key, "x") == 0 &&
               parse_int_value(value, &number)) {
      config->floating_x = number;
      config->floating_x_set = TRUE;
    } else if (section == SECTION_FLOATING && g_strcmp0(key, "y") == 0 &&
               parse_int_value(value, &number)) {
      config->floating_y = number;
      config->floating_y_set = TRUE;
    } else if (section == SECTION_FLOATING && g_strcmp0(key, "relative_to") == 0 &&
               parse_string_value(value, &text)) {
      g_free(config->floating_relative_to);
      config->floating_relative_to = text;
    }

    g_strfreev(parts);
    g_free(without_comment);
  }
  g_strfreev(lines);
  g_free(contents);

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

static void append_preview_fields(GString *out, const ShaulaSettingsConfig *config,
                                  gboolean *seen_mode, gboolean *seen_focused,
                                  gboolean *seen_width, gboolean *seen_height,
                                  gboolean *seen_column) {
  if (!*seen_mode)
    g_string_append_printf(out, "mode = \"%s\"\n", shaula_settings_window_mode_text(config->window_mode));
  if (!*seen_focused)
    g_string_append_printf(out, "focused = %s\n", config->focused ? "true" : "false");
  if (!*seen_width)
    g_string_append_printf(out, "width = %d\n", config->width);
  if (!*seen_height)
    g_string_append_printf(out, "height = %d\n", config->height);
  if (!*seen_column)
    g_string_append_printf(out, "default_column_display = \"%s\"\n", config->column_display);
}

static void append_floating_fields(GString *out, const ShaulaSettingsConfig *config,
                                   gboolean *seen_x, gboolean *seen_y, gboolean *seen_relative) {
  if (config->floating_x_set && !*seen_x)
    g_string_append_printf(out, "x = %d\n", config->floating_x);
  if (config->floating_y_set && !*seen_y)
    g_string_append_printf(out, "y = %d\n", config->floating_y);
  if (!*seen_relative)
    g_string_append_printf(out, "relative_to = \"%s\"\n", config->floating_relative_to);
}

static char *canonical_config(const ShaulaSettingsConfig *config) {
  return g_strdup_printf(
      "[capture]\n"
      "# live keeps the desktop updating while selecting. frozen shows a still\n"
      "# screen while selecting for transient states.\n"
      "region_capture_mode = \"%s\"\n\n"
      "[preview.window]\n"
      "mode = \"%s\"\n"
      "focused = %s\n"
      "width = %d\n"
      "height = %d\n"
      "default_column_display = \"%s\"\n\n"
      "[preview.window.floating_position]\n"
      "# x and y are optional. When both are set, Shaula's generated Niri rule\n"
      "# includes default-floating-position.\n"
      "%s%s%s"
      "relative_to = \"%s\"\n",
      shaula_settings_region_mode_text(config->region_mode), shaula_settings_window_mode_text(config->window_mode),
      config->focused ? "true" : "false", config->width, config->height,
      config->column_display,
      config->floating_x_set ? "x = 80\n" : "",
      config->floating_y_set ? "y = 80\n" : "",
      (config->floating_x_set || config->floating_y_set) ? "" : "",
      config->floating_relative_to);
}

static gboolean line_key_equals(const char *line, const char *key_name) {
  char *without_comment = strip_comment(line);
  char *trimmed = g_strstrip(without_comment);
  gboolean result = FALSE;
  char **parts = g_strsplit(trimmed, "=", 2);
  if (parts[0] != NULL && parts[1] != NULL) {
    char *key = g_strstrip(parts[0]);
    result = g_strcmp0(key, key_name) == 0;
  }
  g_strfreev(parts);
  g_free(without_comment);
  return result;
}

static char *rewrite_existing_config(const char *contents, const ShaulaSettingsConfig *config) {
  enum { SECTION_ROOT, SECTION_CAPTURE, SECTION_PREVIEW, SECTION_FLOATING } section = SECTION_ROOT;
  gboolean saw_capture = FALSE;
  gboolean saw_preview = FALSE;
  gboolean saw_floating = FALSE;
  gboolean seen_region = FALSE;
  gboolean seen_mode = FALSE;
  gboolean seen_focused = FALSE;
  gboolean seen_width = FALSE;
  gboolean seen_height = FALSE;
  gboolean seen_column = FALSE;
  gboolean seen_x = FALSE;
  gboolean seen_y = FALSE;
  gboolean seen_relative = FALSE;
  GString *out = g_string_new(NULL);
  char **lines = g_strsplit(contents, "\n", -1);

  for (int i = 0; lines[i] != NULL; i++) {
    const char *raw = lines[i];
    gboolean last_empty_split = lines[i + 1] == NULL && raw[0] == '\0';
    if (last_empty_split)
      break;

    char *without_comment = strip_comment(raw);
    char *trimmed = g_strstrip(without_comment);
    gboolean is_section = trimmed[0] == '[';

    if (is_section) {
      if (section == SECTION_CAPTURE && !seen_region)
        g_string_append_printf(out, "region_capture_mode = \"%s\"\n", shaula_settings_region_mode_text(config->region_mode));
      if (section == SECTION_PREVIEW)
        append_preview_fields(out, config, &seen_mode, &seen_focused, &seen_width, &seen_height, &seen_column);
      if (section == SECTION_FLOATING)
        append_floating_fields(out, config, &seen_x, &seen_y, &seen_relative);

      if (g_strcmp0(trimmed, "[capture]") == 0) {
        section = SECTION_CAPTURE;
        saw_capture = TRUE;
      } else if (g_strcmp0(trimmed, "[preview.window]") == 0) {
        section = SECTION_PREVIEW;
        saw_preview = TRUE;
      } else if (g_strcmp0(trimmed, "[preview.window.floating_position]") == 0) {
        section = SECTION_FLOATING;
        saw_floating = TRUE;
      } else {
        section = SECTION_ROOT;
      }
      g_string_append_printf(out, "%s\n", raw);
      g_free(without_comment);
      continue;
    }

    if (section == SECTION_CAPTURE && line_key_equals(raw, "region_capture_mode")) {
      g_string_append_printf(out, "region_capture_mode = \"%s\"\n", shaula_settings_region_mode_text(config->region_mode));
      seen_region = TRUE;
    } else if (section == SECTION_PREVIEW && line_key_equals(raw, "mode")) {
      g_string_append_printf(out, "mode = \"%s\"\n", shaula_settings_window_mode_text(config->window_mode));
      seen_mode = TRUE;
    } else if (section == SECTION_PREVIEW && line_key_equals(raw, "focused")) {
      g_string_append_printf(out, "focused = %s\n", config->focused ? "true" : "false");
      seen_focused = TRUE;
    } else if (section == SECTION_PREVIEW && line_key_equals(raw, "width")) {
      g_string_append_printf(out, "width = %d\n", config->width);
      seen_width = TRUE;
    } else if (section == SECTION_PREVIEW && line_key_equals(raw, "height")) {
      g_string_append_printf(out, "height = %d\n", config->height);
      seen_height = TRUE;
    } else if (section == SECTION_PREVIEW && line_key_equals(raw, "default_column_display")) {
      g_string_append_printf(out, "default_column_display = \"%s\"\n", config->column_display);
      seen_column = TRUE;
    } else if (section == SECTION_FLOATING && line_key_equals(raw, "x")) {
      if (config->floating_x_set)
        g_string_append_printf(out, "x = %d\n", config->floating_x);
      seen_x = TRUE;
    } else if (section == SECTION_FLOATING && line_key_equals(raw, "y")) {
      if (config->floating_y_set)
        g_string_append_printf(out, "y = %d\n", config->floating_y);
      seen_y = TRUE;
    } else if (section == SECTION_FLOATING && line_key_equals(raw, "relative_to")) {
      g_string_append_printf(out, "relative_to = \"%s\"\n", config->floating_relative_to);
      seen_relative = TRUE;
    } else {
      g_string_append_printf(out, "%s\n", raw);
    }
    g_free(without_comment);
  }

  if (section == SECTION_CAPTURE && !seen_region)
    g_string_append_printf(out, "region_capture_mode = \"%s\"\n", shaula_settings_region_mode_text(config->region_mode));
  if (section == SECTION_PREVIEW)
    append_preview_fields(out, config, &seen_mode, &seen_focused, &seen_width, &seen_height, &seen_column);
  if (section == SECTION_FLOATING)
    append_floating_fields(out, config, &seen_x, &seen_y, &seen_relative);

  if (!saw_capture)
    g_string_append_printf(out, "\n[capture]\nregion_capture_mode = \"%s\"\n", shaula_settings_region_mode_text(config->region_mode));
  if (!saw_preview) {
    g_string_append(out, "\n[preview.window]\n");
    append_preview_fields(out, config, &seen_mode, &seen_focused, &seen_width, &seen_height, &seen_column);
  }
  if (!saw_floating) {
    g_string_append(out, "\n[preview.window.floating_position]\n");
    append_floating_fields(out, config, &seen_x, &seen_y, &seen_relative);
  }

  g_strfreev(lines);
  return g_string_free(out, FALSE);
}

static gboolean ensure_parent_dir(const char *path, GError **error) {
  char *dir = g_path_get_dirname(path);
  int rc = g_mkdir_with_parents(dir, 0755);
  if (rc != 0) {
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "could not create %s", dir);
    g_free(dir);
    return FALSE;
  }
  g_free(dir);
  return TRUE;
}

static gboolean backup_existing(const char *path, const char *contents, GError **error) {
  if (contents == NULL)
    return TRUE;
  gint64 now = g_get_real_time() / 1000;
  for (int i = 0; i < 100; i++) {
    char *backup = i == 0 ? g_strdup_printf("%s.shaula-backup-%" G_GINT64_FORMAT, path, now)
                          : g_strdup_printf("%s.shaula-backup-%" G_GINT64_FORMAT "-%d", path, now, i);
    if (!g_file_test(backup, G_FILE_TEST_EXISTS)) {
      gboolean ok = g_file_set_contents(backup, contents, -1, error);
      g_free(backup);
      return ok;
    }
    g_free(backup);
  }
  g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "could not create config backup");
  return FALSE;
}

gboolean shaula_settings_write_config_file(const char *config_path, const ShaulaSettingsConfig *config, gboolean force_canonical, GError **error) {
  if (config_path == NULL) {
    g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "configuration path could not be resolved");
    return FALSE;
  }
  char *current = NULL;
  gsize current_len = 0;
  gboolean existed = g_file_get_contents(config_path, &current, &current_len, NULL);
  char *next = NULL;
  if (existed && !force_canonical)
    next = rewrite_existing_config(current, config);
  else
    next = canonical_config(config);

  if (current != NULL && g_strcmp0(current, next) == 0) {
    g_free(current);
    g_free(next);
    return TRUE;
  }

  if (!ensure_parent_dir(config_path, error)) {
    g_free(current);
    g_free(next);
    return FALSE;
  }
  if (existed && !backup_existing(config_path, current, error)) {
    g_free(current);
    g_free(next);
    return FALSE;
  }

  char *tmp_path = g_strdup_printf("%s.tmp", config_path);
  gboolean ok = g_file_set_contents(tmp_path, next, -1, error);
  if (ok && g_rename(tmp_path, config_path) != 0) {
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "could not replace config file");
    ok = FALSE;
  }
  g_free(tmp_path);
  g_free(current);
  g_free(next);
  return ok;
}
