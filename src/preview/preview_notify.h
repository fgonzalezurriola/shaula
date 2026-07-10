#ifndef SHAULA_PREVIEW_NOTIFY_H
#define SHAULA_PREVIEW_NOTIFY_H

#include <glib.h>

/*
 * Sends one best-effort desktop notification through notify-send.
 * summary and body are required borrowed NUL-terminated strings; empty strings
 * are valid. image_path is borrowed and optional, with NULL or an empty string
 * meaning no image. timeout_ms values less than or equal to zero use 2500 ms.
 * The call is synchronous, retains no pointer, suppresses child stdout/stderr,
 * and returns FALSE without setting GError when argv construction, spawn, exit,
 * or signal handling fails.
 */
gboolean shaula_preview_notify(const char *summary, const char *body,
                               const char *image_path, gboolean transient,
                               int timeout_ms);

#endif
