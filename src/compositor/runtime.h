#ifndef SHAULA_COMPOSITOR_RUNTIME_H
#define SHAULA_COMPOSITOR_RUNTIME_H

#include "runtime/env.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaCompositorKind;
enum {
  SHAULA_COMPOSITOR_KIND_INVALID = -1,
  SHAULA_COMPOSITOR_KIND_NIRI = 0,
  SHAULA_COMPOSITOR_KIND_WAYLAND = 1,
  SHAULA_COMPOSITOR_KIND_UNSUPPORTED = 2,
};

typedef int32_t ShaulaCompositorStatus;
enum {
  SHAULA_COMPOSITOR_STATUS_OK = 0,
  SHAULA_COMPOSITOR_STATUS_INVALID_ARGUMENT = 1,
};

/* Borrowed POSIX environment values. NULL means the variable is absent. */
typedef struct {
  const char *shaula_compositor;
  const char *niri_socket;
  const char *xdg_current_desktop;
  const char *xdg_session_desktop;
  const char *wayland_display;
} ShaulaCompositorEnvironment;

/* label borrows an input value/token or immutable process-lifetime storage. */
typedef struct {
  ShaulaCompositorKind kind;
  ShaulaEnvSpan label;
} ShaulaCompositorDetection;

_Static_assert(sizeof(ShaulaCompositorKind) == 4,
               "ShaulaCompositorKind must remain a 32-bit C ABI value");
_Static_assert(sizeof(ShaulaCompositorStatus) == 4,
               "ShaulaCompositorStatus must remain a 32-bit C ABI value");
_Static_assert(SHAULA_COMPOSITOR_KIND_NIRI == 0 &&
                   SHAULA_COMPOSITOR_KIND_WAYLAND == 1 &&
                   SHAULA_COMPOSITOR_KIND_UNSUPPORTED == 2,
               "compositor-kind ABI values changed");
_Static_assert(SHAULA_COMPOSITOR_STATUS_OK == 0 &&
                   SHAULA_COMPOSITOR_STATUS_INVALID_ARGUMENT == 1,
               "compositor-status ABI values changed");

/*
 * Pure compositor classification. Inputs and successful labels are borrowed;
 * this module allocates nothing, performs no process/filesystem access, uses no
 * locale-sensitive classification, and has no mutable global state.
 *
 * Detection precedence is SHAULA_COMPOSITOR, NIRI_SOCKET presence,
 * XDG_CURRENT_DESKTOP first token, XDG_SESSION_DESKTOP, WAYLAND_DISPLAY
 * presence, then unsupported. Explicit/session values are trimmed only through
 * runtime/env.h's ASCII whitespace contract. Empty NIRI_SOCKET and
 * WAYLAND_DISPLAY values still count as present for compatibility.
 */
ShaulaCompositorStatus
shaula_compositor_detect(const ShaulaCompositorEnvironment *environment,
                          ShaulaCompositorDetection *out);

/*
 * Classifies one explicit-length label. A NULL pointer with zero length is a
 * valid empty label; NULL with nonzero length is invalid. Niri is canonicalized
 * to the immutable "niri" token. Other labels borrow the input exactly,
 * including non-ASCII and embedded-NUL bytes.
 */
ShaulaCompositorStatus
shaula_compositor_classify(ShaulaEnvSpan label,
                            ShaulaCompositorDetection *out);

/* Borrowed immutable kind token, or an empty invalid span for an invalid kind. */
ShaulaEnvSpan shaula_compositor_kind_token(ShaulaCompositorKind kind);

/* Return 1/0 for valid input and -1 for an invalid kind/span/boolean value. */
int32_t
shaula_compositor_is_wlroots(ShaulaCompositorDetection detection);
int32_t shaula_compositor_supported_in_current_scope(
    ShaulaCompositorDetection detection, int32_t portal_available);
int32_t
shaula_compositor_overlay_supported(ShaulaCompositorDetection detection);

#ifdef __cplusplus
}
#endif

#endif
