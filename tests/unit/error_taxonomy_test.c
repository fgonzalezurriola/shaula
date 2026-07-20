#include "errors/taxonomy.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

#define EXPECTED(code_value, message_value, retryable_value, class_value,       \
                 action_value, exit_value, budget_value)                       \
  {                                                                            \
    (code_value), (message_value), (retryable_value), (class_value),            \
        (action_value), (exit_value), (budget_value)                            \
  }

typedef struct {
  const char *code;
  const char *message;
  uint8_t retryable;
  ShaulaFailureClass failure_class;
  ShaulaRecoveryAction action;
  uint8_t exit_code;
  uint8_t retry_budget;
} ExpectedSpec;

static const ExpectedSpec expected_specs[] = {
    EXPECTED("ERR_CLI_USAGE", "invalid CLI usage", 0, SHAULA_FAILURE_CLASS_CLI,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 2, 0),
    EXPECTED("ERR_UNSUPPORTED_COMPOSITOR",
             "unsupported compositor for shaula v1", 0,
             SHAULA_FAILURE_CLASS_COMPOSITOR,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 10, 0),
    EXPECTED("ERR_PREFLIGHT_ENV_NOT_READY",
             "wayland environment is not ready", 1,
             SHAULA_FAILURE_CLASS_COMPOSITOR,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 11, 3),
    EXPECTED("ERR_IPC_TIMEOUT", "IPC operation timed out", 1,
             SHAULA_FAILURE_CLASS_IPC, SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 23,
             3),
    EXPECTED("ERR_CAPTURE_BACKEND_UNAVAILABLE", "capture backend unavailable",
             1, SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_DEGRADE_TO_PORTAL, 30, 1),
    EXPECTED("ERR_WINDOW_TARGET_UNRESOLVED",
             "window target could not be resolved", 0,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 31, 0),
    EXPECTED("ERR_CAPTURE_TIMEOUT", "capture operation timed out", 1,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 32, 3),
    EXPECTED("ERR_CAPTURE_IN_PROGRESS",
             "another capture is already in progress", 1,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 46, 3),
    EXPECTED("ERR_CAPTURE_PRECONDITION_TIMEOUT",
             "capture precondition timed out waiting for shell artifact guard",
             1, SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 35, 3),
    EXPECTED("ERR_SELECTION_CANCELLED", "selection was cancelled by user", 0,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 33, 0),
    EXPECTED("ERR_CAPTURE_MODE_UNSUPPORTED",
             "capture mode is unsupported by runtime capabilities", 0,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 34, 0),
    EXPECTED("ERR_PREVIOUS_AREA_UNAVAILABLE", "previous area is unavailable",
             0, SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 39, 0),
    EXPECTED("ERR_CLIPBOARD_UNAVAILABLE", "clipboard backend is unavailable",
             0, SHAULA_FAILURE_CLASS_CLIPBOARD,
             SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 40, 0),
    EXPECTED("ERR_OVERLAY_UNAVAILABLE", "overlay helper is unavailable", 1,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 36, 3),
    EXPECTED("ERR_OVERLAY_TIMEOUT", "overlay helper timed out", 1,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 37, 3),
    EXPECTED("ERR_OVERLAY_PROTOCOL_INVALID",
             "overlay helper produced invalid protocol payload", 0,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 38, 0),
    EXPECTED("ERR_CLIPBOARD_IMPORT_INVALID", "clipboard image import failed",
             0, SHAULA_FAILURE_CLASS_CLIPBOARD,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 41, 0),
    EXPECTED("ERR_CLIPBOARD_COPY_FAILED", "clipboard image copy failed", 0,
             SHAULA_FAILURE_CLASS_CLIPBOARD,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 42, 0),
    EXPECTED("ERR_PREVIEW_INPUT_INVALID", "preview input image is invalid", 0,
             SHAULA_FAILURE_CLASS_OUTPUT,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 43, 0),
    EXPECTED("ERR_PREVIEW_UNAVAILABLE", "preview helper is unavailable", 1,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 44, 3),
    EXPECTED("ERR_SETTINGS_UNAVAILABLE", "settings helper is unavailable", 1,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 45, 3),
    EXPECTED("ERR_HISTORY_STORE_UNAVAILABLE", "history store unavailable", 0,
             SHAULA_FAILURE_CLASS_OUTPUT,
             SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 50, 0),
    EXPECTED("ERR_HISTORY_ENTRY_NOT_FOUND", "history entry was not found", 0,
             SHAULA_FAILURE_CLASS_OUTPUT,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 52, 0),
    EXPECTED("ERR_OUTPUT_PATH_INVALID", "output path is not writable", 0,
             SHAULA_FAILURE_CLASS_OUTPUT,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 51, 0),
    EXPECTED("ERR_CONFIG_UNREADABLE", "configuration file is unreadable", 0,
             SHAULA_FAILURE_CLASS_OUTPUT,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 60, 0),
    EXPECTED("ERR_CONFIG_INVALID", "invalid configuration file", 0,
             SHAULA_FAILURE_CLASS_CLI,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 61, 0),
    EXPECTED("ERR_SHORTCUTS_UNSUPPORTED",
             "automatic global shortcuts are unsupported", 0,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 63, 0),
    EXPECTED("ERR_SHORTCUT_PERMISSION_DENIED",
             "desktop shortcut permission was denied", 0,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, 64, 0),
    EXPECTED("ERR_SHORTCUT_PROVIDER_UNAVAILABLE",
             "shortcut provider is unavailable", 1,
             SHAULA_FAILURE_CLASS_BACKEND,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 65, 3),
    EXPECTED("ERR_SHORTCUT_SESSION_LOST",
             "shortcut portal session was lost", 1,
             SHAULA_FAILURE_CLASS_IPC,
             SHAULA_RECOVERY_ACTION_RETRY_LIMITED, 66, 3),
    EXPECTED("ERR_SHORTCUT_CONFIGURATION_INVALID",
             "shortcut configuration is invalid", 0,
             SHAULA_FAILURE_CLASS_OUTPUT,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 67, 0),
    EXPECTED("ERR_UNKNOWN_UNMAPPED", "unmapped internal failure class", 0,
             SHAULA_FAILURE_CLASS_UNKNOWN,
             SHAULA_RECOVERY_ACTION_FAIL_FAST, 99, 0),
};

