#include "preview_tool_defaults.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include <glib/gstdio.h>

#define SHAULA_TOOL_DEFAULTS_VERSION 1
#define SHAULA_TOOL_DEFAULTS_SAVE_DELAY_MS 500

typedef enum {
  SHAULA_PREF_COLOR,
  SHAULA_PREF_DOUBLE,
  SHAULA_PREF_BOOLEAN,
  SHAULA_PREF_ENUM,
} ShaulaPreferenceType;

typedef struct {
  const char *name;
  int value;
} ShaulaEnumValue;

typedef struct {
  const char *group;
  const char *key;
  ShaulaPreferenceType type;
  size_t offset;
  guint dirty_group;
  double minimum;
  double maximum;
  const ShaulaEnumValue *enum_values;
} ShaulaPreferenceSpec;

#define PREF_OFFSET(group_type, group_member, field)                           \
  (offsetof(ShaulaPreviewToolDefaults, group_member) +                         \
   offsetof(group_type, field))
#define PREF_COLOR(group, key, group_type, group_member, field, dirty)         \
  {group, key, SHAULA_PREF_COLOR,                                              \
   PREF_OFFSET(group_type, group_member, field), dirty, 0.0, 0.0, NULL}
#define PREF_DOUBLE(group, key, group_type, group_member, field, dirty, min,   \
                    max)                                                       \
  {group, key, SHAULA_PREF_DOUBLE,                                             \
   PREF_OFFSET(group_type, group_member, field), dirty, min, max, NULL}
#define PREF_BOOLEAN(group, key, group_type, group_member, field, dirty)       \
  {group, key, SHAULA_PREF_BOOLEAN,                                            \
   PREF_OFFSET(group_type, group_member, field), dirty, 0.0, 0.0, NULL}
#define PREF_ENUM(group, key, group_type, group_member, field, dirty, values)  \
  {group, key, SHAULA_PREF_ENUM,                                               \
   PREF_OFFSET(group_type, group_member, field), dirty, 0.0, 0.0, values}

static const ShaulaEnumValue stroke_styles[] = {
    {"solid", PREVIEW_ARROW_STROKE_SOLID},
    {"dashed", PREVIEW_ARROW_STROKE_DASHED},
    {"dotted", PREVIEW_ARROW_STROKE_DOTTED},
    {NULL, 0},
};

static const ShaulaEnumValue rectangle_styles[] = {
    {"solid", PREVIEW_ARROW_STROKE_SOLID},
    {"dashed", PREVIEW_ARROW_STROKE_DASHED},
    {NULL, 0},
};

static const ShaulaEnumValue rectangle_corners[] = {
    {"rounded", PREVIEW_RECTANGLE_CORNERS_ROUNDED},
    {"square", PREVIEW_RECTANGLE_CORNERS_SQUARE},
    {NULL, 0},
};

static const ShaulaEnumValue text_alignments[] = {
    {"left", SHAULA_TEXT_ALIGN_LEFT},
    {"center", SHAULA_TEXT_ALIGN_CENTER},
    {"right", SHAULA_TEXT_ALIGN_RIGHT},
    {NULL, 0},
};

static const ShaulaEnumValue text_font_modes[] = {
    {"normal", SHAULA_TEXT_FONT_NORMAL},
    {"sketch", SHAULA_TEXT_FONT_SKETCH},
    {NULL, 0},
};

static const ShaulaEnumValue spotlight_shapes[] = {
    {"sharp", SHAULA_SPOTLIGHT_SHAPE_SHARP_RECTANGLE},
    {"rounded", SHAULA_SPOTLIGHT_SHAPE_ROUNDED_RECTANGLE},
    {NULL, 0},
};

