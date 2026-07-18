#include "preview_clipboard.h"

#include "clipboard/clipboard.h"

#include <string.h>

#define SHAULA_PREVIEW_CLIPBOARD_ERROR_DOMAIN "shaula-preview-clipboard"

enum {
  SHAULA_PREVIEW_CLIPBOARD_ERROR_MISSING_PNG_PATH = 1,
  SHAULA_PREVIEW_CLIPBOARD_ERROR_COPY_IMAGE_FAILED = 2,
  SHAULA_PREVIEW_CLIPBOARD_ERROR_COPY_TEXT_FAILED = 3,
};

static GQuark shaula_preview_clipboard_error_quark(void) {
  return g_quark_from_static_string(SHAULA_PREVIEW_CLIPBOARD_ERROR_DOMAIN);
}

static void set_clipboard_error(GError **error, int code, const char *message,
                                ShaulaClipboardStatus status) {
  g_set_error(error, shaula_preview_clipboard_error_quark(), code, "%s (%s)",
              message, shaula_clipboard_status_token(status));
}

gboolean shaula_clipboard_copy_png_file(const char *path, GError **error) {
  if (path == NULL || path[0] == '\0') {
    g_set_error_literal(error, shaula_preview_clipboard_error_quark(),
                        SHAULA_PREVIEW_CLIPBOARD_ERROR_MISSING_PNG_PATH,
                        "missing PNG path");
    return FALSE;
  }

  ShaulaClipboardStatus status = shaula_clipboard_publish_png_file(path);
  if (status != SHAULA_CLIPBOARD_STATUS_OK) {
    set_clipboard_error(error,
                        SHAULA_PREVIEW_CLIPBOARD_ERROR_COPY_IMAGE_FAILED,
                        "Shaula clipboard image copy failed", status);
    return FALSE;
  }
  return TRUE;
}

gboolean shaula_clipboard_copy_text(const char *text, GError **error) {
  const char *input = text != NULL ? text : "";
  const size_t length = strlen(input);
  if (length == 0U) {
    g_set_error_literal(error, shaula_preview_clipboard_error_quark(),
                        SHAULA_PREVIEW_CLIPBOARD_ERROR_COPY_TEXT_FAILED,
                        "Shaula clipboard text copy failed (invalid_argument)");
    return FALSE;
  }

  ShaulaClipboardStatus status = shaula_clipboard_publish_text(input, length);
  if (status != SHAULA_CLIPBOARD_STATUS_OK) {
    set_clipboard_error(error,
                        SHAULA_PREVIEW_CLIPBOARD_ERROR_COPY_TEXT_FAILED,
                        "Shaula clipboard text copy failed", status);
    return FALSE;
  }
  return TRUE;
}
