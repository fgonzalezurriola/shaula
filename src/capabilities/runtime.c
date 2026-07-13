#include "capabilities/runtime.h"

#include "core/capture_mode.h"
#include "runtime/helper_resolution.h"
#include "runtime/process_exec.h"

#include <glib.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ENV_SPAN_LITERAL(value)                                                \
  { (value), sizeof(value) - 1U }

static const char backend_niri_wayland_direct[] = "niri-wayland-direct";
static const char backend_grim_wlroots[] = "grim-wlroots";
static const char backend_portal_screenshot[] = "portal-screenshot";
static const char backend_stub[] = "__stub__";

typedef struct {
  int32_t available;
  int32_t window_capable;
} PortalCapabilities;

static ShaulaEnvSpan invalid_span(void) {
  return (ShaulaEnvSpan){NULL, 0U};
}

static ShaulaEnvSpan literal_span(const char *value, size_t length) {
  return (ShaulaEnvSpan){value, length};
}

static int span_valid(ShaulaEnvSpan value) {
  return value.data != NULL || value.length == 0U;
}

static int span_equals(ShaulaEnvSpan left, ShaulaEnvSpan right) {
  return span_valid(left) && span_valid(right) &&
         left.length == right.length &&
         (left.length == 0U || memcmp(left.data, right.data, left.length) == 0);
}

static int boolean_valid(int32_t value) {
  return value == 0 || value == 1;
}

static int backend_valid(ShaulaBackendKind backend) {
  return backend == SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT ||
         backend == SHAULA_BACKEND_KIND_GRIM_WLROOTS ||
         backend == SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT ||
         backend == SHAULA_BACKEND_KIND_STUB;
}

static void capture_modes_reset(ShaulaCaptureModes *capture) {
  capture->area = 0;
  capture->fullscreen = 0;
  capture->all_screens = 0;
  capture->window = 0;
}

static void decision_reset(ShaulaRuntimeDecision *out) {
  if (out == NULL) {
    return;
  }
  out->compositor_supported = 0;
  out->overlay_supported = 0;
  out->backend = SHAULA_BACKEND_KIND_INVALID;
  capture_modes_reset(&out->capture);
  out->portal_available = 0;
  out->portal_window_capable = 0;
  out->compositor.kind = SHAULA_COMPOSITOR_KIND_INVALID;
  out->compositor.label = invalid_span();
}

static ShaulaCaptureModes capture_modes_for(ShaulaBackendKind backend,
                                             int32_t compositor_supported) {
  ShaulaCaptureModes capture;

  capture_modes_reset(&capture);
  if (compositor_supported == 0 || backend == SHAULA_BACKEND_KIND_STUB) {
    return capture;
  }
  capture.area = 1;
  capture.fullscreen = 1;
  capture.all_screens = 1;
  return capture;
}

static int process_property(const char *property, ShaulaProcessOutput *out) {
  const char *argv[] = {
      "gdbus",
      "call",
      "--session",
      "--timeout",
      "2",
      "--dest",
      "org.freedesktop.portal.Desktop",
      "--object-path",
      "/org/freedesktop/portal/desktop",
      "--method",
      "org.freedesktop.DBus.Properties.Get",
      "org.freedesktop.portal.Screenshot",
      property,
      NULL,
  };

  memset(out, 0, sizeof(*out));
  if (shaula_process_run(argv, NULL, 2048U, 2048U, out) !=
      SHAULA_PROCESS_STATUS_OK) {
    return 0;
  }
  return out->term_kind == SHAULA_PROCESS_TERM_EXITED && out->term_value == 0U;
}

static int parse_last_unsigned(ShaulaProcessOwnedBytes bytes,
                               uint64_t *out_value) {
  size_t end = bytes.length;
  size_t start;
  size_t index;
  uint64_t value = 0U;

  if (out_value == NULL || (bytes.data == NULL && bytes.length != 0U)) {
    return 0;
  }
  while (end > 0U) {
    while (end > 0U &&
           (bytes.data[end - 1U] < '0' || bytes.data[end - 1U] > '9')) {
      end -= 1U;
    }
    if (end == 0U) {
      return 0;
    }
    start = end;
    while (start > 0U && bytes.data[start - 1U] >= '0' &&
           bytes.data[start - 1U] <= '9') {
      start -= 1U;
    }
    for (index = start; index < end; index += 1U) {
      const uint64_t digit = (uint64_t)(bytes.data[index] - '0');
      if (value > (UINT64_MAX - digit) / 10U) {
        return 0;
      }
      value = value * 10U + digit;
    }
    *out_value = value;
    return 1;
  }
  return 0;
}

