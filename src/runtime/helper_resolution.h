#ifndef SHAULA_RUNTIME_HELPER_RESOLUTION_H
#define SHAULA_RUNTIME_HELPER_RESOLUTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Status values are part of the temporary Zig/C ABI. Keep their exact numeric
 * values stable for all direct C ABI callers.
 */
typedef int32_t ShaulaRuntimeHelperStatus;
enum {
  SHAULA_RUNTIME_HELPER_STATUS_OK = 0,
  SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT = 1,
  SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY = 2,
};

typedef struct {
  const char *data;
  size_t length;
} ShaulaRuntimeHelperSpan;

typedef struct {
  char *data;
  size_t length;
} ShaulaRuntimeHelperOwnedPath;

_Static_assert(sizeof(ShaulaRuntimeHelperStatus) == 4,
               "ShaulaRuntimeHelperStatus must remain a 32-bit C ABI value");
_Static_assert(SHAULA_RUNTIME_HELPER_STATUS_OK == 0,
               "helper resolution success ABI value changed");
_Static_assert(SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT == 1,
               "helper resolution invalid-argument ABI value changed");
_Static_assert(SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY == 2,
               "helper resolution out-of-memory ABI value changed");

/*
 * The module has no mutable global state and is safe to call concurrently.
 * Inputs are borrowed for the synchronous call. override_value is a nullable
 * NUL-terminated environment value. Span inputs may contain arbitrary bytes; a
 * NULL pointer is valid only when length is zero. A NULL/empty executable_dir
 * means executable-directory discovery was unavailable.
 *
 * Successful results are independent GLib-owned, length-bearing buffers with a
 * trailing NUL. Release them with shaula_runtime_helper_owned_path_clear(),
 * which is safe on empty values and repeated calls.
 *
 * Resolution preserves the existing order and existence-only semantics:
 * 1. nonempty ASCII-trimmed override, without filesystem validation;
 * 2. byte-exact <executable_dir>/<binary_name> when that path exists;
 * 3. an owned copy of binary_name. The third result is intentionally a bare
 *    name: later process spawning, not this module, performs PATH lookup.
 *
 * Sibling checks do not require executable permission. No normalization,
 * canonicalization, shell interpretation, locale-sensitive classification, or
 * process spawning occurs. Embedded NUL bytes cannot match a sibling POSIX path
 * but remain preserved in the bare-name fallback.
 */

void shaula_runtime_helper_owned_path_clear(ShaulaRuntimeHelperOwnedPath *path);

/*
 * out_path is required and is initialized empty on every call. Callers must
 * clear any previous owned value before reuse.
 */
ShaulaRuntimeHelperStatus
shaula_runtime_helper_resolve(const char *override_value,
                              ShaulaRuntimeHelperSpan executable_dir,
                              ShaulaRuntimeHelperSpan binary_name,
                              ShaulaRuntimeHelperOwnedPath *out_path);

#ifdef __cplusplus
}
#endif

#endif
