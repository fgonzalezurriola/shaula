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
      "  box-shadow: 0 10px 28px alpha(@theme_fg_color, 0.18);"
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

static GtkWidget *make_panel_icon(GtkDrawingAreaDrawFunc draw_func) {
  GtkWidget *icon = gtk_drawing_area_new();
  gtk_widget_set_size_request(icon, 16, 16);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(icon), draw_func, NULL, NULL);
  return icon;
}

static GtkWidget *make_panel_button(ShaulaPreviewState *state,
                                    GtkDrawingAreaDrawFunc draw_func,
                                    const char *tooltip,
                                    GCallback callback) {
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

static GtkWidget *make_arrow_stroke_style_toggle(ShaulaPreviewState *state,
                                                 const char *icon_name,
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

GtkWidget *shaula_preview_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back = make_panel_button(
      state, draw_back_icon, "Back",
      G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->spotlight_color_button = color;
  GdkRGBA rgba = {state->spotlight_border_color.r,
                  state->spotlight_border_color.g,
                  state->spotlight_border_color.b,
                  state->spotlight_border_color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Spotlight border color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_spotlight_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0,
                                              16.0, 1.0);
  state->spotlight_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->spotlight_border_width);
  gtk_widget_set_tooltip_text(width, "Spotlight border width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_spotlight_width_changed),
                   state);
  gtk_box_append(GTK_BOX(panel), width);

  state->spotlight_sharp_button =
      make_panel_shape_toggle(state, draw_rectangle_icon,
                              "Pointed spotlight corners",
                              SHAULA_SPOTLIGHT_SHAPE_SHARP_RECTANGLE);
  state->spotlight_rounded_button =
      make_panel_shape_toggle(state, draw_rounded_icon,
                              "Rounded spotlight corners",
                              SHAULA_SPOTLIGHT_SHAPE_ROUNDED_RECTANGLE);
  gtk_box_append(GTK_BOX(panel), state->spotlight_sharp_button);
  gtk_box_append(GTK_BOX(panel), state->spotlight_rounded_button);

  state->properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

/* Arrow properties HUD targets a just-created or currently selected Arrow
 * annotation. Controls: Back, Color, Stroke width, and stroke style.
 */
GtkWidget *shaula_preview_arrow_properties_panel_build(
    ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back = make_panel_button(
      state, draw_back_icon, "Back",
      G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->arrow_color_button = color;
  GdkRGBA rgba = {state->arrow_color.r, state->arrow_color.g,
                   state->arrow_color.b, state->arrow_color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Arrow color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_arrow_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0,
                                              12.0, 0.5);
  state->arrow_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->arrow_stroke_width);
  gtk_widget_set_tooltip_text(width, "Arrow stroke width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_arrow_width_changed), state);
  gtk_box_append(GTK_BOX(panel), width);

  state->arrow_stroke_buttons[PREVIEW_ARROW_STROKE_SOLID] =
      make_arrow_stroke_style_toggle(state, "shaula-line-solid-symbolic",
                                     "Normal arrow stroke",
                                     PREVIEW_ARROW_STROKE_SOLID);
  state->arrow_stroke_buttons[PREVIEW_ARROW_STROKE_DASHED] =
      make_arrow_stroke_style_toggle(state, "shaula-line-dashed-symbolic",
                                     "Dashed arrow stroke",
                                     PREVIEW_ARROW_STROKE_DASHED);
  state->arrow_stroke_buttons[PREVIEW_ARROW_STROKE_DOTTED] =
      make_arrow_stroke_style_toggle(state, "shaula-line-dotted-symbolic",
                                     "Dotted arrow stroke",
                                     PREVIEW_ARROW_STROKE_DOTTED);
  gtk_box_append(GTK_BOX(panel),
                 state->arrow_stroke_buttons[PREVIEW_ARROW_STROKE_SOLID]);
  gtk_box_append(GTK_BOX(panel),
                 state->arrow_stroke_buttons[PREVIEW_ARROW_STROKE_DASHED]);
  gtk_box_append(GTK_BOX(panel),
                 state->arrow_stroke_buttons[PREVIEW_ARROW_STROKE_DOTTED]);

  state->arrow_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

GtkWidget *shaula_preview_pen_properties_panel_build(ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back = make_panel_button(
      state, draw_back_icon, "Back",
      G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->pen_color_button = color;
  GdkRGBA rgba = {state->pen_color.r, state->pen_color.g,
                  state->pen_color.b, state->pen_color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Pen color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_pen_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0,
                                              24.0, 0.5);
  state->pen_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->pen_stroke_width);
  gtk_widget_set_tooltip_text(width, "Pen stroke width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_pen_width_changed), state);
  gtk_box_append(GTK_BOX(panel), width);

  GtkWidget *opacity = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1,
                                                1.0, 0.05);
  state->pen_opacity_scale = opacity;
  gtk_range_set_value(GTK_RANGE(opacity), state->pen_opacity);
  gtk_widget_set_tooltip_text(opacity, "Pen opacity");
  gtk_widget_set_size_request(opacity, 90, -1);
  gtk_scale_set_draw_value(GTK_SCALE(opacity), FALSE);
  gtk_widget_set_valign(opacity, GTK_ALIGN_CENTER);
  g_signal_connect(opacity, "value-changed",
                   G_CALLBACK(shaula_preview_on_pen_opacity_changed), state);
  gtk_box_append(GTK_BOX(panel), opacity);

  state->pen_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}

GtkWidget *shaula_preview_highlight_properties_panel_build(
    ShaulaPreviewState *state) {
  install_properties_panel_css();

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(panel, "shaula-properties-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_END);
  gtk_widget_set_valign(panel, GTK_ALIGN_START);
  gtk_widget_set_margin_top(panel, 16);
  gtk_widget_set_margin_end(panel, 16);

  GtkWidget *back = make_panel_button(
      state, draw_back_icon, "Back",
      G_CALLBACK(shaula_preview_on_properties_back_clicked));
  gtk_box_append(GTK_BOX(panel), back);

  GtkWidget *color = gtk_color_button_new();
  state->highlight_color_button = color;
  GdkRGBA rgba = {state->highlight_color.r, state->highlight_color.g,
                  state->highlight_color.b, state->highlight_color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color), &rgba);
  gtk_widget_set_tooltip_text(color, "Highlight color");
  gtk_widget_set_valign(color, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(color, 30, 28);
  g_signal_connect(color, "color-set",
                   G_CALLBACK(shaula_preview_on_highlight_color_set), state);
  gtk_box_append(GTK_BOX(panel), color);

  GtkWidget *width = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 4.0,
                                              48.0, 1.0);
  state->highlight_width_scale = width;
  gtk_range_set_value(GTK_RANGE(width), state->highlight_stroke_width);
  gtk_widget_set_tooltip_text(width, "Highlight width");
  gtk_widget_set_size_request(width, 120, -1);
  gtk_scale_set_draw_value(GTK_SCALE(width), FALSE);
  gtk_widget_set_valign(width, GTK_ALIGN_CENTER);
  g_signal_connect(width, "value-changed",
                   G_CALLBACK(shaula_preview_on_highlight_width_changed),
                   state);
  gtk_box_append(GTK_BOX(panel), width);

  GtkWidget *opacity = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.05,
                                                1.0, 0.05);
  state->highlight_opacity_scale = opacity;
  gtk_range_set_value(GTK_RANGE(opacity), state->highlight_opacity);
  gtk_widget_set_tooltip_text(opacity, "Highlight opacity");
  gtk_widget_set_size_request(opacity, 90, -1);
  gtk_scale_set_draw_value(GTK_SCALE(opacity), FALSE);
  gtk_widget_set_valign(opacity, GTK_ALIGN_CENTER);
  g_signal_connect(opacity, "value-changed",
                   G_CALLBACK(shaula_preview_on_highlight_opacity_changed),
                   state);
  gtk_box_append(GTK_BOX(panel), opacity);

  state->highlight_properties_box = panel;
  gtk_widget_set_visible(panel, FALSE);
  return panel;
}
