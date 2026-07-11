#ifndef SHAULA_NOTIFY_REQUEST_H
#define SHAULA_NOTIFY_REQUEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const uint8_t *data;
  size_t length;
} ShaulaNotifySpan;

typedef struct {
  uint8_t *data;
  size_t length;
} ShaulaNotifyOwnedBytes;

typedef int32_t ShaulaNotifyUrgency;
enum {
  SHAULA_NOTIFY_URGENCY_INVALID = -1,
  SHAULA_NOTIFY_URGENCY_LOW = 0,
  SHAULA_NOTIFY_URGENCY_NORMAL = 1,
  SHAULA_NOTIFY_URGENCY_CRITICAL = 2,
};

typedef int32_t ShaulaNotifyImageMode;
enum {
  SHAULA_NOTIFY_IMAGE_MODE_INVALID = -1,
  SHAULA_NOTIFY_IMAGE_MODE_HINT = 0,
  SHAULA_NOTIFY_IMAGE_MODE_ICON = 1,
};

typedef int32_t ShaulaNotifyStatus;
enum {
  SHAULA_NOTIFY_STATUS_OK = 0,
  SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT = 1,
  SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW = 2,
  SHAULA_NOTIFY_STATUS_OUT_OF_MEMORY = 3,
  SHAULA_NOTIFY_STATUS_INVALID_URGENCY = 4,
  SHAULA_NOTIFY_STATUS_INVALID_IMAGE_MODE = 5,
};

enum { SHAULA_NOTIFY_SEND_ARG_CAPACITY = 16 };

typedef struct {
  ShaulaNotifySpan summary;
  ShaulaNotifySpan body;
  uint8_t has_image_path;
  ShaulaNotifySpan image_path;
  ShaulaNotifyUrgency urgency;
  uint32_t timeout_ms;
  uint8_t transient;
  uint8_t has_action;
  ShaulaNotifySpan action_id;
  ShaulaNotifySpan action_label;
} ShaulaNotifyRequest;

typedef struct {
  ShaulaNotifySpan items[SHAULA_NOTIFY_SEND_ARG_CAPACITY];
  size_t length;
  ShaulaNotifyOwnedBytes timeout;
  ShaulaNotifyOwnedBytes image_hint;
  ShaulaNotifyOwnedBytes action_arg;
} ShaulaNotifySendArgs;

_Static_assert(sizeof(ShaulaNotifyUrgency) == 4,
               "notification urgency ABI must remain 32-bit");
_Static_assert(sizeof(ShaulaNotifyImageMode) == 4,
               "notification image-mode ABI must remain 32-bit");
_Static_assert(sizeof(ShaulaNotifyStatus) == 4,
               "notification status ABI must remain 32-bit");
_Static_assert(SHAULA_NOTIFY_URGENCY_LOW == 0 &&
                   SHAULA_NOTIFY_URGENCY_NORMAL == 1 &&
                   SHAULA_NOTIFY_URGENCY_CRITICAL == 2,
               "notification urgency ABI values changed");
_Static_assert(SHAULA_NOTIFY_IMAGE_MODE_HINT == 0 &&
                   SHAULA_NOTIFY_IMAGE_MODE_ICON == 1,
               "notification image-mode ABI values changed");
_Static_assert(SHAULA_NOTIFY_STATUS_OK == 0 &&
                   SHAULA_NOTIFY_STATUS_INVALID_ARGUMENT == 1 &&
                   SHAULA_NOTIFY_STATUS_SIZE_OVERFLOW == 2 &&
                   SHAULA_NOTIFY_STATUS_OUT_OF_MEMORY == 3 &&
                   SHAULA_NOTIFY_STATUS_INVALID_URGENCY == 4 &&
                   SHAULA_NOTIFY_STATUS_INVALID_IMAGE_MODE == 5,
               "notification status ABI values changed");
_Static_assert(SHAULA_NOTIFY_SEND_ARG_CAPACITY == 16,
               "notification argv capacity changed");

/*
 * Initializes the historical request defaults: normal urgency, 2500 ms timeout,
 * transient delivery, and absent image/action optionals. Summary and body are
 * valid empty borrowed spans until the caller replaces them.
 */
void shaula_notify_request_init(ShaulaNotifyRequest *request);

/* Borrowed immutable process-lifetime storage; invalid values return {NULL, 0}. */
ShaulaNotifySpan shaula_notify_urgency_token(ShaulaNotifyUrgency urgency);

/* Zero-initialized owned byte storage is also valid. */
void shaula_notify_owned_bytes_init(ShaulaNotifyOwnedBytes *output);
void shaula_notify_owned_bytes_clear(ShaulaNotifyOwnedBytes *output);

/* Zero-initialized send-argument storage is also valid. */
void shaula_notify_send_args_init(ShaulaNotifySendArgs *output);
void shaula_notify_send_args_clear(ShaulaNotifySendArgs *output);

/*
 * Builds the exact notify-send argv contract without invoking a shell.
 *
 * Input spans are borrowed explicit-length bytes and may contain embedded NUL.
 * The returned item spans borrow request bytes, process-lifetime literals, or
 * the GLib-owned timeout/image/action buffers retained by output. The request
 * bytes and output must therefore remain alive until argv consumption finishes.
 * Clear output with shaula_notify_send_args_clear(). Repeated clear is safe.
 *
 * has_image_path, transient, and has_action must each be 0 or 1. Present empty
 * image/action spans remain present rather than collapsing to absence. The
 * builder validates and observes every byte; it makes no hidden NUL-termination,
 * UTF-8, locale, filesystem, or shell assumptions.
 */
ShaulaNotifyStatus shaula_notify_send_args_build(
    const ShaulaNotifyRequest *request,
    ShaulaNotifyImageMode image_mode,
    ShaulaNotifySendArgs *output);

/*
 * Produces file:// followed by bytewise URI escaping. ASCII alphanumerics plus
 * '/', '-', '_', '.', and '~' remain literal; every other byte uses uppercase
 * percent encoding. The GLib-owned result is length-bearing with trailing-NUL
 * storage and must be cleared with shaula_notify_owned_bytes_clear().
 */
ShaulaNotifyStatus shaula_notify_file_uri_build(
    ShaulaNotifySpan path,
    ShaulaNotifyOwnedBytes *output);

#ifdef __cplusplus
}
#endif

#endif
