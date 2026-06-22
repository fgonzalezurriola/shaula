#include "preview_properties_hud.h"

#include <math.h>
#include <string.h>

#include <gtk/gtk.h>

#include "preview_annotation_editor.h"
#include "preview_state.h"
#include "preview_toolbar.h"

typedef struct {
  ShaulaStrokeToolDefaults arrow;
  ShaulaRectangleToolDefaults rectangle;
  ShaulaFreehandToolDefaults pen;
  ShaulaFreehandToolDefaults highlight;
  ShaulaTextToolDefaults text;
  ShaulaMeasureToolDefaults measure;
  ShaulaSpotlightToolDefaults spotlight;
  double eraser_size;
} ShaulaPropertyValues;

static gboolean colors_equal(ShaulaColor a, ShaulaColor b) {
  return fabs(a.r - b.r) <= 0.0001 && fabs(a.g - b.g) <= 0.0001 &&
         fabs(a.b - b.b) <= 0.0001 && fabs(a.a - b.a) <= 0.0001;
}

static ShaulaPropertiesPanel
panel_for_annotation(const ShaulaAnnotation *annotation) {
  if (annotation == NULL)
    return SHAULA_PROPERTIES_PANEL_NONE;
  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    return SHAULA_PROPERTIES_PANEL_ARROW;
  case SHAULA_ANNOTATION_RECTANGLE:
    return SHAULA_PROPERTIES_PANEL_RECTANGLE;
  case SHAULA_ANNOTATION_PEN:
    return SHAULA_PROPERTIES_PANEL_PEN;
  case SHAULA_ANNOTATION_HIGHLIGHT:
    return SHAULA_PROPERTIES_PANEL_HIGHLIGHT;
  case SHAULA_ANNOTATION_TEXT:
    return SHAULA_PROPERTIES_PANEL_TEXT;
  case SHAULA_ANNOTATION_MEASURE:
    return SHAULA_PROPERTIES_PANEL_MEASURE;
  case SHAULA_ANNOTATION_IMAGE:
    return SHAULA_PROPERTIES_PANEL_NONE;
  }
  return SHAULA_PROPERTIES_PANEL_NONE;
}

static ShaulaAnnotation *selected_annotation_of_type(
    ShaulaPreviewState *state, ShaulaAnnotationType type) {
  ShaulaAnnotation *annotation =
      shaula_annotation_editor_single_selection(state);
  return annotation != NULL && annotation->type == type ? annotation : NULL;
}

static ShaulaSpotlightRegion *active_spotlight_region(
    ShaulaPreviewState *state) {
  if (state == NULL || state->document.spotlight_regions == NULL ||
      state->properties_hud.spotlight_index < 0 ||
      (guint)state->properties_hud.spotlight_index >=
          state->document.spotlight_regions->len)
    return NULL;
  return &g_array_index(state->document.spotlight_regions,
                        ShaulaSpotlightRegion,
                        (guint)state->properties_hud.spotlight_index);
}

static void persist_color_default(
    ShaulaPreviewState *state, ShaulaColor *target, ShaulaColor value,
    ShaulaToolDefaultsDirtyGroup dirty_group) {
  if (colors_equal(*target, value))
    return;
  *target = value;
  shaula_tool_defaults_mark_dirty(&state->tool_defaults, dirty_group);
}

static void persist_double_default(
    ShaulaPreviewState *state, double *target, double value,
    ShaulaToolDefaultsDirtyGroup dirty_group) {
  if (fabs(*target - value) <= 0.0001)
    return;
  *target = value;
  shaula_tool_defaults_mark_dirty(&state->tool_defaults, dirty_group);
}

static void begin_property_history_if_targeted(ShaulaPreviewState *state,
                                               gboolean has_target) {
  if (!has_target)
    return;
  gboolean starts_transaction =
      state->document.pending_history_snapshot == NULL;
  shaula_preview_begin_history_gesture(state);
  if (starts_transaction)
    shaula_preview_history_clear_redo(&state->document.history);
  shaula_preview_toolbar_update_history_state(state);
}

