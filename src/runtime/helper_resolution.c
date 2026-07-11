#include "helper_resolution.h"

#include "env.h"
#include "tool_lookup.h"

#include <glib.h>
#include <string.h>

static int span_is_valid(ShaulaRuntimeHelperSpan span) {
  return span.data != NULL || span.length == 0;
}

static int checked_add_size(size_t left, size_t right, size_t *out) {
  if (left > SIZE_MAX - right) {
    return 0;
  }
  *out = left + right;
  return 1;
}

static ShaulaRuntimeHelperStatus
copy_span(ShaulaRuntimeHelperSpan value,
          ShaulaRuntimeHelperOwnedPath *out_path) {
  size_t allocation_size;
  char *copy;

  if (!span_is_valid(value)) {
    return SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT;
  }
  if (!checked_add_size(value.length, (size_t)1, &allocation_size)) {
    return SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY;
  }

  copy = g_try_malloc(allocation_size);
  if (copy == NULL) {
    return SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY;
  }
  if (value.length > 0) {
    memcpy(copy, value.data, value.length);
  }
  copy[value.length] = '\0';

  out_path->data = copy;
  out_path->length = value.length;
  return SHAULA_RUNTIME_HELPER_STATUS_OK;
}

static ShaulaRuntimeHelperStatus
join_sibling(ShaulaRuntimeHelperSpan executable_dir,
             ShaulaRuntimeHelperSpan binary_name,
             ShaulaRuntimeHelperOwnedPath *out_path) {
  size_t joined_length;
  size_t allocation_size;
  char *joined;

  if (!checked_add_size(executable_dir.length, (size_t)1, &joined_length) ||
      !checked_add_size(joined_length, binary_name.length, &joined_length) ||
      !checked_add_size(joined_length, (size_t)1, &allocation_size)) {
    return SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY;
  }

  joined = g_try_malloc(allocation_size);
  if (joined == NULL) {
    return SHAULA_RUNTIME_HELPER_STATUS_OUT_OF_MEMORY;
  }

  memcpy(joined, executable_dir.data, executable_dir.length);
  joined[executable_dir.length] = '/';
  if (binary_name.length > 0) {
    memcpy(joined + executable_dir.length + 1, binary_name.data,
           binary_name.length);
  }
  joined[joined_length] = '\0';

  out_path->data = joined;
  out_path->length = joined_length;
  return SHAULA_RUNTIME_HELPER_STATUS_OK;
}

void shaula_runtime_helper_owned_path_clear(
    ShaulaRuntimeHelperOwnedPath *path) {
  if (path == NULL) {
    return;
  }
  g_free(path->data);
  path->data = NULL;
  path->length = 0;
}

ShaulaRuntimeHelperStatus
shaula_runtime_helper_resolve(const char *override_value,
                              ShaulaRuntimeHelperSpan executable_dir,
                              ShaulaRuntimeHelperSpan binary_name,
                              ShaulaRuntimeHelperOwnedPath *out_path) {
  ShaulaEnvSpan override_path = {0};
  ShaulaRuntimeHelperStatus status;

  if (out_path == NULL) {
    return SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT;
  }
  out_path->data = NULL;
  out_path->length = 0;

  if (!span_is_valid(executable_dir) || !span_is_valid(binary_name)) {
    return SHAULA_RUNTIME_HELPER_STATUS_INVALID_ARGUMENT;
  }

  if (shaula_env_value_trimmed(override_value, &override_path) ==
      SHAULA_ENV_STATUS_VALID) {
    return copy_span(
        (ShaulaRuntimeHelperSpan){override_path.data, override_path.length},
        out_path);
  }

  if (executable_dir.length > 0) {
    status = join_sibling(executable_dir, binary_name, out_path);
    if (status != SHAULA_RUNTIME_HELPER_STATUS_OK) {
      return status;
    }

    if (shaula_runtime_tool_path_exists(
            (ShaulaRuntimeToolSpan){out_path->data, out_path->length})) {
      return SHAULA_RUNTIME_HELPER_STATUS_OK;
    }
    shaula_runtime_helper_owned_path_clear(out_path);
  }

  return copy_span(binary_name, out_path);
}
