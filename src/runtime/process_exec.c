#define _GNU_SOURCE

#include "process_exec.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  char **items;
  size_t length;
} OwnedArgv;

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} ByteBuffer;

extern char **environ;

static const char default_path[] = "/usr/local/bin:/bin/:/usr/bin";

static int checked_add_size(size_t left, size_t right, size_t *out) {
  if (left > SIZE_MAX - right) {
    return 0;
  }
  *out = left + right;
  return 1;
}

static char *try_strdup(const char *value) {
  size_t length = strlen(value);
  char *copy = g_try_malloc(length + 1U);

  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, value, length + 1U);
  return copy;
}

static void owned_argv_clear(OwnedArgv *argv) {
  size_t index;

  if (argv == NULL) {
    return;
  }
  for (index = 0; index < argv->length; index += 1) {
    g_free(argv->items[index]);
  }
  g_free(argv->items);
  argv->items = NULL;
  argv->length = 0;
}

static ShaulaProcessStatus owned_argv_init(const char *const *input,
                                           OwnedArgv *out_argv) {
  size_t length = 0;
  size_t pointer_count;
  size_t index;

  out_argv->items = NULL;
  out_argv->length = 0;
  if (input == NULL || input[0] == NULL || input[0][0] == '\0') {
    return SHAULA_PROCESS_STATUS_INVALID_ARGUMENT;
  }
  while (input[length] != NULL) {
    if (length == SIZE_MAX - 1) {
      return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
    }
    length += 1;
  }
  if (!checked_add_size(length, (size_t)1, &pointer_count) ||
      pointer_count > SIZE_MAX / sizeof(char *)) {
    return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
  }

  out_argv->items = g_try_new0(char *, pointer_count);
  if (out_argv->items == NULL) {
    return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
  }

  for (index = 0; index < length; index += 1) {
    out_argv->items[index] = try_strdup(input[index]);
    if (out_argv->items[index] == NULL) {
      owned_argv_clear(out_argv);
      return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
    }
    out_argv->length += 1;
  }

  return SHAULA_PROCESS_STATUS_OK;
}

void shaula_process_output_clear(ShaulaProcessOutput *output) {
  if (output == NULL) {
    return;
  }
  g_free(output->stdout_bytes.data);
  g_free(output->stderr_bytes.data);
  output->stdout_bytes.data = NULL;
  output->stdout_bytes.length = 0;
  output->stderr_bytes.data = NULL;
  output->stderr_bytes.length = 0;
  output->term_kind = SHAULA_PROCESS_TERM_EXITED;
  output->term_value = 0;
}

static void process_output_init(ShaulaProcessOutput *output) {
  output->stdout_bytes.data = NULL;
  output->stdout_bytes.length = 0;
  output->stderr_bytes.data = NULL;
  output->stderr_bytes.length = 0;
  output->term_kind = SHAULA_PROCESS_TERM_EXITED;
  output->term_value = 0;
}

static ShaulaProcessStatus map_errno_to_spawn_status(int error_number) {
  switch (error_number) {
  case ENOENT:
    return SHAULA_PROCESS_STATUS_FILE_NOT_FOUND;
  case EACCES:
    return SHAULA_PROCESS_STATUS_ACCESS_DENIED;
  case EPERM:
    return SHAULA_PROCESS_STATUS_PERMISSION_DENIED;
  case EINVAL:
  case ENOEXEC:
#ifdef ELIBBAD
  case ELIBBAD:
#endif
    return SHAULA_PROCESS_STATUS_INVALID_EXECUTABLE;
  case EISDIR:
    return SHAULA_PROCESS_STATUS_IS_DIRECTORY;
  case ENOTDIR:
    return SHAULA_PROCESS_STATUS_NOT_DIRECTORY;
  case ETXTBSY:
    return SHAULA_PROCESS_STATUS_FILE_BUSY;
  case ENAMETOOLONG:
    return SHAULA_PROCESS_STATUS_NAME_TOO_LONG;
  case EIO:
  case ELOOP:
    return SHAULA_PROCESS_STATUS_FILESYSTEM_ERROR;
  case EMFILE:
    return SHAULA_PROCESS_STATUS_PROCESS_FD_QUOTA;
  case ENFILE:
    return SHAULA_PROCESS_STATUS_FD_QUOTA;
  case E2BIG:
  case EAGAIN:
  case ENOMEM:
    return SHAULA_PROCESS_STATUS_SYSTEM_RESOURCES;
  default:
    return SHAULA_PROCESS_STATUS_SPAWN_ERROR;
  }
}

