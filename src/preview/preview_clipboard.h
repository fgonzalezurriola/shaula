#ifndef SHAULA_PREVIEW_CLIPBOARD_H
#define SHAULA_PREVIEW_CLIPBOARD_H

#include <glib.h>

gboolean shaula_clipboard_copy_png_file(const char *path, GError **error);
gboolean shaula_clipboard_copy_text(const char *text, GError **error);

#endif