static gboolean property_input_allowed(const ShaulaPreviewState *state) {
  return state != NULL && !state->properties_hud.syncing_widgets;
}

static void finish_property_change(ShaulaPreviewState *state) {
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

static void sync_color_button(GtkWidget *widget, ShaulaColor color) {
  if (widget == NULL)
    return;
  GdkRGBA rgba = {color.r, color.g, color.b, color.a};
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(widget), &rgba);
}

static void sync_range(GtkWidget *widget, double value) {
  if (widget == NULL)
    return;
  GtkRange *range = GTK_RANGE(widget);
  if (fabs(gtk_range_get_value(range) - value) > 0.01)
    gtk_range_set_value(range, value);
}

static void sync_toggle(GtkWidget *widget, gboolean active) {
  if (widget != NULL)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), active);
}

static void read_property_values(ShaulaPreviewState *state,
                                 ShaulaPropertyValues *values) {
  values->arrow = state->tool_defaults.arrow_line;
  values->rectangle = state->tool_defaults.rectangle;
  values->pen = state->tool_defaults.pen;
  values->highlight = state->tool_defaults.highlight;
  values->text = state->tool_defaults.text;
  values->measure = state->tool_defaults.measure;
  values->spotlight = state->tool_defaults.spotlight;
  values->eraser_size = state->tool_defaults.eraser.size;

  ShaulaAnnotation *selected =
      shaula_annotation_editor_single_selection(state);
  if (selected != NULL) {
    switch (selected->type) {
    case SHAULA_ANNOTATION_ARROW:
      values->arrow.color = selected->color;
      values->arrow.stroke_width = selected->stroke_width;
      values->arrow.stroke_style = selected->data.arrow.stroke_style;
      break;
    case SHAULA_ANNOTATION_RECTANGLE:
      values->rectangle.color = selected->color;
      values->rectangle.stroke_width = selected->stroke_width;
      values->rectangle.stroke_style =
          selected->data.rectangle.stroke_style;
      values->rectangle.filled = selected->data.rectangle.filled;
      values->rectangle.corners = selected->data.rectangle.corners;
      break;
    case SHAULA_ANNOTATION_PEN:
      values->pen.color = selected->color;
      values->pen.stroke_width = selected->stroke_width;
      values->pen.opacity = selected->color.a;
      break;
    case SHAULA_ANNOTATION_HIGHLIGHT:
      values->highlight.color = selected->color;
      values->highlight.stroke_width = selected->stroke_width;
      values->highlight.opacity = selected->color.a;
      break;
    case SHAULA_ANNOTATION_TEXT:
      values->text.color = selected->color;
      values->text.font_size = selected->data.text.font_size;
      values->text.align = selected->data.text.align;
      values->text.font_mode = selected->data.text.font_mode;
      break;
    case SHAULA_ANNOTATION_MEASURE:
      values->measure.color = selected->color;
      values->measure.stroke_width = selected->stroke_width;
      break;
    case SHAULA_ANNOTATION_IMAGE:
      break;
    }
  }

  ShaulaSpotlightRegion *spotlight = active_spotlight_region(state);
  if (spotlight != NULL) {
    values->spotlight.border_color = spotlight->border_color;
    values->spotlight.border_width = spotlight->border_width;
    values->spotlight.shape = spotlight->shape;
  }
}

void shaula_properties_hud_state_init(ShaulaPropertiesHudState *hud) {
  if (hud == NULL)
    return;
  memset(hud, 0, sizeof(*hud));
  hud->active_panel = SHAULA_PROPERTIES_PANEL_NONE;
  hud->spotlight_index = -1;
}

gboolean shaula_properties_hud_set_panel(ShaulaPropertiesHudState *hud,
                                         ShaulaPropertiesPanel panel) {
  if (hud == NULL || hud->active_panel == panel)
    return FALSE;
  hud->active_panel = panel;
  if (panel != SHAULA_PROPERTIES_PANEL_SPOTLIGHT)
    hud->spotlight_index = -1;
  return TRUE;
}