static const ShaulaPreferenceSpec preferences[] = {
    PREF_COLOR("arrow-line", "color", ShaulaStrokeToolDefaults, arrow_line,
               color, SHAULA_TOOL_DEFAULTS_DIRTY_ARROW_LINE),
    PREF_DOUBLE("arrow-line", "stroke-width", ShaulaStrokeToolDefaults,
                arrow_line, stroke_width,
                SHAULA_TOOL_DEFAULTS_DIRTY_ARROW_LINE, 1.0, 12.0),
    PREF_ENUM("arrow-line", "stroke-style", ShaulaStrokeToolDefaults,
              arrow_line, stroke_style,
              SHAULA_TOOL_DEFAULTS_DIRTY_ARROW_LINE, stroke_styles),

    PREF_COLOR("rectangle", "color", ShaulaRectangleToolDefaults, rectangle,
               color, SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE),
    PREF_DOUBLE("rectangle", "stroke-width", ShaulaRectangleToolDefaults,
                rectangle, stroke_width,
                SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE, 1.0, 12.0),
    PREF_ENUM("rectangle", "stroke-style", ShaulaRectangleToolDefaults,
              rectangle, stroke_style,
              SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE, rectangle_styles),
    PREF_BOOLEAN("rectangle", "filled", ShaulaRectangleToolDefaults,
                 rectangle, filled, SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE),
    PREF_ENUM("rectangle", "corners", ShaulaRectangleToolDefaults, rectangle,
              corners, SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE,
              rectangle_corners),

    PREF_COLOR("pen", "color", ShaulaFreehandToolDefaults, pen, color,
               SHAULA_TOOL_DEFAULTS_DIRTY_PEN),
    PREF_DOUBLE("pen", "stroke-width", ShaulaFreehandToolDefaults, pen,
                stroke_width, SHAULA_TOOL_DEFAULTS_DIRTY_PEN, 1.0, 24.0),
    PREF_DOUBLE("pen", "opacity", ShaulaFreehandToolDefaults, pen, opacity,
                SHAULA_TOOL_DEFAULTS_DIRTY_PEN, 0.1, 1.0),

    PREF_COLOR("highlight", "color", ShaulaFreehandToolDefaults, highlight,
               color, SHAULA_TOOL_DEFAULTS_DIRTY_HIGHLIGHT),
    PREF_DOUBLE("highlight", "stroke-width", ShaulaFreehandToolDefaults,
                highlight, stroke_width,
                SHAULA_TOOL_DEFAULTS_DIRTY_HIGHLIGHT, 4.0, 48.0),
    PREF_DOUBLE("highlight", "opacity", ShaulaFreehandToolDefaults, highlight,
                opacity, SHAULA_TOOL_DEFAULTS_DIRTY_HIGHLIGHT, 0.05, 1.0),

    PREF_COLOR("text", "color", ShaulaTextToolDefaults, text, color,
               SHAULA_TOOL_DEFAULTS_DIRTY_TEXT),
    PREF_DOUBLE("text", "font-size", ShaulaTextToolDefaults, text, font_size,
                SHAULA_TOOL_DEFAULTS_DIRTY_TEXT, SHAULA_TEXT_FONT_SIZE_MIN,
                SHAULA_TEXT_FONT_SIZE_MAX),
    PREF_ENUM("text", "font-mode", ShaulaTextToolDefaults, text, font_mode,
              SHAULA_TOOL_DEFAULTS_DIRTY_TEXT, text_font_modes),
    PREF_ENUM("text", "alignment", ShaulaTextToolDefaults, text, align,
              SHAULA_TOOL_DEFAULTS_DIRTY_TEXT, text_alignments),

    PREF_COLOR("measure", "color", ShaulaMeasureToolDefaults, measure, color,
               SHAULA_TOOL_DEFAULTS_DIRTY_MEASURE),
    PREF_DOUBLE("measure", "stroke-width", ShaulaMeasureToolDefaults, measure,
                stroke_width, SHAULA_TOOL_DEFAULTS_DIRTY_MEASURE, 1.0, 8.0),

    PREF_COLOR("spotlight", "border-color", ShaulaSpotlightToolDefaults,
               spotlight, border_color,
               SHAULA_TOOL_DEFAULTS_DIRTY_SPOTLIGHT),
    PREF_DOUBLE("spotlight", "border-width", ShaulaSpotlightToolDefaults,
                spotlight, border_width,
                SHAULA_TOOL_DEFAULTS_DIRTY_SPOTLIGHT, 0.0, 16.0),
    PREF_ENUM("spotlight", "shape", ShaulaSpotlightToolDefaults, spotlight,
              shape, SHAULA_TOOL_DEFAULTS_DIRTY_SPOTLIGHT, spotlight_shapes),

    PREF_DOUBLE("eraser", "size", ShaulaEraserToolDefaults, eraser, size,
                SHAULA_TOOL_DEFAULTS_DIRTY_ERASER, SHAULA_ERASER_SIZE_MIN,
                SHAULA_ERASER_SIZE_MAX),
};

static ShaulaColor orange(void) {
  return (ShaulaColor){0xFD / 255.0, 0x76 / 255.0, 0x03 / 255.0, 1.0};
}

