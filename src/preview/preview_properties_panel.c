#include "preview_properties_panel.h"

#include "preview_actions.h"
#include "preview_icons.h"

static void install_properties_panel_css(void) {
  static gboolean installed = FALSE;
  if (installed)
    return;

  GtkCssProvider *provider = gtk_css_provider_new();
  const char *css =
      ".shaula-properties-panel {"
      "  background: alpha(@theme_bg_color, 0.94);"
      "  border: 1px solid @borders;"
      "  border-radius: 8px;"
      "  padding: 6px;"
      "  color: @theme_fg_color;"
      "}"
      ".shaula-properties-panel button {"
      "  min-width: 28px;"
      "  min-height: 28px;"
      "  padding: 2px;"
      "  color: @theme_fg_color;"
      "  border: 1px solid transparent;"
      "  border-radius: 7px;"
      "  background: transparent;"
      "}"
      ".shaula-properties-panel button:hover {"
      "  background: alpha(@theme_fg_color, 0.08);"
      "  border-color: alpha(@theme_fg_color, 0.12);"
      "}"
      ".shaula-properties-panel button:checked,"
      ".shaula-properties-panel button:active {"
      "  background: alpha(@theme_fg_color, 0.14);"
      "  border-color: alpha(@theme_fg_color, 0.20);"
      "}"
      ".shaula-properties-panel .linked > button:not(:first-child) {\n"
      "  border-top-left-radius: 0;\n"
      "  border-bottom-left-radius: 0;\n"
      "  border-left-color: transparent;\n"
      "}\n"
      ".shaula-properties-panel .linked > button:not(:last-child) {\n"
      "  border-top-right-radius: 0;\n"
      "  border-bottom-right-radius: 0;\n"
      "  border-right-color: transparent;\n"
      "}\n"
      ".shaula-properties-panel separator {\n"
      "  margin: 4px 2px;\n"
      "  background: alpha(@theme_fg_color, 0.1);\n"
      "}\n"
      ".shaula-properties-panel colorbutton {"
      "  min-width: 30px;"
      "  min-height: 28px;"
      "  padding: 0;"
      "}"
      ".shaula-properties-panel scale trough {"
      "  min-height: 4px;"
      "}";
  gtk_css_provider_load_from_data(provider, css, -1);
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
  installed = TRUE;
}

static void set_theme_fg_source(GtkWidget *widget, cairo_t *cr, double alpha) {
  GdkRGBA color;
  gtk_style_context_get_color(gtk_widget_get_style_context(widget), &color);
  color.alpha *= alpha;
  gdk_cairo_set_source_rgba(cr, &color);
}

static void draw_back_icon(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                           gpointer data) {
  (void)data;
  set_theme_fg_source(GTK_WIDGET(area), cr, 0.94);
  cairo_set_line_width(cr, 2.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_move_to(cr, w * 0.62, h * 0.28);
  cairo_line_to(cr, w * 0.38, h * 0.50);
  cairo_line_to(cr, w * 0.62, h * 0.72);
  cairo_stroke(cr);
}

static void draw_rectangle_icon(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                                gpointer data) {
  (void)data;
  set_theme_fg_source(GTK_WIDGET(area), cr, 0.92);
  cairo_set_line_width(cr, 1.8);
  cairo_rectangle(cr, w * 0.24, h * 0.32, w * 0.52, h * 0.36);
  cairo_stroke(cr);
}

static void draw_rounded_icon(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                              gpointer data) {
  (void)data;
  double x = w * 0.24;
  double y = h * 0.32;
  double rw = w * 0.52;
  double rh = h * 0.36;
  double r = MIN(rw, rh) * 0.35;
  set_theme_fg_source(GTK_WIDGET(area), cr, 0.92);
  cairo_set_line_width(cr, 1.8);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + rw - r, y + r, r, -0.5 * G_PI, 0);
  cairo_arc(cr, x + rw - r, y + rh - r, r, 0, 0.5 * G_PI);
  cairo_arc(cr, x + r, y + rh - r, r, 0.5 * G_PI, G_PI);
  cairo_arc(cr, x + r, y + r, r, G_PI, 1.5 * G_PI);
  cairo_close_path(cr);
  cairo_stroke(cr);
}

