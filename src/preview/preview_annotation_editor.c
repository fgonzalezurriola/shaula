#include "preview_annotation_editor.h"

#include "preview_state.h"
#include "preview_toolbar.h"

#define SHAULA_PASTE_OFFSET_PX 8.0

static ShaulaAnnotation *annotation_by_id(const ShaulaPreviewState *state,
                                          int id) {
  if (state == NULL || state->document.annotations == NULL || id <= 0)
    return NULL;
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, i);
    if (annotation != NULL && annotation->id == id)
      return annotation;
  }
  return NULL;
}

static gboolean selected_ids_contain(const ShaulaPreviewState *state, int id) {
  if (state == NULL || state->annotation_editor.selected_ids == NULL || id <= 0)
    return FALSE;
  for (guint i = 0; i < state->annotation_editor.selected_ids->len; i++) {
    if (g_array_index(state->annotation_editor.selected_ids, int, i) == id)
      return TRUE;
  }
  return FALSE;
}

static void selected_ids_add(ShaulaPreviewState *state, int id) {
  if (state == NULL || state->annotation_editor.selected_ids == NULL || id <= 0 ||
      selected_ids_contain(state, id))
    return;
  g_array_append_val(state->annotation_editor.selected_ids, id);
}

static void selected_ids_remove(ShaulaPreviewState *state, int id) {
  if (state == NULL || state->annotation_editor.selected_ids == NULL || id <= 0)
    return;
  for (guint i = 0; i < state->annotation_editor.selected_ids->len; i++) {
    if (g_array_index(state->annotation_editor.selected_ids, int, i) == id) {
      g_array_remove_index(state->annotation_editor.selected_ids, i);
      return;
    }
  }
}

/* Selection IDs are the editor source of truth. Annotation flags remain synced
 * because rendering and document snapshots persist that contract.
 */
static void sync_selection(ShaulaPreviewState *state) {
  if (state == NULL || state->annotation_editor.selected_ids == NULL)
    return;

  for (gint i = (gint)state->annotation_editor.selected_ids->len - 1; i >= 0;
       i--) {
    int id =
        g_array_index(state->annotation_editor.selected_ids, int, (guint)i);
    if (annotation_by_id(state, id) == NULL)
      g_array_remove_index(state->annotation_editor.selected_ids, (guint)i);
  }

  if (state->document.annotations != NULL) {
    for (guint i = 0; i < state->document.annotations->len; i++) {
      ShaulaAnnotation *annotation =
          g_ptr_array_index(state->document.annotations, i);
      if (annotation != NULL)
        annotation->selected = selected_ids_contain(state, annotation->id);
    }
  }

  shaula_properties_hud_target_annotation(
      &state->properties_hud,
      shaula_annotation_editor_single_selection(state));
}

static void refresh_selection_ui(ShaulaPreviewState *state) {
  shaula_preview_queue_draw(state);
  shaula_preview_toolbar_update_selection_state(state);
}

static ShaulaAnnotation *annotation_clone_with_offset(
    const ShaulaAnnotation *base, double dx, double dy) {
  ShaulaAnnotation *clone = shaula_annotation_clone(base);
  if (clone == NULL)
    return NULL;
  clone->id = 0;
  clone->selected = FALSE;
  shaula_annotation_move(clone, dx, dy);
  return clone;
}

static GPtrArray *annotation_array_clone_with_offset(GPtrArray *base, double dx,
                                                     double dy) {
  GPtrArray *clone = g_ptr_array_new_with_free_func(shaula_annotation_free);
  if (base == NULL)
    return clone;

  for (guint i = 0; i < base->len; i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(base, i);
    ShaulaAnnotation *copy = annotation_clone_with_offset(annotation, dx, dy);
    if (copy == NULL) {
      g_ptr_array_unref(clone);
      return NULL;
    }
    g_ptr_array_add(clone, copy);
  }
  return clone;
}

/* Repeated paste always uses one deterministic down-right step. */
static GPtrArray *annotation_array_clone_for_paste(GPtrArray *base) {
  if (base == NULL || base->len == 0)
    return NULL;
  return annotation_array_clone_with_offset(base, SHAULA_PASTE_OFFSET_PX,
                                            SHAULA_PASTE_OFFSET_PX);
}

