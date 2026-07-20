#include "taxonomy.h"

#include <string.h>

#define SHAULA_LITERAL_SPAN(value)                                            \
  { (value), sizeof(value) - 1U }

#define SHAULA_ERROR_SPEC(code_value, message_value, retryable_value,          \
                          class_value, action_value, exit_value)               \
  {                                                                            \
    SHAULA_LITERAL_SPAN(code_value), SHAULA_LITERAL_SPAN(message_value),        \
        (retryable_value), (class_value), (action_value), (exit_value)          \
  }

static const ShaulaErrorSpec error_specs[] = {
    SHAULA_ERROR_SPEC("ERR_CLI_USAGE", "invalid CLI usage", 0,
                      SHAULA_FAILURE_CLASS_CLI,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 2),
    SHAULA_ERROR_SPEC("ERR_UNSUPPORTED_COMPOSITOR",
                      "unsupported compositor for shaula v1", 0,
                      SHAULA_FAILURE_CLASS_COMPOSITOR,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 10),
    SHAULA_ERROR_SPEC("ERR_PREFLIGHT_ENV_NOT_READY",
                      "wayland environment is not ready", 1,
                      SHAULA_FAILURE_CLASS_COMPOSITOR,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 11),
    SHAULA_ERROR_SPEC("ERR_IPC_TIMEOUT", "IPC operation timed out", 1,
                      SHAULA_FAILURE_CLASS_IPC,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 23),
    SHAULA_ERROR_SPEC("ERR_CAPTURE_BACKEND_UNAVAILABLE",
                      "capture backend unavailable", 1,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_DEGRADE_TO_PORTAL, 30),
    SHAULA_ERROR_SPEC("ERR_WINDOW_TARGET_UNRESOLVED",
                      "window target could not be resolved", 0,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 31),
    SHAULA_ERROR_SPEC("ERR_CAPTURE_TIMEOUT", "capture operation timed out", 1,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 32),
    SHAULA_ERROR_SPEC("ERR_CAPTURE_IN_PROGRESS",
                      "another capture is already in progress", 1,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 46),
    SHAULA_ERROR_SPEC(
        "ERR_CAPTURE_PRECONDITION_TIMEOUT",
        "capture precondition timed out waiting for shell artifact guard", 1,
        SHAULA_FAILURE_CLASS_BACKEND,
        SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 35),
    SHAULA_ERROR_SPEC("ERR_SELECTION_CANCELLED",
                      "selection was cancelled by user", 0,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 33),
    SHAULA_ERROR_SPEC(
        "ERR_CAPTURE_MODE_UNSUPPORTED",
        "capture mode is unsupported by runtime capabilities", 0,
        SHAULA_FAILURE_CLASS_BACKEND,
        SHAULA_RECOVERY_ACTION_FAIL_FAST, 34),
    SHAULA_ERROR_SPEC("ERR_PREVIOUS_AREA_UNAVAILABLE",
                      "previous area is unavailable", 0,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 39),
    SHAULA_ERROR_SPEC("ERR_CLIPBOARD_UNAVAILABLE",
                      "clipboard backend is unavailable", 0,
                      SHAULA_FAILURE_CLASS_CLIPBOARD,
                      SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 40),
    SHAULA_ERROR_SPEC("ERR_OVERLAY_UNAVAILABLE",
                      "overlay helper is unavailable", 1,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 36),
    SHAULA_ERROR_SPEC("ERR_OVERLAY_TIMEOUT", "overlay helper timed out", 1,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 37),
    SHAULA_ERROR_SPEC("ERR_OVERLAY_PROTOCOL_INVALID",
                      "overlay helper produced invalid protocol payload", 0,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 38),
    SHAULA_ERROR_SPEC("ERR_CLIPBOARD_IMPORT_INVALID",
                      "clipboard image import failed", 0,
                      SHAULA_FAILURE_CLASS_CLIPBOARD,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 41),
    SHAULA_ERROR_SPEC("ERR_CLIPBOARD_COPY_FAILED",
                      "clipboard image copy failed", 0,
                      SHAULA_FAILURE_CLASS_CLIPBOARD,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 42),
    SHAULA_ERROR_SPEC("ERR_PREVIEW_INPUT_INVALID",
                      "preview input image is invalid", 0,
                      SHAULA_FAILURE_CLASS_OUTPUT,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 43),
    SHAULA_ERROR_SPEC("ERR_PREVIEW_UNAVAILABLE",
                      "preview helper is unavailable", 1,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 44),
    SHAULA_ERROR_SPEC("ERR_SETTINGS_UNAVAILABLE",
                      "settings helper is unavailable", 1,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 45),
    SHAULA_ERROR_SPEC("ERR_HISTORY_STORE_UNAVAILABLE",
                      "history store unavailable", 0,
                      SHAULA_FAILURE_CLASS_OUTPUT,
                      SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 50),
    SHAULA_ERROR_SPEC("ERR_HISTORY_ENTRY_NOT_FOUND",
                      "history entry was not found", 0,
                      SHAULA_FAILURE_CLASS_OUTPUT,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 52),
    SHAULA_ERROR_SPEC("ERR_OUTPUT_PATH_INVALID",
                      "output path is not writable", 0,
                      SHAULA_FAILURE_CLASS_OUTPUT,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 51),
    SHAULA_ERROR_SPEC("ERR_CONFIG_UNREADABLE",
                      "configuration file is unreadable", 0,
                      SHAULA_FAILURE_CLASS_OUTPUT,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 60),
    SHAULA_ERROR_SPEC("ERR_CONFIG_INVALID", "invalid configuration file", 0,
                      SHAULA_FAILURE_CLASS_CLI,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 61),
    SHAULA_ERROR_SPEC("ERR_NIRI_KEYBIND_CONFLICT",
                      "existing Niri keybind conflict detected", 0,
                      SHAULA_FAILURE_CLASS_CLI,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 62),
    SHAULA_ERROR_SPEC("ERR_SHORTCUTS_UNSUPPORTED",
                      "automatic global shortcuts are unsupported", 0,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 63),
    SHAULA_ERROR_SPEC("ERR_SHORTCUT_PERMISSION_DENIED",
                      "desktop shortcut permission was denied", 0,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 64),
    SHAULA_ERROR_SPEC("ERR_SHORTCUT_PROVIDER_UNAVAILABLE",
                      "shortcut provider is unavailable", 1,
                      SHAULA_FAILURE_CLASS_BACKEND,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 65),
    SHAULA_ERROR_SPEC("ERR_SHORTCUT_SESSION_LOST",
                      "shortcut portal session was lost", 1,
                      SHAULA_FAILURE_CLASS_IPC,
                      SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 66),
    SHAULA_ERROR_SPEC("ERR_SHORTCUT_CONFIGURATION_INVALID",
                      "shortcut configuration is invalid", 0,
                      SHAULA_FAILURE_CLASS_OUTPUT,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 67),
    SHAULA_ERROR_SPEC("ERR_UNKNOWN_UNMAPPED",
                      "unmapped internal failure class", 0,
                      SHAULA_FAILURE_CLASS_UNKNOWN,
                      SHAULA_RECOVERY_ACTION_FAIL_FAST, 99),
};