static ShaulaProcessStatus copy_parent_path(char **out_path) {
  const char *path = getenv("PATH");
  size_t path_length;
  size_t allocation_size;

  path = path != NULL ? path : default_path;
  path_length = strlen(path);
  if (!checked_add_size(path_length, (size_t)1, &allocation_size)) {
    *out_path = NULL;
    return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
  }

  *out_path = g_try_malloc(allocation_size);
  if (*out_path == NULL) {
    return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
  }
  memcpy(*out_path, path, allocation_size);
  return SHAULA_PROCESS_STATUS_OK;
}

static int make_pipe(int pipe_fds[2]) {
  return pipe2(pipe_fds, O_CLOEXEC) == 0;
}

static void close_fd(int *fd) {
  if (*fd >= 0) {
    (void)close(*fd);
    *fd = -1;
  }
}

static void child_report_errno_and_exit(int error_fd, int error_number) {
  const unsigned char *bytes = (const unsigned char *)&error_number;
  size_t written = 0;

  while (written < sizeof(error_number)) {
    ssize_t result =
        write(error_fd, bytes + written, sizeof(error_number) - written);
    if (result > 0) {
      written += (size_t)result;
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  _exit(127);
}

static void child_dup_or_report(int source_fd, int target_fd, int error_fd) {
  if (dup2(source_fd, target_fd) < 0) {
    child_report_errno_and_exit(error_fd, errno);
  }
}

static void child_exec_path(const OwnedArgv *argv,
                            const char *const *replacement_environment,
                            const char *parent_path, int error_fd) {
  char *const *child_environment =
      replacement_environment != NULL
          ? (char *const *)replacement_environment
          : environ;
  const char *program = argv->items[0];
  size_t program_length = strlen(program);
  size_t component_start = 0;
  int saw_access_denied = 0;
  int last_error = ENOENT;

  if (strchr(program, '/') != NULL) {
    execve(program, argv->items, child_environment);
    child_report_errno_and_exit(error_fd, errno);
  }

  for (;;) {
    size_t component_end = component_start;
    size_t component_length;
    size_t candidate_length;
    char candidate[PATH_MAX];

    while (parent_path[component_end] != '\0' &&
           parent_path[component_end] != ':') {
      component_end += 1;
    }
    component_length = component_end - component_start;

    if (component_length > 0) {
      if (component_length > SIZE_MAX - program_length - 1) {
        child_report_errno_and_exit(error_fd, ENAMETOOLONG);
      }
      candidate_length = component_length + 1 + program_length;
      if (candidate_length >= sizeof(candidate)) {
        child_report_errno_and_exit(error_fd, ENAMETOOLONG);
      }

      memcpy(candidate, parent_path + component_start, component_length);
      candidate[component_length] = '/';
      memcpy(candidate + component_length + 1, program, program_length);
      candidate[candidate_length] = '\0';

      execve(candidate, argv->items, child_environment);
      last_error = errno;
      if (last_error == EACCES) {
        saw_access_denied = 1;
      } else if (last_error != ENOENT && last_error != ENOTDIR) {
        child_report_errno_and_exit(error_fd, last_error);
      }
    }

    if (parent_path[component_end] == '\0') {
      break;
    }
    component_start = component_end + 1;
  }

  child_report_errno_and_exit(error_fd,
                              saw_access_denied ? EACCES : last_error);
}

static ShaulaProcessStatus read_exec_error(int fd, int *out_error_number) {
  unsigned char *bytes = (unsigned char *)out_error_number;
  size_t length = 0;

  *out_error_number = 0;
  while (length < sizeof(*out_error_number)) {
    ssize_t result = read(fd, bytes + length, sizeof(*out_error_number) - length);
    if (result > 0) {
      length += (size_t)result;
      continue;
    }
    if (result == 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    return SHAULA_PROCESS_STATUS_IO_ERROR;
  }

  if (length == 0) {
    return SHAULA_PROCESS_STATUS_OK;
  }
  if (length != sizeof(*out_error_number)) {
    return SHAULA_PROCESS_STATUS_SPAWN_ERROR;
  }
  return map_errno_to_spawn_status(*out_error_number);
}

static void wait_reap(pid_t pid) {
  int status;

  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return;
    }
  }
}

static void terminate_and_reap(pid_t pid) {
  if (kill(pid, SIGTERM) < 0 && errno != ESRCH) {
    /* Match the former best-effort child cleanup boundary. */
  }
  wait_reap(pid);
}

static void term_from_wait_status(int wait_status,
                                  ShaulaProcessTermKind *out_kind,
                                  uint32_t *out_value) {
  if (WIFEXITED(wait_status)) {
    *out_kind = SHAULA_PROCESS_TERM_EXITED;
    *out_value = (uint32_t)WEXITSTATUS(wait_status);
  } else if (WIFSIGNALED(wait_status)) {
    *out_kind = SHAULA_PROCESS_TERM_SIGNAL;
    *out_value = (uint32_t)WTERMSIG(wait_status);
  } else if (WIFSTOPPED(wait_status)) {
    *out_kind = SHAULA_PROCESS_TERM_STOPPED;
    *out_value = (uint32_t)WSTOPSIG(wait_status);
  } else {
    *out_kind = SHAULA_PROCESS_TERM_UNKNOWN;
    *out_value = (uint32_t)wait_status;
  }
}

static ShaulaProcessStatus wait_for_term(pid_t pid,
                                         ShaulaProcessTermKind *out_kind,
                                         uint32_t *out_value) {
  int wait_status;
  pid_t result;

  do {
    result = waitpid(pid, &wait_status, 0);
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    return SHAULA_PROCESS_STATUS_IO_ERROR;
  }

  term_from_wait_status(wait_status, out_kind, out_value);
  return SHAULA_PROCESS_STATUS_OK;
}

static void byte_buffer_clear(ByteBuffer *buffer) {
  g_free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
}

static ShaulaProcessStatus byte_buffer_append(ByteBuffer *buffer,
                                              const char *data, size_t length,
                                              size_t limit) {
  size_t required;

  if (buffer->length > limit || length > limit - buffer->length) {
    return SHAULA_PROCESS_STATUS_STREAM_TOO_LONG;
  }
  if (length == 0) {
    return SHAULA_PROCESS_STATUS_OK;
  }
  if (!checked_add_size(buffer->length, length, &required)) {
    return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
  }

  if (required > buffer->capacity) {
    size_t capacity = buffer->capacity == 0 ? (size_t)64 : buffer->capacity;
    char *resized;

    while (capacity < required) {
      if (capacity > SIZE_MAX / 2) {
        capacity = required;
        break;
      }
      capacity *= 2;
    }
    resized = g_try_realloc(buffer->data, capacity);
    if (resized == NULL) {
      return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
    }
    buffer->data = resized;
    buffer->capacity = capacity;
  }

  memcpy(buffer->data + buffer->length, data, length);
  buffer->length = required;
  return SHAULA_PROCESS_STATUS_OK;
}

static ShaulaProcessStatus collect_streams(int stdout_fd, int stderr_fd,
                                           size_t stdout_limit,
                                           size_t stderr_limit,
                                           ByteBuffer *stdout_buffer,
                                           ByteBuffer *stderr_buffer) {
  struct pollfd streams[2] = {
      {stdout_fd, POLLIN | POLLHUP | POLLERR, 0},
      {stderr_fd, POLLIN | POLLHUP | POLLERR, 0},
  };
  ByteBuffer *buffers[2] = {stdout_buffer, stderr_buffer};
  size_t limits[2] = {stdout_limit, stderr_limit};
  size_t active = 2;

  while (active > 0) {
    int poll_result;
    size_t index;

    do {
      poll_result = poll(streams, 2, -1);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result < 0) {
      return SHAULA_PROCESS_STATUS_IO_ERROR;
    }

    for (index = 0; index < 2; index += 1) {
      char chunk[4096];
      ssize_t read_result;
      ShaulaProcessStatus append_status;

      if (streams[index].fd < 0 || streams[index].revents == 0) {
        continue;
      }
      if ((streams[index].revents & POLLNVAL) != 0) {
        return SHAULA_PROCESS_STATUS_IO_ERROR;
      }

      do {
        read_result = read(streams[index].fd, chunk, sizeof(chunk));
      } while (read_result < 0 && errno == EINTR);

      if (read_result > 0) {
        append_status = byte_buffer_append(
            buffers[index], chunk, (size_t)read_result, limits[index]);
        if (append_status != SHAULA_PROCESS_STATUS_OK) {
          return append_status;
        }
        continue;
      }
      if (read_result == 0) {
        streams[index].fd = -1;
        active -= 1;
        continue;
      }
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        return SHAULA_PROCESS_STATUS_IO_ERROR;
      }
    }
  }

  return SHAULA_PROCESS_STATUS_OK;
}

