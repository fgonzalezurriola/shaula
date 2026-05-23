#include "preview_document.h"

#define SHAULA_HISTORY_DEFAULT_CAPACITY 24

struct ShaulaPreviewSnapshot {
  GdkPixbuf *image;
  GPtrArray *annotations;
  GArray *spotlight_regions;
  int next_annotation_id;
  gboolean modified;
};

static GArray *spotlight_regions_clone(GArray *regions) {
  GArray *clone = g_array_new(FALSE, FALSE, sizeof(ShaulaSpotlightRegion));
  if (regions != NULL && regions->len > 0)
    g_array_append_vals(clone, regions->data, regions->len);
  return clone;
}

ShaulaPreviewSnapshot *
shaula_preview_document_snapshot_new(ShaulaPreviewDocument *document) {
  ShaulaPreviewSnapshot *snapshot = g_new0(ShaulaPreviewSnapshot, 1);
  snapshot->image = document->image != NULL
                        ? GDK_PIXBUF(g_object_ref(document->image))
                        : NULL;
  snapshot->annotations =
      shaula_annotations_clone_array(document->annotations);
  snapshot->spotlight_regions =
      spotlight_regions_clone(document->spotlight_regions);
  snapshot->next_annotation_id = document->next_annotation_id;
  snapshot->modified = document->modified;
  return snapshot;
}

void shaula_preview_snapshot_free(gpointer data) {
  ShaulaPreviewSnapshot *snapshot = data;
  if (snapshot == NULL)
    return;
  if (snapshot->image != NULL)
    g_object_unref(snapshot->image);
  if (snapshot->annotations != NULL)
    g_ptr_array_unref(snapshot->annotations);
  if (snapshot->spotlight_regions != NULL)
    g_array_unref(snapshot->spotlight_regions);
  g_free(snapshot);
}

