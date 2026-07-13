#ifndef SHAULA_RUNTIME_TOOL_LOOKUP_H
#define SHAULA_RUNTIME_TOOL_LOOKUP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status values are stable within the runtime implementation interface. */
typedef int32_t ShaulaRuntimeToolLookupStatus;
enum {
  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK = 0,
  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND = 1,
  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT = 2,
  SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OUT_OF_MEMORY = 3,
};

typedef struct {
  const char *data;
  size_t length;
} ShaulaRuntimeToolSpan;

typedef struct {
  char *data;
  size_t length;
} ShaulaRuntimeToolOwnedPath;

_Static_assert(
    sizeof(ShaulaRuntimeToolLookupStatus) == 4,
    "ShaulaRuntimeToolLookupStatus must remain a 32-bit C ABI value");
_Static_assert(SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK == 0,
               "tool lookup success ABI value changed");
_Static_assert(SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND == 1,
               "tool lookup not-found ABI value changed");
_Static_assert(SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT == 2,
               "tool lookup invalid-argument ABI value changed");
_Static_assert(SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OUT_OF_MEMORY == 3,
               "tool lookup out-of-memory ABI value changed");

/*
 * All functions have no mutable global state and are safe to call concurrently.
 * Span inputs and candidate arrays are borrowed for the synchronous call and
 * may contain arbitrary bytes; a NULL span pointer is valid only when length is
 * zero. Filesystem checks are bytewise, use no shell, perform no normalization,
 * and treat embedded NUL bytes as inaccessible because POSIX paths cannot
 * represent them.
 *
 * Existing Shaula behavior checks only path existence. It does not require the
 * executable permission bit, so directories and non-executable regular files
 * count as present when the operating system reports them accessible.
 *
 * Owned paths use GLib allocation and must be released with
 * shaula_runtime_tool_owned_path_clear(), which is idempotent. The byte length
 * is authoritative and successful storage also has a trailing NUL.
 */

/* Releases one GLib-owned path and leaves it empty. A NULL object is ignored.
 */
void shaula_runtime_tool_owned_path_clear(ShaulaRuntimeToolOwnedPath *path);

/*
 * Returns 1 when path exists relative to the current working directory (or as
 * an absolute path), otherwise 0. Empty, invalid, embedded-NUL, inaccessible,
 * and allocation-failure cases all return 0, matching the former non-fallible
 * Zig helper.
 */
int32_t shaula_runtime_tool_path_exists(ShaulaRuntimeToolSpan path);

/*
 * Returns the first existing absolute candidate in input order. Relative and
 * empty candidates are skipped. On success out_path borrows the matching
 * candidate storage and remains valid only as long as that input storage does.
 * The output is empty on every non-success result.
 */
ShaulaRuntimeToolLookupStatus
shaula_runtime_tool_find_absolute(const ShaulaRuntimeToolSpan *candidates,
                                  size_t candidate_count,
                                  ShaulaRuntimeToolSpan *out_path);

/*
 * Returns the first existing fixed grim candidate in this exact order:
 * /usr/bin/grim, /bin/grim, /usr/local/bin/grim. The successful output borrows
 * immutable process-lifetime storage and requires no cleanup.
 */
ShaulaRuntimeToolLookupStatus
shaula_runtime_tool_grim_path(ShaulaRuntimeToolSpan *out_path);

/*
 * Splits path_value on ':' and skips every empty component, including leading,
 * repeated, and trailing components. Each nonempty component is joined exactly
 * as <component>/<tool>, without trimming or normalization. Missing and empty
 * PATH values return NOT_FOUND. out_path is required and is initialized empty
 * on every call; callers must clear any previous owned value before reuse.
 */
ShaulaRuntimeToolLookupStatus
shaula_runtime_tool_find_in_path(const char *path_value,
                                 ShaulaRuntimeToolSpan tool,
                                 ShaulaRuntimeToolOwnedPath *out_path);

#ifdef __cplusplus
}
#endif

#endif
