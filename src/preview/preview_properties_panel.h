#ifndef SHAULA_PREVIEW_PROPERTIES_PANEL_H
#define SHAULA_PREVIEW_PROPERTIES_PANEL_H

#include <gtk/gtk.h>

#include "preview_state.h"

GtkWidget *shaula_preview_select_properties_panel_build(
    ShaulaPreviewState *state);
GtkWidget *shaula_preview_properties_panel_build(ShaulaPreviewState *state);
GtkWidget *shaula_preview_arrow_properties_panel_build(
    ShaulaPreviewState *state);
GtkWidget *shaula_preview_rectangle_properties_panel_build(
    ShaulaPreviewState *state);
GtkWidget *shaula_preview_pen_properties_panel_build(ShaulaPreviewState *state);
GtkWidget *shaula_preview_highlight_properties_panel_build(
    ShaulaPreviewState *state);
GtkWidget *shaula_preview_text_properties_panel_build(
    ShaulaPreviewState *state);
GtkWidget *shaula_preview_measure_properties_panel_build(
    ShaulaPreviewState *state);
GtkWidget *shaula_preview_eraser_properties_panel_build(
    ShaulaPreviewState *state);

#endif
