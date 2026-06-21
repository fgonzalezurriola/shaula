#ifndef SHAULA_PREVIEW_ACTIONS_H
#define SHAULA_PREVIEW_ACTIONS_H

#include "preview_state.h"

/* Internal command runtime operations. UI adapters must execute the public
 * preview_commands Interface instead of calling these functions directly.
 */
void shaula_preview_action_set_tool(ShaulaPreviewState *state, ShaulaTool tool);
void shaula_preview_action_copy(ShaulaPreviewState *state);
void shaula_preview_action_save(ShaulaPreviewState *state);
void shaula_preview_action_done(ShaulaPreviewState *state);
void shaula_preview_action_save_as(ShaulaPreviewState *state);
void shaula_preview_action_close(ShaulaPreviewState *state);
void shaula_preview_action_discard(ShaulaPreviewState *state);
void shaula_preview_action_copy_path(ShaulaPreviewState *state);
void shaula_preview_action_copy_hover_color(ShaulaPreviewState *state);
void shaula_preview_action_use_hover_color(ShaulaPreviewState *state);
void shaula_preview_action_open_containing_folder(ShaulaPreviewState *state);

#endif