ShaulaProcessStatus shaula_process_run(
    const char *const *argv, const char *const *replacement_environment,
    size_t stdout_limit, size_t stderr_limit, ShaulaProcessOutput *out_output) {
  OwnedArgv owned_argv = {0};
  ByteBuffer stdout_buffer = {0};
  ByteBuffer stderr_buffer = {0};
  ShaulaProcessStatus status;
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  int error_pipe[2] = {-1, -1};
  int null_fd = -1;
  int exec_error = 0;
  char *parent_path = NULL;
  pid_t pid;

  if (out_output == NULL) {
    return SHAULA_PROCESS_STATUS_INVALID_ARGUMENT;
  }
  process_output_init(out_output);

  status = owned_argv_init(argv, &owned_argv);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    return status;
  }
  status = copy_parent_path(&parent_path);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    owned_argv_clear(&owned_argv);
    return status;
  }
  if (!make_pipe(stdout_pipe) || !make_pipe(stderr_pipe) ||
      !make_pipe(error_pipe)) {
    status = map_errno_to_spawn_status(errno);
    goto fail_before_fork;
  }

  null_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
  if (null_fd < 0) {
    status = map_errno_to_spawn_status(errno);
    goto fail_before_fork;
  }

  pid = fork();
  if (pid < 0) {
    status = map_errno_to_spawn_status(errno);
    goto fail_before_fork;
  }
  if (pid == 0) {
    close_fd(&stdout_pipe[0]);
    close_fd(&stderr_pipe[0]);
    close_fd(&error_pipe[0]);
    child_dup_or_report(null_fd, STDIN_FILENO, error_pipe[1]);
    child_dup_or_report(stdout_pipe[1], STDOUT_FILENO, error_pipe[1]);
    child_dup_or_report(stderr_pipe[1], STDERR_FILENO, error_pipe[1]);
    close_fd(&null_fd);
    close_fd(&stdout_pipe[1]);
    close_fd(&stderr_pipe[1]);
    child_exec_path(&owned_argv, replacement_environment, parent_path,
                    error_pipe[1]);
  }

  close_fd(&null_fd);
  close_fd(&stdout_pipe[1]);
  close_fd(&stderr_pipe[1]);
  close_fd(&error_pipe[1]);

  status = read_exec_error(error_pipe[0], &exec_error);
  close_fd(&error_pipe[0]);
  g_clear_pointer(&parent_path, g_free);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    close_fd(&stdout_pipe[0]);
    close_fd(&stderr_pipe[0]);
    terminate_and_reap(pid);
    owned_argv_clear(&owned_argv);
    return status;
  }

  status = collect_streams(stdout_pipe[0], stderr_pipe[0], stdout_limit,
                           stderr_limit, &stdout_buffer, &stderr_buffer);
  close_fd(&stdout_pipe[0]);
  close_fd(&stderr_pipe[0]);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    terminate_and_reap(pid);
    byte_buffer_clear(&stdout_buffer);
    byte_buffer_clear(&stderr_buffer);
    owned_argv_clear(&owned_argv);
    return status;
  }

  status = wait_for_term(pid, &out_output->term_kind, &out_output->term_value);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    byte_buffer_clear(&stdout_buffer);
    byte_buffer_clear(&stderr_buffer);
    owned_argv_clear(&owned_argv);
    return status;
  }

  out_output->stdout_bytes.data = stdout_buffer.data;
  out_output->stdout_bytes.length = stdout_buffer.length;
  out_output->stderr_bytes.data = stderr_buffer.data;
  out_output->stderr_bytes.length = stderr_buffer.length;
  owned_argv_clear(&owned_argv);
  return SHAULA_PROCESS_STATUS_OK;

