#ifndef SHAULA_RUNTIME_PATHS_H
#define SHAULA_RUNTIME_PATHS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Status values are part of the temporary Zig/C ABI. Keep their exact numeric
 * values stable for all direct C ABI callers.
 */
typedef int32_t ShaulaRuntimePathStatus;
enum {
  SHAULA_RUNTIME_PATH_STATUS_OK = 0,
  SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT = 1,
  SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY = 2,
  SHAULA_RUNTIME_PATH_STATUS_FILESYSTEM_ERROR = 3,
};

typedef struct {
  const char *data;
  size_t length;
} ShaulaRuntimePathSpan;

typedef struct {
  char *data;
  size_t length;
} ShaulaRuntimeOwnedPath;

_Static_assert(sizeof(ShaulaRuntimePathStatus) == 4,
               "ShaulaRuntimePathStatus must remain a 32-bit C ABI value");
_Static_assert(SHAULA_RUNTIME_PATH_STATUS_OK == 0,
               "runtime path success ABI value changed");
_Static_assert(SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT == 1,
               "runtime path invalid-argument ABI value changed");
_Static_assert(SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY == 2,
               "runtime path out-of-memory ABI value changed");
_Static_assert(SHAULA_RUNTIME_PATH_STATUS_FILESYSTEM_ERROR == 3,
               "runtime path filesystem-error ABI value changed");

/*
 * All functions have no mutable global state and are safe to call concurrently.
 * Input environment pointers are borrowed NUL-terminated strings and may be
 * NULL to represent missing values. Span inputs are borrowed and may contain
 * arbitrary bytes; a NULL span pointer is valid only when length is zero.
 *
 * Owned paths use GLib allocation and must be released with
 * shaula_runtime_owned_path_clear(), which is idempotent. The byte length is
 * authoritative: returned storage has a trailing NUL for C interoperability,
 * but a relative-path input may contain embedded NUL bytes. Filesystem
 * operations reject embedded NUL bytes because POSIX paths cannot represent
 * them.
 */

/* Releases one GLib-owned path and leaves it empty. A NULL object is ignored.
 */
void shaula_runtime_owned_path_clear(ShaulaRuntimeOwnedPath *path);

/*
 * Resolves one runtime-state path. A nonempty ASCII-trimmed override wins.
 * Otherwise a nonempty ASCII-trimmed runtime directory produces
 * <runtime>/shaula/<relative>; missing/empty runtime directories fall back to
 * /tmp/shaula/<relative>. No normalization, absolute-path validation, or
 * filesystem access occurs. out_path is required and is initialized empty on
 * every call; callers must clear any previous owned value before reuse.
 */
ShaulaRuntimePathStatus shaula_runtime_path_resolve(
    const char *override_value, const char *runtime_dir_value,
    ShaulaRuntimePathSpan relative_path, ShaulaRuntimeOwnedPath *out_path);

/*
 * Creates the parent directory tree for path using mode 0755 subject to umask.
 * Paths without a parent component and root paths are successful no-ops.
 * Repeated separators, '.', and '..' are passed through without
 * canonicalization, matching the existing Linux Zig behavior.
 */
ShaulaRuntimePathStatus
shaula_runtime_path_ensure_parent(ShaulaRuntimePathSpan path);

/*
 * Returns 1 for /tmp/shaula/captures/... or any byte span containing
 * /shaula/captures/; otherwise returns 0. Matching is bytewise and does not
 * inspect the filesystem or environment.
 */
int32_t shaula_runtime_path_is_capture_artifact(ShaulaRuntimePathSpan path);

#ifdef __cplusplus
}
#endif

#endif
