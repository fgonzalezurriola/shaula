#ifndef SHAULA_PREVIEW_CLIPBOARD_H
#define SHAULA_PREVIEW_CLIPBOARD_H

#include <glib.h>

/*
 * Copies the PNG at borrowed path through the public Shaula clipboard CLI.
 * path must be non-NULL and non-empty. The call is synchronous and retains no
 * pointer. On failure, error receives a caller-owned GError when provided; free
 * it with g_error_free() or g_clear_error().
 */
gboolean shaula_clipboard_copy_png_file(const char *path, GError **error);

/*
 * Copies borrowed NUL-terminated text to wl-copy through an explicit argv and
 * stdin pipe. NULL is treated as an empty string. The bytes are copied into an
 * owned GBytes for the synchronous subprocess call and released before return.
 * On failure, error receives a caller-owned GError when provided; free it with
 * g_error_free() or g_clear_error().
 */
gboolean shaula_clipboard_copy_text(const char *text, GError **error);

#endif
