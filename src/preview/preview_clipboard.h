#ifndef SHAULA_PREVIEW_CLIPBOARD_H
#define SHAULA_PREVIEW_CLIPBOARD_H

#include <glib.h>

/*
 * Publish Preview PNG/text output through Shaula's clipboard module. The
 * bundled provider owns the selection after Preview exits; Preview's system
 * paste path remains the asynchronous GTK/GDK reader from ADR-0001.
 */
gboolean shaula_clipboard_copy_png_file(const char *path, GError **error);
gboolean shaula_clipboard_copy_text(const char *text, GError **error);

#endif