void shaula_properties_hud_show_panel(ShaulaPreviewState *state,
                                      ShaulaPropertiesPanel panel) {
  if (state == NULL || state->properties_hud.active_panel == panel)
    return;
  if (panel == SHAULA_PROPERTIES_PANEL_NONE)
    shaula_preview_commit_history_gesture(state, TRUE);
  (void)shaula_properties_hud_set_panel(&state->properties_hud, panel);
  shaula_preview_toolbar_update_selection_state(state);
  shaula_preview_queue_draw(state);
}

void shaula_properties_hud_target_annotation(
    ShaulaPropertiesHudState *hud, const ShaulaAnnotation *annotation) {
  if (hud == NULL)
    return;
  hud->spotlight_index = -1;
  hud->active_panel = panel_for_annotation(annotation);
}

void shaula_properties_hud_target_spotlight(ShaulaPropertiesHudState *hud,
                                            int spotlight_index) {
  if (hud == NULL)
    return;
  hud->spotlight_index = spotlight_index;
  hud->active_panel = SHAULA_PROPERTIES_PANEL_SPOTLIGHT;
}

void shaula_properties_hud_sync_widgets(ShaulaPreviewState *state) {
  if (state == NULL)
    return;

  ShaulaPropertiesHudState *hud = &state->properties_hud;
  gboolean was_syncing = hud->syncing_widgets;
  hud->syncing_widgets = TRUE;

  if (hud->properties_box != NULL)
    gtk_widget_set_visible(
        hud->properties_box,
        hud->active_panel == SHAULA_PROPERTIES_PANEL_SPOTLIGHT);
  if (hud->arrow_properties_box != NULL)
    gtk_widget_set_visible(
        hud->arrow_properties_box,
        hud->active_panel == SHAULA_PROPERTIES_PANEL_ARROW);
  if (hud->rectangle_properties_box != NULL)
    gtk_widget_set_visible(
        hud->rectangle_properties_box,
        hud->active_panel == SHAULA_PROPERTIES_PANEL_RECTANGLE);
  if (hud->pen_properties_box != NULL)
    gtk_widget_set_visible(
        hud->pen_properties_box,
        hud->active_panel == SHAULA_PROPERTIES_PANEL_PEN);
  if (hud->highlight_properties_box != NULL)
    gtk_widget_set_visible(
        hud->highlight_properties_box,
        hud->active_panel == SHAULA_PROPERTIES_PANEL_HIGHLIGHT);
  if (hud->text_properties_box != NULL)
    gtk_widget_set_visible(
        hud->text_properties_box,
        hud->active_panel == SHAULA_PROPERTIES_PANEL_TEXT);
  if (hud->measure_properties_box != NULL)
    gtk_widget_set_visible(
        hud->measure_properties_box,
        hud->active_panel == SHAULA_PROPERTIES_PANEL_MEASURE);
  if (hud->eraser_properties_box != NULL)
    gtk_widget_set_visible(
        hud->eraser_properties_box,
        hud->active_panel == SHAULA_PROPERTIES_PANEL_ERASER);

  ShaulaPropertyValues values;
  read_property_values(state, &values);

  sync_color_button(hud->arrow_color_button, values.arrow.color);
  sync_range(hud->arrow_width_scale, values.arrow.stroke_width);
  for (int i = PREVIEW_ARROW_STROKE_SOLID;
       i <= PREVIEW_ARROW_STROKE_DOTTED; i++)
    sync_toggle(hud->arrow_stroke_buttons[i],
                i == (int)values.arrow.stroke_style);

  sync_color_button(hud->rectangle_color_button, values.rectangle.color);
  sync_range(hud->rectangle_width_scale, values.rectangle.stroke_width);
  for (int i = PREVIEW_ARROW_STROKE_SOLID;
       i <= PREVIEW_ARROW_STROKE_DASHED; i++)
    sync_toggle(hud->rectangle_stroke_buttons[i],
                i == (int)values.rectangle.stroke_style);
  sync_toggle(hud->rectangle_fill_button, values.rectangle.filled);
  for (int i = PREVIEW_RECTANGLE_CORNERS_ROUNDED;
       i <= PREVIEW_RECTANGLE_CORNERS_SQUARE; i++)
    sync_toggle(hud->rectangle_corner_buttons[i],
                i == (int)values.rectangle.corners);

  sync_color_button(hud->spotlight_color_button,
                    values.spotlight.border_color);
  sync_range(hud->spotlight_width_scale, values.spotlight.border_width);
  sync_toggle(hud->spotlight_sharp_button,
              values.spotlight.shape ==
                  SHAULA_SPOTLIGHT_SHAPE_SHARP_RECTANGLE);
  sync_toggle(hud->spotlight_rounded_button,
              values.spotlight.shape ==
                  SHAULA_SPOTLIGHT_SHAPE_ROUNDED_RECTANGLE);

  sync_color_button(hud->pen_color_button, values.pen.color);
  sync_range(hud->pen_width_scale, values.pen.stroke_width);
  sync_range(hud->pen_opacity_scale, values.pen.opacity);

  sync_color_button(hud->highlight_color_button, values.highlight.color);
  sync_range(hud->highlight_width_scale, values.highlight.stroke_width);
  sync_range(hud->highlight_opacity_scale, values.highlight.opacity);

  sync_color_button(hud->text_color_button, values.text.color);
  const double font_sizes[] = {16.0, 24.0, 36.0, 64.0};
  for (guint i = 0; i < G_N_ELEMENTS(font_sizes); i++)
    sync_toggle(hud->text_size_buttons[i],
                fabs(values.text.font_size - font_sizes[i]) < 0.01);
  for (int i = SHAULA_TEXT_FONT_NORMAL; i <= SHAULA_TEXT_FONT_SKETCH; i++)
    sync_toggle(hud->text_font_mode_buttons[i],
                values.text.font_mode == (ShaulaTextFontMode)i);
  for (int i = SHAULA_TEXT_ALIGN_LEFT; i <= SHAULA_TEXT_ALIGN_RIGHT; i++)
    sync_toggle(hud->text_align_buttons[i],
                i == (int)values.text.align);

  sync_color_button(hud->measure_color_button, values.measure.color);
  sync_range(hud->measure_width_scale, values.measure.stroke_width);
  sync_range(hud->eraser_size_scale, values.eraser_size);

  hud->syncing_widgets = was_syncing;
}

