#include "tool_lookup.h"

#include <glib.h>
#include <string.h>
#include <unistd.h>

static int span_is_valid(ShaulaRuntimeToolSpan span) {
  return span.data != NULL || span.length == 0;
}

static int checked_add_size(size_t left, size_t right, size_t *out) {
  if (left > SIZE_MAX - right) {
    return 0;
  }
  *out = left + right;
  return 1;
}

static int span_has_nul(ShaulaRuntimeToolSpan span) {
  return span.length > 0 && memchr(span.data, '\0', span.length) != NULL;
}

static int nul_terminated_path_exists(const char *path) {
  return access(path, F_OK) == 0;
}

static ShaulaRuntimeToolLookupStatus
build_candidate(const char *component, size_t component_length,
                ShaulaRuntimeToolSpan tool,
                ShaulaRuntimeToolOwnedPath *out_path) {
  size_t candidate_length;
  size_t allocation_size;
  char *candidate;

  if (!checked_add_size(component_length, (size_t)1, &candidate_length) ||
      !checked_add_size(candidate_length, tool.length, &candidate_length) ||
      !checked_add_size(candidate_length, (size_t)1, &allocation_size)) {
    return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OUT_OF_MEMORY;
  }

  candidate = g_try_malloc(allocation_size);
  if (candidate == NULL) {
    return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OUT_OF_MEMORY;
  }

  memcpy(candidate, component, component_length);
  candidate[component_length] = '/';
  if (tool.length > 0) {
    memcpy(candidate + component_length + 1, tool.data, tool.length);
  }
  candidate[candidate_length] = '\0';

  if (span_has_nul((ShaulaRuntimeToolSpan){candidate, candidate_length}) ||
      !nul_terminated_path_exists(candidate)) {
    g_free(candidate);
    return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND;
  }

  out_path->data = candidate;
  out_path->length = candidate_length;
  return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK;
}

void shaula_runtime_tool_owned_path_clear(ShaulaRuntimeToolOwnedPath *path) {
  if (path == NULL) {
    return;
  }
  g_free(path->data);
  path->data = NULL;
  path->length = 0;
}

int32_t shaula_runtime_tool_path_exists(ShaulaRuntimeToolSpan path) {
  size_t allocation_size;
  char *nul_terminated;
  int exists;

  if (!span_is_valid(path) || path.length == 0 || span_has_nul(path) ||
      !checked_add_size(path.length, (size_t)1, &allocation_size)) {
    return 0;
  }

  nul_terminated = g_try_malloc(allocation_size);
  if (nul_terminated == NULL) {
    return 0;
  }
  memcpy(nul_terminated, path.data, path.length);
  nul_terminated[path.length] = '\0';

  exists = nul_terminated_path_exists(nul_terminated);
  g_free(nul_terminated);
  return exists ? 1 : 0;
}

ShaulaRuntimeToolLookupStatus
shaula_runtime_tool_find_absolute(const ShaulaRuntimeToolSpan *candidates,
                                  size_t candidate_count,
                                  ShaulaRuntimeToolSpan *out_path) {
  size_t index;

  if (out_path == NULL) {
    return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT;
  }
  out_path->data = NULL;
  out_path->length = 0;

  if (candidates == NULL && candidate_count != 0) {
    return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT;
  }

  for (index = 0; index < candidate_count; index += 1) {
    ShaulaRuntimeToolSpan candidate = candidates[index];

    if (!span_is_valid(candidate)) {
      return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT;
    }
    if (candidate.length == 0 || candidate.data[0] != '/') {
      continue;
    }
    if (shaula_runtime_tool_path_exists(candidate)) {
      *out_path = candidate;
      return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK;
    }
  }

  return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND;
}

ShaulaRuntimeToolLookupStatus
shaula_runtime_tool_grim_path(ShaulaRuntimeToolSpan *out_path) {
  static const char usr_bin_grim[] = "/usr/bin/grim";
  static const char bin_grim[] = "/bin/grim";
  static const char usr_local_bin_grim[] = "/usr/local/bin/grim";
  static const ShaulaRuntimeToolSpan candidates[] = {
      {usr_bin_grim, sizeof(usr_bin_grim) - 1},
      {bin_grim, sizeof(bin_grim) - 1},
      {usr_local_bin_grim, sizeof(usr_local_bin_grim) - 1},
  };

  return shaula_runtime_tool_find_absolute(
      candidates, sizeof(candidates) / sizeof(candidates[0]), out_path);
}

ShaulaRuntimeToolLookupStatus
shaula_runtime_tool_find_in_path(const char *path_value,
                                 ShaulaRuntimeToolSpan tool,
                                 ShaulaRuntimeToolOwnedPath *out_path) {
  const char *component_start;
  const char *cursor;

  if (out_path == NULL) {
    return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT;
  }
  out_path->data = NULL;
  out_path->length = 0;

  if (!span_is_valid(tool)) {
    return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_INVALID_ARGUMENT;
  }
  if (path_value == NULL) {
    return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND;
  }

  component_start = path_value;
  cursor = path_value;
  for (;;) {
    if (*cursor == ':' || *cursor == '\0') {
      size_t component_length = (size_t)(cursor - component_start);

      if (component_length > 0) {
        ShaulaRuntimeToolLookupStatus status =
            build_candidate(component_start, component_length, tool, out_path);
        if (status == SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK ||
            status == SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OUT_OF_MEMORY) {
          return status;
        }
      }

      if (*cursor == '\0') {
        break;
      }
      component_start = cursor + 1;
    }
    cursor += 1;
  }

  return SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_NOT_FOUND;
}
