#ifndef SHAULA_PREVIEW_PROPERTIES_HUD_H
#define SHAULA_PREVIEW_PROPERTIES_HUD_H

#include <glib.h>

#include "preview_annotations.h"
#include "preview_tool_defaults.h"

typedef struct _GtkWidget GtkWidget;
typedef struct ShaulaPreviewState ShaulaPreviewState;

typedef enum {
  SHAULA_PROPERTIES_PANEL_NONE,
  SHAULA_PROPERTIES_PANEL_SPOTLIGHT,
  SHAULA_PROPERTIES_PANEL_ARROW,
  SHAULA_PROPERTIES_PANEL_RECTANGLE,
  SHAULA_PROPERTIES_PANEL_HIGHLIGHT,
  SHAULA_PROPERTIES_PANEL_BLUR,
  SHAULA_PROPERTIES_PANEL_ERASER,
  SHAULA_PROPERTIES_PANEL_PEN,
  SHAULA_PROPERTIES_PANEL_TEXT,
  SHAULA_PROPERTIES_PANEL_MEASURE
} ShaulaPropertiesPanel;

/* Stores the active target and GTK adapter handles. Property derivation,
 * mutation, validation, history, persistence, and widget synchronization stay
 * behind the functions below.
 */
typedef struct {
  ShaulaPropertiesPanel active_panel;
  gboolean syncing_widgets;
  int spotlight_index;

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
  GtkWidget *eraser_properties_box;
  GtkWidget *eraser_size_scale;
} ShaulaPropertiesHudState;

void shaula_properties_hud_state_init(ShaulaPropertiesHudState *hud);
gboolean shaula_properties_hud_set_panel(ShaulaPropertiesHudState *hud,
                                         ShaulaPropertiesPanel panel);
void shaula_properties_hud_show_panel(ShaulaPreviewState *state,
                                      ShaulaPropertiesPanel panel);
void shaula_properties_hud_target_annotation(
    ShaulaPropertiesHudState *hud, const ShaulaAnnotation *annotation);
void shaula_properties_hud_target_spotlight(ShaulaPropertiesHudState *hud,
                                            int spotlight_index);
void shaula_properties_hud_sync_widgets(ShaulaPreviewState *state);

void shaula_properties_hud_set_eraser_size(ShaulaPreviewState *state,
                                           double size);
void shaula_properties_hud_set_spotlight_border_color(
    ShaulaPreviewState *state, ShaulaColor color);
void shaula_properties_hud_set_spotlight_border_width(
    ShaulaPreviewState *state, double width);
void shaula_properties_hud_set_spotlight_shape(ShaulaPreviewState *state,
                                               ShaulaSpotlightShape shape);
void shaula_properties_hud_set_arrow_color(ShaulaPreviewState *state,
                                           ShaulaColor color);
void shaula_properties_hud_set_arrow_stroke_width(ShaulaPreviewState *state,
                                                  double width);
void shaula_properties_hud_set_arrow_stroke_style(
    ShaulaPreviewState *state, PreviewArrowStrokeStyle style);
void shaula_properties_hud_set_rectangle_color(ShaulaPreviewState *state,
                                               ShaulaColor color);
void shaula_properties_hud_set_rectangle_stroke_width(
    ShaulaPreviewState *state, double width);
void shaula_properties_hud_set_rectangle_stroke_style(
    ShaulaPreviewState *state, PreviewArrowStrokeStyle style);
void shaula_properties_hud_set_rectangle_filled(ShaulaPreviewState *state,
                                                gboolean filled);
void shaula_properties_hud_set_rectangle_corners(
    ShaulaPreviewState *state, PreviewRectangleCorners corners);
void shaula_properties_hud_set_pen_color(ShaulaPreviewState *state,
                                         ShaulaColor color);
void shaula_properties_hud_set_pen_stroke_width(ShaulaPreviewState *state,
                                                double width);
void shaula_properties_hud_set_pen_opacity(ShaulaPreviewState *state,
                                           double opacity);
void shaula_properties_hud_set_highlight_color(ShaulaPreviewState *state,
                                               ShaulaColor color);
void shaula_properties_hud_set_highlight_stroke_width(
    ShaulaPreviewState *state, double width);
void shaula_properties_hud_set_highlight_opacity(ShaulaPreviewState *state,
                                                 double opacity);
void shaula_properties_hud_set_text_color(ShaulaPreviewState *state,
                                          ShaulaColor color);
void shaula_properties_hud_set_text_font_size(ShaulaPreviewState *state,
                                              double font_size);
void shaula_properties_hud_set_text_align(ShaulaPreviewState *state,
                                          ShaulaTextAlign align);
void shaula_properties_hud_set_text_font_mode(ShaulaPreviewState *state,
                                              ShaulaTextFontMode font_mode);
void shaula_properties_hud_set_measure_color(ShaulaPreviewState *state,
                                             ShaulaColor color);
void shaula_properties_hud_set_measure_stroke_width(ShaulaPreviewState *state,
                                                    double width);

#endif
