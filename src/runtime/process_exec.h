#ifndef SHAULA_RUNTIME_PROCESS_EXEC_H
#define SHAULA_RUNTIME_PROCESS_EXEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
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
} ShaulaProcessStatus;

typedef enum {
  SHAULA_PROCESS_TERM_EXITED = 0,
  SHAULA_PROCESS_TERM_SIGNAL = 1,
  SHAULA_PROCESS_TERM_STOPPED = 2,
  SHAULA_PROCESS_TERM_UNKNOWN = 3,
} ShaulaProcessTermKind;

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

typedef struct {
  const void *data;
  size_t length;
} ShaulaProcessInputChunk;

typedef enum {
  SHAULA_PROCESS_READY_OK = 0,
  SHAULA_PROCESS_READY_TIMEOUT = 1,
  SHAULA_PROCESS_READY_PROTOCOL_INVALID = 2,
  SHAULA_PROCESS_READY_CHILD_EXITED = 3,
  SHAULA_PROCESS_READY_IO_ERROR = 4,
} ShaulaProcessReadyStatus;

typedef struct {
  ShaulaProcessReadyStatus ready_status;
  ShaulaProcessTermKind term_kind;
  uint32_t term_value;
} ShaulaProcessReadyResult;

/*
 * Direct-argv synchronous process execution. argv must be NULL-terminated and
 * is never interpreted by a shell. Bare argv[0] names are resolved against the
 * parent process PATH even when replacement_environment replaces the child
 * environment.
 *
 * shaula_process_run captures and independently bounds both binary output
 * streams. shaula_process_run_sync is the text adapter for command modules: it
 * returns GLib-owned NUL-terminated strings, maps non-exit termination to a
 * conventional nonzero exit code, and applies the shared runtime stream limits.
 *
 * On every post-fork failure the child is terminated and reaped before return.
 * All output clear functions are idempotent.
 */
void shaula_process_output_clear(ShaulaProcessOutput *output);

ShaulaProcessStatus shaula_process_run(
    const char *const *argv, const char *const *replacement_environment,
    size_t stdout_limit, size_t stderr_limit, ShaulaProcessOutput *out_output);

ShaulaProcessStatus shaula_process_run_sync(
    const char *const *argv, const char *const *replacement_environment,
    char **out_stdout_text, char **out_stderr_text, int *out_exit_code);

/*
 * Pipes input_length bytes from input to stdin, discards child output, and
 * waits. A NULL input pointer is valid only when input_length is zero. SIGPIPE
 * is contained to the calling thread so an early child close returns IO_ERROR.
 */
ShaulaProcessStatus shaula_process_run_with_input(
    const char *const *argv, const void *input, size_t input_length,
    ShaulaProcessTermKind *out_term_kind, uint32_t *out_term_value);

/*
 * Starts a long-lived provider behind a short-lived launcher. The launcher
 * writes all borrowed input chunks, validates one exact readiness record, and
 * reports only after every failure path has terminated and reaped the provider.
 * On success the launcher exits, orphaning the ready provider so it survives the
 * initiating process without leaving a child for the caller to reap.
 *
 * Provider stdout is private protocol data. Provider stderr remains inherited
 * so diagnostics stay on stderr. Expected deterministic outcomes are returned
 * through ShaulaProcessReadyResult rather than mixed with spawn failures.
 */
ShaulaProcessStatus shaula_process_spawn_ready_detached(
    const char *const *argv, const ShaulaProcessInputChunk *input_chunks,
    size_t input_chunk_count, const void *expected_ready,
    size_t expected_ready_length, uint32_t timeout_ms,
    ShaulaProcessReadyResult *out_result);

#ifdef __cplusplus
}
#endif

#endif
