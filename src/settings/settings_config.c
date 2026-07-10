#include "settings_config.h"

#include <string.h>

G_STATIC_ASSERT(REGION_LIVE == 0);
G_STATIC_ASSERT(REGION_FROZEN == 1);
G_STATIC_ASSERT(WINDOW_AUTO == 0);
G_STATIC_ASSERT(WINDOW_TILING == 1);
G_STATIC_ASSERT(WINDOW_FLOATING == 2);
G_STATIC_ASSERT(WINDOW_MAXIMIZED == 3);
G_STATIC_ASSERT(WINDOW_MAXIMIZED_TO_EDGES == 4);
G_STATIC_ASSERT(WINDOW_FULLSCREEN == 5);
G_STATIC_ASSERT(SIZE_SMALL == 0);
G_STATIC_ASSERT(SIZE_MEDIUM == 1);
G_STATIC_ASSERT(SIZE_LARGE == 2);
G_STATIC_ASSERT(SIZE_CUSTOM == 3);
G_STATIC_ASSERT(POSITION_CENTERED == 0);
G_STATIC_ASSERT(POSITION_TOP_LEFT == 1);
G_STATIC_ASSERT(POSITION_TOP_RIGHT == 2);
G_STATIC_ASSERT(POSITION_CUSTOM == 3);
G_STATIC_ASSERT(sizeof(gboolean) == sizeof(int));
G_STATIC_ASSERT(sizeof(RegionMode) == sizeof(gint));
G_STATIC_ASSERT(sizeof(WindowMode) == sizeof(gint));
G_STATIC_ASSERT(sizeof(SizePreset) == sizeof(gint));
G_STATIC_ASSERT(sizeof(PositionPreset) == sizeof(gint));

#if GLIB_SIZEOF_VOID_P == 8
G_STATIC_ASSERT(sizeof(ShaulaSettingsConfig) == 136);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, region_mode) == 0);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, window_mode) == 4);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, focused) == 8);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, close_preview_on_save) ==
                12);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, width) == 16);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, height) == 20);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, column_display) == 24);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, floating_x_set) == 32);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, floating_y_set) == 36);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, floating_x) == 40);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, floating_y) == 44);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, floating_relative_to) ==
                48);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, position_preset) == 56);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, quick_skip_preview) ==
                60);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, quick_copy) == 64);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, quick_save) == 68);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, area_skip_preview) == 72);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, area_copy) == 76);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, area_save) == 80);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig,
                                fullscreen_skip_preview) == 84);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, fullscreen_copy) == 88);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, fullscreen_save) == 92);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig,
                                all_screens_skip_preview) == 96);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, all_screens_copy) == 100);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, all_screens_save) == 104);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, save_folder) == 112);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, notifications_success) ==
                120);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig, notifications_errors) ==
                124);
G_STATIC_ASSERT(G_STRUCT_OFFSET(ShaulaSettingsConfig,
                                notifications_thumbnails) == 128);
#endif

static char *json_string_after(const char *json, const char *needle) {
  const char *start = strstr(json, needle);
  if (start == NULL)
    return NULL;

  const char *value_start = start + strlen(needle);
  const char *end = strchr(value_start, '"');
  if (end == NULL)
    return NULL;
  return g_strndup(value_start, (gsize)(end - value_start));
}

static gboolean json_bool_after(const char *json, const char *needle,
                                gboolean fallback) {
  const char *start = strstr(json, needle);
  if (start == NULL)
    return fallback;

  const char *tail = start + strlen(needle);
  if (g_str_has_prefix(tail, "true"))
    return TRUE;
  if (g_str_has_prefix(tail, "false"))
    return FALSE;
  return fallback;
}

static gboolean parse_leading_int(const char *value, int *out) {
  const char *cursor = value;
  gboolean negative = FALSE;
  if (*cursor == '-' || *cursor == '+') {
    negative = *cursor == '-';
    cursor++;
  }

  if (!g_ascii_isdigit(*cursor))
    return FALSE;

  const guint64 limit = negative ? ((guint64)G_MAXINT + 1U) : (guint64)G_MAXINT;
  guint64 parsed = 0;
  while (g_ascii_isdigit(*cursor)) {
    const guint64 digit = (guint64)(*cursor - '0');
    if (parsed > (limit - digit) / 10U)
      return FALSE;
    parsed = parsed * 10U + digit;
    cursor++;
  }

  if (negative) {
    if (parsed == (guint64)G_MAXINT + 1U)
      *out = G_MININT;
    else
      *out = -(int)parsed;
  } else {
    *out = (int)parsed;
  }
  return TRUE;
}

static int json_int_after(const char *json, const char *needle, int fallback) {
  const char *start = strstr(json, needle);
  if (start == NULL)
    return fallback;

  const char *tail = start + strlen(needle);
  if (g_str_has_prefix(tail, "null"))
    return fallback;

  int parsed = 0;
  return parse_leading_int(tail, &parsed) ? parsed : fallback;
}

