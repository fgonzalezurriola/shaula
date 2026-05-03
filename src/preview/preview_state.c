#include "preview_state.h"

#include <math.h>
#include <string.h>

#include "preview_toolbar.h"

#define SHAULA_HISTORY_DEFAULT_CAPACITY 24
#define SHAULA_CROP_MIN_SIZE_PX 4

/* History tracks the editable document that affects exported/copied pixels:
 * the current image buffer while crop remains destructive, annotation objects,
 * and annotation id allocation. View-only state such as zoom, pan, fit mode,
 * active tool, hover, menus, and crop/text drafts is intentionally excluded.
 * Selection is restored only because annotations currently carry that flag; any
 * restored pointer is rebuilt from cloned annotations to avoid stale ownership.
 */
struct ShaulaPreviewSnapshot {
  GdkPixbuf *image;
  GPtrArray *annotations;
  int next_annotation_id;
  gboolean modified;
};

static ShaulaPreviewSnapshot *snapshot_new(ShaulaPreviewState *state) {
  ShaulaPreviewSnapshot *snapshot = g_new0(ShaulaPreviewSnapshot, 1);
  snapshot->image = state->image != NULL ? gdk_pixbuf_copy(state->image) : NULL;
  snapshot->annotations = shaula_annotations_clone_array(state->annotations);
  snapshot->next_annotation_id = state->next_annotation_id;
  snapshot->modified = state->modified;
  return snapshot;
}

static void snapshot_free(gpointer data) {
  ShaulaPreviewSnapshot *snapshot = data;
  if (snapshot == NULL)
    return;
  if (snapshot->image != NULL)
    g_object_unref(snapshot->image);
  if (snapshot->annotations != NULL)
    g_ptr_array_unref(snapshot->annotations);
  g_free(snapshot);
}

static void clear_stack(GPtrArray *stack) {
  if (stack != NULL)
    g_ptr_array_set_size(stack, 0);
}

static void history_stack_init(ShaulaHistoryStack *history, guint capacity) {
  history->undo = g_ptr_array_new_with_free_func(snapshot_free);
  history->redo = g_ptr_array_new_with_free_func(snapshot_free);
  history->capacity = capacity > 0 ? capacity : SHAULA_HISTORY_DEFAULT_CAPACITY;
}

static void history_stack_free(ShaulaHistoryStack *history) {
  if (history->undo != NULL)
    g_ptr_array_unref(history->undo);
  if (history->redo != NULL)
    g_ptr_array_unref(history->redo);
  history->undo = NULL;
  history->redo = NULL;
}

static gboolean history_stack_can_undo(ShaulaHistoryStack *history) {
  return history != NULL && history->undo != NULL && history->undo->len > 0;
}

static gboolean history_stack_can_redo(ShaulaHistoryStack *history) {
  return history != NULL && history->redo != NULL && history->redo->len > 0;
}

static void history_stack_trim_to_capacity(ShaulaHistoryStack *history) {
  if (history == NULL || history->undo == NULL || history->capacity == 0)
    return;
  while (history->undo->len > history->capacity)
    g_ptr_array_remove_index(history->undo, 0);
}

static void history_stack_push_undo(ShaulaHistoryStack *history,
                                    ShaulaPreviewSnapshot *snapshot,
                                    gboolean clear_redo) {
  if (history == NULL || history->undo == NULL || snapshot == NULL)
    return;
  g_ptr_array_add(history->undo, snapshot);
  history_stack_trim_to_capacity(history);
  if (clear_redo)
    clear_stack(history->redo);
}

static ShaulaPreviewSnapshot *history_stack_pop_undo(
    ShaulaHistoryStack *history) {
  if (!history_stack_can_undo(history))
    return NULL;
  return g_ptr_array_steal_index(history->undo, history->undo->len - 1);
}

static ShaulaPreviewSnapshot *history_stack_pop_redo(
    ShaulaHistoryStack *history) {
  if (!history_stack_can_redo(history))
    return NULL;
  return g_ptr_array_steal_index(history->redo, history->redo->len - 1);
}

static void history_stack_push_redo(ShaulaHistoryStack *history,
                                    ShaulaPreviewSnapshot *snapshot) {
  if (history == NULL || history->redo == NULL || snapshot == NULL)
    return;
  g_ptr_array_add(history->redo, snapshot);
}

