#ifndef SHAULA_PREVIEW_TOOLBAR_H
#define SHAULA_PREVIEW_TOOLBAR_H

#include <gtk/gtk.h>

#include "preview_state.h"

GtkWidget *shaula_preview_toolbar_build(ShaulaPreviewState *state);
void shaula_preview_toolbar_prepare_initial_layout(ShaulaPreviewState *state,
                                                   int window_width);
void shaula_preview_toolbar_update_tool_state(ShaulaPreviewState *state);
void shaula_preview_toolbar_update_history_state(ShaulaPreviewState *state);
void shaula_preview_toolbar_update_selection_state(ShaulaPreviewState *state);
void shaula_preview_toolbar_update_system_paste_state(ShaulaPreviewState *state);

#endif
