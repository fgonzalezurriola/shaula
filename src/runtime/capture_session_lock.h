#ifndef SHAULA_RUNTIME_CAPTURE_SESSION_LOCK_H
#define SHAULA_RUNTIME_CAPTURE_SESSION_LOCK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaCaptureSessionStatus;
enum {
  SHAULA_CAPTURE_SESSION_STATUS_OK = 0,
  SHAULA_CAPTURE_SESSION_STATUS_BUSY = 1,
  SHAULA_CAPTURE_SESSION_STATUS_INVALID_ARGUMENT = 2,
  SHAULA_CAPTURE_SESSION_STATUS_OUT_OF_MEMORY = 3,
  SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR = 4,
};

typedef struct {
  const char *data;
  size_t length;
} ShaulaCaptureSessionSpan;

_Static_assert(sizeof(ShaulaCaptureSessionStatus) == 4,
               "ShaulaCaptureSessionStatus must remain a 32-bit C ABI value");
_Static_assert(SHAULA_CAPTURE_SESSION_STATUS_OK == 0,
               "capture-session success ABI value changed");
_Static_assert(SHAULA_CAPTURE_SESSION_STATUS_BUSY == 1,
               "capture-session busy ABI value changed");
_Static_assert(SHAULA_CAPTURE_SESSION_STATUS_INVALID_ARGUMENT == 2,
               "capture-session invalid-argument ABI value changed");
_Static_assert(SHAULA_CAPTURE_SESSION_STATUS_OUT_OF_MEMORY == 3,
               "capture-session out-of-memory ABI value changed");
_Static_assert(SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR == 4,
               "capture-session filesystem-error ABI value changed");

/*
 * This module has no mutable global state. Path spans are borrowed for each
 * synchronous call; a NULL pointer is valid only when length is zero. Paths may
 * be relative, but embedded NUL bytes cannot cross the POSIX filesystem
 * boundary.
 *
 * Acquire creates parent directories through the runtime-path contract, then
 * creates the lock file exclusively with mode 0666 subject to umask. The file
 * contains exactly the current decimal PID followed by a newline.
 *
 * When a lock already exists, at most 64 bytes are read and ASCII whitespace is
 * trimmed around the complete file. A valid decimal pid_t accepts an optional
 * sign and internal underscores between digits. kill(pid, 0) classifies only
 * ESRCH as stale. A stale file is unlinked and exclusive creation is retried
 * once; every live, inaccessible, malformed, oversized, or concurrently
 * replaced lock reports BUSY.
 *
 * Release is best-effort and ignores missing files and unlink failures. Callers
 * remain responsible for idempotence and for releasing before Preview work.
 */
ShaulaCaptureSessionStatus
shaula_capture_session_lock_acquire(ShaulaCaptureSessionSpan path);

void shaula_capture_session_lock_release(ShaulaCaptureSessionSpan path);

#ifdef __cplusplus
}
#endif

#endif
