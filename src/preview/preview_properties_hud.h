#ifndef SHAULA_PREVIEW_PROPERTIES_HUD_H
#define SHAULA_PREVIEW_PROPERTIES_HUD_H

#include <glib.h>

#include "preview_annotations.h"
#include "preview_geometry.h"

typedef struct _GtkWidget GtkWidget;

typedef enum {
  SHAULA_PROPERTIES_PANEL_NONE,
  SHAULA_PROPERTIES_PANEL_SPOTLIGHT,
  SHAULA_PROPERTIES_PANEL_ARROW,
  SHAULA_PROPERTIES_PANEL_RECTANGLE,
  SHAULA_PROPERTIES_PANEL_HIGHLIGHT,
  SHAULA_PROPERTIES_PANEL_BLUR,
  SHAULA_PROPERTIES_PANEL_ERASE,
  SHAULA_PROPERTIES_PANEL_PEN,
  SHAULA_PROPERTIES_PANEL_TEXT,
  SHAULA_PROPERTIES_PANEL_MEASURE
} ShaulaPropertiesPanel;

typedef enum {
  SHAULA_SPOTLIGHT_SHAPE_SHARP_RECTANGLE,
  SHAULA_SPOTLIGHT_SHAPE_ROUNDED_RECTANGLE
} ShaulaSpotlightShape;

/* Owns floating properties HUD state and widget handles. These fields are
 * UI/config state only: keep them out of undo/redo document snapshots.
 */
typedef struct {
  ShaulaPropertiesPanel active_panel;
  int spotlight_index;
  int arrow_index;
  int rectangle_index;
  int measure_index;
  ShaulaColor spotlight_border_color;
  double spotlight_border_width;
  ShaulaSpotlightShape spotlight_shape;
  ShaulaColor arrow_color;
  double arrow_stroke_width;
  ShaulaColor rectangle_color;
  double rectangle_stroke_width;
  PreviewArrowStrokeStyle rectangle_stroke_style;
  gboolean rectangle_filled;
  PreviewRectangleCorners rectangle_corners;
  ShaulaColor pen_color;
  double pen_stroke_width;
  double pen_opacity;
  ShaulaColor highlight_color;
  double highlight_stroke_width;
  double highlight_opacity;
  ShaulaColor text_color;
  double text_font_size;
  ShaulaTextAlign text_align;
  ShaulaTextFontMode text_font_mode;
  ShaulaColor measure_color;
  double measure_stroke_width;

  GtkWidget *properties_box;
  GtkWidget *spotlight_color_button;
  GtkWidget *spotlight_width_scale;
  GtkWidget *spotlight_sharp_button;
  GtkWidget *spotlight_rounded_button;
  GtkWidget *arrow_properties_box;
  GtkWidget *arrow_color_button;
  GtkWidget *arrow_width_scale;
  GtkWidget *arrow_stroke_buttons[3];
  GtkWidget *rectangle_properties_box;
  GtkWidget *rectangle_color_button;
  GtkWidget *rectangle_width_scale;
  GtkWidget *rectangle_stroke_buttons[2];
  GtkWidget *rectangle_fill_button;
  GtkWidget *rectangle_corner_buttons[2];
  GtkWidget *pen_properties_box;
  GtkWidget *pen_color_button;
  GtkWidget *pen_width_scale;
  GtkWidget *pen_opacity_scale;
  GtkWidget *highlight_properties_box;
  GtkWidget *highlight_color_button;
  GtkWidget *highlight_width_scale;
  GtkWidget *highlight_opacity_scale;
  GtkWidget *text_properties_box;
  GtkWidget *text_color_button;
  GtkWidget *text_size_buttons[4];
  GtkWidget *text_align_buttons[3];
  GtkWidget *text_font_mode_buttons[2];
  GtkWidget *measure_properties_box;
  GtkWidget *measure_color_button;
  GtkWidget *measure_width_scale;
} ShaulaPropertiesHudState;

void shaula_properties_hud_state_init(ShaulaPropertiesHudState *hud);
gboolean shaula_properties_hud_set_panel(ShaulaPropertiesHudState *hud,
                                         ShaulaPropertiesPanel panel);

#endif
