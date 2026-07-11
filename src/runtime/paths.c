#include "paths.h"

#include "env.h"

#include <glib.h>
#include <string.h>

static int span_is_valid(ShaulaRuntimePathSpan span) {
  return span.data != NULL || span.length == 0;
}

static int checked_add_size(size_t left, size_t right, size_t *out) {
  if (left > SIZE_MAX - right) {
    return 0;
  }
  *out = left + right;
  return 1;
}

static ShaulaRuntimePathStatus copy_span(ShaulaRuntimePathSpan value,
                                         ShaulaRuntimeOwnedPath *out_path) {
  size_t allocation_size;
  char *copy;

  if (!span_is_valid(value) ||
      !checked_add_size(value.length, (size_t)1, &allocation_size)) {
    return SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY;
  }

  copy = g_try_malloc(allocation_size);
  if (copy == NULL) {
    return SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY;
  }

  if (value.length > 0) {
    memcpy(copy, value.data, value.length);
  }
  copy[value.length] = '\0';
  out_path->data = copy;
  out_path->length = value.length;
  return SHAULA_RUNTIME_PATH_STATUS_OK;
}

static ShaulaRuntimePathStatus join_path(ShaulaRuntimePathSpan prefix,
                                         const char *middle,
                                         ShaulaRuntimePathSpan relative_path,
                                         ShaulaRuntimeOwnedPath *out_path) {
  size_t middle_length = strlen(middle);
  size_t joined_length;
  size_t allocation_size;
  size_t offset = 0;
  char *joined;

  if (!span_is_valid(prefix) || !span_is_valid(relative_path) ||
      !checked_add_size(prefix.length, middle_length, &joined_length) ||
      !checked_add_size(joined_length, relative_path.length, &joined_length) ||
      !checked_add_size(joined_length, (size_t)1, &allocation_size)) {
    return SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY;
  }

  joined = g_try_malloc(allocation_size);
  if (joined == NULL) {
    return SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY;
  }

  if (prefix.length > 0) {
    memcpy(joined + offset, prefix.data, prefix.length);
    offset += prefix.length;
  }
  if (middle_length > 0) {
    memcpy(joined + offset, middle, middle_length);
    offset += middle_length;
  }
  if (relative_path.length > 0) {
    memcpy(joined + offset, relative_path.data, relative_path.length);
    offset += relative_path.length;
  }
  joined[offset] = '\0';

  out_path->data = joined;
  out_path->length = joined_length;
  return SHAULA_RUNTIME_PATH_STATUS_OK;
}

static ShaulaRuntimePathSpan env_trimmed_span(const char *value) {
  ShaulaEnvSpan trimmed = {0};

  if (shaula_env_value_trimmed(value, &trimmed) != SHAULA_ENV_STATUS_VALID) {
    return (ShaulaRuntimePathSpan){0};
  }
  return (ShaulaRuntimePathSpan){trimmed.data, trimmed.length};
}

static int span_starts_with(ShaulaRuntimePathSpan value, const char *expected) {
  size_t expected_length = strlen(expected);

  return span_is_valid(value) && value.length >= expected_length &&
         memcmp(value.data, expected, expected_length) == 0;
}

static int span_contains(ShaulaRuntimePathSpan value, const char *needle) {
  size_t needle_length = strlen(needle);
  size_t index;

  if (!span_is_valid(value) || needle_length == 0 ||
      value.length < needle_length) {
    return 0;
  }

  for (index = 0; index <= value.length - needle_length; index += 1) {
    if (memcmp(value.data + index, needle, needle_length) == 0) {
      return 1;
    }
  }
  return 0;
}

static int span_has_nul(ShaulaRuntimePathSpan value) {
  return value.length > 0 && memchr(value.data, '\0', value.length) != NULL;
}

static int parent_length(ShaulaRuntimePathSpan path, size_t *out_length) {
  size_t root_length = 0;
  size_t root_end = 0;
  size_t end;
  size_t start;
  size_t previous_end;

  if (path.length > 0 && path.data[0] == '/') {
    root_length = 1;
    root_end = 1;
    while (root_end < path.length && path.data[root_end] == '/') {
      root_end += 1;
    }
  }

  end = path.length;
  while (end > root_end && path.data[end - 1] == '/') {
    end -= 1;
  }
  if (end == root_end) {
    return 0;
  }

  start = end;
  while (start > root_end && path.data[start - 1] != '/') {
    start -= 1;
  }

  previous_end = start;
  while (previous_end > root_end && path.data[previous_end - 1] == '/') {
    previous_end -= 1;
  }

  if (previous_end == root_end) {
    if (root_length == 0) {
      return 0;
    }
    *out_length = root_length;
    return 1;
  }

  *out_length = previous_end;
  return 1;
}

void shaula_runtime_owned_path_clear(ShaulaRuntimeOwnedPath *path) {
  if (path == NULL) {
    return;
  }
  g_free(path->data);
  path->data = NULL;
  path->length = 0;
}

ShaulaRuntimePathStatus shaula_runtime_path_resolve(
    const char *override_value, const char *runtime_dir_value,
    ShaulaRuntimePathSpan relative_path, ShaulaRuntimeOwnedPath *out_path) {
  static const char fallback_prefix[] = "/tmp/shaula";
  ShaulaRuntimePathSpan override_path;
  ShaulaRuntimePathSpan runtime_dir;

  if (out_path == NULL) {
    return SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT;
  }

  out_path->data = NULL;
  out_path->length = 0;
  if (!span_is_valid(relative_path)) {
    return SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT;
  }

  override_path = env_trimmed_span(override_value);
  if (override_path.length > 0) {
    return copy_span(override_path, out_path);
  }

  runtime_dir = env_trimmed_span(runtime_dir_value);
  if (runtime_dir.length > 0) {
    return join_path(runtime_dir, "/shaula/", relative_path, out_path);
  }

  return join_path(
      (ShaulaRuntimePathSpan){fallback_prefix, sizeof(fallback_prefix) - 1},
      "/", relative_path, out_path);
}

ShaulaRuntimePathStatus
shaula_runtime_path_ensure_parent(ShaulaRuntimePathSpan path) {
  size_t dirname_length;
  char *dirname;

  if (!span_is_valid(path) || span_has_nul(path)) {
    return SHAULA_RUNTIME_PATH_STATUS_INVALID_ARGUMENT;
  }
  if (!parent_length(path, &dirname_length) || dirname_length == 1) {
    return SHAULA_RUNTIME_PATH_STATUS_OK;
  }
  if (dirname_length == SIZE_MAX) {
    return SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY;
  }

  dirname = g_try_malloc(dirname_length + 1);
  if (dirname == NULL) {
    return SHAULA_RUNTIME_PATH_STATUS_OUT_OF_MEMORY;
  }
  memcpy(dirname, path.data, dirname_length);
  dirname[dirname_length] = '\0';

  if (g_mkdir_with_parents(dirname, 0755) != 0) {
    g_free(dirname);
    return SHAULA_RUNTIME_PATH_STATUS_FILESYSTEM_ERROR;
  }

  g_free(dirname);
  return SHAULA_RUNTIME_PATH_STATUS_OK;
}

int32_t shaula_runtime_path_is_capture_artifact(ShaulaRuntimePathSpan path) {
  if (!span_is_valid(path)) {
    return 0;
  }
  if (span_starts_with(path, "/tmp/shaula/captures/")) {
    return 1;
  }
  return span_contains(path, "/shaula/captures/") ? 1 : 0;
}