static PortalCapabilities probe_portal_capabilities(
    const ShaulaCapabilitiesEnvironment *environment) {
  PortalCapabilities portal = {0, 0};
  int32_t flag = 0;
  ShaulaProcessOutput output;
  uint64_t targets = 0U;

  if (shaula_env_value_flag(environment->portal_available, &flag) ==
      SHAULA_ENV_STATUS_VALID) {
    portal.available = flag != 0 ? 1 : 0;
    if (portal.available != 0 &&
        shaula_env_value_flag(environment->portal_window_capable, &flag) ==
            SHAULA_ENV_STATUS_VALID) {
      portal.window_capable = flag != 0 ? 1 : 0;
    }
    return portal;
  }

  if (!process_property("version", &output)) {
    shaula_process_output_clear(&output);
    return portal;
  }
  shaula_process_output_clear(&output);
  portal.available = 1;

  if (!process_property("AvailableTargets", &output)) {
    shaula_process_output_clear(&output);
    return portal;
  }
  if (parse_last_unsigned(output.stdout_bytes, &targets) != 0 &&
      ((targets & UINT64_C(2)) != 0U || (targets & UINT64_C(8)) != 0U)) {
    portal.window_capable = 1;
  }
  shaula_process_output_clear(&output);
  return portal;
}

static ShaulaBackendKind backend_override(const char *value) {
  ShaulaEnvSpan token = {NULL, 0U};

  if (shaula_env_value_trimmed(value, &token) != SHAULA_ENV_STATUS_VALID) {
    return SHAULA_BACKEND_KIND_INVALID;
  }
  if (span_equals(token, literal_span(backend_stub, sizeof(backend_stub) - 1U))) {
    return SHAULA_BACKEND_KIND_STUB;
  }
  if (span_equals(token, literal_span(backend_portal_screenshot,
                                      sizeof(backend_portal_screenshot) - 1U))) {
    return SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT;
  }
  if (span_equals(token, literal_span(backend_grim_wlroots,
                                      sizeof(backend_grim_wlroots) - 1U))) {
    return SHAULA_BACKEND_KIND_GRIM_WLROOTS;
  }
  if (span_equals(token, literal_span(backend_niri_wayland_direct,
                                      sizeof(backend_niri_wayland_direct) - 1U))) {
    return SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT;
  }
  return SHAULA_BACKEND_KIND_INVALID;
}

static int force_portal(const char *value) {
  int32_t enabled = 0;
  return shaula_env_value_flag(value, &enabled) == SHAULA_ENV_STATUS_VALID &&
                 enabled != 0
             ? 1
             : 0;
}

static int grim_available(void) {
  g_autofree char *path = shaula_executable_find_grim();
  return path != NULL ? 1 : 0;
}

static ShaulaBackendKind resolve_backend(
    const ShaulaCapabilitiesEnvironment *environment,
    ShaulaCompositorDetection compositor, int32_t portal_available) {
  const ShaulaBackendKind override = backend_override(environment->capture_backend);
  int32_t wlroots;

  if (backend_valid(override)) {
    return override;
  }
  if (force_portal(environment->capture_force_portal) != 0) {
    return SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT;
  }
  if (compositor.kind == SHAULA_COMPOSITOR_KIND_NIRI) {
    return SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT;
  }

  wlroots = shaula_compositor_is_wlroots(compositor);
  if (wlroots > 0) {
    if (grim_available() != 0) {
      return SHAULA_BACKEND_KIND_GRIM_WLROOTS;
    }
    if (portal_available != 0) {
      return SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT;
    }
    return SHAULA_BACKEND_KIND_GRIM_WLROOTS;
  }
  if (portal_available != 0 &&
      compositor.kind == SHAULA_COMPOSITOR_KIND_WAYLAND) {
    return SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT;
  }
  return SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT;
}

ShaulaCapabilitiesStatus
shaula_capabilities_resolve(const ShaulaCapabilitiesEnvironment *environment,
                            ShaulaRuntimeDecision *out) {
  PortalCapabilities portal;
  int32_t supported;
  int32_t overlay_supported;

  decision_reset(out);
  if (environment == NULL || out == NULL) {
    return SHAULA_CAPABILITIES_STATUS_INVALID_ARGUMENT;
  }
  if (shaula_compositor_detect(&environment->compositor, &out->compositor) !=
      SHAULA_COMPOSITOR_STATUS_OK) {
    return SHAULA_CAPABILITIES_STATUS_INVALID_ARGUMENT;
  }

  portal = probe_portal_capabilities(environment);
  supported = shaula_compositor_supported_in_current_scope(out->compositor,
                                                            portal.available);
  overlay_supported = shaula_compositor_overlay_supported(out->compositor);
  if (!boolean_valid(supported) || !boolean_valid(overlay_supported)) {
    decision_reset(out);
    return SHAULA_CAPABILITIES_STATUS_INVALID_ARGUMENT;
  }

  out->compositor_supported = supported;
  out->overlay_supported = overlay_supported;
  out->backend = resolve_backend(environment, out->compositor, portal.available);
  out->capture = capture_modes_for(out->backend, supported);
  out->portal_available = portal.available;
  out->portal_window_capable = portal.window_capable;
  return SHAULA_CAPABILITIES_STATUS_OK;
}

