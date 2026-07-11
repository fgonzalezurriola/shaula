#include "compositor/runtime.h"

#include <stddef.h>
#include <string.h>

#define SPAN_LITERAL(value)                                                    \
  { (value), sizeof(value) - 1U }

static const char niri_token[] = "niri";
static const char wayland_token[] = "wayland";
static const char unsupported_token[] = "unsupported";

static const ShaulaEnvSpan wayland_tokens[] = {
    SPAN_LITERAL("wayland"),    SPAN_LITERAL("gnome"),
    SPAN_LITERAL("gnome-shell"), SPAN_LITERAL("kde"),
    SPAN_LITERAL("plasma"),     SPAN_LITERAL("sway"),
    SPAN_LITERAL("hyprland"),   SPAN_LITERAL("river"),
    SPAN_LITERAL("wayfire"),    SPAN_LITERAL("weston"),
    SPAN_LITERAL("labwc"),      SPAN_LITERAL("cage"),
    SPAN_LITERAL("dwl"),
};

static const ShaulaEnvSpan wlroots_tokens[] = {
    SPAN_LITERAL("sway"),     SPAN_LITERAL("hyprland"),
    SPAN_LITERAL("river"),    SPAN_LITERAL("wayfire"),
    SPAN_LITERAL("labwc"),    SPAN_LITERAL("cage"),
    SPAN_LITERAL("dwl"),
};

static ShaulaEnvSpan invalid_span(void) {
  return (ShaulaEnvSpan){NULL, 0U};
}

static ShaulaEnvSpan literal_span(const char *value, size_t length) {
  return (ShaulaEnvSpan){value, length};
}

static void detection_reset(ShaulaCompositorDetection *out) {
  if (out == NULL) {
    return;
  }
  out->kind = SHAULA_COMPOSITOR_KIND_INVALID;
  out->label = invalid_span();
}

static int span_valid(ShaulaEnvSpan value) {
  return value.data != NULL || value.length == 0U;
}

static int kind_valid(ShaulaCompositorKind kind) {
  return kind == SHAULA_COMPOSITOR_KIND_NIRI ||
         kind == SHAULA_COMPOSITOR_KIND_WAYLAND ||
         kind == SHAULA_COMPOSITOR_KIND_UNSUPPORTED;
}

static unsigned char ascii_lower(unsigned char value) {
  if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') {
    return (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'));
  }
  return value;
}

static int span_equals_ascii_ignore_case(ShaulaEnvSpan left,
                                         ShaulaEnvSpan right) {
  size_t index;

  if (!span_valid(left) || !span_valid(right) ||
      left.length != right.length) {
    return 0;
  }
  for (index = 0U; index < left.length; index += 1U) {
    if (ascii_lower((unsigned char)left.data[index]) !=
        ascii_lower((unsigned char)right.data[index])) {
      return 0;
    }
  }
  return 1;
}

static int span_contains_lowercase_wayland(ShaulaEnvSpan value) {
  static const char needle[] = "wayland";
  const size_t needle_length = sizeof(needle) - 1U;
  size_t index;

  if (!span_valid(value) || value.length < needle_length) {
    return 0;
  }
  for (index = 0U; index <= value.length - needle_length; index += 1U) {
    if (memcmp(value.data + index, needle, needle_length) == 0) {
      return 1;
    }
  }
  return 0;
}

static int span_matches_any_ignore_case(ShaulaEnvSpan value,
                                        const ShaulaEnvSpan *tokens,
                                        size_t token_count) {
  size_t index;

  for (index = 0U; index < token_count; index += 1U) {
    if (span_equals_ascii_ignore_case(value, tokens[index])) {
      return 1;
    }
  }
  return 0;
}

static int is_wayland_token(ShaulaEnvSpan value) {
  return span_matches_any_ignore_case(
             value, wayland_tokens,
             sizeof(wayland_tokens) / sizeof(wayland_tokens[0])) ||
         span_contains_lowercase_wayland(value);
}

static int is_wlroots_token(ShaulaEnvSpan value) {
  return span_matches_any_ignore_case(
      value, wlroots_tokens,
      sizeof(wlroots_tokens) / sizeof(wlroots_tokens[0]));
}