static ShaulaErrorSpan span_from_bytes(const char *data, size_t length) {
  ShaulaErrorSpan span = {data, length};
  return span;
}

static ShaulaErrorSpan span_from_text(const char *text) {
  return span_from_bytes(text, strlen(text));
}

static void assert_span_text(ShaulaErrorSpan actual, const char *expected) {
  g_assert_nonnull(actual.data);
  g_assert_cmpuint(actual.length, ==, strlen(expected));
  g_assert_cmpmem(actual.data, actual.length, expected, strlen(expected));
}

static const char *failure_class_text(ShaulaFailureClass value) {
  ShaulaErrorSpan token = shaula_failure_class_token(value);
  return token.data;
}

static const char *recovery_action_text(ShaulaRecoveryAction value) {
  ShaulaErrorSpan token = shaula_recovery_action_token(value);
  return token.data;
}

static void test_enum_abi(void) {
  g_assert_cmpint(sizeof(ShaulaFailureClass), ==, 4);
  g_assert_cmpint(sizeof(ShaulaRecoveryAction), ==, 4);
  g_assert_cmpint(SHAULA_FAILURE_CLASS_CLI, ==, 0);
  g_assert_cmpint(SHAULA_FAILURE_CLASS_COMPOSITOR, ==, 1);
  g_assert_cmpint(SHAULA_FAILURE_CLASS_IPC, ==, 2);
  g_assert_cmpint(SHAULA_FAILURE_CLASS_BACKEND, ==, 3);
  g_assert_cmpint(SHAULA_FAILURE_CLASS_CLIPBOARD, ==, 4);
  g_assert_cmpint(SHAULA_FAILURE_CLASS_OUTPUT, ==, 5);
  g_assert_cmpint(SHAULA_FAILURE_CLASS_UNKNOWN, ==, 6);
  g_assert_cmpint(SHAULA_RECOVERY_ACTION_FAIL_FAST, ==, 0);
  g_assert_cmpint(SHAULA_RECOVERY_ACTION_RETRY_LIMITED, ==, 1);
  g_assert_cmpint(SHAULA_RECOVERY_ACTION_DEGRADE_CONTINUE, ==, 2);
  g_assert_cmpint(SHAULA_RECOVERY_ACTION_DEGRADE_TO_PORTAL, ==, 3);
}

