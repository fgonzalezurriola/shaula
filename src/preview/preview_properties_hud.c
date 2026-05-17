#include "preview_properties_hud.h"

#include <string.h>

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
