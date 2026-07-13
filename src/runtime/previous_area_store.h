#ifndef SHAULA_RUNTIME_PREVIOUS_AREA_STORE_H
#define SHAULA_RUNTIME_PREVIOUS_AREA_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaPreviousAreaStatus;
enum {
  SHAULA_PREVIOUS_AREA_STATUS_OK = 0,
  SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT = 1,
  SHAULA_PREVIOUS_AREA_STATUS_OUT_OF_MEMORY = 2,
  SHAULA_PREVIOUS_AREA_STATUS_FILESYSTEM_ERROR = 3,
};

typedef struct {
  const char *data;
  size_t length;
} ShaulaPreviousAreaSpan;

typedef struct {
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
} ShaulaPreviousAreaGeometry;

_Static_assert(sizeof(ShaulaPreviousAreaStatus) == 4,
               "ShaulaPreviousAreaStatus must remain a 32-bit C ABI value");
_Static_assert(SHAULA_PREVIOUS_AREA_STATUS_OK == 0,
               "previous-area success ABI value changed");
_Static_assert(SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT == 1,
               "previous-area invalid-argument ABI value changed");
_Static_assert(SHAULA_PREVIOUS_AREA_STATUS_OUT_OF_MEMORY == 2,
               "previous-area out-of-memory ABI value changed");
_Static_assert(SHAULA_PREVIOUS_AREA_STATUS_FILESYSTEM_ERROR == 3,
               "previous-area filesystem-error ABI value changed");
_Static_assert(sizeof(ShaulaPreviousAreaGeometry) == 16,
               "previous-area geometry ABI size changed");
_Static_assert(offsetof(ShaulaPreviousAreaGeometry, x) == 0,
               "previous-area x ABI offset changed");
_Static_assert(offsetof(ShaulaPreviousAreaGeometry, y) == 4,
               "previous-area y ABI offset changed");
_Static_assert(offsetof(ShaulaPreviousAreaGeometry, width) == 8,
               "previous-area width ABI offset changed");
_Static_assert(offsetof(ShaulaPreviousAreaGeometry, height) == 12,
               "previous-area height ABI offset changed");

/*
 * The module has no mutable global state and is safe to call concurrently for
 * distinct files. Path spans are borrowed for each synchronous call; a NULL
 * pointer is valid only when length is zero. Paths are bytewise and may be
 * relative. Embedded NUL cannot cross the POSIX filesystem boundary.
 *
 * The persisted format is exactly one decimal line:
 *   x|y|width|height\n
 * Stores create parent directories through the runtime-path contract, open the
 * target with create/truncate semantics and mode 0666 subject to umask, write
 * the complete line, and do not fsync. Width and height are stored verbatim,
 * including zero, because validation occurs when loading.
 *
 * Loads fail closed: missing, unreadable, empty, malformed,
 * numeric-overflowing, allocation-failed, or embedded-NUL path/content cases
 * report OK with *out_present set to zero. Valid geometry requires exactly four
 * fields, signed 32-bit x/y, unsigned 32-bit nonzero width/height, optional
 * leading signs, and internal underscores between digits. Only ASCII
 * space, tab, carriage return, and newline are trimmed around the whole file.
 */

ShaulaPreviousAreaStatus
shaula_previous_area_store(ShaulaPreviousAreaSpan path,
                           ShaulaPreviousAreaGeometry geometry);

ShaulaPreviousAreaStatus
shaula_previous_area_load(ShaulaPreviousAreaSpan path, int32_t *out_present,
                          ShaulaPreviousAreaGeometry *out_geometry);

/* Returns zero only for the exact byte string "portal-screenshot". */
int32_t shaula_previous_area_supported_for_backend(
    ShaulaPreviousAreaSpan backend_label);

#ifdef __cplusplus
}
#endif

#endif