static void test_inventory_and_order(void) {
  size_t index;
  size_t other;

  g_assert_cmpuint(shaula_error_taxonomy_count(), ==, G_N_ELEMENTS(expected_specs));
  for (index = 0; index < G_N_ELEMENTS(expected_specs); ++index) {
    const ExpectedSpec *expected = &expected_specs[index];
    const ShaulaErrorSpec *actual = shaula_error_taxonomy_at(index);
    const ShaulaErrorSpec *found;

    g_assert_nonnull(actual);
    assert_span_text(actual->code, expected->code);
    assert_span_text(actual->message, expected->message);
    g_assert_cmpuint(actual->retryable, ==, expected->retryable);
    g_assert_cmpint(actual->failure_class, ==, expected->failure_class);
    g_assert_cmpint(actual->action, ==, expected->action);
    g_assert_cmpuint(actual->exit_code, ==, expected->exit_code);
    assert_span_text(shaula_failure_class_token(actual->failure_class),
                     failure_class_text(expected->failure_class));
    assert_span_text(shaula_recovery_action_token(actual->action),
                     recovery_action_text(expected->action));

    found = shaula_error_taxonomy_find(span_from_text(expected->code));
    g_assert_true(found == actual);
    g_assert_cmpuint(shaula_error_exit_code_for(span_from_text(expected->code)),
                     ==, expected->exit_code);
    g_assert_cmpuint(shaula_error_retry_budget_for(span_from_text(expected->code)),
                     ==, expected->retry_budget);

    for (other = index + 1; other < G_N_ELEMENTS(expected_specs); ++other) {
      g_assert_cmpstr(expected->code, !=, expected_specs[other].code);
    }
  }

  g_assert_null(shaula_error_taxonomy_at(G_N_ELEMENTS(expected_specs)));
  g_assert_null(shaula_error_taxonomy_at(SIZE_MAX));
  g_assert_true(shaula_error_taxonomy_unknown() ==
                shaula_error_taxonomy_at(G_N_ELEMENTS(expected_specs) - 1));
}

static void assert_rejected(ShaulaErrorSpan value) {
  g_assert_null(shaula_error_taxonomy_find(value));
  g_assert_true(shaula_error_taxonomy_spec_for(value) ==
                shaula_error_taxonomy_unknown());
  g_assert_cmpuint(shaula_error_exit_code_for(value), ==, 99);
  g_assert_cmpuint(shaula_error_retry_budget_for(value), ==, 0);
}

static void test_strict_lookup_and_fallback(void) {
  static const char embedded_nul[] = {'E', 'R', 'R', '_', 'C', 'L', 'I', '\0',
                                      '_', 'U', 'S', 'A', 'G', 'E'};
  static const char non_ascii[] = {'E', 'R', 'R', '_', (char)0xc3, (char)0x89};

  assert_rejected(span_from_bytes(NULL, 0));
  assert_rejected(span_from_bytes(NULL, 1));
  assert_rejected(span_from_text(""));
  assert_rejected(span_from_text("err_cli_usage"));
  assert_rejected(span_from_text("ERR_cli_usage"));
  assert_rejected(span_from_text(" ERR_CLI_USAGE"));
  assert_rejected(span_from_text("ERR_CLI_USAGE "));
  assert_rejected(span_from_text("ERR_CLI"));
  assert_rejected(span_from_text("ERR_CLI_USAGE_SUFFIX"));
  assert_rejected(span_from_text("XERR_CLI_USAGE"));
  assert_rejected(span_from_bytes(non_ascii, sizeof(non_ascii)));
  assert_rejected(span_from_bytes(embedded_nul, sizeof(embedded_nul)));
  assert_rejected(span_from_text("ERR_PREVIEW_RESULT_INVALID"));
  assert_rejected(span_from_text("ERR_CAPABILITIES_PROBE_FAILED"));
}

