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

typedef guint64 ConfigFieldMask;

typedef struct {
  const char *key;
  ConfigFieldMask bit;
} ConfigFieldSpec;

enum {
  FIELD_REGION_MODE = G_GUINT64_CONSTANT(1) << 0,
  FIELD_SAVE_FOLDER = G_GUINT64_CONSTANT(1) << 1,
  FIELD_SKIP_PREVIEW = G_GUINT64_CONSTANT(1) << 2,
  FIELD_COPY_TO_CLIPBOARD = G_GUINT64_CONSTANT(1) << 3,
  FIELD_SAVE_TO_FOLDER = G_GUINT64_CONSTANT(1) << 4,
  FIELD_NOTIFY_SUCCESS = G_GUINT64_CONSTANT(1) << 5,
  FIELD_NOTIFY_ERRORS = G_GUINT64_CONSTANT(1) << 6,
  FIELD_NOTIFY_THUMBNAILS = G_GUINT64_CONSTANT(1) << 7,
  FIELD_PREVIEW_MODE = G_GUINT64_CONSTANT(1) << 8,
  FIELD_PREVIEW_FOCUSED = G_GUINT64_CONSTANT(1) << 9,
  FIELD_CLOSE_ON_SAVE = G_GUINT64_CONSTANT(1) << 10,
  FIELD_PREVIEW_WIDTH = G_GUINT64_CONSTANT(1) << 11,
  FIELD_PREVIEW_HEIGHT = G_GUINT64_CONSTANT(1) << 12,
  FIELD_COLUMN_DISPLAY = G_GUINT64_CONSTANT(1) << 13,
  FIELD_FLOATING_X = G_GUINT64_CONSTANT(1) << 14,
  FIELD_FLOATING_Y = G_GUINT64_CONSTANT(1) << 15,
  FIELD_FLOATING_RELATIVE = G_GUINT64_CONSTANT(1) << 16,
};

static const ConfigFieldSpec capture_fields[] = {
    {"region_capture_mode", FIELD_REGION_MODE},
};
static const ConfigFieldSpec capture_after_fields[] = {
    {"save_folder", FIELD_SAVE_FOLDER},
};
static const ConfigFieldSpec mode_fields[] = {
    {"skip_preview", FIELD_SKIP_PREVIEW},
    {"copy_to_clipboard", FIELD_COPY_TO_CLIPBOARD},
    {"save_to_folder", FIELD_SAVE_TO_FOLDER},
};
static const ConfigFieldSpec notification_fields[] = {
    {"success", FIELD_NOTIFY_SUCCESS},
    {"errors", FIELD_NOTIFY_ERRORS},
    {"thumbnails", FIELD_NOTIFY_THUMBNAILS},
};
static const ConfigFieldSpec preview_fields[] = {
    {"mode", FIELD_PREVIEW_MODE},
    {"focused", FIELD_PREVIEW_FOCUSED},
    {"close_preview_on_save", FIELD_CLOSE_ON_SAVE},
    {"width", FIELD_PREVIEW_WIDTH},
    {"height", FIELD_PREVIEW_HEIGHT},
    {"default_column_display", FIELD_COLUMN_DISPLAY},
};
static const ConfigFieldSpec floating_fields[] = {
    {"x", FIELD_FLOATING_X},
    {"y", FIELD_FLOATING_Y},
    {"relative_to", FIELD_FLOATING_RELATIVE},
};

static const ConfigFieldSpec *fields_for_section(ConfigSection section,
                                                  gsize *count) {
  switch (section) {
  case SECTION_CAPTURE:
    *count = G_N_ELEMENTS(capture_fields);
    return capture_fields;
  case SECTION_CAPTURE_AFTER:
    *count = G_N_ELEMENTS(capture_after_fields);
    return capture_after_fields;
  case SECTION_QUICK:
  case SECTION_AREA:
  case SECTION_FULLSCREEN:
  case SECTION_ALL_SCREENS:
    *count = G_N_ELEMENTS(mode_fields);
    return mode_fields;
  case SECTION_NOTIFICATIONS:
    *count = G_N_ELEMENTS(notification_fields);
    return notification_fields;
  case SECTION_PREVIEW_WINDOW:
    *count = G_N_ELEMENTS(preview_fields);
    return preview_fields;
  case SECTION_FLOATING_POSITION:
    *count = G_N_ELEMENTS(floating_fields);
    return floating_fields;
  case SECTION_ROOT:
    break;
  }
  *count = 0;
  return NULL;
}

static const char *section_name(ConfigSection section) {
  switch (section) {
  case SECTION_CAPTURE:
    return "capture";
  case SECTION_CAPTURE_AFTER:
    return "capture.after";
  case SECTION_QUICK:
    return "capture.after.quick";
  case SECTION_AREA:
    return "capture.after.area";
  case SECTION_FULLSCREEN:
    return "capture.after.fullscreen";
  case SECTION_ALL_SCREENS:
    return "capture.after.all_screens";
  case SECTION_NOTIFICATIONS:
    return "notifications";
  case SECTION_PREVIEW_WINDOW:
    return "preview.window";
  case SECTION_FLOATING_POSITION:
    return "preview.window.floating_position";
  case SECTION_ROOT:
    return NULL;
  }
  return NULL;
}

