#ifndef SHAULA_PREVIEW_STATE_H
#define SHAULA_PREVIEW_STATE_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include "preview_annotations.h"
#include "preview_geometry.h"
#include "preview_measure.h"

enum {
  PREVIEW_MIN_W = 900,
  PREVIEW_MIN_H = 650,
  PREVIEW_DEFAULT_W = 960,
  PREVIEW_DEFAULT_H = 680,
  PREVIEW_TOOLBAR_BASE_VISIBLE_W = 320,
  PREVIEW_TOOLBAR_REVEAL_STEP_W = 36,
};

typedef enum {
  SHAULA_TOOL_SELECT,
  SHAULA_TOOL_CROP,
  SHAULA_TOOL_ARROW,
  SHAULA_TOOL_TEXT,
  SHAULA_TOOL_MEASURE,
  SHAULA_TOOL_RECTANGLE,
  SHAULA_TOOL_HIGHLIGHT,
  SHAULA_TOOL_PEN,
  SHAULA_TOOL_SPOTLIGHT,
  SHAULA_TOOL_COUNT
} ShaulaTool;

typedef enum {
  SHAULA_OPERATION_NONE,
  SHAULA_OPERATION_PAN,
  SHAULA_OPERATION_MOVE,
  SHAULA_OPERATION_BEND_ARROW,
  SHAULA_OPERATION_SELECT_REGION,
  SHAULA_OPERATION_CROP,
  SHAULA_OPERATION_ARROW,
  SHAULA_OPERATION_RECTANGLE,
  SHAULA_OPERATION_HIGHLIGHT,
  SHAULA_OPERATION_PEN,
  SHAULA_OPERATION_SPOTLIGHT,
  SHAULA_OPERATION_MEASURE,
  SHAULA_OPERATION_TEXT
} ShaulaPreviewOperation;

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

typedef struct {
  /* Document effect entry: copied/saved output must use these stored values,
   * not the current Spotlight toolbar settings.
   */
  ShaulaRect rect;
  ShaulaSpotlightShape shape;
  ShaulaColor border_color;
  double border_width;
} ShaulaSpotlightRegion;

typedef struct ShaulaPreviewSnapshot ShaulaPreviewSnapshot;

typedef struct {
  GPtrArray *undo;
  GPtrArray *redo;
  guint capacity;
} ShaulaHistoryStack;

typedef struct {
  GPtrArray *annotations;
  int last_pasted_id;
} ShaulaAnnotationClipboard;