fail_before_fork:
  close_fd(&null_fd);
  close_fd(&stdout_pipe[0]);
  close_fd(&stdout_pipe[1]);
  close_fd(&stderr_pipe[0]);
  close_fd(&stderr_pipe[1]);
  close_fd(&error_pipe[0]);
  close_fd(&error_pipe[1]);
  g_clear_pointer(&parent_path, g_free);
  owned_argv_clear(&owned_argv);
  return status;
}

ShaulaProcessStatus shaula_process_run_sync(
    const char *const *argv, const char *const *replacement_environment,
    char **out_stdout_text, char **out_stderr_text, int *out_exit_code) {
  static const size_t stdout_limit = 4U * 1024U * 1024U;
  static const size_t stderr_limit = 1024U * 1024U;
  ShaulaProcessOutput output = {0};
  ShaulaProcessStatus status;
  char *stdout_text = NULL;
  char *stderr_text = NULL;

  if (out_stdout_text != NULL) {
    g_clear_pointer(out_stdout_text, g_free);
  }
  if (out_stderr_text != NULL) {
    g_clear_pointer(out_stderr_text, g_free);
  }
  if (out_exit_code != NULL) {
    *out_exit_code = 127;
  }

  status = shaula_process_run(argv, replacement_environment, stdout_limit,
                              stderr_limit, &output);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    return status;
  }

  if (out_stdout_text != NULL) {
    stdout_text = g_try_malloc(output.stdout_bytes.length + 1U);
    if (stdout_text == NULL) {
      shaula_process_output_clear(&output);
      return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
    }
    if (output.stdout_bytes.length > 0) {
      memcpy(stdout_text, output.stdout_bytes.data, output.stdout_bytes.length);
    }
    stdout_text[output.stdout_bytes.length] = '\0';
    *out_stdout_text = stdout_text;
  }
  if (out_stderr_text != NULL) {
    stderr_text = g_try_malloc(output.stderr_bytes.length + 1U);
    if (stderr_text == NULL) {
      if (out_stdout_text != NULL) {
        g_clear_pointer(out_stdout_text, g_free);
      }
      shaula_process_output_clear(&output);
      return SHAULA_PROCESS_STATUS_OUT_OF_MEMORY;
    }
    if (output.stderr_bytes.length > 0) {
      memcpy(stderr_text, output.stderr_bytes.data, output.stderr_bytes.length);
    }
    stderr_text[output.stderr_bytes.length] = '\0';
    *out_stderr_text = stderr_text;
  }
  if (out_exit_code != NULL) {
    if (output.term_kind == SHAULA_PROCESS_TERM_EXITED) {
      *out_exit_code = (int)output.term_value;
    } else if (output.term_kind == SHAULA_PROCESS_TERM_SIGNAL) {
      *out_exit_code = 128 + (int)output.term_value;
    } else {
      *out_exit_code = 1;
    }
  }

  shaula_process_output_clear(&output);
  return SHAULA_PROCESS_STATUS_OK;
}

