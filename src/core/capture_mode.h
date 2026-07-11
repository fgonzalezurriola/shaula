#ifndef SHAULA_CORE_CAPTURE_MODE_H
#define SHAULA_CORE_CAPTURE_MODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaCaptureMode;
enum {
  SHAULA_CAPTURE_MODE_INVALID = -1,
  SHAULA_CAPTURE_MODE_QUICK = 0,
  SHAULA_CAPTURE_MODE_AREA = 1,
  SHAULA_CAPTURE_MODE_FULLSCREEN = 2,
  SHAULA_CAPTURE_MODE_ALL_SCREENS = 3,
  SHAULA_CAPTURE_MODE_FOCUSED = 4,
  SHAULA_CAPTURE_MODE_WINDOW = 5,
  SHAULA_CAPTURE_MODE_PREVIOUS_AREA = 6,
  SHAULA_CAPTURE_MODE_ALL_IN_ONE = 7,
};

typedef int32_t ShaulaRuntimeCaptureMode;
enum {
  SHAULA_RUNTIME_CAPTURE_MODE_INVALID = -1,
  SHAULA_RUNTIME_CAPTURE_MODE_AREA = 0,
  SHAULA_RUNTIME_CAPTURE_MODE_CURRENT_OUTPUT = 1,
  SHAULA_RUNTIME_CAPTURE_MODE_ALL_OUTPUTS = 2,
  SHAULA_RUNTIME_CAPTURE_MODE_WINDOW = 3,
};

typedef int32_t ShaulaRegionCaptureMode;
enum {
  SHAULA_REGION_CAPTURE_MODE_INVALID = -1,
  SHAULA_REGION_CAPTURE_MODE_LIVE = 0,
  SHAULA_REGION_CAPTURE_MODE_FROZEN = 1,
};

typedef struct {
  const char *data;
  size_t length;
} ShaulaCaptureModeSpan;

_Static_assert(sizeof(ShaulaCaptureMode) == 4,
               "ShaulaCaptureMode must remain a 32-bit C ABI value");
_Static_assert(sizeof(ShaulaRuntimeCaptureMode) == 4,
               "ShaulaRuntimeCaptureMode must remain a 32-bit C ABI value");
_Static_assert(sizeof(ShaulaRegionCaptureMode) == 4,
               "ShaulaRegionCaptureMode must remain a 32-bit C ABI value");
_Static_assert(SHAULA_CAPTURE_MODE_QUICK == 0 &&
                   SHAULA_CAPTURE_MODE_AREA == 1 &&
                   SHAULA_CAPTURE_MODE_FULLSCREEN == 2 &&
                   SHAULA_CAPTURE_MODE_ALL_SCREENS == 3 &&
                   SHAULA_CAPTURE_MODE_FOCUSED == 4 &&
                   SHAULA_CAPTURE_MODE_WINDOW == 5 &&
                   SHAULA_CAPTURE_MODE_PREVIOUS_AREA == 6 &&
                   SHAULA_CAPTURE_MODE_ALL_IN_ONE == 7,
               "capture-mode ABI values changed");
_Static_assert(SHAULA_RUNTIME_CAPTURE_MODE_AREA == 0 &&
                   SHAULA_RUNTIME_CAPTURE_MODE_CURRENT_OUTPUT == 1 &&
                   SHAULA_RUNTIME_CAPTURE_MODE_ALL_OUTPUTS == 2 &&
                   SHAULA_RUNTIME_CAPTURE_MODE_WINDOW == 3,
               "runtime capture-mode ABI values changed");
_Static_assert(SHAULA_REGION_CAPTURE_MODE_LIVE == 0 &&
                   SHAULA_REGION_CAPTURE_MODE_FROZEN == 1,
               "region capture-mode ABI values changed");

/*
 * This pure model has no allocation, filesystem access, locale dependence, or
 * mutable global state. Input spans are borrowed for each call and may contain
 * embedded NUL bytes. Returned spans borrow process-lifetime string literals.
 * Parsing is exact and case-sensitive; no trimming, aliases beyond the explicit
 * CLI table, prefixes, suffixes, or Unicode normalization are applied.
 */
ShaulaCaptureMode
shaula_capture_mode_parse_cli_token(ShaulaCaptureModeSpan token);
ShaulaCaptureModeSpan shaula_capture_mode_cli_token(ShaulaCaptureMode mode);

ShaulaRuntimeCaptureMode
shaula_capture_mode_runtime_mode(ShaulaCaptureMode mode);
ShaulaCaptureModeSpan
shaula_runtime_capture_mode_token(ShaulaRuntimeCaptureMode mode);
ShaulaCaptureModeSpan
shaula_capture_mode_backend_token(ShaulaCaptureMode mode);

/* Returns 1 or 0 for valid modes and -1 for an invalid enum value. */
int32_t shaula_capture_mode_requires_interactive_selection(
    ShaulaCaptureMode mode);

ShaulaRegionCaptureMode
shaula_region_capture_mode_parse(ShaulaCaptureModeSpan token);
ShaulaCaptureModeSpan
shaula_region_capture_mode_token(ShaulaRegionCaptureMode mode);

#ifdef __cplusplus
}
#endif

#endif
