#ifndef SHAULA_PREVIEW_ICONS_H
#define SHAULA_PREVIEW_ICONS_H

#include <gtk/gtk.h>

#include "preview_state.h"

void shaula_preview_register_custom_icons(ShaulaPreviewState *state);
GtkWidget *shaula_preview_make_toolbar_icon(ShaulaPreviewState *state,
                                            const char *icon_name);

#endif
