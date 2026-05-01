#ifndef SHAULA_PREVIEW_RENDER_H
#define SHAULA_PREVIEW_RENDER_H

#include <glib.h>

#include "preview_state.h"

char *shaula_render_composited_png_temp(ShaulaPreviewState *state,
                                        GError **error);

#endif