static gboolean detect_dark_theme(void) {
  GtkSettings *settings = gtk_settings_get_default();
  if (settings == NULL)
    return TRUE;
  gchar *theme = NULL;
  g_object_get(settings, "gtk-theme-name", &theme, NULL);
  if (theme != NULL) {
    gboolean dark =
        (g_str_has_suffix(theme, "-dark") || g_str_has_suffix(theme, "-Dark"));
    g_free(theme);
    if (dark)
      return TRUE;
  }
  gint pref = 0;
  g_object_get(settings, "gtk-application-prefer-dark-theme", &pref, NULL);
  if (pref)
    return TRUE;
  return FALSE;
}

void shaula_preview_state_init(ShaulaPreviewState *state, const char *path,
                               GdkPixbuf *image) {
  memset(state, 0, sizeof(*state));
  state->path = g_strdup(path);
  state->image = image;
  state->zoom = 1.0;
  state->fit_zoom = 1.0;
  state->fit_mode = TRUE;
  state->active_tool = SHAULA_TOOL_SELECT;
  state->operation = SHAULA_OPERATION_NONE;
  state->last_action = "close";
  state->is_dark = TRUE;
  state->current_color = shaula_color_default();
  state->annotations = g_ptr_array_new_with_free_func(shaula_annotation_free);
  history_stack_init(&state->history, SHAULA_HISTORY_DEFAULT_CAPACITY);
  state->pending_history_snapshot = NULL;
  state->draft_pen_points = g_array_new(FALSE, FALSE, sizeof(ShaulaPoint));
  state->next_annotation_id = 1;
  state->toolbar_overflow_visible_count = -1;
}

void shaula_preview_state_free(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  if (state->image != NULL)
    g_object_unref(state->image);
  if (state->annotations != NULL)
    g_ptr_array_unref(state->annotations);
  if (state->pending_history_snapshot != NULL)
    snapshot_free(state->pending_history_snapshot);
  history_stack_free(&state->history);
  if (state->draft_pen_points != NULL)
    g_array_unref(state->draft_pen_points);
  for (int i = 0; i < state->icon_root_count; i++)
    g_free(state->icon_roots[i]);
  g_free(state->saved_path);
  g_free(state->path);
}

gboolean shaula_preview_state_has_modifications(ShaulaPreviewState *state) {
  return state != NULL && state->modified;
}

int shaula_preview_image_width(ShaulaPreviewState *state) {
  return state != NULL && state->image != NULL ? gdk_pixbuf_get_width(state->image)
                                               : 0;
}

int shaula_preview_image_height(ShaulaPreviewState *state) {
  return state != NULL && state->image != NULL ? gdk_pixbuf_get_height(state->image)
                                               : 0;
}

void shaula_preview_update_theme_state(ShaulaPreviewState *state) {
  state->is_dark = detect_dark_theme();
}

void shaula_preview_queue_draw(ShaulaPreviewState *state) {
  if (state->area != NULL)
    gtk_widget_queue_draw(state->area);
  if (state->color_swatch != NULL)
    gtk_widget_queue_draw(state->color_swatch);
}

void shaula_preview_update_dimensions_label(ShaulaPreviewState *state) {
  if (state->dimensions_label == NULL || state->image == NULL)
    return;
  char size_buf[32];
  snprintf(size_buf, sizeof(size_buf), "%d\xc3\x97%d px",
           shaula_preview_image_width(state), shaula_preview_image_height(state));
  gtk_label_set_text(GTK_LABEL(state->dimensions_label), size_buf);
}

void shaula_preview_update_zoom_label(ShaulaPreviewState *state) {
  if (state->zoom_label == NULL)
    return;
  int pct = (int)(state->zoom * 100.0 + 0.5);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d%% Zoom", pct);
  gtk_label_set_text(GTK_LABEL(state->zoom_label), buf);
}

void shaula_preview_update_fit_zoom(ShaulaPreviewState *state) {
  if (state->area == NULL || state->image == NULL)
    return;
  int area_w = MAX(1, gtk_widget_get_width(state->area));
  int area_h = MAX(1, gtk_widget_get_height(state->area));
  int image_w = MAX(1, shaula_preview_image_width(state));
  int image_h = MAX(1, shaula_preview_image_height(state));
  double scale_x = (double)MAX(1, area_w - 48) / (double)image_w;
  double scale_y = (double)MAX(1, area_h - 48) / (double)image_h;
  state->fit_zoom = MIN(1.0, MAX(0.05, MIN(scale_x, scale_y)));
  if (state->fit_mode) {
    state->zoom = state->fit_zoom;
    state->pan_x = ((double)area_w - (double)image_w * state->zoom) / 2.0;
    state->pan_y = ((double)area_h - (double)image_h * state->zoom) / 2.0;
  }
}