ShaulaEnvSpan shaula_capabilities_backend_label(ShaulaBackendKind backend) {
  switch (backend) {
  case SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT:
    return literal_span(backend_niri_wayland_direct,
                        sizeof(backend_niri_wayland_direct) - 1U);
  case SHAULA_BACKEND_KIND_GRIM_WLROOTS:
    return literal_span(backend_grim_wlroots,
                        sizeof(backend_grim_wlroots) - 1U);
  case SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT:
    return literal_span(backend_portal_screenshot,
                        sizeof(backend_portal_screenshot) - 1U);
  case SHAULA_BACKEND_KIND_STUB:
    return literal_span(backend_stub, sizeof(backend_stub) - 1U);
  default:
    return invalid_span();
  }
}

size_t shaula_capabilities_fallback_count(ShaulaBackendKind backend) {
  switch (backend) {
  case SHAULA_BACKEND_KIND_NIRI_WAYLAND_DIRECT:
  case SHAULA_BACKEND_KIND_GRIM_WLROOTS:
  case SHAULA_BACKEND_KIND_STUB:
    return 1U;
  case SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT:
  default:
    return 0U;
  }
}

ShaulaBackendKind shaula_capabilities_fallback_at(ShaulaBackendKind backend,
                                                   size_t index) {
  if (shaula_capabilities_fallback_count(backend) == 1U && index == 0U) {
    return SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT;
  }
  return SHAULA_BACKEND_KIND_INVALID;
}

int32_t shaula_capabilities_mode_supported(ShaulaCaptureModes capture,
                                           ShaulaEnvSpan mode) {
  ShaulaCaptureMode parsed;

  if (!span_valid(mode) || !boolean_valid(capture.area) ||
      !boolean_valid(capture.fullscreen) ||
      !boolean_valid(capture.all_screens) || !boolean_valid(capture.window)) {
    return -1;
  }
  parsed = shaula_capture_mode_parse_cli_token(
      (ShaulaCaptureModeSpan){mode.data, mode.length});
  switch (parsed) {
  case SHAULA_CAPTURE_MODE_QUICK:
  case SHAULA_CAPTURE_MODE_AREA:
  case SHAULA_CAPTURE_MODE_ALL_IN_ONE:
    return capture.area;
  case SHAULA_CAPTURE_MODE_FULLSCREEN:
  case SHAULA_CAPTURE_MODE_FOCUSED:
    return capture.fullscreen;
  case SHAULA_CAPTURE_MODE_ALL_SCREENS:
    return capture.all_screens;
  case SHAULA_CAPTURE_MODE_WINDOW:
    return capture.window;
  default:
    return 0;
  }
}

static int decision_valid(ShaulaRuntimeDecision decision) {
  return boolean_valid(decision.compositor_supported) &&
         boolean_valid(decision.overlay_supported) &&
         boolean_valid(decision.capture.area) &&
         boolean_valid(decision.capture.fullscreen) &&
         boolean_valid(decision.capture.all_screens) &&
         boolean_valid(decision.capture.window) &&
         boolean_valid(decision.portal_available) &&
         boolean_valid(decision.portal_window_capable) &&
         backend_valid(decision.backend) && span_valid(decision.compositor.label);
}

int32_t
shaula_capabilities_uses_portal_backend(ShaulaRuntimeDecision decision) {
  if (!decision_valid(decision)) {
    return -1;
  }
  return decision.backend == SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT ? 1 : 0;
}

int32_t shaula_capabilities_degraded_backend(ShaulaRuntimeDecision decision) {
  return shaula_capabilities_uses_portal_backend(decision);
}

int32_t shaula_capabilities_should_bypass_overlay_selection(
    ShaulaRuntimeDecision decision) {
  const int32_t portal = shaula_capabilities_uses_portal_backend(decision);
  if (portal < 0) {
    return -1;
  }
  return portal != 0 || decision.overlay_supported == 0 ? 1 : 0;
}

int32_t shaula_capabilities_portal_selection_available(
    ShaulaRuntimeDecision decision) {
  const int32_t portal = shaula_capabilities_uses_portal_backend(decision);
  if (portal < 0) {
    return -1;
  }
  return portal != 0 || decision.portal_available != 0 ? 1 : 0;
}

int32_t shaula_capabilities_previous_area_supported(
    ShaulaRuntimeDecision decision) {
  const int32_t portal = shaula_capabilities_uses_portal_backend(decision);
  if (portal < 0) {
    return -1;
  }
  return portal == 0 ? 1 : 0;
}

ShaulaCapabilitiesStatus
shaula_capabilities_select_portal_fallback(ShaulaRuntimeDecision *decision) {
  if (decision == NULL || !decision_valid(*decision)) {
    return SHAULA_CAPABILITIES_STATUS_INVALID_ARGUMENT;
  }
  decision->backend = SHAULA_BACKEND_KIND_PORTAL_SCREENSHOT;
  decision->capture = capture_modes_for(decision->backend,
                                        decision->compositor_supported);
  return SHAULA_CAPABILITIES_STATUS_OK;
}
