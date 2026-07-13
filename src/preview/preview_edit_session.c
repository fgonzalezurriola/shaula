#include "preview_edit_session.h"

#include "preview_annotation_editor.h"
#include "preview_gesture.h"
#include "preview_properties_hud.h"
#include "preview_state.h"
#include "preview_toolbar.h"

ShaulaAnnotationHit shaula_preview_edit_hit_test(GPtrArray *annotations,
                                                 ShaulaPoint point,
                                                 double tolerance) {
  return shaula_annotations_hit_test_ranked(annotations, point, tolerance);
}

gboolean shaula_preview_edit_matches(const ShaulaAnnotation *annotation,
                                      ShaulaPreviewEditQuery query) {
  switch (query.kind) {
  case SHAULA_PREVIEW_EDIT_QUERY_SELECTION:
    return shaula_annotation_intersects_selection_rect(annotation, query.rect);
  case SHAULA_PREVIEW_EDIT_QUERY_ERASER:
    return shaula_annotation_intersects_eraser_segment(
        annotation, query.start, query.end, query.tolerance);
  }
  return FALSE;
}

void shaula_preview_edit_begin(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  shaula_preview_push_undo(state);
  state->document.modified = TRUE;
}

void shaula_preview_edit_replace_image(ShaulaPreviewState *state,
                                       GdkPixbuf *replacement) {
  if (state == NULL || replacement == NULL)
    return;
  if (state->document.image != NULL)
    g_object_unref(state->document.image);
  state->document.image = replacement;
}

void shaula_preview_edit_finish(ShaulaPreviewState *state,
                                ShaulaPreviewEditFinish finish) {
  if (state == NULL)
    return;

  if (finish.clear_crop_draft)
    state->has_crop_draft = FALSE;
  if (finish.clear_region_selection)
    state->has_region_selection = FALSE;
  if (finish.reset_tool_to_select) {
    state->operation = SHAULA_OPERATION_NONE;
    state->active_tool = SHAULA_TOOL_SELECT;
  }
  if (finish.update_dimensions)
    shaula_preview_update_dimensions_label(state);
  if (finish.fit_to_screen)
    shaula_preview_set_fit_mode(state, TRUE);
  if (finish.reset_tool_to_select)
    shaula_preview_toolbar_update_tool_state(state);
  shaula_preview_toolbar_update_history_state(state);
  shaula_preview_toolbar_update_selection_state(state);
  if (finish.queue_draw)
    shaula_preview_queue_draw(state);
}

void shaula_preview_clear_region_selection(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  state->has_region_selection = FALSE;
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
}

void shaula_preview_push_undo(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  shaula_preview_history_push_undo(
      &state->document.history,
      shaula_preview_document_snapshot_new(&state->document), TRUE);
  shaula_preview_toolbar_update_history_state(state);
}

void shaula_preview_begin_history_gesture(ShaulaPreviewState *state) {
  if (state == NULL || state->document.pending_history_snapshot != NULL)
    return;
  state->document.pending_history_snapshot =
      shaula_preview_document_snapshot_new(&state->document);
}

void shaula_preview_commit_history_gesture(ShaulaPreviewState *state,
                                           gboolean changed) {
  if (state == NULL || state->document.pending_history_snapshot == NULL)
    return;
  if (changed) {
    shaula_preview_history_push_undo(&state->document.history,
                                     state->document.pending_history_snapshot,
                                     TRUE);
    state->document.pending_history_snapshot = NULL;
    shaula_preview_toolbar_update_history_state(state);
    return;
  }
  shaula_preview_snapshot_free(state->document.pending_history_snapshot);
  state->document.pending_history_snapshot = NULL;
}

void shaula_preview_cancel_history_gesture(ShaulaPreviewState *state) {
  if (state == NULL || state->document.pending_history_snapshot == NULL)
    return;
  shaula_preview_snapshot_free(state->document.pending_history_snapshot);
  state->document.pending_history_snapshot = NULL;
}

gboolean shaula_preview_can_undo(ShaulaPreviewState *state) {
  return state != NULL && (state->document.pending_history_snapshot != NULL ||
                           shaula_preview_history_can_undo(
                               &state->document.history));
}

gboolean shaula_preview_can_redo(ShaulaPreviewState *state) {
  return state != NULL &&
         shaula_preview_history_can_redo(&state->document.history);
}

static void restore_snapshot(ShaulaPreviewState *state,
                             ShaulaPreviewSnapshot *snapshot) {
  shaula_preview_document_restore_snapshot(&state->document, snapshot);
  shaula_annotation_editor_rebuild_selection(state);
  state->has_crop_draft = FALSE;
  state->has_region_selection = FALSE;
  state->operation = SHAULA_OPERATION_NONE;
  shaula_preview_cancel_eraser_gesture(state);
  shaula_properties_hud_set_panel(&state->properties_hud,
                                  SHAULA_PROPERTIES_PANEL_NONE);
  if (state->text_entry != NULL) {
    if (state->canvas_overlay != NULL)
      gtk_overlay_remove_overlay(GTK_OVERLAY(state->canvas_overlay),
                                 state->text_entry);
    else
      gtk_widget_unparent(state->text_entry);
    state->text_entry = NULL;
  }
  state->text_editing_id = 0;
  if (state->draft_pen_points != NULL)
    g_array_set_size(state->draft_pen_points, 0);
  shaula_preview_cancel_history_gesture(state);
  shaula_preview_update_dimensions_label(state);
  shaula_preview_set_fit_mode(state, TRUE);
}

gboolean shaula_preview_undo(ShaulaPreviewState *state) {
  if (state == NULL)
    return FALSE;
  shaula_preview_commit_history_gesture(state, TRUE);
  if (!shaula_preview_can_undo(state))
    return FALSE;
  ShaulaPreviewSnapshot *snapshot =
      shaula_preview_history_pop_undo(&state->document.history);
  shaula_preview_history_push_redo(
      &state->document.history,
      shaula_preview_document_snapshot_new(&state->document));
  restore_snapshot(state, snapshot);
  shaula_preview_snapshot_free(snapshot);
  shaula_preview_toolbar_update_history_state(state);
  return TRUE;
}

gboolean shaula_preview_redo(ShaulaPreviewState *state) {
  if (state == NULL)
    return FALSE;
  shaula_preview_commit_history_gesture(state, TRUE);
  if (!shaula_preview_can_redo(state))
    return FALSE;
  ShaulaPreviewSnapshot *snapshot =
      shaula_preview_history_pop_redo(&state->document.history);
  shaula_preview_history_push_undo(
      &state->document.history,
      shaula_preview_document_snapshot_new(&state->document), FALSE);
  restore_snapshot(state, snapshot);
  shaula_preview_snapshot_free(snapshot);
  shaula_preview_toolbar_update_history_state(state);
  return TRUE;
}

void shaula_preview_cancel_operation(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  if (state->operation == SHAULA_OPERATION_ERASE_ANNOTATIONS)
    shaula_preview_cancel_eraser_gesture(state);
  state->operation = SHAULA_OPERATION_NONE;
  state->operation_changed = FALSE;
  shaula_preview_gesture_reset(&state->gesture);
  shaula_preview_cancel_history_gesture(state);
  state->has_crop_draft = FALSE;
  state->has_region_selection = FALSE;
  shaula_properties_hud_set_panel(&state->properties_hud,
                                  SHAULA_PROPERTIES_PANEL_NONE);
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
  state->text_editing_id = 0;
  shaula_preview_queue_draw(state);
}
