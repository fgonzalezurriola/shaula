#ifndef SHAULA_CAPABILITIES_RUNTIME_H
#define SHAULA_CAPABILITIES_RUNTIME_H

#include "compositor/runtime.h"
#include "runtime/env.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaBackendKind;
enum {
  SHAULA_BACKEND_KIND_INVALID = -1,
  SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT = 0,
  SHAULA_BACKEND_KIND_GRIM_WLROOTS = 1,
  SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT = 2,
  SHAULA_BACKEND_KIND_STUB = 3,
};

typedef int32_t ShaulaCapabilitiesStatus;
enum {
  SHAULA_CAPABILITIES_STATUS_OK = 0,
  SHAULA_CAPABILITIES_STATUS_INVALID_ARGUMENT = 1,
};

typedef struct {
  int32_t area;
  int32_t fullscreen;
  int32_t all_screens;
  int32_t window;
} ShaulaCaptureModes;

/* Borrowed POSIX environment values. NULL means the variable is absent. */
typedef struct {
  ShaulaCompositorEnvironment compositor;
  const char *capture_backend;
  const char *capture_force_portal;
  const char *portal_available;
  const char *portal_window_capable;
} ShaulaCapabilitiesEnvironment;

/*
 * All spans borrow environment storage or immutable process-lifetime literals.
 * The structure owns no memory and requires no cleanup.
 */
typedef struct {
  int32_t compositor_supported;
  int32_t overlay_supported;
  ShaulaBackendKind backend;
  ShaulaCaptureModes capture;
  int32_t portal_available;
  int32_t portal_window_capable;
  ShaulaCompositorDetection compositor;
} ShaulaRuntimeDecision;

_Static_assert(sizeof(ShaulaBackendKind) == 4,
               "ShaulaBackendKind must remain a 32-bit C ABI value");
_Static_assert(sizeof(ShaulaCapabilitiesStatus) == 4,
               "ShaulaCapabilitiesStatus must remain a 32-bit C ABI value");
_Static_assert(SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT == 0 &&
                   SHAULA_BACKEND_KIND_GRIM_WLROOTS == 1 &&
                   SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT == 2 &&
                   SHAULA_BACKEND_KIND_STUB == 3,
               "backend-kind ABI values changed");

/*
 * Resolve compositor support, portal availability, backend selection, overlay
 * support, and the strict capture-mode matrix. Environment storage is borrowed
 * for this synchronous call and for any noncanonical compositor label returned
 * in out. Portal probing is advisory: spawn, output, parse, and exit failures
 * produce an unavailable portal rather than a public error.
 */
ShaulaCapabilitiesStatus
shaula_capabilities_resolve(const ShaulaCapabilitiesEnvironment *environment,
                            ShaulaRuntimeDecision *out);

/* Borrowed immutable public backend label, or an empty invalid span. */
ShaulaEnvSpan shaula_capabilities_backend_label(ShaulaBackendKind backend);

/* Ordered fallback backend kinds. Invalid backend/index returns INVALID. */
size_t shaula_capabilities_fallback_count(ShaulaBackendKind backend);
ShaulaBackendKind shaula_capabilities_fallback_at(ShaulaBackendKind backend,
                                                   size_t index);

/* Exact public/compatibility mode-token support check; invalid inputs return -1. */
int32_t shaula_capabilities_mode_supported(ShaulaCaptureModes capture,
                                           ShaulaEnvSpan mode);

/* Runtime-decision policy helpers. Invalid structures return -1. */
int32_t
shaula_capabilities_uses_portal_backend(ShaulaRuntimeDecision decision);
int32_t shaula_capabilities_degraded_backend(ShaulaRuntimeDecision decision);
int32_t
shaula_capabilities_should_bypass_overlay_selection(ShaulaRuntimeDecision decision);
int32_t
shaula_capabilities_portal_selection_available(ShaulaRuntimeDecision decision);
int32_t
shaula_capabilities_previous_area_supported(ShaulaRuntimeDecision decision);

/* Mutates only backend and capture. Returns INVALID for malformed decisions. */
ShaulaCapabilitiesStatus
shaula_capabilities_select_portal_fallback(ShaulaRuntimeDecision *decision);

#ifdef __cplusplus
}
#endif

#endif