static void draw_fill_icon(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                           gpointer data) {
  (void)data;
  set_theme_fg_source(GTK_WIDGET(area), cr, 0.92);
  cairo_rectangle(cr, w * 0.26, h * 0.30, w * 0.48, h * 0.40);
  cairo_fill_preserve(cr);
  set_theme_fg_source(GTK_WIDGET(area), cr, 0.92);
  cairo_set_line_width(cr, 1.6);
  cairo_stroke(cr);
}

static GtkWidget *make_panel_icon(GtkDrawingAreaDrawFunc draw_func) {
  GtkWidget *icon = gtk_drawing_area_new();
  gtk_widget_set_size_request(icon, 16, 16);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon), draw_func, NULL, NULL);
  return icon;
}

static GtkWidget *make_panel_button(ShaulaPreviewState *state,
                                    GtkDrawingAreaDrawFunc draw_func,
                                    const char *tooltip, GCallback callback) {
  GtkWidget *button = gtk_button_new();
  gtk_button_set_child(GTK_BUTTON(button), make_panel_icon(draw_func));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 28, 28);
  g_signal_connect(button, "clicked", callback, state);
  return button;
}

static GtkWidget *make_panel_shape_toggle(ShaulaPreviewState *state,
                                          GtkDrawingAreaDrawFunc draw_func,
                                          const char *tooltip,
                                          ShaulaSpotlightShape shape) {
  GtkWidget *button = gtk_toggle_button_new();
  gtk_button_set_child(GTK_BUTTON(button), make_panel_icon(draw_func));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 28, 28);
  g_object_set_data(G_OBJECT(button), "spotlight-shape",
                    GINT_TO_POINTER(shape));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(shaula_preview_on_spotlight_shape_clicked),
                   state);
  return button;
}

static GtkWidget *
make_arrow_stroke_style_toggle(ShaulaPreviewState *state, const char *icon_name,
                               const char *tooltip,
                               PreviewArrowStrokeStyle style) {
  GtkWidget *button = gtk_toggle_button_new();
  gtk_button_set_child(GTK_BUTTON(button),
                       shaula_preview_make_toolbar_icon(state, icon_name));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 34, 28);
  g_object_set_data(G_OBJECT(button), "arrow-stroke-style",
                    GINT_TO_POINTER(style));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(shaula_preview_on_arrow_stroke_style_clicked),
                   state);
  return button;
}

static GtkWidget *
make_rectangle_stroke_style_toggle(ShaulaPreviewState *state,
                                   const char *icon_name, const char *tooltip,
                                   PreviewArrowStrokeStyle style) {
  GtkWidget *button = gtk_toggle_button_new();
  gtk_button_set_child(GTK_BUTTON(button),
                       shaula_preview_make_toolbar_icon(state, icon_name));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 34, 28);
  g_object_set_data(G_OBJECT(button), "rectangle-stroke-style",
                    GINT_TO_POINTER(style));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(shaula_preview_on_rectangle_stroke_style_clicked),
                   state);
  return button;
}

static GtkWidget *make_rectangle_corner_toggle(
    ShaulaPreviewState *state, GtkDrawingAreaDrawFunc draw_func,
    const char *tooltip, PreviewRectangleCorners corners) {
  GtkWidget *button = gtk_toggle_button_new();
  gtk_button_set_child(GTK_BUTTON(button), make_panel_icon(draw_func));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 34, 28);
  g_object_set_data(G_OBJECT(button), "rectangle-corners",
                    GINT_TO_POINTER(corners));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(shaula_preview_on_rectangle_corners_clicked),
                   state);
  return button;
}

static GtkWidget *make_text_align_toggle(ShaulaPreviewState *state,
                                         const char *icon_name,
                                         const char *tooltip,
                                         ShaulaTextAlign align) {
  GtkWidget *button = gtk_toggle_button_new();
  gtk_button_set_child(GTK_BUTTON(button),
                       shaula_preview_make_toolbar_icon(state, icon_name));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 34, 28);
  g_object_set_data(G_OBJECT(button), "text-align", GINT_TO_POINTER(align));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(shaula_preview_on_text_align_clicked), state);
  return button;
}

