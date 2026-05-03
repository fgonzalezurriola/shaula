#ifndef SHAULA_PREVIEW_STATE_H
#define SHAULA_PREVIEW_STATE_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include "preview_annotations.h"
#include "preview_geometry.h"

enum {
  PREVIEW_MIN_W = 900,
  PREVIEW_MIN_H = 650,
  PREVIEW_DEFAULT_W = 960,
  PREVIEW_DEFAULT_H = 680,
  PREVIEW_TOOLBAR_BASE_VISIBLE_W = 940,
  PREVIEW_TOOLBAR_REVEAL_STEP_W = 48,
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
  SHAULA_TOOL_COUNT
} ShaulaTool;

typedef enum {
  SHAULA_OPERATION_NONE,
  SHAULA_OPERATION_PAN,
  SHAULA_OPERATION_MOVE,
  SHAULA_OPERATION_SELECT_REGION,
  SHAULA_OPERATION_CROP,
  SHAULA_OPERATION_ARROW,
  SHAULA_OPERATION_RECTANGLE,
  SHAULA_OPERATION_HIGHLIGHT,
  SHAULA_OPERATION_PEN,
  SHAULA_OPERATION_MEASURE,
  SHAULA_OPERATION_TEXT
} ShaulaPreviewOperation;

typedef struct ShaulaPreviewSnapshot ShaulaPreviewSnapshot;

typedef struct {
  GPtrArray *undo;
  GPtrArray *redo;
  guint capacity;
} ShaulaHistoryStack;

typedef struct {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *canvas_overlay;
  GtkWidget *area;
  GtkWidget *zoom_label;
  GtkWidget *dimensions_label;
  GtkWidget *color_swatch;
  GtkWidget *color_hex_label;
  GtkWidget *toolbar_secondary[6];
  ShaulaTool toolbar_secondary_tools[6];
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
  GArray *spotlight_regions;
  int next_annotation_id;
  ShaulaHistoryStack history;
  ShaulaPreviewSnapshot *pending_history_snapshot;

  ShaulaColor current_color;
  gboolean modified;
  gboolean copied;
  gboolean saved;
  /* Set when the helper already emitted the user-facing save/copy banner. */
  gboolean notified;
  char *saved_path;
  const char *last_action;
  gboolean is_dark;

  int toolbar_secondary_count;
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
void shaula_preview_replace_annotations(ShaulaPreviewState *state,
                                        GPtrArray *annotations);

#endif
