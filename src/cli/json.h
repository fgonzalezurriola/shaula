#ifndef SHAULA_CLI_JSON_H
#define SHAULA_CLI_JSON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *data;
    size_t length;
} ShaulaJsonSpan;

typedef struct {
    uint8_t *data;
    size_t length;
} ShaulaJsonOwnedBytes;

typedef int32_t ShaulaJsonStatus;

enum {
    SHAULA_JSON_STATUS_OK = 0,
    SHAULA_JSON_STATUS_INVALID_ARGUMENT = 1,
    SHAULA_JSON_STATUS_SIZE_OVERFLOW = 2,
    SHAULA_JSON_STATUS_OUT_OF_MEMORY = 3,
    SHAULA_JSON_STATUS_TIMESTAMP_OUT_OF_RANGE = 4,
};

_Static_assert(sizeof(ShaulaJsonStatus) == 4, "JSON status ABI must be 32-bit");

/* Borrowed immutable process-lifetime storage. */
ShaulaJsonSpan shaula_json_contract_version(void);

/* Zero-initialized storage is also valid. */
void shaula_json_owned_bytes_init(ShaulaJsonOwnedBytes *output);
void shaula_json_owned_bytes_clear(ShaulaJsonOwnedBytes *output);

/*
 * Formats a caller-supplied Unix timestamp as YYYY-MM-DDTHH:MM:SSZ.
 * The returned bytes are GLib-owned, length-bearing, and trailing-NUL storage.
 */
ShaulaJsonStatus shaula_json_timestamp_from_unix_seconds(
    int64_t epoch_seconds,
    ShaulaJsonOwnedBytes *output);

/*
 * Serializes one borrowed byte span as a JSON string. The input is not required
 * to be UTF-8. Bytes >= 0x20 are preserved except quote and backslash; control
 * bytes use the same escapes as Zig std.json.Stringify with default options.
 */
ShaulaJsonStatus shaula_json_string_escape(
    ShaulaJsonSpan value,
    ShaulaJsonOwnedBytes *output);

/* has_value must be 0 or 1. Absent values emit null; present empty spans emit "". */
ShaulaJsonStatus shaula_json_nullable_string_escape(
    uint8_t has_value,
    ShaulaJsonSpan value,
    ShaulaJsonOwnedBytes *output);

/* Preserves warning order and applies the shared byte-string escaping policy. */
ShaulaJsonStatus shaula_json_warnings_serialize(
    const ShaulaJsonSpan *warnings,
    size_t warning_count,
    ShaulaJsonOwnedBytes *output);

/*
 * Builds the complete shared basic-error envelope, including exactly one final
 * newline. details_json is inserted verbatim to preserve the historical raw
 * fragment contract; it is not parsed or validated by this function.
 */
ShaulaJsonStatus shaula_json_basic_error_build(
    int64_t epoch_seconds,
    ShaulaJsonSpan command,
    ShaulaJsonSpan code,
    ShaulaJsonSpan message,
    uint8_t retryable,
    ShaulaJsonSpan details_json,
    ShaulaJsonOwnedBytes *output);

#ifdef __cplusplus
}
#endif

#endif