static GtkWidget *make_selection_action_button(ShaulaPreviewState *state,
                                               const char *icon_name,
                                               const char *tooltip,
                                               GCallback callback) {
  GtkWidget *button = gtk_button_new();
  gtk_button_set_child(GTK_BUTTON(button),
                       shaula_preview_make_toolbar_icon(state, icon_name));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 28, 28);
  g_signal_connect(button, "clicked", callback, state);
  return button;
}

static GtkWidget *make_text_size_toggle(ShaulaPreviewState *state,
                                        const char *label, const char *tooltip,
                                        double font_size) {
  GtkWidget *button = gtk_toggle_button_new_with_label(label);
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 28, 28);
  g_object_set_data(G_OBJECT(button), "text-font-size",
                    GINT_TO_POINTER((int)font_size));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(shaula_preview_on_text_size_clicked), state);
  return button;
}

static GtkWidget *make_text_style_toggle(ShaulaPreviewState *state,
                                         const char *tooltip,
                                         ShaulaTextFontMode font_mode) {
  GtkWidget *button = gtk_toggle_button_new();
  GtkWidget *label = gtk_label_new("Ab");
  gtk_label_set_xalign(GTK_LABEL(label), 0.5);
  gtk_widget_set_hexpand(label, TRUE);
  PangoAttrList *attrs = pango_attr_list_new();
  pango_attr_list_insert(
      attrs, pango_attr_family_new(shaula_text_font_family(font_mode)));
  pango_attr_list_insert(attrs, pango_attr_weight_new(
                                    font_mode == SHAULA_TEXT_FONT_SKETCH
                                        ? PANGO_WEIGHT_NORMAL
                                        : PANGO_WEIGHT_BOLD));
  pango_attr_list_insert(attrs, pango_attr_size_new(15 * PANGO_SCALE));
  gtk_label_set_attributes(GTK_LABEL(label), attrs);
  pango_attr_list_unref(attrs);
  gtk_button_set_child(GTK_BUTTON(button), label);
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                 GTK_ACCESSIBLE_PROPERTY_LABEL, tooltip, -1);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 34, 28);
  g_object_set_data(G_OBJECT(button), "text-font-mode",
                    GINT_TO_POINTER(font_mode));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(shaula_preview_on_text_style_clicked), state);
  return button;
}

GtkWidget *
shaula_preview_select_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  state->duplicate_button = make_selection_action_button(
      state, "shaula-duplicate-symbolic",
      "Duplicate selected annotation (Ctrl+D)",
      G_CALLBACK(shaula_preview_on_duplicate_clicked));
  state->crop_selected_button = make_selection_action_button(
      state, "shaula-crop-symbolic", "Crop to selected annotation",
      G_CALLBACK(shaula_preview_on_crop_selected_clicked));
  state->blur_region_button = make_selection_action_button(
      state, "shaula-blur-symbolic", "Blur selected region",
      G_CALLBACK(shaula_preview_on_blur_region_clicked));
  state->erase_region_button = make_selection_action_button(
      state, "shaula-erase-symbolic", "Erase selected region",
      G_CALLBACK(shaula_preview_on_erase_region_clicked));
  state->spotlight_region_button = make_selection_action_button(
      state, "shaula-spotlight-symbolic", "Spotlight selected region",
      G_CALLBACK(shaula_preview_on_spotlight_region_clicked));
  state->delete_button = make_selection_action_button(
      state, "shaula-trash-symbolic", "Delete selected annotation (Delete)",
      G_CALLBACK(shaula_preview_on_delete_clicked));

  gtk_box_append(GTK_BOX(panel), state->duplicate_button);
  gtk_box_append(GTK_BOX(panel), state->crop_selected_button);
  gtk_box_append(GTK_BOX(panel), state->blur_region_button);
  gtk_box_append(GTK_BOX(panel), state->erase_region_button);
  gtk_box_append(GTK_BOX(panel), state->spotlight_region_button);
  gtk_box_append(GTK_BOX(panel), state->delete_button);

  state->selection_actions_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