static void init_builtin_defaults(ShaulaPreviewToolDefaults *defaults) {
  memset(defaults, 0, sizeof(*defaults));
  defaults->arrow_line = (ShaulaStrokeToolDefaults){
      orange(), 3.5, PREVIEW_ARROW_STROKE_SOLID};
  defaults->rectangle = (ShaulaRectangleToolDefaults){
      orange(), 3.5, PREVIEW_ARROW_STROKE_DASHED, FALSE,
      PREVIEW_RECTANGLE_CORNERS_ROUNDED};
  defaults->pen = (ShaulaFreehandToolDefaults){orange(), 3.0, 1.0};
  defaults->highlight = (ShaulaFreehandToolDefaults){
      {0xFF / 255.0, 0xD7 / 255.0, 0x5A / 255.0, 0.30}, 18.0, 0.30};
  defaults->text = (ShaulaTextToolDefaults){
      orange(), 24.0, SHAULA_TEXT_ALIGN_LEFT, SHAULA_TEXT_FONT_NORMAL};
  defaults->measure = (ShaulaMeasureToolDefaults){orange(), 2.0};
  defaults->spotlight = (ShaulaSpotlightToolDefaults){
      orange(), 3.0, SHAULA_SPOTLIGHT_SHAPE_SHARP_RECTANGLE};
  defaults->eraser.size = SHAULA_ERASER_SIZE_DEFAULT;
}

static char *state_path(gboolean create_parent) {
  const char *state_dir = g_get_user_state_dir();
  if (state_dir == NULL || state_dir[0] == '\0')
    return NULL;
  char *dir = g_build_filename(state_dir, "shaula", NULL);
  if (create_parent && g_mkdir_with_parents(dir, 0700) != 0) {
    g_free(dir);
    return NULL;
  }
  char *path = g_build_filename(dir, "preview-tool-hud.ini", NULL);
  g_free(dir);
  return path;
}

static gboolean parse_color(const char *text, ShaulaColor *color) {
  unsigned int r = 0, g = 0, b = 0;
  if (text == NULL || strlen(text) != 7 || text[0] != '#' ||
      sscanf(text + 1, "%2x%2x%2x", &r, &g, &b) != 3)
    return FALSE;
  color->r = r / 255.0;
  color->g = g / 255.0;
  color->b = b / 255.0;
  return TRUE;
}

static const char *enum_name(const ShaulaEnumValue *values, int value) {
  for (const ShaulaEnumValue *entry = values; entry->name != NULL; entry++)
    if (entry->value == value)
      return entry->name;
  return values[0].name;
}

static gboolean enum_value(const ShaulaEnumValue *values, const char *name,
                           int *value) {
  for (const ShaulaEnumValue *entry = values; entry->name != NULL; entry++) {
    if (g_strcmp0(entry->name, name) == 0) {
      *value = entry->value;
      return TRUE;
    }
  }
  return FALSE;
}

static void load_preference(GKeyFile *key_file,
                            ShaulaPreviewToolDefaults *defaults,
                            const ShaulaPreferenceSpec *spec) {
  gpointer target = (guint8 *)defaults + spec->offset;
  GError *error = NULL;
  switch (spec->type) {
  case SHAULA_PREF_COLOR: {
    char *value =
        g_key_file_get_string(key_file, spec->group, spec->key, &error);
    if (error == NULL)
      (void)parse_color(value, target);
    g_free(value);
    break;
  }
  case SHAULA_PREF_DOUBLE: {
    double value =
        g_key_file_get_double(key_file, spec->group, spec->key, &error);
    if (error == NULL && isfinite(value))
      *(double *)target = CLAMP(value, spec->minimum, spec->maximum);
    break;
  }
  case SHAULA_PREF_BOOLEAN: {
    gboolean value =
        g_key_file_get_boolean(key_file, spec->group, spec->key, &error);
    if (error == NULL)
      *(gboolean *)target = value;
    break;
  }
  case SHAULA_PREF_ENUM: {
    char *value =
        g_key_file_get_string(key_file, spec->group, spec->key, &error);
    if (error == NULL)
      (void)enum_value(spec->enum_values, value, target);
    g_free(value);
    break;
  }
  }
  g_clear_error(&error);
}

static void write_preference(GKeyFile *key_file,
                             const ShaulaPreviewToolDefaults *defaults,
                             const ShaulaPreferenceSpec *spec) {
  gconstpointer source = (const guint8 *)defaults + spec->offset;
  switch (spec->type) {
  case SHAULA_PREF_COLOR: {
    const ShaulaColor *color = source;
    char value[8];
    g_snprintf(value, sizeof(value), "#%02X%02X%02X",
               (int)(CLAMP(color->r, 0.0, 1.0) * 255.0 + 0.5),
               (int)(CLAMP(color->g, 0.0, 1.0) * 255.0 + 0.5),
               (int)(CLAMP(color->b, 0.0, 1.0) * 255.0 + 0.5));
    g_key_file_set_string(key_file, spec->group, spec->key, value);
    break;
  }
  case SHAULA_PREF_DOUBLE:
    g_key_file_set_double(key_file, spec->group, spec->key,
                          *(const double *)source);
    break;
  case SHAULA_PREF_BOOLEAN:
    g_key_file_set_boolean(key_file, spec->group, spec->key,
                           *(const gboolean *)source);
    break;
  case SHAULA_PREF_ENUM:
    g_key_file_set_string(key_file, spec->group, spec->key,
                          enum_name(spec->enum_values, *(const int *)source));
    break;
  }
}

