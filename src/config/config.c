#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
  SECTION_ROOT,
  SECTION_CAPTURE,
  SECTION_CAPTURE_AFTER,
  SECTION_QUICK,
  SECTION_AREA,
  SECTION_FULLSCREEN,
  SECTION_ALL_SCREENS,
  SECTION_NOTIFICATIONS,
  SECTION_PREVIEW_WINDOW,
  SECTION_FLOATING_POSITION,
} ConfigSection;

static gboolean text_is_one_of(const char *value, const char *const *choices) {
  for (gsize i = 0; choices[i] != NULL; i++) {
    if (g_str_equal(value, choices[i]))
      return TRUE;
  }
  return FALSE;
}

void shaula_config_init_defaults(ShaulaConfig *config) {
  g_return_if_fail(config != NULL);
  memset(config, 0, sizeof(*config));
  g_strlcpy(config->region_capture_mode, "frozen",
            sizeof(config->region_capture_mode));
  g_strlcpy(config->save_folder, "~/Pictures/shaula",
            sizeof(config->save_folder));
  config->quick.copy_to_clipboard = TRUE;
  config->area.copy_to_clipboard = TRUE;
  config->fullscreen.skip_preview = TRUE;
  config->fullscreen.copy_to_clipboard = TRUE;
  config->fullscreen.save_to_folder = TRUE;
  config->all_screens.skip_preview = TRUE;
  config->all_screens.copy_to_clipboard = TRUE;
  config->all_screens.save_to_folder = TRUE;
  config->notifications_success = TRUE;
  config->notifications_errors = TRUE;
  config->notifications_thumbnails = TRUE;
  g_strlcpy(config->preview_mode, "floating", sizeof(config->preview_mode));
  config->preview_focused = TRUE;
  config->close_preview_on_save = TRUE;
  config->preview_width = 1100;
  config->preview_height = 720;
  g_strlcpy(config->column_display, "normal",
            sizeof(config->column_display));
  g_strlcpy(config->floating_relative_to, "top-left",
            sizeof(config->floating_relative_to));
}

char *shaula_config_path_new(void) {
  const char *explicit_path = g_getenv("SHAULA_CONFIG_FILE");
  if (explicit_path != NULL) {
    g_autofree char *trimmed = g_strdup(explicit_path);
    g_strstrip(trimmed);
    if (trimmed[0] != '\0')
      return g_steal_pointer(&trimmed);
  }

  const char *xdg = g_getenv("XDG_CONFIG_HOME");
  if (xdg != NULL && xdg[0] != '\0')
    return g_build_filename(xdg, "shaula", "config.toml", NULL);
  const char *home = g_getenv("HOME");
  if (home != NULL && home[0] != '\0')
    return g_build_filename(home, ".config", "shaula", "config.toml", NULL);
  return NULL;
}

static char *strip_comment(char *line) {
  gboolean in_string = FALSE;
  gboolean escaped = FALSE;
  for (char *cursor = line; *cursor != '\0'; cursor++) {
    if (escaped) {
      escaped = FALSE;
      continue;
    }
    if (*cursor == '\\' && in_string) {
      escaped = TRUE;
      continue;
    }
    if (*cursor == '"') {
      in_string = !in_string;
      continue;
    }
    if (*cursor == '#' && !in_string) {
      *cursor = '\0';
      break;
    }
  }
  return g_strstrip(line);
}

