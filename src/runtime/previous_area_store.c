#define _POSIX_C_SOURCE 200809L

#include "previous_area_store.h"

#include "paths.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int span_is_valid(ShaulaPreviousAreaSpan span) {
  return span.data != NULL || span.length == 0;
}

static int checked_add_size(size_t left, size_t right, size_t *out) {
  if (left > SIZE_MAX - right) {
    return 0;
  }
  *out = left + right;
  return 1;
}

static int span_has_nul(ShaulaPreviousAreaSpan span) {
  return span.length > 0 && memchr(span.data, '\0', span.length) != NULL;
}

static ShaulaPreviousAreaStatus path_to_c_string(ShaulaPreviousAreaSpan path,
                                                 char **out_path) {
  size_t allocation_size;
  char *copy;

  *out_path = NULL;
  if (!span_is_valid(path)) {
    return SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT;
  }
  if (!checked_add_size(path.length, (size_t)1, &allocation_size)) {
    return SHAULA_PREVIOUS_AREA_STATUS_OUT_OF_MEMORY;
  }
  if (span_has_nul(path)) {
    return SHAULA_PREVIOUS_AREA_STATUS_FILESYSTEM_ERROR;
  }

  copy = g_try_malloc(allocation_size);
  if (copy == NULL) {
    return SHAULA_PREVIOUS_AREA_STATUS_OUT_OF_MEMORY;
  }
  if (path.length > 0) {
    memcpy(copy, path.data, path.length);
  }
  copy[path.length] = '\0';
  *out_path = copy;
  return SHAULA_PREVIOUS_AREA_STATUS_OK;
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

static int read_all(const char *path, char **out_data, size_t *out_length) {
  size_t capacity = 4096;
  size_t length = 0;
  char *data;
  int fd;

  *out_data = NULL;
  *out_length = 0;

  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return 0;
  }

  data = g_try_malloc(capacity);
  if (data == NULL) {
    close(fd);
    return 0;
  }

  for (;;) {
    ssize_t result;

    if (length == capacity) {
      size_t next_capacity;
      char *resized;

      if (capacity > SIZE_MAX / 2) {
        g_free(data);
        close(fd);
        return 0;
      }
      next_capacity = capacity * 2;
      resized = g_try_realloc(data, next_capacity);
      if (resized == NULL) {
        g_free(data);
        close(fd);
        return 0;
      }
      data = resized;
      capacity = next_capacity;
    }

    result = read(fd, data + length, capacity - length);
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

    g_free(data);
    close(fd);
    return 0;
  }

  close(fd);
  *out_data = data;
  *out_length = length;
  return 1;
}