void shaula_annotation_editor_init(ShaulaAnnotationEditor *editor) {
  if (editor == NULL)
    return;
  editor->selected_ids = g_array_new(FALSE, FALSE, sizeof(int));
}

void shaula_annotation_editor_dispose(ShaulaAnnotationEditor *editor) {
  if (editor == NULL)
    return;
  if (editor->selected_ids != NULL)
    g_array_unref(editor->selected_ids);
  editor->selected_ids = NULL;
}

void shaula_annotation_editor_rebuild_selection(ShaulaPreviewState *state) {
  if (state == NULL || state->annotation_editor.selected_ids == NULL)
    return;
  g_array_set_size(state->annotation_editor.selected_ids, 0);
  if (state->document.annotations != NULL) {
    for (guint i = 0; i < state->document.annotations->len; i++) {
      ShaulaAnnotation *annotation =
          g_ptr_array_index(state->document.annotations, i);
      if (annotation != NULL && annotation->selected)
        selected_ids_add(state, annotation->id);
    }
  }
  sync_selection(state);
  shaula_preview_toolbar_update_selection_state(state);
}

gboolean shaula_annotation_editor_has_selection(
    const ShaulaPreviewState *state) {
  return state != NULL && state->annotation_editor.selected_ids != NULL &&
         state->annotation_editor.selected_ids->len > 0;
}

guint shaula_annotation_editor_selected_count(
    const ShaulaPreviewState *state) {
  return state != NULL && state->annotation_editor.selected_ids != NULL
             ? state->annotation_editor.selected_ids->len
             : 0;
}

ShaulaAnnotation *shaula_annotation_editor_single_selection(
    const ShaulaPreviewState *state) {
  if (shaula_annotation_editor_selected_count(state) != 1)
    return NULL;
  int id = g_array_index(state->annotation_editor.selected_ids, int, 0);
  return annotation_by_id(state, id);
}

gboolean shaula_annotation_editor_is_selected(
    const ShaulaPreviewState *state, const ShaulaAnnotation *annotation) {
  return annotation != NULL && selected_ids_contain(state, annotation->id);
}

gboolean shaula_annotation_editor_selected_bounds(
    const ShaulaPreviewState *state, ShaulaRect *bounds_out) {
  if (state == NULL)
    return FALSE;
  return shaula_annotations_selected_bounds(state->document.annotations,
                                             bounds_out);
}

void shaula_annotation_editor_select_only(ShaulaPreviewState *state,
                                          ShaulaAnnotation *annotation) {
  if (state == NULL)
    return;
  gboolean changed =
      (annotation == NULL && shaula_annotation_editor_has_selection(state)) ||
      (annotation != NULL &&
       (shaula_annotation_editor_selected_count(state) != 1 ||
        !selected_ids_contain(state, annotation->id)));
  if (changed)
    shaula_preview_commit_history_gesture(state, TRUE);

  g_array_set_size(state->annotation_editor.selected_ids, 0);
  if (annotation != NULL) {
    state->has_region_selection = FALSE;
    selected_ids_add(state, annotation->id);
  }
  sync_selection(state);
  refresh_selection_ui(state);
}

void shaula_annotation_editor_toggle_selection(ShaulaPreviewState *state,
                                               ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL)
    return;
  shaula_preview_commit_history_gesture(state, TRUE);
  if (selected_ids_contain(state, annotation->id))
    selected_ids_remove(state, annotation->id);
  else
    selected_ids_add(state, annotation->id);
  if (shaula_annotation_editor_has_selection(state))
    state->has_region_selection = FALSE;
  sync_selection(state);
  refresh_selection_ui(state);
}

void shaula_annotation_editor_clear_selection(ShaulaPreviewState *state) {
  shaula_annotation_editor_select_only(state, NULL);
}

void shaula_annotation_editor_select_all(ShaulaPreviewState *state) {
  if (state == NULL || state->document.annotations == NULL ||
      state->document.annotations->len == 0)
    return;
  shaula_preview_commit_history_gesture(state, TRUE);
  g_array_set_size(state->annotation_editor.selected_ids, 0);
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, i);
    if (annotation != NULL)
      selected_ids_add(state, annotation->id);
  }
  state->has_region_selection = FALSE;
  sync_selection(state);
  refresh_selection_ui(state);
}

