#ifndef SHAULA_RUNTIME_HELPER_RESOLUTION_H
#define SHAULA_RUNTIME_HELPER_RESOLUTION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Executable discovery is the sole runtime seam for helper and external-tool
 * resolution. Successful results are GLib-owned NUL-terminated strings released
 * with g_free(); NULL means invalid input, allocation failure, or not found.
 *
 * Helpers resolve in this order: nonempty ASCII-trimmed environment override,
 * an existing sibling of /proc/self/exe, then the bare binary name. The bare
 * name deliberately defers PATH lookup and failure mapping to process execution.
 *
 * Tools resolve in this order: caller-provided absolute candidates, then the
 * current parent PATH. Existence checks preserve Shaula's historical behavior:
 * executable permission is not required during discovery.
 */
char *shaula_executable_resolve_helper(const char *override_environment_name,
                                       const char *binary_name);

char *shaula_executable_find_tool(const char *tool_name,
                                  const char *const *absolute_candidates,
                                  size_t candidate_count);

char *shaula_executable_current_path(void);
char *shaula_executable_find_program(const char *tool_name);
char *shaula_executable_find_grim(void);

#ifdef __cplusplus
}
#endif

#endif