static const ShaulaErrorSpan failure_class_tokens[] = {
    SHAULA_LITERAL_SPAN("cli"),       SHAULA_LITERAL_SPAN("compositor"),
    SHAULA_LITERAL_SPAN("ipc"),       SHAULA_LITERAL_SPAN("backend"),
    SHAULA_LITERAL_SPAN("clipboard"), SHAULA_LITERAL_SPAN("output"),
    SHAULA_LITERAL_SPAN("unknown"),
};

static const ShaulaErrorSpan recovery_action_tokens[] = {
    SHAULA_LITERAL_SPAN("fail_fast"),
    SHAULA_LITERAL_SPAN("retry_limited"),
    SHAULA_LITERAL_SPAN("degrade_continue"),
    SHAULA_LITERAL_SPAN("degrade_to_portal"),
};

_Static_assert(sizeof(error_specs) / sizeof(error_specs[0]) == 33,
               "canonical public error inventory changed");
_Static_assert(sizeof(failure_class_tokens) /
                       sizeof(failure_class_tokens[0]) ==
                   SHAULA_FAILURE_CLASS_UNKNOWN + 1,
               "failure-class token table changed");
_Static_assert(sizeof(recovery_action_tokens) /
                       sizeof(recovery_action_tokens[0]) ==
                   SHAULA_RECOVERY_ACTION_DEGRADE_TO_PORTAL + 1,
               "recovery-action token table changed");