void shaula_preview_set_fit_mode(ShaulaPreviewState *state, gboolean fit) {
  state->fit_mode = fit;
  shaula_preview_update_fit_zoom(state);
  shaula_preview_update_zoom_label(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_set_actual_size(ShaulaPreviewState *state) {
  if (state->area == NULL || state->image == NULL)
    return;
  state->fit_mode = FALSE;
  state->zoom = 1.0;
  shaula_preview_update_fit_zoom(state);
  int area_w = MAX(1, gtk_widget_get_width(state->area));
  int area_h = MAX(1, gtk_widget_get_height(state->area));
  int image_w = MAX(1, shaula_preview_image_width(state));
  int image_h = MAX(1, shaula_preview_image_height(state));
  state->pan_x = ((double)area_w - (double)image_w) / 2.0;
  state->pan_y = ((double)area_h - (double)image_h) / 2.0;
  shaula_preview_update_zoom_label(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_zoom_by_factor(ShaulaPreviewState *state, double factor) {
  if (state->image == NULL || state->area == NULL)
    return;
  state->fit_mode = FALSE;
  double old_zoom = state->zoom;
  double next = CLAMP(old_zoom * factor, 0.05, 8.0);
  if (fabs(next - old_zoom) < 0.001)
    return;

  int area_w = MAX(1, gtk_widget_get_width(state->area));
  int area_h = MAX(1, gtk_widget_get_height(state->area));
  double cx = (double)area_w / 2.0;
  double cy = (double)area_h / 2.0;
  double image_cx = (cx - state->pan_x) / old_zoom;
  double image_cy = (cy - state->pan_y) / old_zoom;
  state->zoom = next;
  state->pan_x = cx - image_cx * state->zoom;
  state->pan_y = cy - image_cy * state->zoom;
  shaula_preview_update_zoom_label(state);
  shaula_preview_queue_draw(state);
}

void shaula_preview_select_annotation(ShaulaPreviewState *state,
                                      ShaulaAnnotation *annotation) {
  if (state->annotations != NULL) {
    for (guint i = 0; i < state->annotations->len; i++) {
      ShaulaAnnotation *item = g_ptr_array_index(state->annotations, i);
      item->selected = FALSE;
    }
  }
  state->selected_annotation = annotation;
  if (annotation != NULL)
    annotation->selected = TRUE;
  shaula_preview_queue_draw(state);
}

void shaula_preview_clear_selection(ShaulaPreviewState *state) {
  shaula_preview_select_annotation(state, NULL);
}

static void add_annotation_without_history(ShaulaPreviewState *state,
                                           ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL)
    return;
  if (annotation->id <= 0)
    annotation->id = state->next_annotation_id++;
  g_ptr_array_add(state->annotations, annotation);
  shaula_preview_select_annotation(state, annotation);
  state->modified = TRUE;
  shaula_preview_toolbar_update_history_state(state);
}

void shaula_preview_add_annotation(ShaulaPreviewState *state,
                                   ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL)
    return;
  shaula_preview_push_undo(state);
  add_annotation_without_history(state, annotation);
}

void shaula_preview_delete_selected(ShaulaPreviewState *state) {
  if (state == NULL || state->selected_annotation == NULL)
    return;
  ShaulaAnnotation *selected = state->selected_annotation;
  for (guint i = 0; i < state->annotations->len; i++) {
    if (g_ptr_array_index(state->annotations, i) == selected) {
      shaula_preview_push_undo(state);
      state->selected_annotation = NULL;
      g_ptr_array_remove_index(state->annotations, i);
      state->modified = TRUE;
      shaula_preview_queue_draw(state);
      shaula_preview_toolbar_update_history_state(state);
      return;
    }
  }
  state->selected_annotation = NULL;
  shaula_preview_queue_draw(state);
}

void shaula_preview_reset_annotations(ShaulaPreviewState *state) {
  if (state == NULL || state->annotations == NULL || state->annotations->len == 0)
    return;

  shaula_preview_cancel_operation(state);

  /* Reset is a single document edit: capture the exact pre-clear state once,
   * and let the normal edit path clear redo so later annotation creation
   * replaces any undone reset branch.
   */
  shaula_preview_push_undo(state);
  state->selected_annotation = NULL;
  g_ptr_array_set_size(state->annotations, 0);
  state->modified = TRUE;
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_history_state(state);
}

void shaula_preview_push_undo(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  history_stack_push_undo(&state->history, snapshot_new(state), TRUE);
  shaula_preview_toolbar_update_history_state(state);
}

void shaula_preview_begin_history_gesture(ShaulaPreviewState *state) {
  if (state == NULL || state->pending_history_snapshot != NULL)
    return;
  state->pending_history_snapshot = snapshot_new(state);
}

void shaula_preview_commit_history_gesture(ShaulaPreviewState *state,
                                           gboolean changed) {
  if (state == NULL || state->pending_history_snapshot == NULL)
    return;
  if (changed) {
    history_stack_push_undo(&state->history, state->pending_history_snapshot,
                            TRUE);
    state->pending_history_snapshot = NULL;
    shaula_preview_toolbar_update_history_state(state);
    return;
  }
  snapshot_free(state->pending_history_snapshot);
  state->pending_history_snapshot = NULL;
}

void shaula_preview_cancel_history_gesture(ShaulaPreviewState *state) {
  if (state == NULL || state->pending_history_snapshot == NULL)
    return;
  snapshot_free(state->pending_history_snapshot);
  state->pending_history_snapshot = NULL;
}

gboolean shaula_preview_can_undo(ShaulaPreviewState *state) {
  return state != NULL && history_stack_can_undo(&state->history);
}

gboolean shaula_preview_can_redo(ShaulaPreviewState *state) {
  return state != NULL && history_stack_can_redo(&state->history);
}

void shaula_preview_replace_annotations(ShaulaPreviewState *state,
                                        GPtrArray *annotations) {
  if (state->annotations != NULL)
    g_ptr_array_unref(state->annotations);
  state->annotations = annotations;
  state->selected_annotation = NULL;
  if (state->annotations != NULL) {
    for (guint i = 0; i < state->annotations->len; i++) {
      ShaulaAnnotation *annotation = g_ptr_array_index(state->annotations, i);
      if (annotation->selected) {
        state->selected_annotation = annotation;
        break;
      }
    }
  }
}

static void restore_snapshot(ShaulaPreviewState *state,
                             ShaulaPreviewSnapshot *snapshot) {
  if (state->image != NULL)
    g_object_unref(state->image);
  state->image = snapshot->image != NULL ? gdk_pixbuf_copy(snapshot->image) : NULL;
  shaula_preview_replace_annotations(
      state, shaula_annotations_clone_array(snapshot->annotations));
  state->next_annotation_id = snapshot->next_annotation_id;
  state->modified = snapshot->modified;
  state->has_crop_draft = FALSE;
  state->operation = SHAULA_OPERATION_NONE;
  if (state->text_entry != NULL) {
    if (state->canvas_overlay != NULL)
      gtk_overlay_remove_overlay(GTK_OVERLAY(state->canvas_overlay),
                                 state->text_entry);
    else
      gtk_widget_unparent(state->text_entry);
    state->text_entry = NULL;
  }
  if (state->draft_pen_points != NULL)
    g_array_set_size(state->draft_pen_points, 0);
  shaula_preview_cancel_history_gesture(state);
  shaula_preview_update_dimensions_label(state);
  shaula_preview_set_fit_mode(state, TRUE);
}

gboolean shaula_preview_undo(ShaulaPreviewState *state) {
  if (state == NULL || !shaula_preview_can_undo(state))
    return FALSE;
  ShaulaPreviewSnapshot *snapshot = history_stack_pop_undo(&state->history);
  history_stack_push_redo(&state->history, snapshot_new(state));
  restore_snapshot(state, snapshot);
  snapshot_free(snapshot);
  shaula_preview_toolbar_update_history_state(state);
  return TRUE;
}

gboolean shaula_preview_redo(ShaulaPreviewState *state) {
  if (state == NULL || !shaula_preview_can_redo(state))
    return FALSE;
  ShaulaPreviewSnapshot *snapshot = history_stack_pop_redo(&state->history);
  history_stack_push_undo(&state->history, snapshot_new(state), FALSE);
  restore_snapshot(state, snapshot);
  snapshot_free(snapshot);
  shaula_preview_toolbar_update_history_state(state);
  return TRUE;
}

void shaula_preview_cancel_operation(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  state->operation = SHAULA_OPERATION_NONE;
  state->operation_changed = FALSE;
  shaula_preview_cancel_history_gesture(state);
  state->has_crop_draft = FALSE;
  if (state->draft_pen_points != NULL)
    g_array_set_size(state->draft_pen_points, 0);
  if (state->text_entry != NULL) {
    if (state->canvas_overlay != NULL)
      gtk_overlay_remove_overlay(GTK_OVERLAY(state->canvas_overlay),
                                 state->text_entry);
    else
      gtk_widget_unparent(state->text_entry);
    state->text_entry = NULL;
  }
  shaula_preview_queue_draw(state);
}

static gboolean crop_rect_to_pixels(ShaulaPreviewState *state, ShaulaRect rect,
                                    int *x, int *y, int *w, int *h) {
  if (state == NULL || state->image == NULL)
    return FALSE;

  ShaulaRect crop = shaula_rect_clamped(
      shaula_rect_normalized(rect), shaula_preview_image_width(state),
      shaula_preview_image_height(state));
  if (crop.width < SHAULA_CROP_MIN_SIZE_PX ||
      crop.height < SHAULA_CROP_MIN_SIZE_PX)
    return FALSE;

  *x = (int)floor(crop.x);
  *y = (int)floor(crop.y);
  *w = MAX(1, (int)ceil(crop.x + crop.width) - *x);
  *h = MAX(1, (int)ceil(crop.y + crop.height) - *y);
  *w = MIN(*w, shaula_preview_image_width(state) - *x);
  *h = MIN(*h, shaula_preview_image_height(state) - *y);
  return *w >= SHAULA_CROP_MIN_SIZE_PX && *h >= SHAULA_CROP_MIN_SIZE_PX;
}

static void remap_annotations_after_crop(ShaulaPreviewState *state,
                                         ShaulaRect crop,
                                         ShaulaAnnotation *remove_annotation) {
  if (state == NULL || state->annotations == NULL)
    return;

  state->selected_annotation = NULL;
  for (gint i = (gint)state->annotations->len - 1; i >= 0; i--) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->annotations, (guint)i);
    if (annotation == remove_annotation || annotation == NULL ||
        !shaula_rect_intersects(annotation->bounds, crop)) {
      g_ptr_array_remove_index(state->annotations, (guint)i);
      continue;
    }
    annotation->selected = FALSE;
    shaula_annotation_move(annotation, -crop.x, -crop.y);
  }
}