static gboolean json_nullable_int_after(const char *json, const char *needle,
                                        int *out) {
  const char *start = strstr(json, needle);
  if (start == NULL)
    return FALSE;

  const char *tail = start + strlen(needle);
  if (g_str_has_prefix(tail, "null"))
    return FALSE;
  return parse_leading_int(tail, out);
}

static WindowMode parse_window_mode(const char *value) {
  if (strcmp(value, "auto") == 0)
    return WINDOW_AUTO;
  if (strcmp(value, "tiling") == 0)
    return WINDOW_TILING;
  if (strcmp(value, "maximized") == 0)
    return WINDOW_MAXIMIZED;
  if (strcmp(value, "maximized-to-edges") == 0)
    return WINDOW_MAXIMIZED_TO_EDGES;
  if (strcmp(value, "fullscreen") == 0)
    return WINDOW_FULLSCREEN;
  return WINDOW_FLOATING;
}

static PositionPreset classify_position(const ShaulaSettingsConfig *config) {
  if (config->floating_x_set != TRUE || config->floating_y_set != TRUE)
    return POSITION_CENTERED;

  const char *relative =
      config->floating_relative_to != NULL ? config->floating_relative_to : "";
  if (config->floating_x == 80 && config->floating_y == 80 &&
      strcmp(relative, "top-left") == 0)
    return POSITION_TOP_LEFT;
  if (config->floating_x == 80 && config->floating_y == 80 &&
      strcmp(relative, "top-right") == 0)
    return POSITION_TOP_RIGHT;
  return POSITION_CUSTOM;
}

static void replace_string(char **slot, const char *value) {
  char *replacement = g_strdup(value);
  g_free(*slot);
  *slot = replacement;
}

static void parse_after_mode(const char *json, const char *object_needle,
                             gboolean *skip_preview,
                             gboolean *copy_to_clipboard,
                             gboolean *save_to_folder) {
  const char *start = strstr(json, object_needle);
  if (start == NULL)
    return;

  const char *body_start = start + strlen(object_needle);
  const char *body_end = strchr(body_start, '}');
  if (body_end == NULL)
    return;

  g_autofree char *body = g_strndup(body_start, (gsize)(body_end - body_start));
  *skip_preview = json_bool_after(body, "\"skip_preview\":", *skip_preview);
  *copy_to_clipboard =
      json_bool_after(body, "\"copy_to_clipboard\":", *copy_to_clipboard);
  *save_to_folder =
      json_bool_after(body, "\"save_to_folder\":", *save_to_folder);
}

void shaula_settings_config_init_defaults(ShaulaSettingsConfig *config) {
  *config = (ShaulaSettingsConfig){
      .region_mode = REGION_FROZEN,
      .window_mode = WINDOW_FLOATING,
      .focused = TRUE,
      .close_preview_on_save = TRUE,
      .width = 1100,
      .height = 720,
      .column_display = g_strdup("normal"),
      .floating_x_set = FALSE,
      .floating_y_set = FALSE,
      .floating_x = 0,
      .floating_y = 0,
      .floating_relative_to = g_strdup("top-left"),
      .position_preset = POSITION_CENTERED,
      .quick_skip_preview = FALSE,
      .quick_copy = TRUE,
      .quick_save = FALSE,
      .area_skip_preview = FALSE,
      .area_copy = TRUE,
      .area_save = FALSE,
      .fullscreen_skip_preview = TRUE,
      .fullscreen_copy = TRUE,
      .fullscreen_save = TRUE,
      .all_screens_skip_preview = TRUE,
      .all_screens_copy = TRUE,
      .all_screens_save = TRUE,
      .save_folder = g_strdup("~/Pictures/shaula"),
      .notifications_success = TRUE,
      .notifications_errors = TRUE,
      .notifications_thumbnails = TRUE,
  };
}

void shaula_settings_config_clear(ShaulaSettingsConfig *config) {
  g_clear_pointer(&config->column_display, g_free);
  g_clear_pointer(&config->floating_relative_to, g_free);
  g_clear_pointer(&config->save_folder, g_free);
}

const char *shaula_settings_region_mode_text(RegionMode mode) {
  switch (mode) {
  case REGION_LIVE:
    return "live";
  case REGION_FROZEN:
    return "frozen";
  }
  g_assert_not_reached();
}

const char *shaula_settings_window_mode_text(WindowMode mode) {
  switch (mode) {
  case WINDOW_AUTO:
    return "auto";
  case WINDOW_TILING:
    return "tiling";
  case WINDOW_FLOATING:
    return "floating";
  case WINDOW_MAXIMIZED:
    return "maximized";
  case WINDOW_MAXIMIZED_TO_EDGES:
    return "maximized-to-edges";
  case WINDOW_FULLSCREEN:
    return "fullscreen";
  }
  g_assert_not_reached();
}

SizePreset
shaula_settings_size_preset_for_config(const ShaulaSettingsConfig *config) {
  if (config->width == 900 && config->height == 600)
    return SIZE_SMALL;
  if (config->width == 1100 && config->height == 720)
    return SIZE_MEDIUM;
  if (config->width == 1400 && config->height == 900)
    return SIZE_LARGE;
  return SIZE_CUSTOM;
}