ShaulaCompositorStatus
shaula_compositor_classify(ShaulaEnvSpan label,
                            ShaulaCompositorDetection *out) {
  const ShaulaEnvSpan niri =
      literal_span(niri_token, sizeof(niri_token) - 1U);

  detection_reset(out);
  if (out == NULL || !span_valid(label)) {
    return SHAULA_COMPOSITOR_STATUS_INVALID_ARGUMENT;
  }

  if (span_equals_ascii_ignore_case(label, niri)) {
    out->kind = SHAULA_COMPOSITOR_KIND_NIRI;
    out->label = niri;
  } else if (is_wayland_token(label)) {
    out->kind = SHAULA_COMPOSITOR_KIND_WAYLAND;
    out->label = label;
  } else {
    out->kind = SHAULA_COMPOSITOR_KIND_UNSUPPORTED;
    out->label = label;
  }
  return SHAULA_COMPOSITOR_STATUS_OK;
}

ShaulaCompositorStatus
shaula_compositor_detect(const ShaulaCompositorEnvironment *environment,
                          ShaulaCompositorDetection *out) {
  ShaulaEnvSpan value = {NULL, 0U};

  detection_reset(out);
  if (environment == NULL || out == NULL) {
    return SHAULA_COMPOSITOR_STATUS_INVALID_ARGUMENT;
  }

  if (shaula_env_value_trimmed(environment->shaula_compositor, &value) ==
      SHAULA_ENV_STATUS_VALID) {
    return shaula_compositor_classify(value, out);
  }

  if (environment->niri_socket != NULL) {
    out->kind = SHAULA_COMPOSITOR_KIND_NIRI;
    out->label = literal_span(niri_token, sizeof(niri_token) - 1U);
    return SHAULA_COMPOSITOR_STATUS_OK;
  }

  if (shaula_env_value_slice(environment->xdg_current_desktop, &value) ==
          SHAULA_ENV_STATUS_VALID &&
      shaula_env_first_desktop_token(value, &value) ==
          SHAULA_ENV_STATUS_VALID) {
    return shaula_compositor_classify(value, out);
  }

  if (shaula_env_value_trimmed(environment->xdg_session_desktop, &value) ==
      SHAULA_ENV_STATUS_VALID) {
    return shaula_compositor_classify(value, out);
  }

  if (environment->wayland_display != NULL) {
    out->kind = SHAULA_COMPOSITOR_KIND_WAYLAND;
    out->label = literal_span(wayland_token, sizeof(wayland_token) - 1U);
    return SHAULA_COMPOSITOR_STATUS_OK;
  }

  out->kind = SHAULA_COMPOSITOR_KIND_UNSUPPORTED;
  out->label =
      literal_span(unsupported_token, sizeof(unsupported_token) - 1U);
  return SHAULA_COMPOSITOR_STATUS_OK;
}

ShaulaEnvSpan shaula_compositor_kind_token(ShaulaCompositorKind kind) {
  switch (kind) {
  case SHAULA_COMPOSITOR_KIND_NIRI:
    return literal_span(niri_token, sizeof(niri_token) - 1U);
  case SHAULA_COMPOSITOR_KIND_WAYLAND:
    return literal_span(wayland_token, sizeof(wayland_token) - 1U);
  case SHAULA_COMPOSITOR_KIND_UNSUPPORTED:
    return literal_span(unsupported_token, sizeof(unsupported_token) - 1U);
  default:
    return invalid_span();
  }
}

int32_t
shaula_compositor_is_wlroots(ShaulaCompositorDetection detection) {
  if (!kind_valid(detection.kind) || !span_valid(detection.label)) {
    return -1;
  }
  if (detection.kind == SHAULA_COMPOSITOR_KIND_NIRI) {
    return 0;
  }
  return is_wlroots_token(detection.label) ? 1 : 0;
}

int32_t shaula_compositor_supported_in_current_scope(
    ShaulaCompositorDetection detection, int32_t portal_available) {
  int32_t wlroots;

  if ((portal_available != 0 && portal_available != 1) ||
      !kind_valid(detection.kind) || !span_valid(detection.label)) {
    return -1;
  }
  if (detection.kind == SHAULA_COMPOSITOR_KIND_NIRI) {
    return 1;
  }
  wlroots = shaula_compositor_is_wlroots(detection);
  if (wlroots < 0) {
    return -1;
  }
  if (wlroots != 0) {
    return 1;
  }
  return detection.kind == SHAULA_COMPOSITOR_KIND_WAYLAND &&
                 portal_available != 0
             ? 1
             : 0;
}

int32_t
shaula_compositor_overlay_supported(ShaulaCompositorDetection detection) {
  int32_t wlroots;

  if (!kind_valid(detection.kind) || !span_valid(detection.label)) {
    return -1;
  }
  if (detection.kind == SHAULA_COMPOSITOR_KIND_NIRI) {
    return 1;
  }
  wlroots = shaula_compositor_is_wlroots(detection);
  return wlroots;
}