typedef struct {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *canvas_overlay;
  GtkWidget *area;
  GtkWidget *zoom_label;
  GtkWidget *dimensions_label;
  GtkWidget *color_swatch;
  GtkWidget *color_hex_label;
  GtkWidget *toolbar_actions;
  GtkWidget *toolbar_metadata;
  GtkWidget *toolbar_secondary[6];
  ShaulaTool toolbar_secondary_tools[6];
  GtkWidget *toolbar_utility_actions[3];
  GtkWidget *tool_buttons[SHAULA_TOOL_COUNT];
  GtkWidget *undo_button;
  GtkWidget *redo_button;
  GtkWidget *selection_actions_box;
  GtkWidget *duplicate_button;
  GtkWidget *crop_selected_button;
  GtkWidget *blur_region_button;
  GtkWidget *erase_region_button;
  GtkWidget *spotlight_region_button;
  GtkWidget *delete_button;
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
 GtkWidget *text_size_scale;
 GtkWidget *text_align_buttons[3];
 GtkWidget *measure_properties_box;
 GtkWidget *measure_color_button;
 GtkWidget *measure_width_scale;
 GtkWidget *more_button;
  GtkWidget *more_popover;
  GtkWidget *more_menu_box;
  GtkWidget *text_entry;

  GdkPixbuf *image;
  char *path;

  double zoom;
  double fit_zoom;
  double pan_x;
  double pan_y;
  gboolean fit_mode;

  ShaulaTool active_tool;
  ShaulaPreviewOperation operation;
  gboolean operation_changed;
  ShaulaPoint drag_start_image;
  ShaulaPoint drag_current_image;
  ShaulaPoint drag_last_image;
  double drag_start_x;
  double drag_start_y;
  double pan_origin_x;
  double pan_origin_y;

  gboolean has_crop_draft;
  ShaulaRect crop_draft;
  gboolean has_region_selection;
  ShaulaRect region_selection_rect;
  GArray *draft_pen_points;
  ShaulaPoint text_anchor_image;

  GPtrArray *annotations;
  ShaulaAnnotation *selected_annotation;
  /* Preview-local edit clipboard. It intentionally stays out of undo/redo and
   * never publishes to the system clipboard; v1 copies one annotation while
   * keeping list ownership ready for future multi-selection paste.
   */
  ShaulaAnnotationClipboard annotation_clipboard;
  GArray *spotlight_regions;
  int next_annotation_id;
  ShaulaHistoryStack history;
  ShaulaPreviewSnapshot *pending_history_snapshot;

  ShaulaColor current_color;
  /* Hover sampling is view state for the metadata readout and Tab copy. It is
   * intentionally separate from current_color, which remains the annotation
   * tool color and part of the user's editing defaults.
   */
  gboolean hover_color_valid;
  ShaulaPoint hover_image_point;
  ShaulaColor hover_color;
  char hover_hex[8];
  /* UI/config state only. Must stay out of undo history snapshots. The active
   * Spotlight index only points the HUD at the just-created document entry.
   */
  ShaulaPropertiesPanel active_properties_panel;
  int active_spotlight_index;
  ShaulaColor spotlight_border_color;
  double spotlight_border_width;
  ShaulaSpotlightShape spotlight_shape;
  int active_arrow_index;
  ShaulaColor arrow_color;
  double arrow_stroke_width;
  int active_rectangle_index;
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
 int active_measure_index;
 ShaulaColor measure_color;
 double measure_stroke_width;
 gboolean modified;
  gboolean copied;
  gboolean saved;
  /* Set when the helper already emitted the user-facing save/copy banner. */
  gboolean notified;
  char *saved_path;
  const char *last_action;
  gboolean is_dark;

  gboolean measure_has_live;
 int measure_tolerance;
 ShaulaMeasureMode measure_mode;
 ShaulaMeasurePixelCompare measure_compare;
 gboolean measure_outer_bounds;
 ShaulaMeasureResult measure_result;
 int toolbar_secondary_count;
  int toolbar_utility_action_count;
  int toolbar_overflow_visible_count;
  char *icon_roots[2];
  int icon_root_count;
} ShaulaPreviewState;

void shaula_preview_state_init(ShaulaPreviewState *state, const char *path,
                               GdkPixbuf *image);
void shaula_preview_state_free(ShaulaPreviewState *state);

gboolean shaula_preview_state_has_modifications(ShaulaPreviewState *state);
int shaula_preview_image_width(ShaulaPreviewState *state);
int shaula_preview_image_height(ShaulaPreviewState *state);

void shaula_preview_update_theme_state(ShaulaPreviewState *state);
void shaula_preview_queue_draw(ShaulaPreviewState *state);
void shaula_preview_update_dimensions_label(ShaulaPreviewState *state);
void shaula_preview_update_zoom_label(ShaulaPreviewState *state);
void shaula_preview_update_fit_zoom(ShaulaPreviewState *state);
void shaula_preview_set_fit_mode(ShaulaPreviewState *state, gboolean fit);
void shaula_preview_set_actual_size(ShaulaPreviewState *state);
void shaula_preview_zoom_by_factor(ShaulaPreviewState *state, double factor);

void shaula_preview_select_annotation(ShaulaPreviewState *state,
                                      ShaulaAnnotation *annotation);
void shaula_preview_clear_selection(ShaulaPreviewState *state);
void shaula_preview_clear_region_selection(ShaulaPreviewState *state);
void shaula_preview_add_annotation(ShaulaPreviewState *state,
                                   ShaulaAnnotation *annotation);
gboolean shaula_preview_can_duplicate_selected(ShaulaPreviewState *state);
gboolean shaula_preview_can_delete_selected(ShaulaPreviewState *state);
gboolean shaula_preview_duplicate_selected(ShaulaPreviewState *state);
gboolean shaula_preview_can_copy_selected_annotation(ShaulaPreviewState *state);
gboolean shaula_preview_copy_selected_annotation(ShaulaPreviewState *state);
gboolean shaula_preview_can_paste_annotation(ShaulaPreviewState *state);
gboolean shaula_preview_paste_annotation(ShaulaPreviewState *state);
void shaula_preview_delete_selected(ShaulaPreviewState *state);
void shaula_preview_reset_annotations(ShaulaPreviewState *state);