static void history_stack_init(ShaulaHistoryStack *history, guint capacity) {
  history->undo = g_ptr_array_new_with_free_func(shaula_preview_snapshot_free);
  history->redo = g_ptr_array_new_with_free_func(shaula_preview_snapshot_free);
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

gboolean shaula_preview_history_can_undo(ShaulaHistoryStack *history) {
  return history != NULL && history->undo != NULL && history->undo->len > 0;
}

gboolean shaula_preview_history_can_redo(ShaulaHistoryStack *history) {
  return history != NULL && history->redo != NULL && history->redo->len > 0;
}

void shaula_preview_history_clear_redo(ShaulaHistoryStack *history) {
  if (history != NULL && history->redo != NULL)
    g_ptr_array_set_size(history->redo, 0);
}

static void history_stack_clear(GPtrArray *stack) {
  if (stack != NULL)
    g_ptr_array_set_size(stack, 0);
}

static void history_stack_trim_to_capacity(ShaulaHistoryStack *history,
                                           GPtrArray *stack) {
  if (history == NULL || stack == NULL || history->capacity == 0)
    return;
  while (stack->len > history->capacity)
    g_ptr_array_remove_index(stack, 0);
}

void shaula_preview_history_push_undo(ShaulaHistoryStack *history,
                                      ShaulaPreviewSnapshot *snapshot,
                                      gboolean clear_redo) {
  if (history == NULL || history->undo == NULL || snapshot == NULL)
    return;
  g_ptr_array_add(history->undo, snapshot);
  history_stack_trim_to_capacity(history, history->undo);
  if (clear_redo)
    history_stack_clear(history->redo);
}

ShaulaPreviewSnapshot *
shaula_preview_history_pop_undo(ShaulaHistoryStack *history) {
  if (!shaula_preview_history_can_undo(history))
    return NULL;
  return g_ptr_array_steal_index(history->undo, history->undo->len - 1);
}

ShaulaPreviewSnapshot *
shaula_preview_history_pop_redo(ShaulaHistoryStack *history) {
  if (!shaula_preview_history_can_redo(history))
    return NULL;
  return g_ptr_array_steal_index(history->redo, history->redo->len - 1);
}

void shaula_preview_history_push_redo(ShaulaHistoryStack *history,
                                      ShaulaPreviewSnapshot *snapshot) {
  if (history == NULL || history->redo == NULL || snapshot == NULL)
    return;
  g_ptr_array_add(history->redo, snapshot);
  history_stack_trim_to_capacity(history, history->redo);
}

static void annotation_clipboard_init(ShaulaAnnotationClipboard *clipboard) {
  clipboard->annotations =
      g_ptr_array_new_with_free_func(shaula_annotation_free);
  clipboard->last_pasted_id = 0;
}

void shaula_preview_annotation_clipboard_clear(
    ShaulaAnnotationClipboard *clipboard) {
  if (clipboard == NULL)
    return;
  if (clipboard->annotations != NULL)
    g_ptr_array_set_size(clipboard->annotations, 0);
  clipboard->last_pasted_id = 0;
}

static void annotation_clipboard_free(ShaulaAnnotationClipboard *clipboard) {
  if (clipboard == NULL)
    return;
  if (clipboard->annotations != NULL)
    g_ptr_array_unref(clipboard->annotations);
  clipboard->annotations = NULL;
  clipboard->last_pasted_id = 0;
}

void shaula_preview_document_init(ShaulaPreviewDocument *document,
                                  const char *path, GdkPixbuf *image) {
  document->path = g_strdup(path);
  document->image = image;
  document->annotations =
      g_ptr_array_new_with_free_func(shaula_annotation_free);
  annotation_clipboard_init(&document->annotation_clipboard);
  document->spotlight_regions =
      g_array_new(FALSE, FALSE, sizeof(ShaulaSpotlightRegion));
  document->next_annotation_id = 1;
  history_stack_init(&document->history, SHAULA_HISTORY_DEFAULT_CAPACITY);
}

void shaula_preview_document_free(ShaulaPreviewDocument *document) {
  if (document == NULL)
    return;
  if (document->image != NULL)
    g_object_unref(document->image);
  if (document->annotations != NULL)
    g_ptr_array_unref(document->annotations);
  annotation_clipboard_free(&document->annotation_clipboard);
  if (document->spotlight_regions != NULL)
    g_array_unref(document->spotlight_regions);
  if (document->pending_history_snapshot != NULL)
    shaula_preview_snapshot_free(document->pending_history_snapshot);
  history_stack_free(&document->history);
  g_free(document->saved_path);
  g_free(document->path);
}

gboolean
shaula_preview_document_has_modifications(ShaulaPreviewDocument *document) {
  return document != NULL && document->modified;
}

int shaula_preview_document_width(ShaulaPreviewDocument *document) {
  return document != NULL && document->image != NULL
             ? gdk_pixbuf_get_width(document->image)
             : 0;
}

int shaula_preview_document_height(ShaulaPreviewDocument *document) {
  return document != NULL && document->image != NULL
             ? gdk_pixbuf_get_height(document->image)
             : 0;
}

void shaula_preview_document_add_annotation(ShaulaPreviewDocument *document,
                                            ShaulaAnnotation *annotation) {
  if (document == NULL || annotation == NULL || document->annotations == NULL)
    return;
  if (annotation->id <= 0)
    annotation->id = document->next_annotation_id++;
  g_ptr_array_add(document->annotations, annotation);
  document->modified = TRUE;
}

gboolean shaula_preview_document_remove_annotation_at(
    ShaulaPreviewDocument *document, guint index) {
  if (document == NULL || document->annotations == NULL ||
      index >= document->annotations->len)
    return FALSE;
  g_ptr_array_remove_index(document->annotations, index);
  document->modified = TRUE;
  return TRUE;
}

gboolean
shaula_preview_document_clear_annotations(ShaulaPreviewDocument *document) {
  if (document == NULL || document->annotations == NULL ||
      document->annotations->len == 0)
    return FALSE;
  g_ptr_array_set_size(document->annotations, 0);
  document->modified = TRUE;
  return TRUE;
}

void shaula_preview_document_restore_snapshot(
    ShaulaPreviewDocument *document, ShaulaPreviewSnapshot *snapshot,
    ShaulaAnnotation **selected_annotation) {
  if (document == NULL || snapshot == NULL)
    return;

  GPtrArray *annotations = shaula_annotations_clone_array(snapshot->annotations);
  if (document->annotations != NULL)
    g_ptr_array_unref(document->annotations);
  document->annotations = annotations;

  if (selected_annotation != NULL)
    *selected_annotation = NULL;
  if (document->annotations != NULL && selected_annotation != NULL) {
    for (guint i = 0; i < document->annotations->len; i++) {
      ShaulaAnnotation *annotation = g_ptr_array_index(document->annotations, i);
      if (annotation->selected) {
        *selected_annotation = annotation;
        break;
      }
    }
  }

  if (document->image != NULL)
    g_object_unref(document->image);
  document->image =
      snapshot->image != NULL ? GDK_PIXBUF(g_object_ref(snapshot->image)) : NULL;

  if (document->spotlight_regions != NULL)
    g_array_unref(document->spotlight_regions);
  document->spotlight_regions =
      spotlight_regions_clone(snapshot->spotlight_regions);
  document->next_annotation_id = snapshot->next_annotation_id;
  document->modified = snapshot->modified;
}