void shaula_properties_hud_set_eraser_size(ShaulaPreviewState *state,
                                           double size) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(size, SHAULA_ERASER_SIZE_MIN, SHAULA_ERASER_SIZE_MAX);
  persist_double_default(state, &state->tool_defaults.eraser.size, next,
                         SHAULA_TOOL_DEFAULTS_DIRTY_ERASER);
  finish_property_change(state);
}

void shaula_properties_hud_set_spotlight_border_color(
    ShaulaPreviewState *state, ShaulaColor color) {
  if (!property_input_allowed(state))
    return;
  persist_color_default(state, &state->tool_defaults.spotlight.border_color,
                        color, SHAULA_TOOL_DEFAULTS_DIRTY_SPOTLIGHT);
  ShaulaSpotlightRegion *region = active_spotlight_region(state);
  if (region != NULL && !colors_equal(region->border_color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    region->border_color = color;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_spotlight_border_width(
    ShaulaPreviewState *state, double width) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(width, 0.0, 16.0);
  persist_double_default(state, &state->tool_defaults.spotlight.border_width,
                         next, SHAULA_TOOL_DEFAULTS_DIRTY_SPOTLIGHT);
  ShaulaSpotlightRegion *region = active_spotlight_region(state);
  if (region != NULL && fabs(region->border_width - next) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    region->border_width = next;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_spotlight_shape(ShaulaPreviewState *state,
                                               ShaulaSpotlightShape shape) {
  if (!property_input_allowed(state))
    return;
  if (state->tool_defaults.spotlight.shape != shape) {
    state->tool_defaults.spotlight.shape = shape;
    shaula_tool_defaults_mark_dirty(&state->tool_defaults,
                                    SHAULA_TOOL_DEFAULTS_DIRTY_SPOTLIGHT);
  }
  ShaulaSpotlightRegion *region = active_spotlight_region(state);
  if (region != NULL && region->shape != shape) {
    begin_property_history_if_targeted(state, TRUE);
    region->shape = shape;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_arrow_color(ShaulaPreviewState *state,
                                           ShaulaColor color) {
  if (!property_input_allowed(state))
    return;
  persist_color_default(state, &state->tool_defaults.arrow_line.color, color,
                        SHAULA_TOOL_DEFAULTS_DIRTY_ARROW_LINE);
  ShaulaAnnotation *arrow =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_ARROW);
  if (arrow != NULL && !colors_equal(arrow->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    arrow->color = color;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_arrow_stroke_width(ShaulaPreviewState *state,
                                                  double width) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(width, 1.0, 12.0);
  persist_double_default(state, &state->tool_defaults.arrow_line.stroke_width,
                         next, SHAULA_TOOL_DEFAULTS_DIRTY_ARROW_LINE);
  ShaulaAnnotation *arrow =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_ARROW);
  if (arrow != NULL && fabs(arrow->stroke_width - next) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    arrow->stroke_width = next;
    shaula_annotation_update_bounds(arrow);
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_arrow_stroke_style(
    ShaulaPreviewState *state, PreviewArrowStrokeStyle style) {
  if (!property_input_allowed(state) || style < PREVIEW_ARROW_STROKE_SOLID ||
      style > PREVIEW_ARROW_STROKE_DOTTED)
    return;
  if (state->tool_defaults.arrow_line.stroke_style != style) {
    state->tool_defaults.arrow_line.stroke_style = style;
    shaula_tool_defaults_mark_dirty(
        &state->tool_defaults, SHAULA_TOOL_DEFAULTS_DIRTY_ARROW_LINE);
  }
  ShaulaAnnotation *arrow =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_ARROW);
  if (arrow != NULL && arrow->data.arrow.stroke_style != style) {
    begin_property_history_if_targeted(state, TRUE);
    arrow->data.arrow.stroke_style = style;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_rectangle_color(ShaulaPreviewState *state,
                                               ShaulaColor color) {
  if (!property_input_allowed(state))
    return;
  persist_color_default(state, &state->tool_defaults.rectangle.color, color,
                        SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE);
  ShaulaAnnotation *rectangle =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_RECTANGLE);
  if (rectangle != NULL && !colors_equal(rectangle->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->color = color;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_rectangle_stroke_width(
    ShaulaPreviewState *state, double width) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(width, 1.0, 12.0);
  persist_double_default(state, &state->tool_defaults.rectangle.stroke_width,
                         next, SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE);
  ShaulaAnnotation *rectangle =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_RECTANGLE);
  if (rectangle != NULL && fabs(rectangle->stroke_width - next) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->stroke_width = next;
    shaula_annotation_update_bounds(rectangle);
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_rectangle_stroke_style(
    ShaulaPreviewState *state, PreviewArrowStrokeStyle style) {
  if (!property_input_allowed(state) || style < PREVIEW_ARROW_STROKE_SOLID ||
      style > PREVIEW_ARROW_STROKE_DASHED)
    return;
  if (state->tool_defaults.rectangle.stroke_style != style) {
    state->tool_defaults.rectangle.stroke_style = style;
    shaula_tool_defaults_mark_dirty(
        &state->tool_defaults, SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE);
  }
  ShaulaAnnotation *rectangle =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_RECTANGLE);
  if (rectangle != NULL &&
      rectangle->data.rectangle.stroke_style != style) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->data.rectangle.stroke_style = style;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_rectangle_filled(ShaulaPreviewState *state,
                                                gboolean filled) {
  if (!property_input_allowed(state))
    return;
  if (state->tool_defaults.rectangle.filled != filled) {
    state->tool_defaults.rectangle.filled = filled;
    shaula_tool_defaults_mark_dirty(
        &state->tool_defaults, SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE);
  }
  ShaulaAnnotation *rectangle =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_RECTANGLE);
  if (rectangle != NULL && rectangle->data.rectangle.filled != filled) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->data.rectangle.filled = filled;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_rectangle_corners(
    ShaulaPreviewState *state, PreviewRectangleCorners corners) {
  if (!property_input_allowed(state) ||
      corners < PREVIEW_RECTANGLE_CORNERS_ROUNDED ||
      corners > PREVIEW_RECTANGLE_CORNERS_SQUARE)
    return;
  if (state->tool_defaults.rectangle.corners != corners) {
    state->tool_defaults.rectangle.corners = corners;
    shaula_tool_defaults_mark_dirty(
        &state->tool_defaults, SHAULA_TOOL_DEFAULTS_DIRTY_RECTANGLE);
  }
  ShaulaAnnotation *rectangle =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_RECTANGLE);
  if (rectangle != NULL && rectangle->data.rectangle.corners != corners) {
    begin_property_history_if_targeted(state, TRUE);
    rectangle->data.rectangle.corners = corners;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_pen_color(ShaulaPreviewState *state,
                                         ShaulaColor color) {
  if (!property_input_allowed(state))
    return;
  ShaulaAnnotation *pen =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_PEN);
  ShaulaColor default_color = color;
  default_color.a = CLAMP(state->tool_defaults.pen.opacity, 0.1, 1.0);
  persist_color_default(state, &state->tool_defaults.pen.color, default_color,
                        SHAULA_TOOL_DEFAULTS_DIRTY_PEN);
  color.a = pen != NULL ? pen->color.a : default_color.a;
  if (pen != NULL && !colors_equal(pen->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    pen->color = color;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_pen_stroke_width(ShaulaPreviewState *state,
                                                double width) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(width, 1.0, 24.0);
  persist_double_default(state, &state->tool_defaults.pen.stroke_width, next,
                         SHAULA_TOOL_DEFAULTS_DIRTY_PEN);
  ShaulaAnnotation *pen =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_PEN);
  if (pen != NULL && fabs(pen->stroke_width - next) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    pen->stroke_width = next;
    shaula_annotation_update_bounds(pen);
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_pen_opacity(ShaulaPreviewState *state,
                                           double opacity) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(opacity, 0.1, 1.0);
  persist_double_default(state, &state->tool_defaults.pen.opacity, next,
                         SHAULA_TOOL_DEFAULTS_DIRTY_PEN);
  state->tool_defaults.pen.color.a = next;
  ShaulaAnnotation *pen =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_PEN);
  if (pen != NULL && fabs(pen->color.a - next) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    pen->color.a = next;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_highlight_color(ShaulaPreviewState *state,
                                               ShaulaColor color) {
  if (!property_input_allowed(state))
    return;
  ShaulaAnnotation *highlight =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_HIGHLIGHT);
  ShaulaColor default_color = color;
  default_color.a = CLAMP(state->tool_defaults.highlight.opacity, 0.05, 1.0);
  persist_color_default(state, &state->tool_defaults.highlight.color,
                        default_color, SHAULA_TOOL_DEFAULTS_DIRTY_HIGHLIGHT);
  color.a = highlight != NULL ? highlight->color.a : default_color.a;
  if (highlight != NULL && !colors_equal(highlight->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    highlight->color = color;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_highlight_stroke_width(
    ShaulaPreviewState *state, double width) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(width, 4.0, 48.0);
  persist_double_default(state, &state->tool_defaults.highlight.stroke_width,
                         next, SHAULA_TOOL_DEFAULTS_DIRTY_HIGHLIGHT);
  ShaulaAnnotation *highlight =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_HIGHLIGHT);
  if (highlight != NULL && fabs(highlight->stroke_width - next) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    highlight->stroke_width = next;
    shaula_annotation_update_bounds(highlight);
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_highlight_opacity(ShaulaPreviewState *state,
                                                 double opacity) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(opacity, 0.05, 1.0);
  persist_double_default(state, &state->tool_defaults.highlight.opacity, next,
                         SHAULA_TOOL_DEFAULTS_DIRTY_HIGHLIGHT);
  state->tool_defaults.highlight.color.a = next;
  ShaulaAnnotation *highlight =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_HIGHLIGHT);
  if (highlight != NULL && fabs(highlight->color.a - next) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    highlight->color.a = next;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_text_color(ShaulaPreviewState *state,
                                          ShaulaColor color) {
  if (!property_input_allowed(state))
    return;
  color.a = 1.0;
  persist_color_default(state, &state->tool_defaults.text.color, color,
                        SHAULA_TOOL_DEFAULTS_DIRTY_TEXT);
  ShaulaAnnotation *text =
      state->text_entry == NULL
          ? selected_annotation_of_type(state, SHAULA_ANNOTATION_TEXT)
          : NULL;
  if (text != NULL && !colors_equal(text->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    text->color = color;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_text_font_size(ShaulaPreviewState *state,
                                              double font_size) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(font_size, 12.0, 72.0);
  persist_double_default(state, &state->tool_defaults.text.font_size, next,
                         SHAULA_TOOL_DEFAULTS_DIRTY_TEXT);
  ShaulaAnnotation *text =
      state->text_entry == NULL
          ? selected_annotation_of_type(state, SHAULA_ANNOTATION_TEXT)
          : NULL;
  if (text != NULL && fabs(text->data.text.font_size - next) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    text->data.text.font_size = next;
    shaula_annotation_update_bounds(text);
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_text_align(ShaulaPreviewState *state,
                                          ShaulaTextAlign align) {
  if (!property_input_allowed(state))
    return;
  if (state->tool_defaults.text.align != align) {
    state->tool_defaults.text.align = align;
    shaula_tool_defaults_mark_dirty(&state->tool_defaults,
                                    SHAULA_TOOL_DEFAULTS_DIRTY_TEXT);
  }
  ShaulaAnnotation *text =
      state->text_entry == NULL
          ? selected_annotation_of_type(state, SHAULA_ANNOTATION_TEXT)
          : NULL;
  if (text != NULL && text->data.text.align != align) {
    begin_property_history_if_targeted(state, TRUE);
    text->data.text.align = align;
    shaula_annotation_update_bounds(text);
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_text_font_mode(ShaulaPreviewState *state,
                                              ShaulaTextFontMode font_mode) {
  if (!property_input_allowed(state))
    return;
  if (font_mode != SHAULA_TEXT_FONT_SKETCH)
    font_mode = SHAULA_TEXT_FONT_NORMAL;
  if (state->tool_defaults.text.font_mode != font_mode) {
    state->tool_defaults.text.font_mode = font_mode;
    shaula_tool_defaults_mark_dirty(&state->tool_defaults,
                                    SHAULA_TOOL_DEFAULTS_DIRTY_TEXT);
  }
  ShaulaAnnotation *text =
      state->text_entry == NULL
          ? selected_annotation_of_type(state, SHAULA_ANNOTATION_TEXT)
          : NULL;
  if (text != NULL && text->data.text.font_mode != font_mode) {
    begin_property_history_if_targeted(state, TRUE);
    text->data.text.font_mode = font_mode;
    shaula_annotation_update_bounds(text);
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_measure_color(ShaulaPreviewState *state,
                                             ShaulaColor color) {
  if (!property_input_allowed(state))
    return;
  persist_color_default(state, &state->tool_defaults.measure.color, color,
                        SHAULA_TOOL_DEFAULTS_DIRTY_MEASURE);
  ShaulaAnnotation *measure =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_MEASURE);
  if (measure != NULL && !colors_equal(measure->color, color)) {
    begin_property_history_if_targeted(state, TRUE);
    measure->color = color;
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}

void shaula_properties_hud_set_measure_stroke_width(ShaulaPreviewState *state,
                                                    double width) {
  if (!property_input_allowed(state))
    return;
  double next = CLAMP(width, 1.0, 8.0);
  persist_double_default(state, &state->tool_defaults.measure.stroke_width,
                         next, SHAULA_TOOL_DEFAULTS_DIRTY_MEASURE);
  ShaulaAnnotation *measure =
      selected_annotation_of_type(state, SHAULA_ANNOTATION_MEASURE);
  if (measure != NULL && fabs(measure->stroke_width - next) > 0.0001) {
    begin_property_history_if_targeted(state, TRUE);
    measure->stroke_width = next;
    shaula_annotation_update_bounds(measure);
    state->document.modified = TRUE;
  }
  finish_property_change(state);
}