static const ShaulaAfterModeConfig *const_mode_for_section(
    const ShaulaConfig *config, ConfigSection section) {
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

static char *field_value_new(const ShaulaConfig *config, ConfigSection section,
                             const char *key) {
  const ShaulaAfterModeConfig *mode =
      const_mode_for_section(config, section);
  if (mode != NULL) {
    if (g_str_equal(key, "skip_preview"))
      return g_strdup(mode->skip_preview ? "true" : "false");
    if (g_str_equal(key, "copy_to_clipboard"))
      return g_strdup(mode->copy_to_clipboard ? "true" : "false");
    if (g_str_equal(key, "save_to_folder"))
      return g_strdup(mode->save_to_folder ? "true" : "false");
  }
  if (section == SECTION_CAPTURE &&
      g_str_equal(key, "region_capture_mode"))
    return g_strdup_printf("\"%s\"", config->region_capture_mode);
  if (section == SECTION_CAPTURE_AFTER && g_str_equal(key, "save_folder"))
    return g_strdup_printf("\"%s\"", config->save_folder);
  if (section == SECTION_NOTIFICATIONS) {
    if (g_str_equal(key, "success"))
      return g_strdup(config->notifications_success ? "true" : "false");
    if (g_str_equal(key, "errors"))
      return g_strdup(config->notifications_errors ? "true" : "false");
    if (g_str_equal(key, "thumbnails"))
      return g_strdup(config->notifications_thumbnails ? "true" : "false");
  }
  if (section == SECTION_PREVIEW_WINDOW) {
    if (g_str_equal(key, "mode"))
      return g_strdup_printf("\"%s\"", config->preview_mode);
    if (g_str_equal(key, "focused"))
      return g_strdup(config->preview_focused ? "true" : "false");
    if (g_str_equal(key, "close_preview_on_save"))
      return g_strdup(config->close_preview_on_save ? "true" : "false");
    if (g_str_equal(key, "width"))
      return g_strdup_printf("%u", config->preview_width);
    if (g_str_equal(key, "height"))
      return g_strdup_printf("%u", config->preview_height);
    if (g_str_equal(key, "default_column_display"))
      return g_strdup_printf("\"%s\"", config->column_display);
  }
  if (section == SECTION_FLOATING_POSITION) {
    if (g_str_equal(key, "x"))
      return config->floating_x_set ? g_strdup_printf("%d", config->floating_x)
                                    : NULL;
    if (g_str_equal(key, "y"))
      return config->floating_y_set ? g_strdup_printf("%d", config->floating_y)
                                    : NULL;
    if (g_str_equal(key, "relative_to"))
      return g_strdup_printf("\"%s\"", config->floating_relative_to);
  }
  return NULL;
}

static ConfigFieldMask field_bit(ConfigSection section, const char *key) {
  gsize count = 0;
  const ConfigFieldSpec *fields = fields_for_section(section, &count);
  for (gsize i = 0; i < count; i++) {
    if (g_str_equal(fields[i].key, key))
      return fields[i].bit;
  }
  return 0;
}

static void ensure_line_boundary(GString *output) {
  if (output->len > 0 && output->str[output->len - 1] != '\n')
    g_string_append_c(output, '\n');
}

static void append_missing_fields(GString *output, const ShaulaConfig *config,
                                  ConfigSection section,
                                  ConfigFieldMask seen_fields) {
  gsize count = 0;
  const ConfigFieldSpec *fields = fields_for_section(section, &count);
  if (fields == NULL)
    return;
  for (gsize i = 0; i < count; i++) {
    if ((seen_fields & fields[i].bit) != 0)
      continue;
    g_autofree char *value =
        field_value_new(config, section, fields[i].key);
    if (value == NULL)
      continue;
    ensure_line_boundary(output);
    g_string_append_printf(output, "%s = %s\n", fields[i].key, value);
  }
}

static gboolean section_from_line(const char *line, ConfigSection *section) {
  g_autofree char *copy = g_strdup(line);
  char *trimmed = strip_comment(copy);
  gsize length = strlen(trimmed);
  if (length < 3 || trimmed[0] != '[' || trimmed[length - 1] != ']')
    return FALSE;
  trimmed[length - 1] = '\0';
  return section_from_name(g_strstrip(trimmed + 1), section);
}

static const char *inline_comment_start(const char *value) {
  gboolean in_string = FALSE;
  gboolean escaped = FALSE;
  for (const char *cursor = value; *cursor != '\0'; cursor++) {
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
    if (*cursor == '#' && !in_string)
      return cursor;
  }
  return NULL;
}

static char *patch_config_text(const char *current,
                               const ShaulaConfig *config) {
  static const ConfigSection section_order[] = {
      SECTION_CAPTURE,     SECTION_CAPTURE_AFTER, SECTION_QUICK,
      SECTION_AREA,        SECTION_FULLSCREEN,    SECTION_ALL_SCREENS,
      SECTION_NOTIFICATIONS, SECTION_PREVIEW_WINDOW,
      SECTION_FLOATING_POSITION,
  };
  const gboolean had_trailing_newline = g_str_has_suffix(current, "\n");
  g_auto(GStrv) lines = g_strsplit(current, "\n", -1);
  gsize line_count = g_strv_length(lines);
  GString *output = g_string_new(NULL);
  ConfigSection section = SECTION_ROOT;
  ConfigFieldMask seen_fields = 0;
  ConfigFieldMask seen_sections = 0;

  for (gsize i = 0; i < line_count; i++) {
    if (i + 1 == line_count && had_trailing_newline && lines[i][0] == '\0')
      break;
    ConfigSection next_section = SECTION_ROOT;
    if (section_from_line(lines[i], &next_section)) {
      append_missing_fields(output, config, section, seen_fields);
      section = next_section;
      seen_fields = 0;
      seen_sections |= G_GUINT64_CONSTANT(1) << (guint)section;
      g_string_append(output, lines[i]);
    } else {
      const char *equals = strchr(lines[i], '=');
      gboolean replaced = FALSE;
      if (equals != NULL && section != SECTION_ROOT) {
        g_autofree char *key_text = g_strndup(lines[i], (gsize)(equals - lines[i]));
        char *key = g_strstrip(key_text);
        ConfigFieldMask bit = field_bit(section, key);
        if (bit != 0) {
          g_autofree char *value = field_value_new(config, section, key);
          seen_fields |= bit;
          if (value == NULL) {
            replaced = TRUE;
          } else {
            const char *value_start = equals + 1;
            while (*value_start == ' ' || *value_start == '\t')
              value_start++;
            const char *comment = inline_comment_start(value_start);
            const char *value_end =
                comment != NULL ? comment : lines[i] + strlen(lines[i]);
            while (value_end > value_start &&
                   g_ascii_isspace((guchar)value_end[-1]))
              value_end--;
            g_string_append_len(output, lines[i],
                                (gssize)(value_start - lines[i]));
            g_string_append(output, value);
            g_string_append(output, value_end);
            replaced = TRUE;
          }
        }
      }
      if (!replaced)
        g_string_append(output, lines[i]);
    }
    if (i + 1 < line_count || had_trailing_newline)
      g_string_append_c(output, '\n');
  }
  append_missing_fields(output, config, section, seen_fields);

  for (gsize i = 0; i < G_N_ELEMENTS(section_order); i++) {
    ConfigSection missing = section_order[i];
    if ((seen_sections & (G_GUINT64_CONSTANT(1) << (guint)missing)) != 0)
      continue;
    ensure_line_boundary(output);
    if (output->len > 1 && output->str[output->len - 2] != '\n')
      g_string_append_c(output, '\n');
    g_string_append_printf(output, "[%s]\n", section_name(missing));
    append_missing_fields(output, config, missing, 0);
  }
  return g_string_free(output, FALSE);
}

static char *backup_path_new(const char *path) {
  const gint64 timestamp = (gint64)time(NULL);
  for (guint attempt = 0; attempt < 100; attempt++) {
    g_autofree char *suffix =
        attempt == 0 ? g_strdup("") : g_strdup_printf("-%u", attempt);
    char *candidate =
        g_strdup_printf("%s.shaula-backup-%" G_GINT64_FORMAT "%s", path,
                        timestamp, suffix);
    if (!g_file_test(candidate, G_FILE_TEST_EXISTS))
      return candidate;
    g_free(candidate);
  }
  return NULL;
}

ShaulaConfigStatus shaula_config_save(const char *path,
                                      const ShaulaConfig *config) {
  if (path == NULL || config == NULL || !shaula_config_validate(config))
    return SHAULA_CONFIG_STATUS_INVALID;
  g_autofree char *parent = g_path_get_dirname(path);
  if (g_mkdir_with_parents(parent, 0700) != 0)
    return SHAULA_CONFIG_STATUS_UNREADABLE;

  const gboolean exists = g_file_test(path, G_FILE_TEST_EXISTS);
  g_autofree char *old = NULL;
  gsize old_length = 0;
  if (exists && !g_file_get_contents(path, &old, &old_length, NULL))
    return SHAULA_CONFIG_STATUS_UNREADABLE;
  g_autofree char *contents =
      exists ? patch_config_text(old, config) : shaula_config_serialize(config);
  if (contents == NULL)
    return SHAULA_CONFIG_STATUS_UNREADABLE;
  if (exists && g_str_equal(old, contents))
    return SHAULA_CONFIG_STATUS_OK;

  if (exists) {
    g_autofree char *backup = backup_path_new(path);
    if (backup == NULL ||
        !g_file_set_contents(backup, old, (gssize)old_length, NULL))
      return SHAULA_CONFIG_STATUS_UNREADABLE;
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
