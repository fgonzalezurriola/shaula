#include "helper_resolution.h"

#include "env.h"
#include "tool_lookup.h"

#include <glib.h>
#include <string.h>

static int nonempty(const char *value) {
  return value != NULL && value[0] != '\0';
}

static char *try_copy(const char *data, size_t length) {
  char *copy;

  if (length == SIZE_MAX) {
    return NULL;
  }
  copy = g_try_malloc(length + 1U);
  if (copy == NULL) {
    return NULL;
  }
  if (length > 0) {
    memcpy(copy, data, length);
  }
  copy[length] = '\0';
  return copy;
}

static char *try_strdup(const char *value) {
  return try_copy(value, strlen(value));
}

char *shaula_executable_current_path(void) {
  return g_file_read_link("/proc/self/exe", NULL);
}

char *shaula_executable_resolve_helper(const char *override_environment_name,
                                       const char *binary_name) {
  ShaulaEnvSpan override = {0};
  g_autofree char *self = NULL;
  g_autofree char *directory = NULL;
  g_autofree char *sibling = NULL;

  if (!nonempty(override_environment_name) || !nonempty(binary_name)) {
    return NULL;
  }

  if (shaula_env_value_trimmed(g_getenv(override_environment_name), &override) ==
      SHAULA_ENV_STATUS_VALID) {
    return try_copy(override.data, override.length);
  }

  self = shaula_executable_current_path();
  if (self != NULL) {
    directory = g_path_get_dirname(self);
    sibling = g_build_filename(directory, binary_name, NULL);
    if (sibling != NULL &&
        shaula_runtime_tool_path_exists((ShaulaRuntimeToolSpan){
            .data = sibling, .length = strlen(sibling)})) {
      return g_steal_pointer(&sibling);
    }
  }

  return try_strdup(binary_name);
}

char *shaula_executable_find_tool(const char *tool_name,
                                  const char *const *absolute_candidates,
                                  size_t candidate_count) {
  ShaulaRuntimeToolOwnedPath path = {0};
  size_t index;

  if (!nonempty(tool_name) ||
      (absolute_candidates == NULL && candidate_count > 0)) {
    return NULL;
  }

  for (index = 0; index < candidate_count; index += 1) {
    const char *candidate = absolute_candidates[index];
    if (!nonempty(candidate) || candidate[0] != '/') {
      continue;
    }
    if (shaula_runtime_tool_path_exists((ShaulaRuntimeToolSpan){
            .data = candidate, .length = strlen(candidate)})) {
      return try_strdup(candidate);
    }
  }

  if (shaula_runtime_tool_find_in_path(
          g_getenv("PATH"),
          (ShaulaRuntimeToolSpan){.data = tool_name,
                                  .length = strlen(tool_name)},
          &path) != SHAULA_RUNTIME_TOOL_LOOKUP_STATUS_OK) {
    shaula_runtime_tool_owned_path_clear(&path);
    return NULL;
  }

  return path.data;
}

char *shaula_executable_find_program(const char *tool_name) {
  return shaula_executable_find_tool(tool_name, NULL, 0);
}

char *shaula_executable_find_grim(void) {
  static const char *const candidates[] = {
      "/usr/bin/grim",
      "/bin/grim",
      "/usr/local/bin/grim",
  };
  return shaula_executable_find_tool("grim", candidates,
                                     G_N_ELEMENTS(candidates));
}
