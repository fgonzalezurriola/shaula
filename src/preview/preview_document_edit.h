#ifndef SHAULA_PREVIEW_DOCUMENT_EDIT_H
#define SHAULA_PREVIEW_DOCUMENT_EDIT_H

#include "preview_state.h"

typedef struct {
  gboolean clear_crop_draft;
  gboolean clear_region_selection;
  gboolean reset_tool_to_select;
  gboolean update_dimensions;
  gboolean fit_to_screen;
  gboolean queue_draw;
} ShaulaPreviewDocumentFinish;

void shaula_preview_document_begin_edit(ShaulaPreviewState *state);
void shaula_preview_document_replace_image(ShaulaPreviewState *state,
                                           GdkPixbuf *replacement);
void shaula_preview_document_finish_edit(
    ShaulaPreviewState *state, ShaulaPreviewDocumentFinish finish);

#endif