static gboolean save_dirty_groups(ShaulaPreviewToolDefaults *defaults,
                                  GError **error) {
  if (defaults->dirty_groups == 0)
    return TRUE;

  gboolean success = FALSE;
  char *path = state_path(TRUE);
  char *lock_path = path != NULL ? g_strconcat(path, ".lock", NULL) : NULL;
  int lock_fd = lock_path != NULL ? g_open(lock_path, O_CREAT | O_RDWR, 0600) : -1;
  if (path == NULL || lock_fd < 0 || flock(lock_fd, LOCK_EX) != 0) {
    g_set_error(error, G_FILE_ERROR,
                g_file_error_from_errno(errno != 0 ? errno : EIO),
                "Could not lock preview tool defaults: %s",
                g_strerror(errno != 0 ? errno : EIO));
    goto cleanup;
  }

  GKeyFile *key_file = g_key_file_new();
  (void)g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL);
  g_key_file_set_integer(key_file, "meta", "version",
                         SHAULA_TOOL_DEFAULTS_VERSION);
  for (guint i = 0; i < G_N_ELEMENTS(preferences); i++)
    if (defaults->dirty_groups & preferences[i].dirty_group)
      write_preference(key_file, defaults, &preferences[i]);

  gsize length = 0;
  char *contents = g_key_file_to_data(key_file, &length, error);
  if (contents != NULL)
    success = g_file_set_contents(path, contents, (gssize)length, error);
  g_free(contents);
  g_key_file_unref(key_file);

cleanup:
  if (lock_fd >= 0) {
    (void)flock(lock_fd, LOCK_UN);
    close(lock_fd);
  }
  g_free(lock_path);
  g_free(path);
  return success;
}

void shaula_tool_defaults_flush(ShaulaPreviewToolDefaults *defaults) {
  if (defaults == NULL || defaults->dirty_groups == 0)
    return;
  guint dirty_groups = defaults->dirty_groups;
  GError *error = NULL;
  if (save_dirty_groups(defaults, &error))
    defaults->dirty_groups &= ~dirty_groups;
  else
    g_warning("Could not save preview tool defaults: %s",
              error != NULL ? error->message : "unknown error");
  g_clear_error(&error);
}

static gboolean flush_after_delay(gpointer data) {
  ShaulaPreviewToolDefaults *defaults = data;
  defaults->save_timeout_id = 0;
  shaula_tool_defaults_flush(defaults);
  return G_SOURCE_REMOVE;
}

void shaula_tool_defaults_init(ShaulaPreviewToolDefaults *defaults) {
  if (defaults == NULL)
    return;
  init_builtin_defaults(defaults);
  char *path = state_path(FALSE);
  GKeyFile *key_file = g_key_file_new();
  if (path != NULL &&
      g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL)) {
    for (guint i = 0; i < G_N_ELEMENTS(preferences); i++)
      load_preference(key_file, defaults, &preferences[i]);
  }
  defaults->pen.color.a = defaults->pen.opacity;
  defaults->highlight.color.a = defaults->highlight.opacity;
  g_key_file_unref(key_file);
  g_free(path);
}

void shaula_tool_defaults_dispose(ShaulaPreviewToolDefaults *defaults) {
  if (defaults == NULL)
    return;
  if (defaults->save_timeout_id != 0) {
    g_source_remove(defaults->save_timeout_id);
    defaults->save_timeout_id = 0;
  }
  shaula_tool_defaults_flush(defaults);
}

void shaula_tool_defaults_mark_dirty(ShaulaPreviewToolDefaults *defaults,
                                     ShaulaToolDefaultsDirtyGroup group) {
  if (defaults == NULL)
    return;
  defaults->dirty_groups |= group;
  if (defaults->save_timeout_id != 0)
    g_source_remove(defaults->save_timeout_id);
  defaults->save_timeout_id = g_timeout_add(
      SHAULA_TOOL_DEFAULTS_SAVE_DELAY_MS, flush_after_delay, defaults);
}