static int write_all_without_sigpipe(int fd, const char *data, size_t length) {
  sigset_t blocked;
  sigset_t previous;
  sigset_t pending;
  int was_pending = 0;
  size_t written = 0;
  int success = 1;
  int write_errno = 0;

  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  if (pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0) {
    return 0;
  }
  if (sigpending(&pending) == 0) {
    was_pending = sigismember(&pending, SIGPIPE) == 1;
  }

  while (written < length) {
    ssize_t result = write(fd, data + written, length - written);
    if (result > 0) {
      written += (size_t)result;
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    success = 0;
    write_errno = errno;
    break;
  }

  if (!success && write_errno == EPIPE && !was_pending) {
    struct timespec timeout = {0, 0};
    while (sigtimedwait(&blocked, NULL, &timeout) < 0 && errno == EINTR) {
    }
  }
  (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
  return success;
}

ShaulaProcessStatus shaula_process_run_with_input(
    const char *const *argv, const void *input, size_t input_length,
    ShaulaProcessTermKind *out_term_kind, uint32_t *out_term_value) {
  OwnedArgv owned_argv = {0};
  ShaulaProcessStatus status;
  int stdin_pipe[2] = {-1, -1};
  int error_pipe[2] = {-1, -1};
  int null_fd = -1;
  int exec_error = 0;
  char *parent_path = NULL;
  pid_t pid;

  if (out_term_kind == NULL || out_term_value == NULL ||
      (input == NULL && input_length > 0)) {
    return SHAULA_PROCESS_STATUS_INVALID_ARGUMENT;
  }
  *out_term_kind = SHAULA_PROCESS_TERM_EXITED;
  *out_term_value = 0;

  status = owned_argv_init(argv, &owned_argv);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    return status;
  }
  status = copy_parent_path(&parent_path);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    owned_argv_clear(&owned_argv);
    return status;
  }
  if (!make_pipe(stdin_pipe) || !make_pipe(error_pipe)) {
    status = map_errno_to_spawn_status(errno);
    goto fail_before_fork;
  }

  null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
  if (null_fd < 0) {
    status = map_errno_to_spawn_status(errno);
    goto fail_before_fork;
  }

  pid = fork();
  if (pid < 0) {
    status = map_errno_to_spawn_status(errno);
    goto fail_before_fork;
  }
  if (pid == 0) {
    close_fd(&stdin_pipe[1]);
    close_fd(&error_pipe[0]);
    child_dup_or_report(stdin_pipe[0], STDIN_FILENO, error_pipe[1]);
    child_dup_or_report(null_fd, STDOUT_FILENO, error_pipe[1]);
    child_dup_or_report(null_fd, STDERR_FILENO, error_pipe[1]);
    close_fd(&stdin_pipe[0]);
    close_fd(&null_fd);
    child_exec_path(&owned_argv, NULL, parent_path, error_pipe[1]);
  }

  close_fd(&stdin_pipe[0]);
  close_fd(&null_fd);
  close_fd(&error_pipe[1]);

  status = read_exec_error(error_pipe[0], &exec_error);
  close_fd(&error_pipe[0]);
  g_clear_pointer(&parent_path, g_free);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    close_fd(&stdin_pipe[1]);
    terminate_and_reap(pid);
    owned_argv_clear(&owned_argv);
    return status;
  }

  if (input_length > 0 &&
      !write_all_without_sigpipe(stdin_pipe[1], input, input_length)) {
    close_fd(&stdin_pipe[1]);
    terminate_and_reap(pid);
    owned_argv_clear(&owned_argv);
    return SHAULA_PROCESS_STATUS_IO_ERROR;
  }
  close_fd(&stdin_pipe[1]);

  status = wait_for_term(pid, out_term_kind, out_term_value);
  owned_argv_clear(&owned_argv);
  return status;

fail_before_fork:
  close_fd(&null_fd);
  close_fd(&stdin_pipe[0]);
  close_fd(&stdin_pipe[1]);
  close_fd(&error_pipe[0]);
  close_fd(&error_pipe[1]);
  g_clear_pointer(&parent_path, g_free);
  owned_argv_clear(&owned_argv);
  return status;
}
