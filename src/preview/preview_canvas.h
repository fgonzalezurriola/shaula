#ifndef SHAULA_PREVIEW_CANVAS_H
#define SHAULA_PREVIEW_CANVAS_H

#include <gtk/gtk.h>

#include "preview_state.h"

/* Commit the visible text draft through the normal undo/selection path.
 * Export callers must do this before reading annotations or modification state.
 */
void shaula_preview_commit_text_pending(ShaulaPreviewState *state);

GtkWidget *shaula_preview_canvas_build(ShaulaPreviewState *state);
ShaulaPoint shaula_preview_canvas_screen_to_image(ShaulaPreviewState *state,
                                                  double x, double y);
ShaulaPoint shaula_preview_canvas_image_to_screen(ShaulaPreviewState *state,
                                                  ShaulaPoint point);

#endif
