#include "capture_mode.h"

#include <string.h>

#define LITERAL_SPAN(value)                                                   \
  { (value), sizeof(value) - 1 }

static const ShaulaCaptureModeSpan cli_tokens[] = {
    LITERAL_SPAN("quick"),         LITERAL_SPAN("area"),
    LITERAL_SPAN("fullscreen"),    LITERAL_SPAN("all-screens"),
    LITERAL_SPAN("focused"),       LITERAL_SPAN("window"),
    LITERAL_SPAN("previous-area"), LITERAL_SPAN("all-in-one"),
};

static const ShaulaRuntimeCaptureMode runtime_modes[] = {
    SHAULA_RUNTIME_CAPTURE_MODE_AREA,
    SHAULA_RUNTIME_CAPTURE_MODE_AREA,
    SHAULA_RUNTIME_CAPTURE_MODE_CURRENT_OUTPUT,
    SHAULA_RUNTIME_CAPTURE_MODE_ALL_OUTPUTS,
    SHAULA_RUNTIME_CAPTURE_MODE_CURRENT_OUTPUT,
    SHAULA_RUNTIME_CAPTURE_MODE_WINDOW,
    SHAULA_RUNTIME_CAPTURE_MODE_AREA,
    SHAULA_RUNTIME_CAPTURE_MODE_AREA,
};

static const ShaulaCaptureModeSpan runtime_tokens[] = {
    LITERAL_SPAN("area"), LITERAL_SPAN("current-output"),
    LITERAL_SPAN("all-outputs"), LITERAL_SPAN("window"),
};

static const ShaulaCaptureModeSpan backend_tokens[] = {
    LITERAL_SPAN("area"),          LITERAL_SPAN("area"),
    LITERAL_SPAN("fullscreen"),    LITERAL_SPAN("all-screens"),
    LITERAL_SPAN("focused"),       LITERAL_SPAN("window"),
    LITERAL_SPAN("area"),          LITERAL_SPAN("area"),
};

static const int32_t interactive_selection[] = {1, 1, 0, 0, 0, 0, 0, 1};

static const ShaulaCaptureModeSpan region_tokens[] = {
    LITERAL_SPAN("live"),
    LITERAL_SPAN("frozen"),
};

static ShaulaCaptureModeSpan invalid_span(void) {
  return (ShaulaCaptureModeSpan){NULL, 0};
}

static int span_is_valid(ShaulaCaptureModeSpan span) {
  return span.data != NULL || span.length == 0;
}

static int span_equal(ShaulaCaptureModeSpan left,
                      ShaulaCaptureModeSpan right) {
  return left.length == right.length &&
         (left.length == 0 || memcmp(left.data, right.data, left.length) == 0);
}

static int capture_mode_is_valid(ShaulaCaptureMode mode) {
  return mode >= SHAULA_CAPTURE_MODE_QUICK &&
         mode <= SHAULA_CAPTURE_MODE_ALL_IN_ONE;
}

static int runtime_mode_is_valid(ShaulaRuntimeCaptureMode mode) {
  return mode >= SHAULA_RUNTIME_CAPTURE_MODE_AREA &&
         mode <= SHAULA_RUNTIME_CAPTURE_MODE_WINDOW;
}

static int region_mode_is_valid(ShaulaRegionCaptureMode mode) {
  return mode >= SHAULA_REGION_CAPTURE_MODE_LIVE &&
         mode <= SHAULA_REGION_CAPTURE_MODE_FROZEN;
}

ShaulaCaptureMode
shaula_capture_mode_parse_cli_token(ShaulaCaptureModeSpan token) {
  size_t index;

  if (!span_is_valid(token)) {
    return SHAULA_CAPTURE_MODE_INVALID;
  }
  for (index = 0; index < sizeof(cli_tokens) / sizeof(cli_tokens[0]);
       index += 1) {
    if (span_equal(token, cli_tokens[index])) {
      return (ShaulaCaptureMode)index;
    }
  }
  return SHAULA_CAPTURE_MODE_INVALID;
}

ShaulaCaptureModeSpan shaula_capture_mode_cli_token(ShaulaCaptureMode mode) {
  if (!capture_mode_is_valid(mode)) {
    return invalid_span();
  }
  return cli_tokens[(size_t)mode];
}

ShaulaRuntimeCaptureMode
shaula_capture_mode_runtime_mode(ShaulaCaptureMode mode) {
  if (!capture_mode_is_valid(mode)) {
    return SHAULA_RUNTIME_CAPTURE_MODE_INVALID;
  }
  return runtime_modes[(size_t)mode];
}

ShaulaCaptureModeSpan
shaula_runtime_capture_mode_token(ShaulaRuntimeCaptureMode mode) {
  if (!runtime_mode_is_valid(mode)) {
    return invalid_span();
  }
  return runtime_tokens[(size_t)mode];
}

ShaulaCaptureModeSpan
shaula_capture_mode_backend_token(ShaulaCaptureMode mode) {
  if (!capture_mode_is_valid(mode)) {
    return invalid_span();
  }
  return backend_tokens[(size_t)mode];
}

int32_t shaula_capture_mode_requires_interactive_selection(
    ShaulaCaptureMode mode) {
  if (!capture_mode_is_valid(mode)) {
    return -1;
  }
  return interactive_selection[(size_t)mode];
}

ShaulaRegionCaptureMode
shaula_region_capture_mode_parse(ShaulaCaptureModeSpan token) {
  size_t index;

  if (!span_is_valid(token)) {
    return SHAULA_REGION_CAPTURE_MODE_INVALID;
  }
  for (index = 0; index < sizeof(region_tokens) / sizeof(region_tokens[0]);
       index += 1) {
    if (span_equal(token, region_tokens[index])) {
      return (ShaulaRegionCaptureMode)index;
    }
  }
  return SHAULA_REGION_CAPTURE_MODE_INVALID;
}

ShaulaCaptureModeSpan
shaula_region_capture_mode_token(ShaulaRegionCaptureMode mode) {
  if (!region_mode_is_valid(mode)) {
    return invalid_span();
  }
  return region_tokens[(size_t)mode];
}
