#include "preview_properties_hud.h"

#include <glib/gstdio.h>
#include <string.h>

static char *properties_state_path(void) {
  const char *state_dir = g_get_user_state_dir();
  if (state_dir == NULL || state_dir[0] == '\0')
    return NULL;
  char *dir = g_build_filename(state_dir, "shaula", NULL);
  g_mkdir_with_parents(dir, 0700);
  char *path = g_build_filename(dir, "preview-tool-hud.ini", NULL);
  g_free(dir);
  return path;
}

static double clamp_eraser_size(double size) {
  return CLAMP(size, SHAULA_ERASER_SIZE_MIN, SHAULA_ERASER_SIZE_MAX);
}

static double load_eraser_size(void) {
  char *path = properties_state_path();
  if (path == NULL)
    return SHAULA_ERASER_SIZE_DEFAULT;

  GKeyFile *key_file = g_key_file_new();
  GError *error = NULL;
  double size = SHAULA_ERASER_SIZE_DEFAULT;
  if (g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
    double loaded =
        g_key_file_get_double(key_file, "eraser", "size", &error);
    if (error == NULL)
      size = clamp_eraser_size(loaded);
  }
  if (error != NULL)
    g_error_free(error);
  g_key_file_unref(key_file);
  g_free(path);
  return size;
}

void shaula_properties_hud_save_eraser_size(double size) {
  char *path = properties_state_path();
  if (path == NULL)
    return;

  GKeyFile *key_file = g_key_file_new();
  GError *error = NULL;
  g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL);
  g_key_file_set_double(key_file, "eraser", "size", clamp_eraser_size(size));
  gsize length = 0;
  char *contents = g_key_file_to_data(key_file, &length, &error);
  if (contents != NULL && error == NULL)
    g_file_set_contents(path, contents, (gssize)length, NULL);
  if (error != NULL)
    g_error_free(error);
  g_free(contents);
  g_key_file_unref(key_file);
  g_free(path);
}

void shaula_properties_hud_state_init(ShaulaPropertiesHudState *hud) {
  if (hud == NULL)
    return;
  memset(hud, 0, sizeof(*hud));
  hud->active_panel = SHAULA_PROPERTIES_PANEL_NONE;
  hud->spotlight_index = -1;
  hud->spotlight_border_color =
      (ShaulaColor){0xFD / 255.0, 0x76 / 255.0, 0x03 / 255.0, 1.0};
  hud->spotlight_border_width = 3.0;
  hud->spotlight_shape = SHAULA_SPOTLIGHT_SHAPE_SHARP_RECTANGLE;
  hud->arrow_index = -1;
  hud->arrow_color =
      (ShaulaColor){0xFD / 255.0, 0x76 / 255.0, 0x03 / 255.0, 1.0};
  hud->arrow_stroke_width = 3.5;
  hud->rectangle_index = -1;
  hud->rectangle_color = hud->arrow_color;
  hud->rectangle_stroke_width = 3.5;
  hud->rectangle_stroke_style = PREVIEW_ARROW_STROKE_DASHED;
  hud->rectangle_filled = FALSE;
  hud->rectangle_corners = PREVIEW_RECTANGLE_CORNERS_ROUNDED;
  hud->pen_color = hud->arrow_color;
  hud->pen_stroke_width = 3.0;
  hud->pen_opacity = 1.0;
  hud->highlight_color =
      (ShaulaColor){0xFF / 255.0, 0xD7 / 255.0, 0x5A / 255.0, 0.30};
  hud->highlight_stroke_width = 18.0;
  hud->highlight_opacity = 0.30;
  hud->text_color =
      (ShaulaColor){0xFD / 255.0, 0x76 / 255.0, 0x03 / 255.0, 1.0};
  hud->text_font_size = 24.0;
  hud->text_align = SHAULA_TEXT_ALIGN_LEFT;
  hud->text_font_mode = SHAULA_TEXT_FONT_NORMAL;
  hud->measure_index = -1;
  hud->measure_color =
      (ShaulaColor){0xFD / 255.0, 0x76 / 255.0, 0x03 / 255.0, 1.0};
  hud->measure_stroke_width = 2.0;
  hud->eraser_size = load_eraser_size();
}

gboolean shaula_properties_hud_set_panel(ShaulaPropertiesHudState *hud,
                                         ShaulaPropertiesPanel panel) {
  if (hud == NULL || hud->active_panel == panel)
    return FALSE;
  hud->active_panel = panel;
  if (panel != SHAULA_PROPERTIES_PANEL_SPOTLIGHT)
    hud->spotlight_index = -1;
  if (panel != SHAULA_PROPERTIES_PANEL_ARROW)
    hud->arrow_index = -1;
  if (panel != SHAULA_PROPERTIES_PANEL_RECTANGLE)
    hud->rectangle_index = -1;
  if (panel != SHAULA_PROPERTIES_PANEL_MEASURE)
    hud->measure_index = -1;
  return TRUE;
}
