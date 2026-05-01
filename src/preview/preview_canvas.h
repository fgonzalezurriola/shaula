#ifndef SHAULA_PREVIEW_CANVAS_H
#define SHAULA_PREVIEW_CANVAS_H

#include <gtk/gtk.h>

#include "preview_state.h"

GtkWidget *shaula_preview_canvas_build(ShaulaPreviewState *state);
ShaulaPoint shaula_preview_canvas_screen_to_image(ShaulaPreviewState *state,
                                                  double x, double y);
ShaulaPoint shaula_preview_canvas_image_to_screen(ShaulaPreviewState *state,
                                                  ShaulaPoint point);

#endif
