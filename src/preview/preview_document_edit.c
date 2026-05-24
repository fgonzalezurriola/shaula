#include "preview_document_edit.h"

#include "preview_toolbar.h"

/* Owns the common output-affecting edit lifecycle.
 *
 * Callers still perform the specific pixel/annotation mutation, but undo
 * snapshot timing, modified state, transient selection cleanup, and toolbar
 * refresh stay in one place.
 */
void shaula_preview_document_begin_edit(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  shaula_preview_push_undo(state);
  state->document.modified = TRUE;
}

/* Takes ownership of replacement when non-null. */
void shaula_preview_document_replace_image(ShaulaPreviewState *state,
                                           GdkPixbuf *replacement) {
  if (state == NULL || replacement == NULL)
    return;
  if (state->document.image != NULL)
    g_object_unref(state->document.image);
  state->document.image = replacement;
}

void shaula_preview_document_finish_edit(
    ShaulaPreviewState *state, ShaulaPreviewDocumentFinish finish) {
  if (state == NULL)
    return;

  if (finish.clear_crop_draft)
    state->has_crop_draft = FALSE;
  if (finish.clear_region_selection) {
    state->has_region_selection = FALSE;
  }
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
