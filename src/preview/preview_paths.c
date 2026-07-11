#include "preview_paths.h"

#include <glib/gstdio.h>
#include <string.h>

static gboolean path_has_prefix_dir(const char *path, const char *dir) {
  if (path == NULL || dir == NULL || dir[0] == '\0')
    return FALSE;
  gsize len = strlen(dir);
  return g_str_has_prefix(path, dir) &&
         (path[len] == '\0' || path[len] == G_DIR_SEPARATOR);
}

/* Mirrors the runtime/paths.{c,h} capture-artifact contract on the C helper side.
 * This contract decides whether preview owns cleanup of a helper artifact.
 */
gboolean shaula_preview_path_is_temporary_capture(const char *path) {
  if (path == NULL)
    return TRUE;
  if (g_str_has_prefix(path, "/tmp/shaula/captures/"))
    return TRUE;
  const char *runtime_dir = g_getenv("XDG_RUNTIME_DIR");
  if (runtime_dir != NULL && runtime_dir[0] != '\0') {
    char *runtime_captures =
        g_build_filename(runtime_dir, "shaula", "captures", NULL);
    gboolean temporary = path_has_prefix_dir(path, runtime_captures);
    g_free(runtime_captures);
    if (temporary)
      return TRUE;
  }
  return FALSE;
}