guint shaula_annotation_editor_select_intersecting_rect(
    ShaulaPreviewState *state, ShaulaRect rect) {
  if (state == NULL || state->document.annotations == NULL ||
      state->document.annotations->len == 0)
    return 0;

  rect = shaula_rect_normalized(rect);
  if (shaula_rect_is_empty(rect))
    return 0;

  GArray *matched_ids = g_array_new(FALSE, FALSE, sizeof(int));
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, i);
    if (shaula_annotation_intersects_selection_rect(annotation, rect)) {
      int id = annotation->id;
      g_array_append_val(matched_ids, id);
    }
  }
  guint matched_count = matched_ids->len;
  if (matched_count == 0) {
    g_array_unref(matched_ids);
    return 0;
  }

  shaula_preview_commit_history_gesture(state, TRUE);
  g_array_set_size(state->annotation_editor.selected_ids, 0);
  for (guint i = 0; i < matched_ids->len; i++) {
    int id = g_array_index(matched_ids, int, i);
    selected_ids_add(state, id);
  }
  g_array_unref(matched_ids);
  state->has_region_selection = FALSE;
  sync_selection(state);
  refresh_selection_ui(state);
  return matched_count;
}

void shaula_annotation_editor_add_annotation(ShaulaPreviewState *state,
                                             ShaulaAnnotation *annotation) {
  if (state == NULL || annotation == NULL)
    return;
  shaula_preview_push_undo(state);
  shaula_preview_document_add_annotation(&state->document, annotation);
  shaula_annotation_editor_select_only(state, annotation);
  shaula_preview_toolbar_update_history_state(state);
}

void shaula_annotation_editor_move_selected(ShaulaPreviewState *state,
                                            double dx, double dy) {
  if (state == NULL || state->document.annotations == NULL)
    return;
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, i);
    if (annotation != NULL && annotation->selected)
      shaula_annotation_move(annotation, dx, dy);
  }
  if (shaula_annotation_editor_has_selection(state))
    state->document.modified = TRUE;
}

gboolean shaula_annotation_editor_can_paste(const ShaulaPreviewState *state) {
  return state != NULL && state->document.image != NULL &&
         state->document.annotation_clipboard.annotations != NULL &&
         state->document.annotation_clipboard.annotations->len > 0;
}

static GPtrArray *selected_annotations_payload(ShaulaPreviewState *state) {
  GPtrArray *payload = g_ptr_array_new_with_free_func(shaula_annotation_free);
  if (state == NULL || state->document.annotations == NULL)
    return payload;

  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, i);
    if (!shaula_annotation_editor_is_selected(state, annotation))
      continue;
    ShaulaAnnotation *copy = shaula_annotation_clone(annotation);
    if (copy == NULL) {
      g_ptr_array_unref(payload);
      return NULL;
    }
    copy->selected = FALSE;
    g_ptr_array_add(payload, copy);
  }
  return payload;
}

static GPtrArray *last_pasted_annotations_payload(ShaulaPreviewState *state) {
  if (state == NULL)
    return NULL;
  ShaulaAnnotationClipboard *clipboard = &state->document.annotation_clipboard;
  if (clipboard->last_pasted_ids == NULL ||
      clipboard->last_pasted_ids->len == 0 ||
      clipboard->last_pasted_ids->len != clipboard->annotations->len)
    return NULL;

  GPtrArray *payload = g_ptr_array_new();
  for (guint i = 0; i < clipboard->last_pasted_ids->len; i++) {
    int id = g_array_index(clipboard->last_pasted_ids, int, i);
    ShaulaAnnotation *annotation = annotation_by_id(state, id);
    if (annotation == NULL) {
      g_ptr_array_unref(payload);
      return NULL;
    }
    g_ptr_array_add(payload, annotation);
  }
  return payload;
}

