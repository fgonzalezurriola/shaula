#ifndef SHAULA_CLIPBOARD_CLIPBOARD_H
#define SHAULA_CLIPBOARD_CLIPBOARD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaClipboardStatus;
enum {
  SHAULA_CLIPBOARD_STATUS_OK = 0,
  SHAULA_CLIPBOARD_STATUS_INVALID_ARGUMENT = 1,
  SHAULA_CLIPBOARD_STATUS_UNAVAILABLE = 2,
  SHAULA_CLIPBOARD_STATUS_READ_FAILED = 3,
  SHAULA_CLIPBOARD_STATUS_PAYLOAD_TOO_LARGE = 4,
  SHAULA_CLIPBOARD_STATUS_SPAWN_FAILED = 5,
  SHAULA_CLIPBOARD_STATUS_IO_FAILED = 6,
  SHAULA_CLIPBOARD_STATUS_TIMEOUT = 7,
  SHAULA_CLIPBOARD_STATUS_PROTOCOL_INVALID = 8,
  SHAULA_CLIPBOARD_STATUS_PROVIDER_FAILED = 9,
};

_Static_assert(sizeof(ShaulaClipboardStatus) == 4,
               "ShaulaClipboardStatus must remain a 32-bit C ABI value");

/*
 * Publish one complete PNG or UTF-8 text selection through the bundled
 * provider. The provider loads the payload before claiming ownership and keeps
 * serving it after the caller exits. Failures map to deterministic clipboard
 * statuses; callers adapt them to their public ERR_* contract.
 */
ShaulaClipboardStatus shaula_clipboard_publish_png_file(const char *path);
ShaulaClipboardStatus shaula_clipboard_publish_text(const char *text,
                                                    size_t length);

/* Returns 1 only when the bundled/overridden provider can be resolved now. */
int32_t shaula_clipboard_provider_available(void);

/* Stable diagnostic token for logs and tests; never NULL. */
const char *shaula_clipboard_status_token(ShaulaClipboardStatus status);

#ifdef __cplusplus
}
#endif

#endif
