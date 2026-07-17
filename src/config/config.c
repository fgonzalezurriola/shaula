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

typedef guint64 ConfigFieldMask;

typedef struct {
  const char *name;
  ConfigSection section;
} ConfigSectionSpec;

typedef struct {
  ConfigSection section;
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

static const ConfigSectionSpec CONFIG_SECTIONS[] = {
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

/* This ordered schema is shared by parsing, canonical serialization, and
 * comment-preserving updates so config keys cannot drift between paths.
 */
static const ConfigFieldSpec CONFIG_FIELDS[] = {
    {SECTION_CAPTURE, "region_capture_mode", FIELD_REGION_MODE},
    {SECTION_CAPTURE_AFTER, "save_folder", FIELD_SAVE_FOLDER},
    {SECTION_QUICK, "skip_preview", FIELD_SKIP_PREVIEW},
    {SECTION_QUICK, "copy_to_clipboard", FIELD_COPY_TO_CLIPBOARD},
    {SECTION_QUICK, "save_to_folder", FIELD_SAVE_TO_FOLDER},
    {SECTION_AREA, "skip_preview", FIELD_SKIP_PREVIEW},
    {SECTION_AREA, "copy_to_clipboard", FIELD_COPY_TO_CLIPBOARD},
    {SECTION_AREA, "save_to_folder", FIELD_SAVE_TO_FOLDER},
    {SECTION_FULLSCREEN, "skip_preview", FIELD_SKIP_PREVIEW},
    {SECTION_FULLSCREEN, "copy_to_clipboard", FIELD_COPY_TO_CLIPBOARD},
    {SECTION_FULLSCREEN, "save_to_folder", FIELD_SAVE_TO_FOLDER},
    {SECTION_ALL_SCREENS, "skip_preview", FIELD_SKIP_PREVIEW},
    {SECTION_ALL_SCREENS, "copy_to_clipboard", FIELD_COPY_TO_CLIPBOARD},
    {SECTION_ALL_SCREENS, "save_to_folder", FIELD_SAVE_TO_FOLDER},
    {SECTION_NOTIFICATIONS, "success", FIELD_NOTIFY_SUCCESS},
    {SECTION_NOTIFICATIONS, "errors", FIELD_NOTIFY_ERRORS},
    {SECTION_NOTIFICATIONS, "thumbnails", FIELD_NOTIFY_THUMBNAILS},
    {SECTION_PREVIEW_WINDOW, "mode", FIELD_PREVIEW_MODE},
    {SECTION_PREVIEW_WINDOW, "focused", FIELD_PREVIEW_FOCUSED},
    {SECTION_PREVIEW_WINDOW, "close_preview_on_save", FIELD_CLOSE_ON_SAVE},
    {SECTION_PREVIEW_WINDOW, "width", FIELD_PREVIEW_WIDTH},
    {SECTION_PREVIEW_WINDOW, "height", FIELD_PREVIEW_HEIGHT},
    {SECTION_PREVIEW_WINDOW, "default_column_display", FIELD_COLUMN_DISPLAY},
    {SECTION_FLOATING_POSITION, "x", FIELD_FLOATING_X},
    {SECTION_FLOATING_POSITION, "y", FIELD_FLOATING_Y},
    {SECTION_FLOATING_POSITION, "relative_to", FIELD_FLOATING_RELATIVE},
};

static const ConfigFieldSpec *field_spec(ConfigSection section,
                                         const char *key) {
  for (gsize i = 0; i < G_N_ELEMENTS(CONFIG_FIELDS); i++) {
    if (CONFIG_FIELDS[i].section == section &&
        g_str_equal(CONFIG_FIELDS[i].key, key))
      return &CONFIG_FIELDS[i];
  }
  return NULL;
}

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
  for (gsize i = 0; i < G_N_ELEMENTS(CONFIG_SECTIONS); i++) {
    if (g_str_equal(name, CONFIG_SECTIONS[i].name)) {
      *section = CONFIG_SECTIONS[i].section;
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

static gboolean parse_mode_field(ShaulaAfterModeConfig *mode,
                                 ConfigFieldMask field, const char *value) {
  if (field == FIELD_SKIP_PREVIEW)
    return parse_bool(value, &mode->skip_preview);
  if (field == FIELD_COPY_TO_CLIPBOARD)
    return parse_bool(value, &mode->copy_to_clipboard);
  if (field == FIELD_SAVE_TO_FOLDER)
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

  const ConfigFieldSpec *field = field_spec(section, key);
  if (field == NULL)
    return FALSE;

  ShaulaAfterModeConfig *mode = mode_for_section(config, section);
  if (mode != NULL)
    return parse_mode_field(mode, field->bit, value);

  if (field->bit == FIELD_REGION_MODE) {
    char parsed[sizeof(config->region_capture_mode)];
    if (!parse_quoted(value, parsed, sizeof(parsed)) ||
        !text_is_one_of(parsed, region_modes))
      return FALSE;
    g_strlcpy(config->region_capture_mode, parsed,
              sizeof(config->region_capture_mode));
    return TRUE;
  }
  if (field->bit == FIELD_SAVE_FOLDER) {
    char parsed[sizeof(config->save_folder)];
    if (!parse_quoted(value, parsed, sizeof(parsed)) ||
        strchr(parsed, '"') != NULL || strchr(parsed, '\\') != NULL)
      return FALSE;
    if (parsed[0] != '\0' && parsed[0] != '/' && parsed[0] != '~')
      return FALSE;
    g_strlcpy(config->save_folder, parsed, sizeof(config->save_folder));
    return TRUE;
  }
  if (field->bit == FIELD_NOTIFY_SUCCESS)
    return parse_bool(value, &config->notifications_success);
  if (field->bit == FIELD_NOTIFY_ERRORS)
    return parse_bool(value, &config->notifications_errors);
  if (field->bit == FIELD_NOTIFY_THUMBNAILS)
    return parse_bool(value, &config->notifications_thumbnails);
  if (field->bit == FIELD_PREVIEW_MODE) {
    char parsed[sizeof(config->preview_mode)];
    if (!parse_quoted(value, parsed, sizeof(parsed)) ||
        !text_is_one_of(parsed, preview_modes))
      return FALSE;
    g_strlcpy(config->preview_mode, parsed, sizeof(config->preview_mode));
    return TRUE;
  }
  if (field->bit == FIELD_PREVIEW_FOCUSED)
    return parse_bool(value, &config->preview_focused);
  if (field->bit == FIELD_CLOSE_ON_SAVE)
    return parse_bool(value, &config->close_preview_on_save);
  if (field->bit == FIELD_PREVIEW_WIDTH)
    return parse_u32(value, &config->preview_width);
  if (field->bit == FIELD_PREVIEW_HEIGHT)
    return parse_u32(value, &config->preview_height);
  if (field->bit == FIELD_COLUMN_DISPLAY) {
    char parsed[sizeof(config->column_display)];
    if (!parse_quoted(value, parsed, sizeof(parsed)) ||
        !text_is_one_of(parsed, column_modes))
      return FALSE;
    g_strlcpy(config->column_display, parsed, sizeof(config->column_display));
    return TRUE;
  }
  if (field->bit == FIELD_FLOATING_X) {
    config->floating_x_set = parse_i32(value, &config->floating_x);
    return config->floating_x_set;
  }
  if (field->bit == FIELD_FLOATING_Y) {
    config->floating_y_set = parse_i32(value, &config->floating_y);
    return config->floating_y_set;
  }
  if (field->bit == FIELD_FLOATING_RELATIVE) {
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

static const char *section_name(ConfigSection section) {
  for (gsize i = 0; i < G_N_ELEMENTS(CONFIG_SECTIONS); i++) {
    if (CONFIG_SECTIONS[i].section == section)
      return CONFIG_SECTIONS[i].name;
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
  const ConfigFieldSpec *field = field_spec(section, key);
  if (field == NULL)
    return NULL;
  const ShaulaAfterModeConfig *mode =
      const_mode_for_section(config, section);
  if (mode != NULL) {
    if (field->bit == FIELD_SKIP_PREVIEW)
      return g_strdup(mode->skip_preview ? "true" : "false");
    if (field->bit == FIELD_COPY_TO_CLIPBOARD)
      return g_strdup(mode->copy_to_clipboard ? "true" : "false");
    if (field->bit == FIELD_SAVE_TO_FOLDER)
      return g_strdup(mode->save_to_folder ? "true" : "false");
  }
  if (field->bit == FIELD_REGION_MODE)
    return g_strdup_printf("\"%s\"", config->region_capture_mode);
  if (field->bit == FIELD_SAVE_FOLDER)
    return g_strdup_printf("\"%s\"", config->save_folder);
  if (field->bit == FIELD_NOTIFY_SUCCESS)
    return g_strdup(config->notifications_success ? "true" : "false");
  if (field->bit == FIELD_NOTIFY_ERRORS)
    return g_strdup(config->notifications_errors ? "true" : "false");
  if (field->bit == FIELD_NOTIFY_THUMBNAILS)
    return g_strdup(config->notifications_thumbnails ? "true" : "false");
  if (field->bit == FIELD_PREVIEW_MODE)
    return g_strdup_printf("\"%s\"", config->preview_mode);
  if (field->bit == FIELD_PREVIEW_FOCUSED)
    return g_strdup(config->preview_focused ? "true" : "false");
  if (field->bit == FIELD_CLOSE_ON_SAVE)
    return g_strdup(config->close_preview_on_save ? "true" : "false");
  if (field->bit == FIELD_PREVIEW_WIDTH)
    return g_strdup_printf("%u", config->preview_width);
  if (field->bit == FIELD_PREVIEW_HEIGHT)
    return g_strdup_printf("%u", config->preview_height);
  if (field->bit == FIELD_COLUMN_DISPLAY)
    return g_strdup_printf("\"%s\"", config->column_display);
  if (field->bit == FIELD_FLOATING_X)
    return config->floating_x_set ? g_strdup_printf("%d", config->floating_x)
                                  : NULL;
  if (field->bit == FIELD_FLOATING_Y)
    return config->floating_y_set ? g_strdup_printf("%d", config->floating_y)
                                  : NULL;
  if (field->bit == FIELD_FLOATING_RELATIVE)
    return g_strdup_printf("\"%s\"", config->floating_relative_to);
  return NULL;
}

static ConfigFieldMask field_bit(ConfigSection section, const char *key) {
  const ConfigFieldSpec *field = field_spec(section, key);
  return field != NULL ? field->bit : 0;
}

static void ensure_line_boundary(GString *output) {
  if (output->len > 0 && output->str[output->len - 1] != '\n')
    g_string_append_c(output, '\n');
}

static void append_missing_fields(GString *output, const ShaulaConfig *config,
                                  ConfigSection section,
                                  ConfigFieldMask seen_fields) {
  for (gsize i = 0; i < G_N_ELEMENTS(CONFIG_FIELDS); i++) {
    const ConfigFieldSpec *field = &CONFIG_FIELDS[i];
    if (field->section != section || (seen_fields & field->bit) != 0)
      continue;
    g_autofree char *value = field_value_new(config, section, field->key);
    if (value == NULL)
      continue;
    ensure_line_boundary(output);
    g_string_append_printf(output, "%s = %s\n", field->key, value);
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

  for (gsize i = 0; i < G_N_ELEMENTS(CONFIG_SECTIONS); i++) {
    ConfigSection missing = CONFIG_SECTIONS[i].section;
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

char *shaula_config_serialize(const ShaulaConfig *config) {
  g_return_val_if_fail(config != NULL, NULL);
  return patch_config_text("", config);
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
