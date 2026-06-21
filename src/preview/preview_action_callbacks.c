#include "preview_action_callbacks.h"

#include "preview_commands.h"
#include "preview_properties_hud.h"
#include "preview_state.h"

void shaula_preview_on_copy_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_COPY);
}

void shaula_preview_on_save_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_SAVE);
}

void shaula_preview_on_save_as_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_SAVE_AS);
}

void shaula_preview_on_done_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_DONE);
}

void shaula_preview_on_close_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_CLOSE);
}

void shaula_preview_on_undo_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_UNDO);
}

void shaula_preview_on_redo_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_REDO);
}

void shaula_preview_on_discard_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_DISCARD);
}

void shaula_preview_on_fit_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN);
}

void shaula_preview_on_actual_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE);
}

void shaula_preview_on_reset_annotations_clicked(GtkButton *button,
                                                 gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_RESET_ANNOTATIONS);
}

void shaula_preview_on_duplicate_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED);
}

void shaula_preview_on_delete_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_DELETE_SELECTED);
}

void shaula_preview_on_crop_selected_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_CROP_SELECTED);
}

void shaula_preview_on_blur_region_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_BLUR_REGION);
}

void shaula_preview_on_erase_region_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_ERASE_REGION);
}

void shaula_preview_on_spotlight_toolbar_clicked(GtkButton *button,
                                                 gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT);
}

void shaula_preview_on_spotlight_region_clicked(GtkButton *button,
                                                gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION);
}

void shaula_preview_on_properties_back_clicked(GtkButton *button,
                                               gpointer data) {
  (void)button;
  shaula_properties_hud_show_panel(data, SHAULA_PROPERTIES_PANEL_NONE);
}

void shaula_preview_on_spotlight_color_set(GtkColorButton *button,
                                           gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_properties_hud_set_spotlight_border_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_spotlight_width_changed(GtkRange *range, gpointer data) {
  shaula_properties_hud_set_spotlight_border_width(
      data, gtk_range_get_value(range));
}

void shaula_preview_on_spotlight_shape_clicked(GtkButton *button,
                                               gpointer data) {
  ShaulaSpotlightShape shape = (ShaulaSpotlightShape)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(button), "spotlight-shape"));
  shaula_properties_hud_set_spotlight_shape(data, shape);
}

void shaula_preview_on_copy_path_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_COPY_PATH);
}

void shaula_preview_on_use_hover_color_clicked(GtkGestureClick *gesture,
                                               int n_press, double x, double y,
                                               gpointer data) {
  (void)gesture;
  (void)n_press;
  (void)x;
  (void)y;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_USE_HOVER_COLOR);
}

void shaula_preview_on_open_folder_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER);
}

void shaula_preview_on_tool_clicked(GtkButton *button, gpointer data) {
  ShaulaTool tool =
      (ShaulaTool)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tool"));
  ShaulaPreviewCommand command;
  if (shaula_preview_command_for_tool(tool, &command))
    shaula_preview_execute_command(data, command);
}

void shaula_preview_on_arrow_color_set(GtkColorButton *button, gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_properties_hud_set_arrow_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_arrow_width_changed(GtkRange *range, gpointer data) {
  shaula_properties_hud_set_arrow_stroke_width(
      data, gtk_range_get_value(range));
}

void shaula_preview_on_arrow_stroke_style_clicked(GtkButton *button,
                                                  gpointer data) {
  PreviewArrowStrokeStyle style = (PreviewArrowStrokeStyle)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(button), "arrow-stroke-style"));
  shaula_properties_hud_set_arrow_stroke_style(data, style);
}

void shaula_preview_on_rectangle_color_set(GtkColorButton *button,
                                           gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_properties_hud_set_rectangle_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_rectangle_width_changed(GtkRange *range, gpointer data) {
  shaula_properties_hud_set_rectangle_stroke_width(
      data, gtk_range_get_value(range));
}

void shaula_preview_on_rectangle_stroke_style_clicked(GtkButton *button,
                                                      gpointer data) {
  PreviewArrowStrokeStyle style = (PreviewArrowStrokeStyle)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(button), "rectangle-stroke-style"));
  shaula_properties_hud_set_rectangle_stroke_style(data, style);
}

void shaula_preview_on_rectangle_fill_toggled(GtkButton *button,
                                              gpointer data) {
  shaula_properties_hud_set_rectangle_filled(
      data, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}

void shaula_preview_on_rectangle_corners_clicked(GtkButton *button,
                                                 gpointer data) {
  PreviewRectangleCorners corners = (PreviewRectangleCorners)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(button), "rectangle-corners"));
  shaula_properties_hud_set_rectangle_corners(data, corners);
}

void shaula_preview_on_pen_color_set(GtkColorButton *button, gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_properties_hud_set_pen_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_pen_width_changed(GtkRange *range, gpointer data) {
  shaula_properties_hud_set_pen_stroke_width(
      data, gtk_range_get_value(range));
}

void shaula_preview_on_pen_opacity_changed(GtkRange *range, gpointer data) {
  shaula_properties_hud_set_pen_opacity(data, gtk_range_get_value(range));
}

void shaula_preview_on_highlight_color_set(GtkColorButton *button,
                                           gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_properties_hud_set_highlight_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_highlight_width_changed(GtkRange *range, gpointer data) {
  shaula_properties_hud_set_highlight_stroke_width(
      data, gtk_range_get_value(range));
}

void shaula_preview_on_highlight_opacity_changed(GtkRange *range,
                                                 gpointer data) {
  shaula_properties_hud_set_highlight_opacity(
      data, gtk_range_get_value(range));
}

void shaula_preview_on_text_color_set(GtkColorButton *button, gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_properties_hud_set_text_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, 1.0});
}

void shaula_preview_on_text_size_clicked(GtkButton *button, gpointer data) {
  double font_size = (double)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(button), "text-font-size"));
  shaula_properties_hud_set_text_font_size(data, font_size);
}

void shaula_preview_on_text_style_clicked(GtkButton *button, gpointer data) {
  ShaulaTextFontMode font_mode = (ShaulaTextFontMode)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(button), "text-font-mode"));
  shaula_properties_hud_set_text_font_mode(data, font_mode);
}

void shaula_preview_on_text_align_clicked(GtkButton *button, gpointer data) {
  ShaulaTextAlign align = (ShaulaTextAlign)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(button), "text-align"));
  shaula_properties_hud_set_text_align(data, align);
}

void shaula_preview_on_measure_color_set(GtkColorButton *button,
                                         gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_properties_hud_set_measure_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_measure_width_changed(GtkRange *range, gpointer data) {
  shaula_properties_hud_set_measure_stroke_width(
      data, gtk_range_get_value(range));
}

void shaula_preview_on_eraser_size_changed(GtkRange *range, gpointer data) {
  shaula_properties_hud_set_eraser_size(data, gtk_range_get_value(range));
}