static int span_is_valid(ShaulaErrorSpan span) {
  return span.data != NULL || span.length == 0U;
}

static int span_equals(ShaulaErrorSpan left, ShaulaErrorSpan right) {
  if (!span_is_valid(left) || !span_is_valid(right) ||
      left.length != right.length) {
    return 0;
  }
  return left.length == 0U || memcmp(left.data, right.data, left.length) == 0;
}

size_t shaula_error_taxonomy_count(void) {
  return sizeof(error_specs) / sizeof(error_specs[0]);
}

const ShaulaErrorSpec *shaula_error_taxonomy_at(size_t index) {
  if (index >= shaula_error_taxonomy_count()) {
    return NULL;
  }
  return &error_specs[index];
}

const ShaulaErrorSpec *shaula_error_taxonomy_find(ShaulaErrorSpan code) {
  size_t index;

  if (!span_is_valid(code)) {
    return NULL;
  }

  for (index = 0U; index < shaula_error_taxonomy_count(); ++index) {
    if (span_equals(error_specs[index].code, code)) {
      return &error_specs[index];
    }
  }
  return NULL;
}

const ShaulaErrorSpec *shaula_error_taxonomy_unknown(void) {
  return &error_specs[shaula_error_taxonomy_count() - 1U];
}

const ShaulaErrorSpec *shaula_error_taxonomy_spec_for(ShaulaErrorSpan code) {
  const ShaulaErrorSpec *spec = shaula_error_taxonomy_find(code);
  return spec != NULL ? spec : shaula_error_taxonomy_unknown();
}

ShaulaErrorSpan shaula_failure_class_token(ShaulaFailureClass failure_class) {
  static const ShaulaErrorSpan invalid = {NULL, 0U};

  if (failure_class < SHAULA_FAILURE_CLASS_CLI ||
      failure_class > SHAULA_FAILURE_CLASS_UNKNOWN) {
    return invalid;
  }
  return failure_class_tokens[(size_t)failure_class];
}

ShaulaErrorSpan shaula_recovery_action_token(ShaulaRecoveryAction action) {
  static const ShaulaErrorSpan invalid = {NULL, 0U};

  if (action < SHAULA_RECOVERY_ACTION_FAIL_FAST ||
      action > SHAULA_RECOVERY_ACTION_DEGRADE_TO_PORTAL) {
    return invalid;
  }
  return recovery_action_tokens[(size_t)action];
}

uint8_t shaula_error_exit_code_for(ShaulaErrorSpan code) {
  return shaula_error_taxonomy_spec_for(code)->exit_code;
}

uint8_t shaula_error_retry_budget_for(ShaulaErrorSpan code) {
  const ShaulaErrorSpec *spec = shaula_error_taxonomy_spec_for(code);

  if (spec->retryable == 0U) {
    return 0U;
  }

  switch (spec->action) {
  case SHAULA_RECOVERY_ACTION_RETRY_LIMITED:
    return 3U;
  case SHAULA_RECOVERY_ACTION_DEGRADE_TO_PORTAL:
    return 1U;
  case SHAULA_RECOVERY_ACTION_FAIL_FAST:
  case SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE:
  default:
    return 0U;
  }
}