static gboolean paste_annotation_payload(ShaulaPreviewState *state,
                                         GPtrArray *payload,
                                         gboolean remember_last_paste) {
  if (state == NULL || payload == NULL || payload->len == 0)
    return FALSE;

  GPtrArray *pasted = annotation_array_clone_for_paste(payload);
  if (pasted == NULL)
    return FALSE;

  shaula_preview_push_undo(state);
  state->has_region_selection = FALSE;
  g_array_set_size(state->annotation_editor.selected_ids, 0);
  if (remember_last_paste &&
      state->document.annotation_clipboard.last_pasted_ids != NULL)
    g_array_set_size(state->document.annotation_clipboard.last_pasted_ids, 0);

  for (guint i = 0; i < pasted->len; i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(pasted, i);
    shaula_preview_document_add_annotation(&state->document, annotation);
    g_ptr_array_index(pasted, i) = NULL;
    selected_ids_add(state, annotation->id);
    if (remember_last_paste &&
        state->document.annotation_clipboard.last_pasted_ids != NULL) {
      int id = annotation->id;
      g_array_append_val(state->document.annotation_clipboard.last_pasted_ids,
                         id);
    }
  }

  g_ptr_array_unref(pasted);
  sync_selection(state);
  refresh_selection_ui(state);
  shaula_preview_toolbar_update_history_state(state);
  return TRUE;
}

gboolean shaula_annotation_editor_copy_selected(ShaulaPreviewState *state) {
  if (!shaula_annotation_editor_has_selection(state))
    return FALSE;

  GPtrArray *payload = selected_annotations_payload(state);
  if (payload == NULL || payload->len == 0) {
    if (payload != NULL)
      g_ptr_array_unref(payload);
    return FALSE;
  }

  shaula_preview_annotation_clipboard_clear(
      &state->document.annotation_clipboard);
  for (guint i = 0; i < payload->len; i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(payload, i);
    g_ptr_array_add(state->document.annotation_clipboard.annotations,
                    annotation);
    g_ptr_array_index(payload, i) = NULL;
  }
  g_ptr_array_unref(payload);
  return TRUE;
}

gboolean shaula_annotation_editor_cut_selected(ShaulaPreviewState *state) {
  if (!shaula_annotation_editor_copy_selected(state))
    return FALSE;
  shaula_annotation_editor_delete_selected(state);
  return TRUE;
}

gboolean shaula_annotation_editor_duplicate_selected(ShaulaPreviewState *state) {
  if (!shaula_annotation_editor_has_selection(state))
    return FALSE;

  GPtrArray *payload = selected_annotations_payload(state);
  if (payload == NULL)
    return FALSE;
  gboolean pasted = paste_annotation_payload(state, payload, FALSE);
  g_ptr_array_unref(payload);
  return pasted;
}

gboolean shaula_annotation_editor_paste(ShaulaPreviewState *state) {
  if (!shaula_annotation_editor_can_paste(state))
    return FALSE;

  GPtrArray *base = last_pasted_annotations_payload(state);
  gboolean owns_base = TRUE;
  if (base == NULL) {
    base = state->document.annotation_clipboard.annotations;
    owns_base = FALSE;
  }
  gboolean pasted = paste_annotation_payload(state, base, TRUE);
  if (owns_base)
    g_ptr_array_unref(base);
  return pasted;
}

void shaula_annotation_editor_delete_selected(ShaulaPreviewState *state) {
  if (state == NULL || !shaula_annotation_editor_has_selection(state) ||
      state->document.annotations == NULL)
    return;

  shaula_preview_push_undo(state);
  state->has_region_selection = FALSE;
  for (gint i = (gint)state->document.annotations->len - 1; i >= 0; i--) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, (guint)i);
    if (shaula_annotation_editor_is_selected(state, annotation))
      shaula_preview_document_remove_annotation_at(&state->document, (guint)i);
  }
  g_array_set_size(state->annotation_editor.selected_ids, 0);
  sync_selection(state);
  refresh_selection_ui(state);
  shaula_preview_toolbar_update_history_state(state);
}

void shaula_annotation_editor_reset_annotations(ShaulaPreviewState *state) {
  if (state == NULL || state->document.annotations == NULL ||
      state->document.annotations->len == 0)
    return;

  shaula_preview_cancel_operation(state);
  shaula_preview_push_undo(state);
  g_array_set_size(state->annotation_editor.selected_ids, 0);
  shaula_preview_clear_eraser_pending(state);
  shaula_preview_document_clear_annotations(&state->document);
  sync_selection(state);
  refresh_selection_ui(state);
  shaula_preview_toolbar_update_history_state(state);
}
