#include "cli/json.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>

static ShaulaJsonSpan span_from_bytes(const void *data, size_t length) {
    ShaulaJsonSpan span = {
        .data = data,
        .length = length,
    };
    return span;
}

static ShaulaJsonSpan span_from_literal(const char *value) {
    return span_from_bytes(value, strlen(value));
}

static void assert_owned_equals(
    const ShaulaJsonOwnedBytes *actual,
    const void *expected,
    size_t expected_length) {
    g_assert_nonnull(actual->data);
    g_assert_cmpuint(actual->length, ==, expected_length);
    g_assert_cmpmem(actual->data, actual->length, expected, expected_length);
    g_assert_cmpuint(actual->data[actual->length], ==, 0U);
}

static void test_contract_version_lifetime(void) {
    const ShaulaJsonSpan first = shaula_json_contract_version();
    const ShaulaJsonSpan second = shaula_json_contract_version();

    g_assert_true(first.data == second.data);
    g_assert_cmpuint(first.length, ==, 5U);
    g_assert_cmpmem(first.data, first.length, "1.0.0", 5U);
}

static void test_empty_and_plain_strings(void) {
    ShaulaJsonOwnedBytes output = {0};

    g_assert_cmpint(
        shaula_json_string_escape(span_from_bytes(NULL, 0U), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, "\"\"", 2U);

    g_assert_cmpint(
        shaula_json_string_escape(span_from_literal("plain ASCII / slash"), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, "\"plain ASCII / slash\"", 21U);

    shaula_json_owned_bytes_clear(&output);
}

static void test_nullable_string_distinguishes_null_and_empty(void) {
    ShaulaJsonOwnedBytes output = {0};
    const ShaulaJsonSpan empty = {.data = NULL, .length = 0U};
    const ShaulaJsonSpan invalid = {.data = NULL, .length = 1U};

    g_assert_cmpint(
        shaula_json_nullable_string_escape(0U, empty, &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, "null", 4U);

    g_assert_cmpint(
        shaula_json_nullable_string_escape(1U, empty, &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, "\"\"", 2U);

    g_assert_cmpint(
        shaula_json_nullable_string_escape(2U, empty, &output),
        ==,
        SHAULA_JSON_STATUS_INVALID_ARGUMENT);
    g_assert_cmpuint(output.length, ==, 2U);

    g_assert_cmpint(
        shaula_json_nullable_string_escape(0U, invalid, &output),
        ==,
        SHAULA_JSON_STATUS_INVALID_ARGUMENT);
    g_assert_null(output.data);
    g_assert_cmpuint(output.length, ==, 0U);
    shaula_json_owned_bytes_clear(&output);
}

static void test_quotes_backslashes_and_named_controls(void) {
    static const uint8_t input[] = {'"', '\\', '\t', '\r', '\n', 0x08, 0x0c};
    static const char expected[] = "\"\\\"\\\\\\t\\r\\n\\b\\f\"";
    ShaulaJsonOwnedBytes output = {0};

    g_assert_cmpint(
        shaula_json_string_escape(span_from_bytes(input, sizeof(input)), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, expected, sizeof(expected) - 1U);
    shaula_json_owned_bytes_clear(&output);
}

static void test_every_control_byte(void) {
    uint8_t input[32] = {0};
    g_autoptr(GString) expected = g_string_new("\"");
    ShaulaJsonOwnedBytes output = {0};
    uint8_t value = 0U;

    for (value = 0U; value < 32U; value += 1U) {
        input[value] = value;
        switch (value) {
            case 0x08:
                g_string_append(expected, "\\b");
                break;
            case 0x09:
                g_string_append(expected, "\\t");
                break;
            case 0x0a:
                g_string_append(expected, "\\n");
                break;
            case 0x0c:
                g_string_append(expected, "\\f");
                break;
            case 0x0d:
                g_string_append(expected, "\\r");
                break;
            default:
                g_string_append_printf(expected, "\\u%04x", value);
                break;
        }
    }
    g_string_append_c(expected, '"');

    g_assert_cmpint(
        shaula_json_string_escape(span_from_bytes(input, sizeof(input)), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, expected->str, expected->len);
    shaula_json_owned_bytes_clear(&output);
}

static void test_utf8_and_invalid_bytes_are_preserved(void) {
    static const uint8_t valid_utf8[] = {0xe2, 0x98, 0x83, 0xf0, 0x9f, 0x98, 0x80};
    static const uint8_t invalid_utf8[] = {0x80, 0xc0, 0xaf, 0xff};
    static const uint8_t expected_valid[] = {'"', 0xe2, 0x98, 0x83, 0xf0, 0x9f, 0x98, 0x80, '"'};
    static const uint8_t expected_invalid[] = {'"', 0x80, 0xc0, 0xaf, 0xff, '"'};
    ShaulaJsonOwnedBytes output = {0};

    g_assert_cmpint(
        shaula_json_string_escape(span_from_bytes(valid_utf8, sizeof(valid_utf8)), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, expected_valid, sizeof(expected_valid));

    g_assert_cmpint(
        shaula_json_string_escape(span_from_bytes(invalid_utf8, sizeof(invalid_utf8)), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, expected_invalid, sizeof(expected_invalid));
    shaula_json_owned_bytes_clear(&output);
}

static void test_embedded_nul(void) {
    static const uint8_t input[] = {'a', 0x00, 'b'};
    static const char expected[] = "\"a\\u0000b\"";
    ShaulaJsonOwnedBytes output = {0};

    g_assert_cmpint(
        shaula_json_string_escape(span_from_bytes(input, sizeof(input)), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, expected, sizeof(expected) - 1U);
    shaula_json_owned_bytes_clear(&output);
}

static void test_long_string_and_exact_length(void) {
    const size_t input_length = 1024U * 1024U;
    g_autofree uint8_t *input = g_malloc(input_length);
    ShaulaJsonOwnedBytes output = {0};

    memset(input, 'a', input_length);
    g_assert_cmpint(
        shaula_json_string_escape(span_from_bytes(input, input_length), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    g_assert_cmpuint(output.length, ==, input_length + 2U);
    g_assert_cmpuint(output.data[0], ==, '"');
    g_assert_cmpuint(output.data[output.length - 1U], ==, '"');
    g_assert_cmpmem(output.data + 1U, input_length, input, input_length);
    shaula_json_owned_bytes_clear(&output);
}

static void test_invalid_spans_and_overflow(void) {
    ShaulaJsonOwnedBytes output = {0};
    const ShaulaJsonSpan null_nonempty = {.data = NULL, .length = 1U};
    const ShaulaJsonSpan overflowing = {
        .data = (const uint8_t *)(uintptr_t)1U,
        .length = SIZE_MAX,
    };

    g_assert_cmpint(
        shaula_json_string_escape(null_nonempty, &output),
        ==,
        SHAULA_JSON_STATUS_INVALID_ARGUMENT);
    g_assert_null(output.data);
    g_assert_cmpuint(output.length, ==, 0U);

    g_assert_cmpint(
        shaula_json_string_escape(overflowing, &output),
        ==,
        SHAULA_JSON_STATUS_SIZE_OVERFLOW);
    g_assert_null(output.data);
    g_assert_cmpuint(output.length, ==, 0U);

    g_assert_cmpint(
        shaula_json_string_escape(span_from_literal("valid"), NULL),
        ==,
        SHAULA_JSON_STATUS_INVALID_ARGUMENT);
}

static void test_repeated_init_and_cleanup(void) {
    ShaulaJsonOwnedBytes output;

    shaula_json_owned_bytes_init(&output);
    shaula_json_owned_bytes_init(&output);
    g_assert_cmpint(
        shaula_json_string_escape(span_from_literal("one"), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    g_assert_cmpint(
        shaula_json_string_escape(span_from_literal("two"), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, "\"two\"", 5U);
    shaula_json_owned_bytes_clear(&output);
    shaula_json_owned_bytes_clear(&output);
    g_assert_null(output.data);
    g_assert_cmpuint(output.length, ==, 0U);
}

static void test_warning_arrays(void) {
    static const uint8_t warning_with_nul[] = {'n', 0x00, 'l'};
    const ShaulaJsonSpan warnings[] = {
        {.data = (const uint8_t *)"first", .length = 5U},
        {.data = (const uint8_t *)"q\"\\\n", .length = 4U},
        {.data = warning_with_nul, .length = sizeof(warning_with_nul)},
        {.data = NULL, .length = 0U},
    };
    static const char expected[] = "[\"first\",\"q\\\"\\\\\\n\",\"n\\u0000l\",\"\"]";
    ShaulaJsonOwnedBytes output = {0};

    g_assert_cmpint(
        shaula_json_warnings_serialize(NULL, 0U, &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, "[]", 2U);

    g_assert_cmpint(
        shaula_json_warnings_serialize(warnings, G_N_ELEMENTS(warnings), &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, expected, sizeof(expected) - 1U);

    g_assert_cmpint(
        shaula_json_warnings_serialize(NULL, 1U, &output),
        ==,
        SHAULA_JSON_STATUS_INVALID_ARGUMENT);
    g_assert_null(output.data);
    shaula_json_owned_bytes_clear(&output);
}

static void test_timestamp_format(void) {
    ShaulaJsonOwnedBytes output = {0};

    g_assert_cmpint(
        shaula_json_timestamp_from_unix_seconds(0, &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, "1970-01-01T00:00:00Z", 20U);

    g_assert_cmpint(
        shaula_json_timestamp_from_unix_seconds(-1, &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, "1969-12-31T23:59:59Z", 20U);

    g_assert_cmpint(
        shaula_json_timestamp_from_unix_seconds(INT64_MAX, &output),
        ==,
        SHAULA_JSON_STATUS_TIMESTAMP_OUT_OF_RANGE);
    g_assert_null(output.data);
    shaula_json_owned_bytes_clear(&output);
}

static void test_basic_error_envelope(void) {
    static const uint8_t message[] = {'b', 'a', 'd', '\n', '"', '\\', 0x00};
    static const char expected[] =
        "{\"ok\":false,\"contract_version\":\"1.0.0\",\"command\":\"capture /\","
        "\"timestamp\":\"1970-01-01T00:00:00Z\",\"error\":{\"code\":\"ERR_TEST\","
        "\"message\":\"bad\\n\\\"\\\\\\u0000\",\"retryable\":true,"
        "\"details\":{\"mode\":\"x\"}},\"warnings\":[]}\n";
    ShaulaJsonOwnedBytes output = {0};
    size_t newline_count = 0U;
    size_t index = 0U;

    g_assert_cmpint(
        shaula_json_basic_error_build(
            0,
            span_from_literal("capture /"),
            span_from_literal("ERR_TEST"),
            span_from_bytes(message, sizeof(message)),
            TRUE,
            span_from_literal("{\"mode\":\"x\"}"),
            &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, expected, sizeof(expected) - 1U);

    for (index = 0U; index < output.length; index += 1U) {
        if (output.data[index] == '\n') {
            newline_count += 1U;
        }
    }
    g_assert_cmpuint(newline_count, ==, 1U);
    g_assert_cmpuint(output.data[output.length - 2U], ==, '}');
    g_assert_cmpuint(output.data[output.length - 1U], ==, '\n');
    shaula_json_owned_bytes_clear(&output);
}

static void test_basic_error_raw_details_are_not_validated(void) {
    static const char expected[] =
        "{\"ok\":false,\"contract_version\":\"1.0.0\",\"command\":\"cmd\","
        "\"timestamp\":\"1970-01-01T00:00:00Z\",\"error\":{\"code\":\"ERR\","
        "\"message\":\"message\",\"retryable\":false,\"details\":{},},"
        "\"warnings\":[]}\n";
    ShaulaJsonOwnedBytes output = {0};

    g_assert_cmpint(
        shaula_json_basic_error_build(
            0,
            span_from_literal("cmd"),
            span_from_literal("ERR"),
            span_from_literal("message"),
            FALSE,
            span_from_literal("{},"),
            &output),
        ==,
        SHAULA_JSON_STATUS_OK);
    assert_owned_equals(&output, expected, sizeof(expected) - 1U);
    shaula_json_owned_bytes_clear(&output);
}

static void test_basic_error_invalid_span(void) {
    ShaulaJsonOwnedBytes output = {0};
    const ShaulaJsonSpan invalid = {.data = NULL, .length = 1U};

    g_assert_cmpint(
        shaula_json_basic_error_build(
            0,
            invalid,
            span_from_literal("ERR"),
            span_from_literal("message"),
            FALSE,
            span_from_literal("{}"),
            &output),
        ==,
        SHAULA_JSON_STATUS_INVALID_ARGUMENT);
    g_assert_null(output.data);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/cli-json/contract-version-lifetime", test_contract_version_lifetime);
    g_test_add_func("/cli-json/strings/empty-plain", test_empty_and_plain_strings);
    g_test_add_func("/cli-json/strings/nullable", test_nullable_string_distinguishes_null_and_empty);
    g_test_add_func("/cli-json/strings/quotes-controls", test_quotes_backslashes_and_named_controls);
    g_test_add_func("/cli-json/strings/every-control", test_every_control_byte);
    g_test_add_func("/cli-json/strings/utf8-invalid", test_utf8_and_invalid_bytes_are_preserved);
    g_test_add_func("/cli-json/strings/embedded-nul", test_embedded_nul);
    g_test_add_func("/cli-json/strings/long", test_long_string_and_exact_length);
    g_test_add_func("/cli-json/strings/invalid-overflow", test_invalid_spans_and_overflow);
    g_test_add_func("/cli-json/owned/repeated-cleanup", test_repeated_init_and_cleanup);
    g_test_add_func("/cli-json/warnings", test_warning_arrays);
    g_test_add_func("/cli-json/timestamp", test_timestamp_format);
    g_test_add_func("/cli-json/error/exact-envelope", test_basic_error_envelope);
    g_test_add_func("/cli-json/error/raw-details", test_basic_error_raw_details_are_not_validated);
    g_test_add_func("/cli-json/error/invalid-span", test_basic_error_invalid_span);
    return g_test_run();
}