static gboolean parse_bool(const char *value, gboolean *out) {
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

static gboolean parse_quoted(const char *value, char *out, gsize capacity) {
  gsize length = strlen(value);
  if (length < 2 || value[0] != '"' || value[length - 1] != '"')
    return FALSE;
  if (length - 1 >= capacity)
    return FALSE;
  for (gsize i = 1; i + 1 < length; i++) {
    if (value[i] == '"' || value[i] == '\\')
      return FALSE;
  }
  memcpy(out, value + 1, length - 2);
  out[length - 2] = '\0';
  return TRUE;
}

static gboolean parse_u32(const char *value, guint32 *out) {
  if (value[0] == '\0' || value[0] == '-')
    return FALSE;
  errno = 0;
  char *end = NULL;
  unsigned long long parsed = strtoull(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed == 0 ||
      parsed > G_MAXUINT32)
    return FALSE;
  *out = (guint32)parsed;
  return TRUE;
}

static gboolean parse_i32(const char *value, gint32 *out) {
  if (value[0] == '\0')
    return FALSE;
  errno = 0;
  char *end = NULL;
  long long parsed = strtoll(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < G_MININT32 ||
      parsed > G_MAXINT32)
    return FALSE;
  *out = (gint32)parsed;
  return TRUE;
}

static gboolean section_from_name(const char *name, ConfigSection *section) {
  static const struct {
    const char *name;
    ConfigSection section;
  } sections[] = {
      {"capture", SECTION_CAPTURE},
      {"capture.after", SECTION_CAPTURE_AFTER},
      {"capture.after.quick", SECTION_QUICK},
      {"capture.after.area", SECTION_AREA},
      {"capture.after.fullscreen", SECTION_FULLSCREEN},
      {"capture.after.all_screens", SECTION_ALL_SCREENS},
      {"notifications", SECTION_NOTIFICATIONS},
      {"preview.window", SECTION_PREVIEW_WINDOW},
      {"preview.window.floating_position", SECTION_FLOATING_POSITION},
  };
  for (gsize i = 0; i < G_N_ELEMENTS(sections); i++) {
    if (g_str_equal(name, sections[i].name)) {
      *section = sections[i].section;
      return TRUE;
    }
  }
  return FALSE;
}

static ShaulaAfterModeConfig *mode_for_section(ShaulaConfig *config,
                                                ConfigSection section) {
  switch (section) {
  case SECTION_QUICK:
    return &config->quick;
  case SECTION_AREA:
    return &config->area;
  case SECTION_FULLSCREEN:
    return &config->fullscreen;
  case SECTION_ALL_SCREENS:
    return &config->all_screens;
  default:
    return NULL;
  }
}

static gboolean parse_mode_field(ShaulaAfterModeConfig *mode, const char *key,
                                 const char *value) {
  if (g_str_equal(key, "skip_preview"))
    return parse_bool(value, &mode->skip_preview);
  if (g_str_equal(key, "copy_to_clipboard"))
    return parse_bool(value, &mode->copy_to_clipboard);
  if (g_str_equal(key, "save_to_folder"))
    return parse_bool(value, &mode->save_to_folder);
  return FALSE;
}

static gboolean parse_field(ShaulaConfig *config, ConfigSection section,
                            const char *key, const char *value) {
  static const char *const region_modes[] = {"live", "frozen", NULL};
  static const char *const preview_modes[] = {
      "auto", "tiling", "floating", "maximized", "maximized-to-edges",
      "fullscreen", NULL};
  static const char *const column_modes[] = {"normal", "tabbed", NULL};
  static const char *const relative_modes[] = {
      "top-left", "top-right", "bottom-left", "bottom-right", "center", NULL};

  ShaulaAfterModeConfig *mode = mode_for_section(config, section);
  if (mode != NULL)
    return parse_mode_field(mode, key, value);

  if (section == SECTION_CAPTURE &&
      g_str_equal(key, "region_capture_mode")) {
    char parsed[sizeof(config->region_capture_mode)];
    if (!parse_quoted(value, parsed, sizeof(parsed)) ||
        !text_is_one_of(parsed, region_modes))
      return FALSE;
    g_strlcpy(config->region_capture_mode, parsed,
              sizeof(config->region_capture_mode));
    return TRUE;
  }
  if (section == SECTION_CAPTURE_AFTER && g_str_equal(key, "save_folder")) {
    char parsed[sizeof(config->save_folder)];
    if (!parse_quoted(value, parsed, sizeof(parsed)) ||
        strchr(parsed, '"') != NULL || strchr(parsed, '\\') != NULL)
      return FALSE;
    if (parsed[0] != '\0' && parsed[0] != '/' && parsed[0] != '~')
      return FALSE;
    g_strlcpy(config->save_folder, parsed, sizeof(config->save_folder));
    return TRUE;
  }
  if (section == SECTION_NOTIFICATIONS) {
    if (g_str_equal(key, "success"))
      return parse_bool(value, &config->notifications_success);
    if (g_str_equal(key, "errors"))
      return parse_bool(value, &config->notifications_errors);
    if (g_str_equal(key, "thumbnails"))
      return parse_bool(value, &config->notifications_thumbnails);
    return FALSE;
  }
  if (section == SECTION_PREVIEW_WINDOW) {
    if (g_str_equal(key, "mode")) {
      char parsed[sizeof(config->preview_mode)];
      if (!parse_quoted(value, parsed, sizeof(parsed)) ||
          !text_is_one_of(parsed, preview_modes))
        return FALSE;
      g_strlcpy(config->preview_mode, parsed, sizeof(config->preview_mode));
      return TRUE;
    }
    if (g_str_equal(key, "focused"))
      return parse_bool(value, &config->preview_focused);
    if (g_str_equal(key, "close_preview_on_save"))
      return parse_bool(value, &config->close_preview_on_save);
    if (g_str_equal(key, "width"))
      return parse_u32(value, &config->preview_width);
    if (g_str_equal(key, "height"))
      return parse_u32(value, &config->preview_height);
    if (g_str_equal(key, "default_column_display")) {
      char parsed[sizeof(config->column_display)];
      if (!parse_quoted(value, parsed, sizeof(parsed)) ||
          !text_is_one_of(parsed, column_modes))
        return FALSE;
      g_strlcpy(config->column_display, parsed, sizeof(config->column_display));
      return TRUE;
    }
    return FALSE;
  }
  if (section == SECTION_FLOATING_POSITION) {
    if (g_str_equal(key, "x")) {
      config->floating_x_set = parse_i32(value, &config->floating_x);
      return config->floating_x_set;
    }
    if (g_str_equal(key, "y")) {
      config->floating_y_set = parse_i32(value, &config->floating_y);
      return config->floating_y_set;
    }
    if (g_str_equal(key, "relative_to")) {
      char parsed[sizeof(config->floating_relative_to)];
      if (!parse_quoted(value, parsed, sizeof(parsed)) ||
          !text_is_one_of(parsed, relative_modes))
        return FALSE;
      g_strlcpy(config->floating_relative_to, parsed,
                sizeof(config->floating_relative_to));
      return TRUE;
    }
    return FALSE;
  }
  return FALSE;
}

gboolean shaula_config_validate(const ShaulaConfig *config) {
  g_return_val_if_fail(config != NULL, FALSE);
  const ShaulaAfterModeConfig modes[] = {config->quick, config->area,
                                         config->fullscreen,
                                         config->all_screens};
  for (gsize i = 0; i < G_N_ELEMENTS(modes); i++) {
    if (modes[i].skip_preview && !modes[i].copy_to_clipboard &&
        !modes[i].save_to_folder)
      return FALSE;
  }
  return TRUE;
}

ShaulaConfigStatus shaula_config_load(const char *path, ShaulaConfig *config,
                                      gboolean *loaded) {
  g_return_val_if_fail(config != NULL, SHAULA_CONFIG_STATUS_INVALID);
  shaula_config_init_defaults(config);
  if (loaded != NULL)
    *loaded = FALSE;
  if (path == NULL)
    return SHAULA_CONFIG_STATUS_OK;
  if (!g_file_test(path, G_FILE_TEST_EXISTS))
    return SHAULA_CONFIG_STATUS_OK;

  g_autofree char *contents = NULL;
  gsize length = 0;
  if (!g_file_get_contents(path, &contents, &length, NULL) || length > 64 * 1024)
    return SHAULA_CONFIG_STATUS_UNREADABLE;

  ConfigSection section = SECTION_ROOT;
  g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
  for (gsize i = 0; lines[i] != NULL; i++) {
    char *line = strip_comment(lines[i]);
    if (line[0] == '\0')
      continue;
    gsize line_length = strlen(line);
    if (line[0] == '[') {
      if (line_length < 3 || line[line_length - 1] != ']')
        return SHAULA_CONFIG_STATUS_INVALID;
      line[line_length - 1] = '\0';
      if (!section_from_name(g_strstrip(line + 1), &section))
        return SHAULA_CONFIG_STATUS_INVALID;
      continue;
    }
    char *equals = strchr(line, '=');
    if (equals == NULL || section == SECTION_ROOT)
      return SHAULA_CONFIG_STATUS_INVALID;
    *equals = '\0';
    char *key = g_strstrip(line);
    char *value = g_strstrip(equals + 1);
    if (key[0] == '\0' || value[0] == '\0' ||
        !parse_field(config, section, key, value))
      return SHAULA_CONFIG_STATUS_INVALID;
  }
  if (!shaula_config_validate(config))
    return SHAULA_CONFIG_STATUS_INVALID;
  if (loaded != NULL)
    *loaded = TRUE;
  return SHAULA_CONFIG_STATUS_OK;
}

char *shaula_config_serialize(const ShaulaConfig *config) {
  g_return_val_if_fail(config != NULL, NULL);
  g_autofree char *x_line =
      config->floating_x_set ? g_strdup_printf("x = %d\n", config->floating_x)
                             : g_strdup("");
  g_autofree char *y_line =
      config->floating_y_set ? g_strdup_printf("y = %d\n", config->floating_y)
                             : g_strdup("");
  return g_strdup_printf(
      "[capture]\nregion_capture_mode = \"%s\"\n\n"
      "[capture.after]\nsave_folder = \"%s\"\n\n"
      "[capture.after.quick]\nskip_preview = %s\ncopy_to_clipboard = %s\n"
      "save_to_folder = %s\n\n"
      "[capture.after.area]\nskip_preview = %s\ncopy_to_clipboard = %s\n"
      "save_to_folder = %s\n\n"
      "[capture.after.fullscreen]\nskip_preview = %s\n"
      "copy_to_clipboard = %s\nsave_to_folder = %s\n\n"
      "[capture.after.all_screens]\nskip_preview = %s\n"
      "copy_to_clipboard = %s\nsave_to_folder = %s\n\n"
      "[notifications]\nsuccess = %s\nerrors = %s\nthumbnails = %s\n\n"
      "[preview.window]\nmode = \"%s\"\nfocused = %s\n"
      "close_preview_on_save = %s\nwidth = %u\nheight = %u\n"
      "default_column_display = \"%s\"\n\n"
      "[preview.window.floating_position]\n%s%s"
      "relative_to = \"%s\"\n",
      config->region_capture_mode, config->save_folder,
      config->quick.skip_preview ? "true" : "false",
      config->quick.copy_to_clipboard ? "true" : "false",
      config->quick.save_to_folder ? "true" : "false",
      config->area.skip_preview ? "true" : "false",
      config->area.copy_to_clipboard ? "true" : "false",
      config->area.save_to_folder ? "true" : "false",
      config->fullscreen.skip_preview ? "true" : "false",
      config->fullscreen.copy_to_clipboard ? "true" : "false",
      config->fullscreen.save_to_folder ? "true" : "false",
      config->all_screens.skip_preview ? "true" : "false",
      config->all_screens.copy_to_clipboard ? "true" : "false",
      config->all_screens.save_to_folder ? "true" : "false",
      config->notifications_success ? "true" : "false",
      config->notifications_errors ? "true" : "false",
      config->notifications_thumbnails ? "true" : "false",
      config->preview_mode, config->preview_focused ? "true" : "false",
      config->close_preview_on_save ? "true" : "false", config->preview_width,
      config->preview_height, config->column_display, x_line, y_line,
      config->floating_relative_to);
}

ShaulaConfigStatus shaula_config_save(const char *path,
                                      const ShaulaConfig *config) {
  if (path == NULL || config == NULL || !shaula_config_validate(config))
    return SHAULA_CONFIG_STATUS_INVALID;
  g_autofree char *parent = g_path_get_dirname(path);
  if (g_mkdir_with_parents(parent, 0700) != 0)
    return SHAULA_CONFIG_STATUS_UNREADABLE;
  g_autofree char *contents = shaula_config_serialize(config);
  if (contents == NULL)
    return SHAULA_CONFIG_STATUS_UNREADABLE;

  if (g_file_test(path, G_FILE_TEST_EXISTS)) {
    g_autofree char *backup = g_strconcat(path, ".bak", NULL);
    g_autofree char *old = NULL;
    gsize old_length = 0;
    if (g_file_get_contents(path, &old, &old_length, NULL))
      (void)g_file_set_contents(backup, old, (gssize)old_length, NULL);
  }

  g_autofree char *temporary =
      g_strdup_printf("%s.tmp.%ld", path, (long)getpid());
  if (!g_file_set_contents(temporary, contents, -1, NULL))
    return SHAULA_CONFIG_STATUS_UNREADABLE;
  if (g_rename(temporary, path) != 0) {
    (void)g_unlink(temporary);
    return SHAULA_CONFIG_STATUS_UNREADABLE;
  }
  return SHAULA_CONFIG_STATUS_OK;
}