GtkWidget *shaula_preview_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back =
      make_panel_button(state, draw_back_icon, "Back",
                        G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->properties_hud.spotlight_color_button = color;
  GdkRGBA rgba = {
      state->tool_defaults.spotlight.border_color.r,
      state->tool_defaults.spotlight.border_color.g,
      state->tool_defaults.spotlight.border_color.b,
      state->tool_defaults.spotlight.border_color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Spotlight border color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_spotlight_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 16.0, 1.0);
  state->properties_hud.spotlight_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->tool_defaults.spotlight.border_width);
  gtk_widget_set_tooltip_text(width, "Spotlight border width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_spotlight_width_changed),
                   state);
  gtk_box_append(GTK_BOX(panel), width);

  gtk_box_append(GTK_BOX(panel), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

  GtkWidget *spotlight_shape_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(spotlight_shape_box, "linked");

  state->properties_hud.spotlight_sharp_button = make_panel_shape_toggle(
      state, draw_rectangle_icon, "Pointed spotlight corners",
      SHAULA_SPOTLIGHT_SHAPE_SHARP_RECTANGLE);
  state->properties_hud.spotlight_rounded_button = make_panel_shape_toggle(
      state, draw_rounded_icon, "Rounded spotlight corners",
      SHAULA_SPOTLIGHT_SHAPE_ROUNDED_RECTANGLE);
  gtk_box_append(GTK_BOX(spotlight_shape_box), state->properties_hud.spotlight_sharp_button);
  gtk_box_append(GTK_BOX(spotlight_shape_box), state->properties_hud.spotlight_rounded_button);
  gtk_box_append(GTK_BOX(panel), spotlight_shape_box);

  state->properties_hud.properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

/* Arrow properties HUD targets a just-created or currently selected Arrow
 * annotation. Controls: Back, Color, Stroke width, and stroke style.
 */
GtkWidget *
shaula_preview_arrow_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back =
      make_panel_button(state, draw_back_icon, "Back",
                        G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->properties_hud.arrow_color_button = color;
  GdkRGBA rgba = {state->tool_defaults.arrow_line.color.r,
                  state->tool_defaults.arrow_line.color.g,
                  state->tool_defaults.arrow_line.color.b,
                  state->tool_defaults.arrow_line.color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Arrow color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_arrow_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 12.0, 0.5);
  state->properties_hud.arrow_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->tool_defaults.arrow_line.stroke_width);
  gtk_widget_set_tooltip_text(width, "Arrow stroke width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_arrow_width_changed), state);
  gtk_box_append(GTK_BOX(panel), width);

  gtk_box_append(GTK_BOX(panel), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

  GtkWidget *arrow_stroke_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(arrow_stroke_box, "linked");

  state->properties_hud.arrow_stroke_buttons[PREVIEW_ARROW_STROKE_SOLID] =
      make_arrow_stroke_style_toggle(state, "shaula-line-solid-symbolic",
                                     "Normal arrow stroke",
                                     PREVIEW_ARROW_STROKE_SOLID);
  state->properties_hud.arrow_stroke_buttons[PREVIEW_ARROW_STROKE_DASHED] =
      make_arrow_stroke_style_toggle(state, "shaula-line-dashed-symbolic",
                                     "Dashed arrow stroke",
                                     PREVIEW_ARROW_STROKE_DASHED);
  state->properties_hud.arrow_stroke_buttons[PREVIEW_ARROW_STROKE_DOTTED] =
      make_arrow_stroke_style_toggle(state, "shaula-line-dotted-symbolic",
                                     "Dotted arrow stroke",
                                     PREVIEW_ARROW_STROKE_DOTTED);
  gtk_box_append(GTK_BOX(arrow_stroke_box),
                 state->properties_hud.arrow_stroke_buttons[PREVIEW_ARROW_STROKE_SOLID]);
  gtk_box_append(GTK_BOX(arrow_stroke_box),
                 state->properties_hud.arrow_stroke_buttons[PREVIEW_ARROW_STROKE_DASHED]);
  gtk_box_append(GTK_BOX(arrow_stroke_box),
                 state->properties_hud.arrow_stroke_buttons[PREVIEW_ARROW_STROKE_DOTTED]);
  gtk_box_append(GTK_BOX(panel), arrow_stroke_box);

  state->properties_hud.arrow_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

GtkWidget *
shaula_preview_rectangle_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back =
      make_panel_button(state, draw_back_icon, "Back",
                        G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->properties_hud.rectangle_color_button = color;
  GdkRGBA rgba = {state->tool_defaults.rectangle.color.r,
                  state->tool_defaults.rectangle.color.g,
                  state->tool_defaults.rectangle.color.b,
                  state->tool_defaults.rectangle.color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Rectangle color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_rectangle_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 12.0, 0.5);
  state->properties_hud.rectangle_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->tool_defaults.rectangle.stroke_width);
  gtk_widget_set_tooltip_text(width, "Rectangle stroke width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_rectangle_width_changed),
                   state);
  gtk_box_append(GTK_BOX(panel), width);

  gtk_box_append(GTK_BOX(panel), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

  GtkWidget *stroke_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(stroke_box, "linked");
  state->properties_hud.rectangle_stroke_buttons[PREVIEW_ARROW_STROKE_SOLID] =
      make_rectangle_stroke_style_toggle(state, "shaula-line-solid-symbolic",
                                         "Normal rectangle stroke",
                                         PREVIEW_ARROW_STROKE_SOLID);
  state->properties_hud.rectangle_stroke_buttons[PREVIEW_ARROW_STROKE_DASHED] =
      make_rectangle_stroke_style_toggle(state, "shaula-line-dashed-symbolic",
                                         "Dashed rectangle stroke",
                                         PREVIEW_ARROW_STROKE_DASHED);
  gtk_box_append(GTK_BOX(stroke_box),
                 state->properties_hud.rectangle_stroke_buttons[PREVIEW_ARROW_STROKE_SOLID]);
  gtk_box_append(GTK_BOX(stroke_box),
                 state->properties_hud.rectangle_stroke_buttons[PREVIEW_ARROW_STROKE_DASHED]);
  gtk_box_append(GTK_BOX(panel), stroke_box);

  gtk_box_append(GTK_BOX(panel), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

  GtkWidget *fill = gtk_toggle_button_new();
  state->properties_hud.rectangle_fill_button = fill;
  gtk_button_set_child(GTK_BUTTON(fill), make_panel_icon(draw_fill_icon));
  gtk_widget_set_tooltip_text(fill, "Fill rectangle");
  gtk_widget_add_css_class(fill, "flat");
  gtk_widget_set_valign(fill, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(fill, 34, 28);
  g_signal_connect(fill, "clicked",
                   G_CALLBACK(shaula_preview_on_rectangle_fill_toggled), state);
  gtk_box_append(GTK_BOX(panel), fill);

  gtk_box_append(GTK_BOX(panel), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

  GtkWidget *corner_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(corner_box, "linked");
  state->properties_hud.rectangle_corner_buttons[PREVIEW_RECTANGLE_CORNERS_ROUNDED] =
      make_rectangle_corner_toggle(state, draw_rounded_icon,
                                   "Rounded rectangle corners",
                                   PREVIEW_RECTANGLE_CORNERS_ROUNDED);
  state->properties_hud.rectangle_corner_buttons[PREVIEW_RECTANGLE_CORNERS_SQUARE] =
      make_rectangle_corner_toggle(state, draw_rectangle_icon,
                                   "Square rectangle corners",
                                   PREVIEW_RECTANGLE_CORNERS_SQUARE);
  gtk_box_append(
      GTK_BOX(corner_box),
      state->properties_hud.rectangle_corner_buttons[PREVIEW_RECTANGLE_CORNERS_ROUNDED]);
  gtk_box_append(
      GTK_BOX(corner_box),
      state->properties_hud.rectangle_corner_buttons[PREVIEW_RECTANGLE_CORNERS_SQUARE]);
  gtk_box_append(GTK_BOX(panel), corner_box);

  state->properties_hud.rectangle_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

GtkWidget *
shaula_preview_pen_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back =
      make_panel_button(state, draw_back_icon, "Back",
                        G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->properties_hud.pen_color_button = color;
  GdkRGBA rgba = {state->tool_defaults.pen.color.r,
                  state->tool_defaults.pen.color.g,
                  state->tool_defaults.pen.color.b,
                  state->tool_defaults.pen.color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Pen color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_pen_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 24.0, 0.5);
  state->properties_hud.pen_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->tool_defaults.pen.stroke_width);
  gtk_widget_set_tooltip_text(width, "Pen stroke width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_pen_width_changed), state);
  gtk_box_append(GTK_BOX(panel), width);

  GtkWidget *opacity =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 1.0, 0.05);
  state->properties_hud.pen_opacity_scale = opacity;
  gtk_range_set_value(GTK_RANGE(opacity), state->tool_defaults.pen.opacity);
  gtk_widget_set_tooltip_text(opacity, "Pen opacity");
  gtk_widget_set_size_request(opacity, 90, -1);
  gtk_scale_set_draw_value(GTK_SCALE(opacity), FALSE);
  gtk_widget_set_valign(opacity, GTK_ALIGN_CENTER);
  g_signal_connect(opacity, "value-changed",
                   G_CALLBACK(shaula_preview_on_pen_opacity_changed), state);
  gtk_box_append(GTK_BOX(panel), opacity);

  state->properties_hud.pen_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

GtkWidget *
shaula_preview_highlight_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back =
      make_panel_button(state, draw_back_icon, "Back",
                        G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->properties_hud.highlight_color_button = color;
  GdkRGBA rgba = {state->tool_defaults.highlight.color.r,
                  state->tool_defaults.highlight.color.g,
                  state->tool_defaults.highlight.color.b,
                  state->tool_defaults.highlight.color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Highlight color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_highlight_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 4.0, 48.0, 1.0);
  state->properties_hud.highlight_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->tool_defaults.highlight.stroke_width);
  gtk_widget_set_tooltip_text(width, "Highlight width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_highlight_width_changed),
                   state);
  gtk_box_append(GTK_BOX(panel), width);

  GtkWidget *opacity =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.05, 1.0, 0.05);
  state->properties_hud.highlight_opacity_scale = opacity;
  gtk_range_set_value(GTK_RANGE(opacity), state->tool_defaults.highlight.opacity);
  gtk_widget_set_tooltip_text(opacity, "Highlight opacity");
  gtk_widget_set_size_request(opacity, 90, -1);
  gtk_scale_set_draw_value(GTK_SCALE(opacity), FALSE);
  gtk_widget_set_valign(opacity, GTK_ALIGN_CENTER);
  g_signal_connect(opacity, "value-changed",
                   G_CALLBACK(shaula_preview_on_highlight_opacity_changed),
                   state);
  gtk_box_append(GTK_BOX(panel), opacity);

  state->properties_hud.highlight_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

GtkWidget *
shaula_preview_text_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back =
      make_panel_button(state, draw_back_icon, "Back",
                        G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->properties_hud.text_color_button = color;
  GdkRGBA rgba = {state->tool_defaults.text.color.r,
                  state->tool_defaults.text.color.g,
                  state->tool_defaults.text.color.b,
                  state->tool_defaults.text.color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Text color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_text_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  // Size buttons box
  GtkWidget *size_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(size_box, "linked");

  state->properties_hud.text_size_buttons[0] =
      make_text_size_toggle(state, "S", "Small", 16.0);
  state->properties_hud.text_size_buttons[1] =
      make_text_size_toggle(state, "M", "Medium", 24.0);
  state->properties_hud.text_size_buttons[2] =
      make_text_size_toggle(state, "L", "Large", 36.0);
  state->properties_hud.text_size_buttons[3] =
      make_text_size_toggle(state, "XL", "Extra Large", 64.0);

  gtk_box_append(GTK_BOX(size_box), state->properties_hud.text_size_buttons[0]);
  gtk_box_append(GTK_BOX(size_box), state->properties_hud.text_size_buttons[1]);
  gtk_box_append(GTK_BOX(size_box), state->properties_hud.text_size_buttons[2]);
  gtk_box_append(GTK_BOX(size_box), state->properties_hud.text_size_buttons[3]);

  gtk_box_append(GTK_BOX(panel), size_box);

  gtk_box_append(GTK_BOX(panel), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

  // Style buttons box
  GtkWidget *style_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(style_box, "linked");

  state->properties_hud.text_font_mode_buttons[SHAULA_TEXT_FONT_NORMAL] =
      make_text_style_toggle(state, "Normal", SHAULA_TEXT_FONT_NORMAL);
  state->properties_hud.text_font_mode_buttons[SHAULA_TEXT_FONT_SKETCH] =
      make_text_style_toggle(state, "Sketch", SHAULA_TEXT_FONT_SKETCH);

  gtk_box_append(GTK_BOX(style_box),
                 state->properties_hud.text_font_mode_buttons[SHAULA_TEXT_FONT_NORMAL]);
  gtk_box_append(GTK_BOX(style_box),
                 state->properties_hud.text_font_mode_buttons[SHAULA_TEXT_FONT_SKETCH]);

  gtk_box_append(GTK_BOX(panel), style_box);

  gtk_box_append(GTK_BOX(panel), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

  GtkWidget *text_align_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(text_align_box, "linked");

  state->properties_hud.text_align_buttons[SHAULA_TEXT_ALIGN_LEFT] =
      make_text_align_toggle(state, "shaula-align-left-symbolic",
                             "Align text left", SHAULA_TEXT_ALIGN_LEFT);
  state->properties_hud.text_align_buttons[SHAULA_TEXT_ALIGN_CENTER] =
      make_text_align_toggle(state, "shaula-align-center-symbolic",
                             "Align text center", SHAULA_TEXT_ALIGN_CENTER);
  state->properties_hud.text_align_buttons[SHAULA_TEXT_ALIGN_RIGHT] =
      make_text_align_toggle(state, "shaula-align-right-symbolic",
                             "Align text right", SHAULA_TEXT_ALIGN_RIGHT);
  gtk_box_append(GTK_BOX(text_align_box),
                 state->properties_hud.text_align_buttons[SHAULA_TEXT_ALIGN_LEFT]);
  gtk_box_append(GTK_BOX(text_align_box),
                 state->properties_hud.text_align_buttons[SHAULA_TEXT_ALIGN_CENTER]);
  gtk_box_append(GTK_BOX(text_align_box),
                 state->properties_hud.text_align_buttons[SHAULA_TEXT_ALIGN_RIGHT]);
  gtk_box_append(GTK_BOX(panel), text_align_box);

  state->properties_hud.text_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

GtkWidget *
shaula_preview_measure_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back =
      make_panel_button(state, draw_back_icon, "Back",
                        G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->properties_hud.measure_color_button = color;
  GdkRGBA rgba = {state->tool_defaults.measure.color.r,
                  state->tool_defaults.measure.color.g,
                  state->tool_defaults.measure.color.b,
                  state->tool_defaults.measure.color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Measure color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_measure_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width =
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 8.0, 0.5);
  state->properties_hud.measure_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->tool_defaults.measure.stroke_width);
  gtk_widget_set_tooltip_text(width, "Measure stroke width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_measure_width_changed), state);
  gtk_box_append(GTK_BOX(panel), width);

  state->properties_hud.measure_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

GtkWidget *
shaula_preview_eraser_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back =
      make_panel_button(state, draw_back_icon, "Back",
                        G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *size = gtk_scale_new_with_range(
      GTK_ORIENTATION_HORIZONTAL, SHAULA_ERASER_SIZE_MIN,
      SHAULA_ERASER_SIZE_MAX, SHAULA_ERASER_SIZE_STEP);
  state->properties_hud.eraser_size_scale = size;
  gtk_range_set_value(GTK_RANGE(size), state->tool_defaults.eraser.size);
  gtk_widget_set_tooltip_text(size, "Eraser size");
  gtk_widget_set_size_request(size, 140, -1);
  gtk_scale_set_draw_value(GTK_SCALE(size), TRUE);
  gtk_scale_set_digits(GTK_SCALE(size), 0);
  gtk_scale_set_value_pos(GTK_SCALE(size), GTK_POS_RIGHT);
  gtk_widget_set_valign(size, GTK_ALIGN_CENTER);
  g_signal_connect(size, "value-changed",
                   G_CALLBACK(shaula_preview_on_eraser_size_changed), state);
  gtk_box_append(GTK_BOX(panel), size);

  state->properties_hud.eraser_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}