void shaula_settings_apply_size_preset(ShaulaSettingsConfig *config,
                                       SizePreset preset) {
  switch (preset) {
  case SIZE_SMALL:
    config->width = 900;
    config->height = 600;
    return;
  case SIZE_MEDIUM:
    config->width = 1100;
    config->height = 720;
    return;
  case SIZE_LARGE:
    config->width = 1400;
    config->height = 900;
    return;
  case SIZE_CUSTOM:
    return;
  }
  g_assert_not_reached();
}

void shaula_settings_apply_position_preset(ShaulaSettingsConfig *config,
                                           PositionPreset preset) {
  config->position_preset = preset;
  switch (preset) {
  case POSITION_CENTERED:
    config->floating_x_set = FALSE;
    config->floating_y_set = FALSE;
    replace_string(&config->floating_relative_to, "top-left");
    return;
  case POSITION_TOP_LEFT:
  case POSITION_TOP_RIGHT:
    config->floating_x_set = TRUE;
    config->floating_y_set = TRUE;
    config->floating_x = 80;
    config->floating_y = 80;
    replace_string(&config->floating_relative_to,
                   preset == POSITION_TOP_LEFT ? "top-left" : "top-right");
    return;
  case POSITION_CUSTOM:
    return;
  }
  g_assert_not_reached();
}

char *shaula_settings_resolve_config_path(void) {
  const char *explicit_path = g_getenv("SHAULA_CONFIG_FILE");
  if (explicit_path != NULL) {
    const char *start = explicit_path;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
      start++;

    const char *end = explicit_path + strlen(explicit_path);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\r' || end[-1] == '\n'))
      end--;
    if (end > start)
      return g_strndup(start, (gsize)(end - start));
  }

  const char *xdg_config_home = g_getenv("XDG_CONFIG_HOME");
  if (xdg_config_home != NULL && *xdg_config_home != '\0')
    return g_strconcat(xdg_config_home, "/shaula/config.toml", NULL);

  const char *home = g_get_home_dir();
  if (home != NULL && *home != '\0')
    return g_strconcat(home, "/.config/shaula/config.toml", NULL);
  return NULL;
}

char *shaula_settings_config_path_from_show_json(const char *json) {
  return json != NULL ? json_string_after(json, "\"path\":\"") : NULL;
}

gboolean shaula_settings_config_from_show_json(const char *json,
                                               ShaulaSettingsConfig *config) {
  if (json == NULL)
    return FALSE;

  g_autofree char *region =
      json_string_after(json, "\"region_capture_mode\":\"");
  if (region != NULL)
    config->region_mode =
        strcmp(region, "frozen") == 0 ? REGION_FROZEN : REGION_LIVE;

  g_autofree char *mode = json_string_after(json, "\"mode\":\"");
  if (mode != NULL)
    config->window_mode = parse_window_mode(mode);

  config->focused = json_bool_after(json, "\"focused\":", config->focused);
  config->close_preview_on_save = json_bool_after(
      json, "\"close_preview_on_save\":", config->close_preview_on_save);
  config->width = json_int_after(json, "\"width\":", config->width);
  config->height = json_int_after(json, "\"height\":", config->height);

  char *column_display =
      json_string_after(json, "\"default_column_display\":\"");
  if (column_display != NULL) {
    g_free(config->column_display);
    config->column_display = column_display;
  }

  int parsed_x = 0;
  int parsed_y = 0;
  config->floating_x_set = json_nullable_int_after(json, "\"x\":", &parsed_x);
  config->floating_y_set = json_nullable_int_after(json, "\"y\":", &parsed_y);
  if (config->floating_x_set == TRUE)
    config->floating_x = parsed_x;
  if (config->floating_y_set == TRUE)
    config->floating_y = parsed_y;

  char *relative_to = json_string_after(json, "\"relative_to\":\"");
  if (relative_to != NULL) {
    g_free(config->floating_relative_to);
    config->floating_relative_to = relative_to;
  }
  config->position_preset = classify_position(config);

  parse_after_mode(json, "\"quick\":{", &config->quick_skip_preview,
                   &config->quick_copy, &config->quick_save);
  parse_after_mode(json, "\"area\":{", &config->area_skip_preview,
                   &config->area_copy, &config->area_save);
  parse_after_mode(json, "\"fullscreen\":{", &config->fullscreen_skip_preview,
                   &config->fullscreen_copy, &config->fullscreen_save);
  parse_after_mode(json, "\"all_screens\":{", &config->all_screens_skip_preview,
                   &config->all_screens_copy, &config->all_screens_save);

  char *save_folder = json_string_after(json, "\"save_folder\":\"");
  if (save_folder != NULL) {
    g_free(config->save_folder);
    config->save_folder = save_folder;
  }

  config->notifications_success =
      json_bool_after(json, "\"success\":", config->notifications_success);
  config->notifications_errors =
      json_bool_after(json, "\"errors\":", config->notifications_errors);
  config->notifications_thumbnails = json_bool_after(
      json, "\"thumbnails\":", config->notifications_thumbnails);
  return TRUE;
}
