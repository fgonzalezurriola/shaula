#ifndef SHAULA_RUNTIME_PROCESS_EXEC_H
#define SHAULA_RUNTIME_PROCESS_EXEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ShaulaProcessStatus;
enum {
  SHAULA_PROCESS_STATUS_OK = 0,
  SHAULA_PROCESS_STATUS_INVALID_ARGUMENT = 1,
  SHAULA_PROCESS_STATUS_OUT_OF_MEMORY = 2,
  SHAULA_PROCESS_STATUS_FILE_NOT_FOUND = 3,
  SHAULA_PROCESS_STATUS_ACCESS_DENIED = 4,
  SHAULA_PROCESS_STATUS_INVALID_EXECUTABLE = 5,
  SHAULA_PROCESS_STATUS_IS_DIRECTORY = 6,
  SHAULA_PROCESS_STATUS_NOT_DIRECTORY = 7,
  SHAULA_PROCESS_STATUS_FILE_BUSY = 8,
  SHAULA_PROCESS_STATUS_SYMLINK_LOOP = 9,
  SHAULA_PROCESS_STATUS_FD_QUOTA = 10,
  SHAULA_PROCESS_STATUS_RESOURCE_LIMIT = 11,
  SHAULA_PROCESS_STATUS_SPAWN_ERROR = 12,
  SHAULA_PROCESS_STATUS_STREAM_TOO_LONG = 13,
  SHAULA_PROCESS_STATUS_IO_ERROR = 14,
  SHAULA_PROCESS_STATUS_NAME_TOO_LONG = 15,
  SHAULA_PROCESS_STATUS_FILESYSTEM_ERROR = 16,
  SHAULA_PROCESS_STATUS_PROCESS_FD_QUOTA = 17,
  SHAULA_PROCESS_STATUS_SYSTEM_RESOURCES = 18,
  SHAULA_PROCESS_STATUS_PERMISSION_DENIED = 19,
};

typedef int32_t ShaulaProcessTermKind;
enum {
  SHAULA_PROCESS_TERM_EXITED = 0,
  SHAULA_PROCESS_TERM_SIGNAL = 1,
  SHAULA_PROCESS_TERM_STOPPED = 2,
  SHAULA_PROCESS_TERM_UNKNOWN = 3,
};

typedef struct {
  const char *data;
  size_t length;
} ShaulaProcessSpan;

typedef struct {
  const ShaulaProcessSpan *items;
  size_t length;
} ShaulaProcessArgv;

typedef struct {
  char *data;
  size_t length;
} ShaulaProcessOwnedBytes;

typedef struct {
  ShaulaProcessOwnedBytes stdout_bytes;
  ShaulaProcessOwnedBytes stderr_bytes;
  ShaulaProcessTermKind term_kind;
  uint32_t term_value;
} ShaulaProcessOutput;

_Static_assert(sizeof(ShaulaProcessStatus) == 4,
               "ShaulaProcessStatus must remain a 32-bit C ABI value");
_Static_assert(sizeof(ShaulaProcessTermKind) == 4,
               "ShaulaProcessTermKind must remain a 32-bit C ABI value");
_Static_assert(SHAULA_PROCESS_STATUS_OK == 0,
               "process success ABI value changed");
_Static_assert(SHAULA_PROCESS_TERM_EXITED == 0,
               "process exited ABI value changed");
_Static_assert(SHAULA_PROCESS_TERM_SIGNAL == 1,
               "process signal ABI value changed");
_Static_assert(SHAULA_PROCESS_TERM_STOPPED == 2,
               "process stopped ABI value changed");
_Static_assert(SHAULA_PROCESS_TERM_UNKNOWN == 3,
               "process unknown ABI value changed");

/*
 * This Linux/POSIX module has no mutable global state. Argument and input spans
 * are borrowed for each synchronous call. NULL span pointers are valid only
 * when length is zero. Arguments may not contain embedded NUL bytes; stdin and
 * captured output are length-bearing binary data and may contain NUL bytes.
 *
 * argv is executed directly without a shell. argv[0] is resolved against the
 * parent process PATH even when a replacement environment is supplied, matching
 * Zig std.process.run. replacement_environment is either NULL to inherit the
 * parent environment or a NULL-terminated envp array that completely replaces
 * the child environment.
 *
 * shaula_process_run redirects stdin from /dev/null and concurrently drains
 * stdout and stderr to avoid pipe deadlock. Each stream has an independent hard
 * byte limit; exactly limit bytes are accepted and the first excess byte returns
 * STREAM_TOO_LONG. On every post-fork failure the child is terminated and
 * reaped before return. Successful output uses GLib allocation and must be
 * released with shaula_process_output_clear().
 *
 * shaula_process_run_with_input pipes all input bytes to stdin, ignores child
 * stdout/stderr, closes stdin, and waits. SIGPIPE is contained to the calling
 * thread so an early child close returns IO_ERROR rather than terminating the
 * parent process.
 */

void shaula_process_output_clear(ShaulaProcessOutput *output);

ShaulaProcessStatus shaula_process_run(
    ShaulaProcessArgv argv, const char *const *replacement_environment,
    size_t stdout_limit, size_t stderr_limit, ShaulaProcessOutput *out_output);

ShaulaProcessStatus shaula_process_run_with_input(
    ShaulaProcessArgv argv, ShaulaProcessSpan input,
    ShaulaProcessTermKind *out_term_kind, uint32_t *out_term_value);

#ifdef __cplusplus
}
#endif

#endif
