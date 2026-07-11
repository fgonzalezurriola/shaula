#define _POSIX_C_SOURCE 200809L

#include "capture_session_lock.h"

#include "paths.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

_Static_assert(sizeof(pid_t) == sizeof(int32_t),
               "capture-session lock requires a 32-bit Linux pid_t");

static int span_is_valid(ShaulaCaptureSessionSpan span) {
  return span.data != NULL || span.length == 0;
}

static int checked_add_size(size_t left, size_t right, size_t *out) {
  if (left > SIZE_MAX - right) {
    return 0;
  }
  *out = left + right;
  return 1;
}

static int span_has_nul(ShaulaCaptureSessionSpan span) {
  return span.length > 0 && memchr(span.data, '\0', span.length) != NULL;
}

static ShaulaCaptureSessionStatus
path_to_c_string(ShaulaCaptureSessionSpan path, char **out_path) {
  size_t allocation_size;
  char *copy;

  *out_path = NULL;
  if (!span_is_valid(path)) {
    return SHAULA_CAPTURE_SESSION_STATUS_INVALID_ARGUMENT;
  }
  if (!checked_add_size(path.length, (size_t)1, &allocation_size)) {
    return SHAULA_CAPTURE_SESSION_STATUS_OUT_OF_MEMORY;
  }
  if (span_has_nul(path)) {
    return SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR;
  }

  copy = g_try_malloc(allocation_size);
  if (copy == NULL) {
    return SHAULA_CAPTURE_SESSION_STATUS_OUT_OF_MEMORY;
  }
  if (path.length > 0) {
    memcpy(copy, path.data, path.length);
  }
  copy[path.length] = '\0';
  *out_path = copy;
  return SHAULA_CAPTURE_SESSION_STATUS_OK;
}

static ShaulaCaptureSessionStatus
map_runtime_path_status(ShaulaRuntimePathStatus status) {
  switch (status) {
  case SHAULA_RUNTIME_PATH_STATUS_OK:
    return SHAULA_CAPTURE_SESSION_STATUS_OK;
  case SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT:
    return SHAULA_CAPTURE_SESSION_STATUS_INVALID_ARGUMENT;
  case SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY:
    return SHAULA_CAPTURE_SESSION_STATUS_OUT_OF_MEMORY;
  case SHAULA_RUNTIME_PATH_STATUS_FILESYSTEM_ERROR:
  default:
    return SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR;
  }
}

static int write_all(int fd, const char *data, size_t length) {
  size_t written = 0;

  while (written < length) {
    ssize_t result = write(fd, data + written, length - written);
    if (result > 0) {
      written += (size_t)result;
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return 0;
  }
  return 1;
}

static int read_lock_file(const char *path, char buffer[65],
                          size_t *out_length) {
  size_t length = 0;
  int fd;

  *out_length = 0;
  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return 0;
  }

  while (length < 65) {
    ssize_t result = read(fd, buffer + length, 65 - length);
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
    close(fd);
    return 0;
  }

  close(fd);
  if (length > 64) {
    return 0;
  }
  *out_length = length;
  return 1;
}

static int is_trim_byte(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

static int parse_pid(const char *data, size_t length, pid_t *out_pid) {
  size_t start = 0;
  size_t end = length;
  size_t index;
  uint64_t magnitude = 0;
  uint64_t limit;
  int negative = 0;
  int saw_digit = 0;

  while (start < end && is_trim_byte((unsigned char)data[start])) {
    start += 1;
  }
  while (end > start && is_trim_byte((unsigned char)data[end - 1])) {
    end -= 1;
  }
  if (start == end) {
    return 0;
  }

  index = start;
  if (data[index] == '+' || data[index] == '-') {
    negative = data[index] == '-';
    index += 1;
  }
  if (index == end || data[index] == '_' || data[end - 1] == '_') {
    return 0;
  }

  limit = negative ? (uint64_t)INT32_MAX + 1 : (uint64_t)INT32_MAX;
  for (; index < end; index += 1) {
    unsigned char byte = (unsigned char)data[index];
    uint64_t digit;

    if (byte == '_') {
      continue;
    }
    if (byte < '0' || byte > '9') {
      return 0;
    }
    saw_digit = 1;
    digit = (uint64_t)(byte - '0');
    if (magnitude > (limit - digit) / 10) {
      return 0;
    }
    magnitude = magnitude * 10 + digit;
  }
  if (!saw_digit) {
    return 0;
  }

  if (negative) {
    if (magnitude == (uint64_t)INT32_MAX + 1) {
      *out_pid = (pid_t)INT32_MIN;
    } else {
      *out_pid = (pid_t)(-(int32_t)magnitude);
    }
  } else {
    *out_pid = (pid_t)(int32_t)magnitude;
  }
  return 1;
}

static int clear_stale_lock(const char *path) {
  char buffer[65];
  size_t length;
  pid_t pid;

  if (!read_lock_file(path, buffer, &length) ||
      !parse_pid(buffer, length, &pid)) {
    return 0;
  }

  if (kill(pid, 0) == 0) {
    return 0;
  }
  if (errno != ESRCH) {
    return 0;
  }
  return unlink(path) == 0;
}

static int create_exclusive(const char *path) {
  return open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
}

ShaulaCaptureSessionStatus
shaula_capture_session_lock_acquire(ShaulaCaptureSessionSpan path) {
  ShaulaRuntimePathStatus parent_status;
  ShaulaCaptureSessionStatus status;
  char *c_path = NULL;
  char pid_line[64];
  int pid_length;
  int fd;

  status = path_to_c_string(path, &c_path);
  if (status != SHAULA_CAPTURE_SESSION_STATUS_OK) {
    return status;
  }

  parent_status = shaula_runtime_path_ensure_parent(
      (ShaulaRuntimePathSpan){path.data, path.length});
  status = map_runtime_path_status(parent_status);
  if (status != SHAULA_CAPTURE_SESSION_STATUS_OK) {
    g_free(c_path);
    return status;
  }

  fd = create_exclusive(c_path);
  if (fd < 0 && errno == EEXIST) {
    if (!clear_stale_lock(c_path)) {
      g_free(c_path);
      return SHAULA_CAPTURE_SESSION_STATUS_BUSY;
    }
    fd = create_exclusive(c_path);
    if (fd < 0 && errno == EEXIST) {
      g_free(c_path);
      return SHAULA_CAPTURE_SESSION_STATUS_BUSY;
    }
  }
  if (fd < 0) {
    g_free(c_path);
    return SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR;
  }

  pid_length = snprintf(pid_line, sizeof(pid_line), "%ld\n", (long)getpid());
  if (pid_length < 0 || (size_t)pid_length >= sizeof(pid_line) ||
      !write_all(fd, pid_line, (size_t)pid_length)) {
    close(fd);
    g_free(c_path);
    return SHAULA_CAPTURE_SESSION_STATUS_FILESYSTEM_ERROR;
  }

  close(fd);
  g_free(c_path);
  return SHAULA_CAPTURE_SESSION_STATUS_OK;
}

void shaula_capture_session_lock_release(ShaulaCaptureSessionSpan path) {
  char *c_path = NULL;

  if (path_to_c_string(path, &c_path) != SHAULA_CAPTURE_SESSION_STATUS_OK) {
    return;
  }
  (void)unlink(c_path);
  g_free(c_path);
}