/* Commits a crop as one document edit.
 *
 * The snapshot is pushed only after a valid output pixbuf exists, so failed or
 * tiny crops leave undo/redo untouched. Annotations outside the crop are
 * discarded and remaining annotations are translated into the new image
 * coordinate space.
 */
static gboolean apply_crop_to_rect(ShaulaPreviewState *state, ShaulaRect rect,
                                   ShaulaAnnotation *remove_annotation) {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  if (!crop_rect_to_pixels(state, rect, &x, &y, &w, &h))
    return FALSE;

  GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(state->image, x, y, w, h);
  if (sub == NULL)
    return FALSE;
  GdkPixbuf *copy = gdk_pixbuf_copy(sub);
  g_object_unref(sub);
  if (copy == NULL)
    return FALSE;

  shaula_preview_push_undo(state);
  g_object_unref(state->image);
  state->image = copy;
  remap_annotations_after_crop(state, (ShaulaRect){x, y, w, h},
                               remove_annotation);
  state->has_crop_draft = FALSE;
  state->operation = SHAULA_OPERATION_NONE;
  state->active_tool = SHAULA_TOOL_SELECT;
  state->modified = TRUE;
  shaula_preview_update_dimensions_label(state);
  shaula_preview_set_fit_mode(state, TRUE);
  shaula_preview_toolbar_update_tool_state(state);
  shaula_preview_toolbar_update_history_state(state);
  return TRUE;
}

