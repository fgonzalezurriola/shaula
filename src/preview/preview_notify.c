#include "preview_notify.h"

#include "process_exec.h"

#include <string.h>

static gboolean uri_byte_is_unescaped(guchar byte) {
  return byte == '/' || g_ascii_isalnum(byte) || byte == '-' || byte == '_' ||
         byte == '.' || byte == '~';
}

/* Returns a GLib-owned image hint, or NULL without aborting on allocation
 * failure. The caller releases it with g_free(). */
static char *image_hint_from_path(const char *path) {
  static const char prefix[] = "string:image-path:file://";
  static const char hex[] = "0123456789ABCDEF";
  gsize length = sizeof(prefix) - 1;

  for (const guchar *cursor = (const guchar *)path; *cursor != '\0'; cursor++) {
    gsize encoded_length = uri_byte_is_unescaped(*cursor) ? 1 : 3;
    if (G_MAXSIZE - length < encoded_length)
      return NULL;
    length += encoded_length;
  }
  if (length == G_MAXSIZE)
    return NULL;

  char *hint = g_try_malloc(length + 1);
  if (hint == NULL)
    return NULL;

  gsize offset = sizeof(prefix) - 1;
  memcpy(hint, prefix, offset);
  for (const guchar *cursor = (const guchar *)path; *cursor != '\0'; cursor++) {
    if (uri_byte_is_unescaped(*cursor)) {
      hint[offset++] = (char)*cursor;
    } else {
      hint[offset++] = '%';
      hint[offset++] = hex[*cursor >> 4];
      hint[offset++] = hex[*cursor & 0x0F];
    }
  }
  hint[offset] = '\0';
  return hint;
}

/* Runs one synchronous notify-send attempt with both child streams discarded.
 */
static gboolean spawn_notify(const char *summary, const char *body,
                             const char *image_path, gboolean transient,
                             int timeout_ms, gboolean icon_mode) {
  char timeout[12];
  g_autofree char *image_argument = NULL;
  char *argv[13];
  gsize argc = 0;

  argv[argc++] = "notify-send";
  argv[argc++] = "--app-name=Shaula";
  argv[argc++] = "--urgency";
  argv[argc++] = "normal";
  argv[argc++] = "--expire-time";
  g_snprintf(timeout, sizeof(timeout), "%d", timeout_ms);
  argv[argc++] = timeout;

  if (transient)
    argv[argc++] = "--transient";

  if (image_path != NULL) {
    if (icon_mode) {
      argv[argc++] = "-i";
      argv[argc++] = (char *)image_path;
    } else {
      image_argument = image_hint_from_path(image_path);
      if (image_argument == NULL)
        return FALSE;
      argv[argc++] = "--hint";
      argv[argc++] = image_argument;
    }
  }

  argv[argc++] = (char *)summary;
  argv[argc++] = (char *)body;
  argv[argc] = NULL;

  int exit_code = 127;
  return shaula_process_run_sync((const char *const *)argv, NULL, NULL, NULL,
                                 &exit_code) == SHAULA_PROCESS_STATUS_OK &&
         exit_code == 0;
}

gboolean shaula_preview_notify(const char *summary, const char *body,
                               const char *image_path, gboolean transient,
                               int timeout_ms) {
  if (summary == NULL || body == NULL)
    return FALSE;

  const char *normalized_image =
      image_path != NULL && image_path[0] != '\0' ? image_path : NULL;
  int normalized_timeout = timeout_ms > 0 ? timeout_ms : 2500;

  if (spawn_notify(summary, body, normalized_image, transient,
                   normalized_timeout, FALSE))
    return TRUE;
  if (normalized_image != NULL)
    return spawn_notify(summary, body, normalized_image, transient,
                        normalized_timeout, TRUE);
  return FALSE;
}
