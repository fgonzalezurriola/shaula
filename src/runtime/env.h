#ifndef SHAULA_RUNTIME_ENV_H
#define SHAULA_RUNTIME_ENV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Status values are part of the temporary Zig/C ABI. Keep their exact numeric
 * values stable for all direct C ABI callers.
 */
typedef int32_t ShaulaEnvStatus;
enum {
  SHAULA_ENV_STATUS_MISSING = 0,
  SHAULA_ENV_STATUS_VALID = 1,
  SHAULA_ENV_STATUS_INVALID = 2,
};

/* Borrowed byte span. data is never owned by this module. */
typedef struct {
  const char *data;
  size_t length;
} ShaulaEnvSpan;

_Static_assert(sizeof(ShaulaEnvStatus) == 4,
               "ShaulaEnvStatus must remain a 32-bit C ABI value");
_Static_assert(SHAULA_ENV_STATUS_MISSING == 0,
               "missing status ABI value changed");
_Static_assert(SHAULA_ENV_STATUS_VALID == 1, "valid status ABI value changed");
_Static_assert(SHAULA_ENV_STATUS_INVALID == 2,
               "invalid status ABI value changed");
_Static_assert(sizeof(uint64_t) == 8, "uint64_t must be exactly 64 bits");

/*
 * All functions are allocation-free and have no mutable global state. They are
 * safe to call concurrently when each caller keeps its input storage stable.
 * A value pointer may be NULL to represent a missing environment variable.
 * Output pointers are required; passing NULL returns INVALID without access.
 *
 * Returned spans borrow either the NUL-terminated input value or the supplied
 * input span. The caller must not free them. Mutating or releasing the backing
 * environment invalidates them; another call to this module does not.
 */

/*
 * Converts a borrowed NUL-terminated environment value into a borrowed span.
 * NULL returns MISSING. Empty strings are VALID with length zero.
 */
ShaulaEnvStatus shaula_env_value_slice(const char *value, ShaulaEnvSpan *out);

/*
 * Trims only ASCII space, tab, carriage return, and newline. NULL, empty, and
 * whitespace-only values return MISSING. Non-ASCII bytes are preserved.
 */
ShaulaEnvStatus shaula_env_value_trimmed(const char *value, ShaulaEnvSpan *out);

/*
 * Parses a trimmed boolean value. Accepted true forms are 1, true, and yes;
 * accepted false forms are 0, false, and no. Letter forms are ASCII
 * case-insensitive. Missing/empty values return MISSING, malformed values
 * return INVALID, and both set *out_value to zero.
 */
ShaulaEnvStatus shaula_env_value_flag(const char *value, int32_t *out_value);

/*
 * Parses a trimmed base-10 unsigned integer up to max_value. An optional
 * leading plus is accepted. A leading minus is accepted only when the parsed
 * magnitude is zero, matching Zig parseInt for unsigned types. Internal
 * underscores are ignored, but leading/trailing underscores are invalid.
 * Missing, empty, malformed, negative, or overflowing values return
 * default_value.
 */
uint64_t shaula_env_value_unsigned_or_default(const char *value,
                                              uint64_t max_value,
                                              uint64_t default_value);

/*
 * Returns the first non-empty token after splitting a borrowed byte span on
 * both ':' and ';' and trimming ASCII whitespace from each token. Token bytes
 * and case are preserved; no substring or known-desktop matching is performed.
 * A NULL data pointer returns MISSING. The input need not be NUL-terminated.
 */
ShaulaEnvStatus shaula_env_first_desktop_token(ShaulaEnvSpan value,
                                               ShaulaEnvSpan *out);

#ifdef __cplusplus
}
#endif

#endif
