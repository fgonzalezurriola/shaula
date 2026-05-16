#ifndef SHAULA_PREVIEW_ACTIONS_H
#define SHAULA_PREVIEW_ACTIONS_H

#include <gtk/gtk.h>

#include "preview_state.h"

void shaula_preview_action_set_tool(ShaulaPreviewState *state, ShaulaTool tool);
void shaula_preview_action_copy(ShaulaPreviewState *state);
void shaula_preview_action_save(ShaulaPreviewState *state);
void shaula_preview_action_accept(ShaulaPreviewState *state,
                                  gboolean copy_to_clipboard);
void shaula_preview_action_done(ShaulaPreviewState *state);
void shaula_preview_action_save_as(ShaulaPreviewState *state);
void shaula_preview_action_discard(ShaulaPreviewState *state);
void shaula_preview_action_fit(ShaulaPreviewState *state);
void shaula_preview_action_actual_size(ShaulaPreviewState *state);
void shaula_preview_action_zoom_in(ShaulaPreviewState *state);
void shaula_preview_action_zoom_out(ShaulaPreviewState *state);
void shaula_preview_action_undo(ShaulaPreviewState *state);
void shaula_preview_action_redo(ShaulaPreviewState *state);
void shaula_preview_action_reset_annotations(ShaulaPreviewState *state);
void shaula_preview_action_duplicate_selected(ShaulaPreviewState *state);
void shaula_preview_action_delete_selected(ShaulaPreviewState *state);
void shaula_preview_action_crop_selected(ShaulaPreviewState *state);
void shaula_preview_action_copy_path(ShaulaPreviewState *state);
void shaula_preview_action_copy_hover_color(ShaulaPreviewState *state);
void shaula_preview_action_open_containing_folder(ShaulaPreviewState *state);

void shaula_preview_on_copy_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_save_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_save_as_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_done_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_undo_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_redo_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_discard_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_fit_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_actual_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_reset_annotations_clicked(GtkButton *button,
                                                 gpointer data);
void shaula_preview_on_duplicate_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_delete_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_crop_selected_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_blur_region_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_erase_region_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_spotlight_toolbar_clicked(GtkButton *button,
                                                 gpointer data);
void shaula_preview_on_spotlight_region_clicked(GtkButton *button,
                                                gpointer data);
void shaula_preview_on_properties_back_clicked(GtkButton *button,
                                               gpointer data);
void shaula_preview_on_spotlight_color_set(GtkColorButton *button,
                                           gpointer data);
void shaula_preview_on_spotlight_width_changed(GtkRange *range, gpointer data);
void shaula_preview_on_spotlight_shape_clicked(GtkButton *button,
                                               gpointer data);
void shaula_preview_on_copy_path_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_open_folder_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_tool_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_arrow_color_set(GtkColorButton *button, gpointer data);
void shaula_preview_on_arrow_width_changed(GtkRange *range, gpointer data);
void shaula_preview_on_arrow_stroke_style_clicked(GtkButton *button,
                                                  gpointer data);
void shaula_preview_on_rectangle_color_set(GtkColorButton *button,
                                           gpointer data);
void shaula_preview_on_rectangle_width_changed(GtkRange *range, gpointer data);
void shaula_preview_on_rectangle_stroke_style_clicked(GtkButton *button,
                                                      gpointer data);
void shaula_preview_on_rectangle_fill_toggled(GtkButton *button, gpointer data);
void shaula_preview_on_rectangle_corners_clicked(GtkButton *button,
                                                 gpointer data);
void shaula_preview_on_pen_color_set(GtkColorButton *button, gpointer data);
void shaula_preview_on_pen_width_changed(GtkRange *range, gpointer data);
void shaula_preview_on_pen_opacity_changed(GtkRange *range, gpointer data);
void shaula_preview_on_highlight_color_set(GtkColorButton *button,
                                           gpointer data);
void shaula_preview_on_highlight_width_changed(GtkRange *range, gpointer data);
void shaula_preview_on_highlight_opacity_changed(GtkRange *range,
                                                 gpointer data);
void shaula_preview_on_text_color_set(GtkColorButton *button, gpointer data);
void shaula_preview_on_text_size_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_text_style_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_text_align_clicked(GtkButton *button, gpointer data);
void shaula_preview_on_measure_color_set(GtkColorButton *button, gpointer data);
void shaula_preview_on_measure_width_changed(GtkRange *range, gpointer data);

#endif