static int is_trim_byte(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

static ShaulaPreviousAreaSpan trim_whole_file(ShaulaPreviousAreaSpan value) {
  size_t start = 0;
  size_t end = value.length;

  while (start < end && is_trim_byte((unsigned char)value.data[start])) {
    start += 1;
  }
  while (end > start && is_trim_byte((unsigned char)value.data[end - 1])) {
    end -= 1;
  }

  return (ShaulaPreviousAreaSpan){value.data + start, end - start};
}

static int parse_magnitude(ShaulaPreviousAreaSpan value, uint64_t limit,
                           int *out_negative, uint64_t *out_magnitude) {
  size_t index = 0;
  int negative = 0;
  uint64_t magnitude = 0;

  if (value.length == 0) {
    return 0;
  }
  if (value.data[index] == '+' || value.data[index] == '-') {
    negative = value.data[index] == '-';
    index += 1;
  }
  if (index == value.length || value.data[index] == '_' ||
      value.data[value.length - 1] == '_') {
    return 0;
  }

  for (; index < value.length; index += 1) {
    unsigned char byte = (unsigned char)value.data[index];
    uint64_t digit;

    if (byte == '_') {
      continue;
    }
    if (byte < '0' || byte > '9') {
      return 0;
    }
    digit = (uint64_t)(byte - '0');
    if (magnitude > (limit - digit) / 10) {
      return 0;
    }
    magnitude = magnitude * 10 + digit;
  }

  *out_negative = negative;
  *out_magnitude = magnitude;
  return 1;
}

static int parse_i32(ShaulaPreviousAreaSpan value, int32_t *out_value) {
  int negative;
  uint64_t magnitude;
  uint64_t limit;

  if (value.length > 0 && value.data[0] == '-') {
    limit = (uint64_t)INT32_MAX + 1;
  } else {
    limit = (uint64_t)INT32_MAX;
  }
  if (!parse_magnitude(value, limit, &negative, &magnitude)) {
    return 0;
  }

  if (negative) {
    if (magnitude == (uint64_t)INT32_MAX + 1) {
      *out_value = INT32_MIN;
    } else {
      *out_value = -(int32_t)magnitude;
    }
  } else {
    *out_value = (int32_t)magnitude;
  }
  return 1;
}

static int parse_u32(ShaulaPreviousAreaSpan value, uint32_t *out_value) {
  int negative;
  uint64_t magnitude;

  if (!parse_magnitude(value, UINT32_MAX, &negative, &magnitude)) {
    return 0;
  }
  if (negative && magnitude != 0) {
    return 0;
  }
  *out_value = (uint32_t)magnitude;
  return 1;
}

static int parse_geometry(ShaulaPreviousAreaSpan raw,
                          ShaulaPreviousAreaGeometry *out_geometry) {
  ShaulaPreviousAreaSpan fields[4];
  ShaulaPreviousAreaSpan trimmed;
  size_t field_index = 0;
  size_t field_start = 0;
  size_t index;
  ShaulaPreviousAreaGeometry geometry;

  trimmed = trim_whole_file(raw);
  if (trimmed.length == 0) {
    return 0;
  }

  for (index = 0; index <= trimmed.length; index += 1) {
    if (index == trimmed.length || trimmed.data[index] == '|') {
      if (field_index >= 4) {
        return 0;
      }
      fields[field_index] = (ShaulaPreviousAreaSpan){trimmed.data + field_start,
                                                     index - field_start};
      field_index += 1;
      field_start = index + 1;
    }
  }
  if (field_index != 4) {
    return 0;
  }

  if (!parse_u32(fields[2], &geometry.width) ||
      !parse_u32(fields[3], &geometry.height) || geometry.width == 0 ||
      geometry.height == 0 || !parse_i32(fields[0], &geometry.x) ||
      !parse_i32(fields[1], &geometry.y)) {
    return 0;
  }

  *out_geometry = geometry;
  return 1;
}

ShaulaPreviousAreaStatus
shaula_previous_area_store(ShaulaPreviousAreaSpan path,
                           ShaulaPreviousAreaGeometry geometry) {
  ShaulaRuntimePathStatus parent_status;
  ShaulaPreviousAreaStatus path_status;
  char content[128];
  char *nul_path;
  int content_length;
  int fd;
  int write_ok;

  if (!span_is_valid(path)) {
    return SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT;
  }

  path_status = path_to_c_string(path, &nul_path);
  if (path_status != SHAULA_PREVIOUS_AREA_STATUS_OK) {
    return path_status;
  }

  parent_status = shaula_runtime_path_ensure_parent(
      (ShaulaRuntimePathSpan){path.data, path.length});
  if (parent_status == SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY) {
    g_free(nul_path);
    return SHAULA_PREVIOUS_AREA_STATUS_OUT_OF_MEMORY;
  }
  if (parent_status == SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT) {
    g_free(nul_path);
    return SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT;
  }
  if (parent_status != SHAULA_RUNTIME_PATH_STATUS_OK) {
    g_free(nul_path);
    return SHAULA_PREVIOUS_AREA_STATUS_FILESYSTEM_ERROR;
  }

  content_length =
      snprintf(content, sizeof(content),
               "%" PRId32 "|%" PRId32 "|%" PRIu32 "|%" PRIu32 "\n", geometry.x,
               geometry.y, geometry.width, geometry.height);
  if (content_length < 0 || (size_t)content_length >= sizeof(content)) {
    g_free(nul_path);
    return SHAULA_PREVIOUS_AREA_STATUS_FILESYSTEM_ERROR;
  }

  fd = open(nul_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  g_free(nul_path);
  if (fd < 0) {
    return SHAULA_PREVIOUS_AREA_STATUS_FILESYSTEM_ERROR;
  }

  write_ok = write_all(fd, content, (size_t)content_length);
  close(fd);
  return write_ok ? SHAULA_PREVIOUS_AREA_STATUS_OK
                  : SHAULA_PREVIOUS_AREA_STATUS_FILESYSTEM_ERROR;
}

ShaulaPreviousAreaStatus
shaula_previous_area_load(ShaulaPreviousAreaSpan path, int32_t *out_present,
                          ShaulaPreviousAreaGeometry *out_geometry) {
  ShaulaPreviousAreaStatus path_status;
  char *nul_path;
  char *raw;
  size_t raw_length;

  if (out_present == NULL || out_geometry == NULL || !span_is_valid(path)) {
    return SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT;
  }
  *out_present = 0;
  *out_geometry = (ShaulaPreviousAreaGeometry){0};

  path_status = path_to_c_string(path, &nul_path);
  if (path_status == SHAULA_PREVIOUS_AREA_STATUS_INVALID_ARGUMENT) {
    return path_status;
  }
  if (path_status != SHAULA_PREVIOUS_AREA_STATUS_OK) {
    return SHAULA_PREVIOUS_AREA_STATUS_OK;
  }

  if (!read_all(nul_path, &raw, &raw_length)) {
    g_free(nul_path);
    return SHAULA_PREVIOUS_AREA_STATUS_OK;
  }
  g_free(nul_path);

  if (parse_geometry((ShaulaPreviousAreaSpan){raw, raw_length}, out_geometry)) {
    *out_present = 1;
  }
  g_free(raw);
  return SHAULA_PREVIOUS_AREA_STATUS_OK;
}

int32_t shaula_previous_area_supported_for_backend(
    ShaulaPreviousAreaSpan backend_label) {
  static const char portal_label[] = "portal-screenshot";

  if (!span_is_valid(backend_label)) {
    return 1;
  }
  return !(backend_label.length == sizeof(portal_label) - 1 &&
           memcmp(backend_label.data, portal_label, sizeof(portal_label) - 1) ==
               0);
}
