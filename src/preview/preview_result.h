#ifndef SHAULA_PREVIEW_RESULT_H
#define SHAULA_PREVIEW_RESULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaPreviewAction;
enum {
  SHAULA_PREVIEW_ACTION_CLOSE = 0,
  SHAULA_PREVIEW_ACTION_COPY = 1,
  SHAULA_PREVIEW_ACTION_SAVE = 2,
  SHAULA_PREVIEW_ACTION_DISCARD = 3,
  SHAULA_PREVIEW_ACTION_UNKNOWN = 4,
};

typedef int32_t ShaulaPreviewResultStatus;
enum {
  SHAULA_PREVIEW_RESULT_STATUS_OK = 0,
  SHAULA_PREVIEW_RESULT_STATUS_MISSING = 1,
  SHAULA_PREVIEW_RESULT_STATUS_INVALID_JSON = 2,
  SHAULA_PREVIEW_RESULT_STATUS_OUT_OF_MEMORY = 3,
  SHAULA_PREVIEW_RESULT_STATUS_INVALID_ARGUMENT = 4,
};

typedef struct {
  const uint8_t *data;
  size_t length;
} ShaulaPreviewResultSpan;

typedef struct {
  uint8_t *data;
  size_t length;
} ShaulaPreviewResultOwnedBytes;

typedef struct {
  int32_t closed;
  ShaulaPreviewAction action;
  int32_t copied;
  int32_t saved;
  int32_t notified;
  ShaulaPreviewResultOwnedBytes saved_path;
} ShaulaPreviewResult;

_Static_assert(sizeof(ShaulaPreviewAction) == 4,
               "ShaulaPreviewAction must remain a 32-bit C ABI value");
_Static_assert(SHAULA_PREVIEW_ACTION_CLOSE == 0 &&
                   SHAULA_PREVIEW_ACTION_COPY == 1 &&
                   SHAULA_PREVIEW_ACTION_SAVE == 2 &&
                   SHAULA_PREVIEW_ACTION_DISCARD == 3 &&
                   SHAULA_PREVIEW_ACTION_UNKNOWN == 4,
               "preview-action ABI values changed");

/*
 * Initializes caller-provided storage to the parser defaults. The result owns
 * no memory until a successful parse returns a nonempty saved_path.
 */
void shaula_preview_result_init(ShaulaPreviewResult *result);

/*
 * Releases GLib-owned saved_path bytes and restores parser defaults. This is
 * repeat-safe for initialized or zero-initialized results.
 */
void shaula_preview_result_clear(ShaulaPreviewResult *result);

/*
 * Parses the complete length-bearing helper stdout payload. Output must first
 * be initialized with shaula_preview_result_init() or zero initialization;
 * reparsing then releases any previous owned path. Input is borrowed for the
 * synchronous call and may contain embedded NUL bytes. Only ASCII space,
 * tab, carriage return, and newline are trimmed around the document.
 *
 * A JSON object is required. Known fields are optional and wrong-typed values
 * retain defaults; unknown fields and action tokens are tolerated. Duplicate
 * keys anywhere, malformed JSON, invalid UTF-8, trailing data, and non-object
 * roots are rejected. Escaped strings are decoded, including embedded NUL in
 * saved_path. On every non-OK result, output remains initialized and empty.
 *
 * A nonempty saved_path is an independent GLib allocation with an authoritative
 * length and a trailing NUL byte. Release it only through
 * shaula_preview_result_clear().
 */
ShaulaPreviewResultStatus
shaula_preview_result_parse(ShaulaPreviewResultSpan input,
                            ShaulaPreviewResult *output);

/* Returns a borrowed process-lifetime token, or an empty span for invalid ABI values. */
ShaulaPreviewResultSpan
shaula_preview_action_token(ShaulaPreviewAction action);

#ifdef __cplusplus
}
#endif

#endif
