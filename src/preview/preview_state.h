#ifndef SHAULA_PREVIEW_STATE_H
#define SHAULA_PREVIEW_STATE_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include "preview_annotation_editor.h"
#include "preview_annotations.h"
#include "preview_document.h"
#include "preview_edit_session.h"
#include "preview_geometry.h"
#include "preview_gesture.h"
#include "preview_measure.h"
#include "preview_properties_hud.h"
#include "preview_tool_defaults.h"

enum {
  PREVIEW_MIN_W = 900,
  PREVIEW_MIN_H = 650,
  PREVIEW_DEFAULT_W = 960,
  PREVIEW_DEFAULT_H = 680,
  PREVIEW_TOOLBAR_BASE_VISIBLE_W = 238,
  PREVIEW_TOOLBAR_REVEAL_STEP_W = 34,
  PREVIEW_TOOLBAR_FULL_VISIBLE_W =
      PREVIEW_TOOLBAR_BASE_VISIBLE_W + (12 * PREVIEW_TOOLBAR_REVEAL_STEP_W),
  PREVIEW_TOOLBAR_STABLE_ACTIONS_W = PREVIEW_TOOLBAR_FULL_VISIBLE_W,
  PREVIEW_READY_DEFAULT_MIN_W = PREVIEW_MIN_W,
  PREVIEW_HEADER_ESTIMATED_H = 56,
};

typedef enum {
  SHAULA_TOOL_SELECT,
  SHAULA_TOOL_HAND,
  SHAULA_TOOL_CROP,
  SHAULA_TOOL_ERASER,
  SHAULA_TOOL_ARROW,
  SHAULA_TOOL_LINE,
  SHAULA_TOOL_TEXT,
  SHAULA_TOOL_MEASURE,
  SHAULA_TOOL_RECTANGLE,
  SHAULA_TOOL_HIGHLIGHT,
  SHAULA_TOOL_PEN,
  SHAULA_TOOL_SPOTLIGHT,
  SHAULA_TOOL_COUNT
} ShaulaTool;

typedef struct ShaulaSystemClipboardPaste ShaulaSystemClipboardPaste;

typedef struct {
  double x;
  double y;
  gint64 time_us;
} ShaulaEraserTrailPoint;

typedef struct ShaulaPreviewState {
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
  GtkWidget *toolbar_secondary[8];
  ShaulaTool toolbar_secondary_tools[8];
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
  GtkWidget *more_button;
  GtkWidget *more_popover;
  GtkWidget *more_menu_box;
  GtkWidget *paste_system_button;
  GtkWidget *text_entry;
  GtkWidget *feedback_label;

  /* Output-affecting model state. Keep GTK widgets, view state, tools, and
   * transient gestures in ShaulaPreviewState instead of growing the document.
   */
  ShaulaPreviewDocument document;

  double zoom;
  double fit_zoom;
  double pan_x;
  double pan_y;
  gboolean fit_mode;

  ShaulaTool active_tool;
  ShaulaTool previous_tool_before_space_pan;
  ShaulaTool previous_tool_before_eraser;
  ShaulaPreviewOperation operation;
  gboolean operation_changed;
  gboolean space_pan_active;
  gboolean space_pan_restore_pending;
  ShaulaPoint drag_start_image;
  ShaulaPoint drag_current_image;
  ShaulaPoint drag_last_image;
  double drag_start_x;
  double drag_start_y;
  double pan_origin_x;
  double pan_origin_y;
  ShaulaPreviewGestureState gesture;

  gboolean has_crop_draft;
  ShaulaRect crop_draft;
  gboolean has_region_selection;
  ShaulaRect region_selection_rect;
  GArray *draft_pen_points;
  GArray *eraser_pending_annotation_ids;
  GArray *eraser_trail;
  gboolean eraser_hover_valid;
  gboolean eraser_drag_active;
  gboolean eraser_tail_fading;
  guint eraser_tail_timeout_id;
  gint64 eraser_tail_fade_start_us;
  ShaulaPoint eraser_hover_screen;
  ShaulaPoint eraser_last_screen;
  ShaulaPoint text_anchor_image;
  /* Non-zero while re-editing a committed Text annotation's string content.
   * Zero means the draft creates a new annotation on finish.
   */
  int text_editing_id;

  ShaulaAnnotationEditor annotation_editor;
  ShaulaSystemClipboardPaste *system_clipboard_paste;
  guint feedback_timeout_id;

  ShaulaColor current_color;
  /* Hover sampling is view state for the metadata readout and Tab copy. It is
   * intentionally separate from current_color, which remains the annotation
   * tool color and part of the user's editing defaults.
   */
  gboolean hover_color_valid;
  ShaulaPoint hover_image_point;
  ShaulaColor hover_color;
  char hover_hex[8];
  /* UI/config state only. Must stay out of undo history snapshots. */
  ShaulaPreviewToolDefaults tool_defaults;
  ShaulaPropertiesHudState properties_hud;
  /* Set when the helper already emitted the user-facing save/copy banner. */
  gboolean notified;
  gboolean close_preview_on_save;
  gboolean copy_on_accept;
  char *managed_temp_path;
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
void shaula_preview_show_feedback(ShaulaPreviewState *state,
                                  const char *message, gboolean is_error);
void shaula_preview_update_dimensions_label(ShaulaPreviewState *state);
void shaula_preview_update_zoom_label(ShaulaPreviewState *state);
void shaula_preview_update_fit_zoom(ShaulaPreviewState *state);
void shaula_preview_set_fit_mode(ShaulaPreviewState *state, gboolean fit);
void shaula_preview_set_actual_size(ShaulaPreviewState *state);
void shaula_preview_zoom_by_factor(ShaulaPreviewState *state, double factor);

gboolean shaula_preview_is_annotation_pending_erase(
    ShaulaPreviewState *state, ShaulaAnnotation *annotation);
void shaula_preview_clear_eraser_pending(ShaulaPreviewState *state);
gboolean shaula_preview_commit_eraser_pending(ShaulaPreviewState *state);
void shaula_preview_cancel_eraser_gesture(ShaulaPreviewState *state);

gboolean shaula_preview_apply_crop(ShaulaPreviewState *state);
gboolean shaula_preview_apply_crop_to_rect(ShaulaPreviewState *state,
                                           ShaulaRect rect);
gboolean shaula_preview_apply_crop_to_selected_rect(ShaulaPreviewState *state);
gboolean
shaula_preview_apply_crop_to_region_selection(ShaulaPreviewState *state);
gboolean shaula_preview_blur_region_selection(ShaulaPreviewState *state);
gboolean shaula_preview_erase_region_selection(ShaulaPreviewState *state);
gboolean shaula_preview_spotlight_region_selection(ShaulaPreviewState *state);
gboolean shaula_preview_spotlight_rect(ShaulaPreviewState *state,
                                       ShaulaRect rect);

#endif
