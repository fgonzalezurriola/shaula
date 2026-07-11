#ifndef SHAULA_COMPOSITOR_FOCUSED_OUTPUT_H
#define SHAULA_COMPOSITOR_FOCUSED_OUTPUT_H

#include "compositor/runtime.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaFocusedOutputStatus;
enum {
  SHAULA_FOCUSED_OUTPUT_STATUS_OK = 0,
  SHAULA_FOCUSED_OUTPUT_STATUS_INVALID_ARGUMENT = 1,
  SHAULA_FOCUSED_OUTPUT_STATUS_OUT_OF_MEMORY = 2,
};

typedef struct {
  const char *overlay_output_name;
  ShaulaCompositorEnvironment compositor;
} ShaulaFocusedOutputEnvironment;

typedef struct {
  uint8_t *data;
  size_t length;
} ShaulaFocusedOutputOwnedName;

typedef struct {
  int32_t present;
  ShaulaFocusedOutputOwnedName name;
} ShaulaFocusedOutputResult;

_Static_assert(sizeof(ShaulaFocusedOutputStatus) == 4,
               "ShaulaFocusedOutputStatus must remain a 32-bit C ABI value");
_Static_assert(SHAULA_FOCUSED_OUTPUT_STATUS_OK == 0 &&
                   SHAULA_FOCUSED_OUTPUT_STATUS_INVALID_ARGUMENT == 1 &&
                   SHAULA_FOCUSED_OUTPUT_STATUS_OUT_OF_MEMORY == 2,
               "focused-output status ABI values changed");

void shaula_focused_output_result_init(ShaulaFocusedOutputResult *result);
void shaula_focused_output_result_clear(ShaulaFocusedOutputResult *result);

/*
 * Resolves the advisory focused output used by monitor-scoped capture and
 * overlay placement. Environment pointers are borrowed for this synchronous
 * call. A successful present name is GLib-owned, length-bearing, may contain an
 * embedded NUL decoded from JSON, and must be cleared with
 * shaula_focused_output_result_clear().
 *
 * A nonempty ASCII-trimmed SHAULA_OVERLAY_OUTPUT_NAME wins. Otherwise Niri is
 * probed with `niri msg -j focused-output`, and Sway with
 * `swaymsg -t get_outputs -r`. Spawn failures, nonzero exits, output-limit
 * failures, and malformed or incomplete JSON are best-effort absence, not
 * public ERR_* failures. Only allocation of the final returned name is reported
 * as OUT_OF_MEMORY.
 */
ShaulaFocusedOutputStatus shaula_focused_output_resolve(
    const ShaulaFocusedOutputEnvironment *environment,
    ShaulaFocusedOutputResult *out_result);

#ifdef __cplusplus
}
#endif

#endif