static void test_invalid_enum_tokens(void) {
  ShaulaErrorSpan token;

  token = shaula_failure_class_token(SHAULA_FAILURE_CLASS_INVALID);
  g_assert_null(token.data);
  g_assert_cmpuint(token.length, ==, 0);
  token = shaula_failure_class_token(INT32_MAX);
  g_assert_null(token.data);
  g_assert_cmpuint(token.length, ==, 0);
  token = shaula_recovery_action_token(SHAULA_RECOVERY_ACTION_INVALID);
  g_assert_null(token.data);
  g_assert_cmpuint(token.length, ==, 0);
  token = shaula_recovery_action_token(INT32_MAX);
  g_assert_null(token.data);
  g_assert_cmpuint(token.length, ==, 0);
}

static void test_borrowed_process_lifetime(void) {
  const ShaulaErrorSpec *first =
      shaula_error_taxonomy_find(span_from_text("ERR_CLI_USAGE"));
  const ShaulaErrorSpec *again;
  const char *code_pointer;
  const char *message_pointer;
  const char *class_pointer;
  const char *action_pointer;

  g_assert_nonnull(first);
  code_pointer = first->code.data;
  message_pointer = first->message.data;
  class_pointer = shaula_failure_class_token(first->failure_class).data;
  action_pointer = shaula_recovery_action_token(first->action).data;

  g_assert_nonnull(
      shaula_error_taxonomy_find(span_from_text("ERR_CONFIG_INVALID")));
  again = shaula_error_taxonomy_find(span_from_text("ERR_CLI_USAGE"));
  g_assert_true(first == again);
  g_assert_true(code_pointer == again->code.data);
  g_assert_true(message_pointer == again->message.data);
  g_assert_true(class_pointer ==
                shaula_failure_class_token(again->failure_class).data);
  g_assert_true(action_pointer == shaula_recovery_action_token(again->action).data);
}

static void test_fixture_consistency(void) {
  const char *fixture_path = g_getenv("SHAULA_ERRORS_FIXTURE");
  g_autofree char *contents = NULL;
  gsize contents_length = 0;
  g_autoptr(GError) error = NULL;
  const char *cursor;
  size_t index;
  size_t code_count = 0;

  g_assert_nonnull(fixture_path);
  g_assert_true(g_file_get_contents(fixture_path, &contents, &contents_length,
                                    &error));
  g_assert_no_error(error);
  cursor = contents;

  for (index = 0; index < G_N_ELEMENTS(expected_specs); ++index) {
    const ExpectedSpec *expected = &expected_specs[index];
    const ShaulaErrorSpan class_token =
        shaula_failure_class_token(expected->failure_class);
    const ShaulaErrorSpan action_token =
        shaula_recovery_action_token(expected->action);
    g_autofree char *row = g_strdup_printf(
        "{\"code\":\"%s\",\"message\":\"%s\",\"retryable\":%s,"
        "\"class\":\"%.*s\",\"action\":\"%.*s\",\"exit_code\":%u}",
        expected->code, expected->message,
        expected->retryable != 0 ? "true" : "false", (int)class_token.length,
        class_token.data, (int)action_token.length, action_token.data,
        (unsigned int)expected->exit_code);
    const char *match = strstr(cursor, row);

    g_assert_nonnull(match);
    cursor = match + strlen(row);
  }

  cursor = contents;
  while ((cursor = strstr(cursor, "{\"code\":\"ERR_")) != NULL) {
    ++code_count;
    ++cursor;
  }
  g_assert_cmpuint(code_count, ==, G_N_ELEMENTS(expected_specs));
  g_assert_null(strstr(contents, "ERR_PREVIEW_RESULT_INVALID"));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/errors/taxonomy/enum-abi", test_enum_abi);
  g_test_add_func("/errors/taxonomy/inventory-order", test_inventory_and_order);
  g_test_add_func("/errors/taxonomy/strict-lookup-fallback",
                  test_strict_lookup_and_fallback);
  g_test_add_func("/errors/taxonomy/invalid-enum-tokens",
                  test_invalid_enum_tokens);
  g_test_add_func("/errors/taxonomy/borrowed-process-lifetime",
                  test_borrowed_process_lifetime);
  g_test_add_func("/errors/taxonomy/fixture-consistency",
                  test_fixture_consistency);
  return g_test_run();
}
