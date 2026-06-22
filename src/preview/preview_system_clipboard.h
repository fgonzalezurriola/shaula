#ifndef SHAULA_PREVIEW_SYSTEM_CLIPBOARD_H
#define SHAULA_PREVIEW_SYSTEM_CLIPBOARD_H

#include <gtk/gtk.h>

typedef struct ShaulaPreviewState ShaulaPreviewState;
typedef struct ShaulaSystemClipboardPaste ShaulaSystemClipboardPaste;

ShaulaSystemClipboardPaste *shaula_system_clipboard_paste_new(
    GtkWidget *window, ShaulaPreviewState *state);
void shaula_system_clipboard_paste_free(ShaulaSystemClipboardPaste *paste);
gboolean shaula_system_clipboard_paste_is_busy(
    const ShaulaSystemClipboardPaste *paste);
gboolean shaula_system_clipboard_paste_request(ShaulaPreviewState *state);
void shaula_system_clipboard_paste_cancel(ShaulaPreviewState *state);

#endif
