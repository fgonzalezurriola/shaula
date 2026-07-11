#ifndef SHAULA_PREFLIGHT_PROBE_H
#define SHAULA_PREFLIGHT_PROBE_H

#include "capabilities/runtime.h"
#include "cli/json.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaPreflightStatus;
enum {
  SHAULA_PREFLIGHT_STATUS_OK = 0,
  SHAULA_PREFLIGHT_STATUS_INVALID_ARGUMENT = 1,
  SHAULA_PREFLIGHT_STATUS_SIZE_OVERFLOW = 2,
  SHAULA_PREFLIGHT_STATUS_OUT_OF_MEMORY = 3,
  SHAULA_PREFLIGHT_STATUS_TIMESTAMP_OUT_OF_RANGE = 4,
  SHAULA_PREFLIGHT_STATUS_INTERNAL_ERROR = 5,
};

/*
 * json is GLib-owned, length-bearing, and trailing-NUL storage. Initialize or
 * zero-initialize before use, then release through shaula_preflight_output_clear.
 */
typedef struct {
  ShaulaJsonOwnedBytes json;
  uint8_t exit_code;
} ShaulaPreflightOutput;

_Static_assert(sizeof(ShaulaPreflightStatus) == 4,
               "ShaulaPreflightStatus must remain a 32-bit C ABI value");

void shaula_preflight_output_init(ShaulaPreflightOutput *output);
void shaula_preflight_output_clear(ShaulaPreflightOutput *output);

/*
 * Build the complete public preflight JSON response for one borrowed environment
 * snapshot. The environment and warning span are borrowed only for this
 * synchronous call. WAYLAND_DISPLAY readiness is based on variable presence,
 * so a present empty value remains ready. The returned exit code and JSON are
 * replaced atomically on success; failures leave output empty and clearable.
 */
ShaulaPreflightStatus shaula_preflight_build(
    const ShaulaCapabilitiesEnvironment *environment,
    int64_t unix_seconds,
    ShaulaJsonSpan portal_fallback_warning,
    ShaulaPreflightOutput *output);

#ifdef __cplusplus
}
#endif

#endif