void shaula_preview_push_undo(ShaulaPreviewState *state);
void shaula_preview_begin_history_gesture(ShaulaPreviewState *state);
void shaula_preview_commit_history_gesture(ShaulaPreviewState *state,
                                           gboolean changed);
void shaula_preview_cancel_history_gesture(ShaulaPreviewState *state);
gboolean shaula_preview_can_undo(ShaulaPreviewState *state);
gboolean shaula_preview_can_redo(ShaulaPreviewState *state);
gboolean shaula_preview_undo(ShaulaPreviewState *state);
gboolean shaula_preview_redo(ShaulaPreviewState *state);

void shaula_preview_cancel_operation(ShaulaPreviewState *state);
gboolean shaula_preview_apply_crop(ShaulaPreviewState *state);
gboolean shaula_preview_apply_crop_to_rect(ShaulaPreviewState *state,
                                           ShaulaRect rect);
gboolean shaula_preview_apply_crop_to_selected_rect(ShaulaPreviewState *state);
gboolean shaula_preview_apply_crop_to_region_selection(
    ShaulaPreviewState *state);
gboolean shaula_preview_blur_region_selection(ShaulaPreviewState *state);
gboolean shaula_preview_erase_region_selection(ShaulaPreviewState *state);
gboolean shaula_preview_spotlight_region_selection(ShaulaPreviewState *state);
gboolean shaula_preview_spotlight_rect(ShaulaPreviewState *state,
                                       ShaulaRect rect);
void shaula_preview_set_properties_panel(ShaulaPreviewState *state,
                                         ShaulaPropertiesPanel panel);
void shaula_preview_set_spotlight_border_color(ShaulaPreviewState *state,
                                               ShaulaColor color);
void shaula_preview_set_spotlight_border_width(ShaulaPreviewState *state,
                                               double width);
void shaula_preview_set_spotlight_shape(ShaulaPreviewState *state,
                                        ShaulaSpotlightShape shape);
void shaula_preview_set_arrow_color(ShaulaPreviewState *state,
                                    ShaulaColor color);
void shaula_preview_set_arrow_stroke_width(ShaulaPreviewState *state,
                                           double width);
void shaula_preview_set_arrow_stroke_style(ShaulaPreviewState *state,
                                           PreviewArrowStrokeStyle style);
void shaula_preview_set_rectangle_color(ShaulaPreviewState *state,
                                        ShaulaColor color);
void shaula_preview_set_rectangle_stroke_width(ShaulaPreviewState *state,
                                               double width);
void shaula_preview_set_rectangle_stroke_style(ShaulaPreviewState *state,
                                               PreviewArrowStrokeStyle style);
void shaula_preview_set_rectangle_filled(ShaulaPreviewState *state,
                                         gboolean filled);
void shaula_preview_set_rectangle_corners(ShaulaPreviewState *state,
                                          PreviewRectangleCorners corners);
void shaula_preview_set_pen_color(ShaulaPreviewState *state,
                                  ShaulaColor color);
void shaula_preview_set_pen_stroke_width(ShaulaPreviewState *state,
                                         double width);
void shaula_preview_set_pen_opacity(ShaulaPreviewState *state,
                                    double opacity);
void shaula_preview_set_highlight_color(ShaulaPreviewState *state,
                                        ShaulaColor color);
void shaula_preview_set_highlight_stroke_width(ShaulaPreviewState *state,
                                               double width);
void shaula_preview_set_highlight_opacity(ShaulaPreviewState *state,
                                          double opacity);
void shaula_preview_set_text_color(ShaulaPreviewState *state,
                                   ShaulaColor color);
void shaula_preview_set_text_font_size(ShaulaPreviewState *state,
                                       double font_size);
void shaula_preview_set_text_align(ShaulaPreviewState *state,
 ShaulaTextAlign align);
void shaula_preview_set_measure_color(ShaulaPreviewState *state,
 ShaulaColor color);
void shaula_preview_set_measure_stroke_width(ShaulaPreviewState *state,
 double width);
void shaula_preview_replace_annotations(ShaulaPreviewState *state,
                                        GPtrArray *annotations);

#endif
