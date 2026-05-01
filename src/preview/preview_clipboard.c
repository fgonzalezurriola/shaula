#include "preview_clipboard.h"

#include <sys/wait.h>

static GQuark clipboard_error_quark(void) {
  return g_quark_from_static_string("shaula-preview-clipboard");
}

/* Runtime boundary: copy PNG bytes to the Wayland clipboard via wl-copy.
 * The helper reports failure through GError so JSON never claims success after
 * an unavailable clipboard or rejected subprocess.
 */
gboolean shaula_clipboard_copy_png_file(const char *path, GError **error) {
  if (path == NULL || path[0] == '\0') {
    g_set_error(error, clipboard_error_quark(), 1, "missing PNG path");
    return FALSE;
  }
  gchar *quoted = g_shell_quote(path);
  gchar *command = g_strdup_printf("wl-copy --type image/png < %s", quoted);
  int status = 1;
  gboolean spawned =
      g_spawn_command_line_sync(command, NULL, NULL, &status, error);
  g_free(command);
  g_free(quoted);
  if (!spawned)
    return FALSE;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    g_set_error(error, clipboard_error_quark(), 2,
                "wl-copy failed with status %d", status);
    return FALSE;
  }
  return TRUE;
}

gboolean shaula_clipboard_copy_text(const char *text, GError **error) {
  gchar *quoted = g_shell_quote(text != NULL ? text : "");
  gchar *command =
      g_strdup_printf("printf %%s %s | wl-copy --type text/plain", quoted);
  int status = 1;
  gboolean ok = g_spawn_command_line_sync(command, NULL, NULL, &status, error);
  g_free(command);
  g_free(quoted);
  if (!ok)
    return FALSE;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    g_set_error(error, clipboard_error_quark(), 3,
                "wl-copy text failed with status %d", status);
    return FALSE;
  }
  return TRUE;
}