gboolean shaula_preview_apply_crop_to_rect(ShaulaPreviewState *state,
                                           ShaulaRect rect) {
  return apply_crop_to_rect(state, rect, NULL);
}

gboolean shaula_preview_apply_crop(ShaulaPreviewState *state) {
  if (state == NULL || !state->has_crop_draft)
    return FALSE;
  return shaula_preview_apply_crop_to_rect(state, state->crop_draft);
}

gboolean shaula_preview_apply_crop_to_selected_rect(ShaulaPreviewState *state) {
  if (state == NULL || state->selected_annotation == NULL)
    return FALSE;

  ShaulaAnnotation *annotation = state->selected_annotation;
  switch (annotation->type) {
  case SHAULA_ANNOTATION_RECTANGLE:
    return apply_crop_to_rect(state, annotation->data.rectangle.rect,
                              annotation);
  case SHAULA_ANNOTATION_HIGHLIGHT:
    return apply_crop_to_rect(state, annotation->data.highlight.rect,
                              annotation);
  case SHAULA_ANNOTATION_ARROW:
  case SHAULA_ANNOTATION_TEXT:
  case SHAULA_ANNOTATION_MEASURE:
  case SHAULA_ANNOTATION_PEN:
    return FALSE;
  }
  return FALSE;
}
