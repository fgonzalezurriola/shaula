#include "preview_clipboard.h"

#include <sys/wait.h>

static GQuark clipboard_error_quark(void) {
  return g_quark_from_static_string("shaula-preview-clipboard");
}

static char *resolve_shaula_cli(void) {
  char *exe = g_file_read_link("/proc/self/exe", NULL);
  if (exe != NULL) {
    char *dir = g_path_get_dirname(exe);
    char *candidate = g_build_filename(dir, "shaula", NULL);
    g_free(dir);
    g_free(exe);
    if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE))
      return candidate;
    g_free(candidate);
  }
  return g_strdup("shaula");
}

/* Runtime boundary: delegate PNG clipboard writes to Shaula's clipboard command.
 * This keeps Preview on the same byte-writing and ERR_* contract as capture.
 */
gboolean shaula_clipboard_copy_png_file(const char *path, GError **error) {
  if (path == NULL || path[0] == '\0') {
    g_set_error(error, clipboard_error_quark(), 1, "missing PNG path");
    return FALSE;
  }

  char *shaula = resolve_shaula_cli();
  gchar *argv[] = {shaula, "clipboard", "copy-image", "--input", (gchar *)path,
                   "--json", NULL};
  gchar *stderr_text = NULL;
  int status = 1;
  gboolean spawned =
      g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
                   &stderr_text, &status, error);
  g_free(shaula);
  if (!spawned) {
    g_free(stderr_text);
    return FALSE;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    g_strchomp(stderr_text);
    g_set_error(error, clipboard_error_quark(), 2,
                "shaula clipboard copy-image failed with status %d%s%s",
                status, stderr_text != NULL && stderr_text[0] != '\0' ? ": " : "",
                stderr_text != NULL ? stderr_text : "");
    g_free(stderr_text);
    return FALSE;
  }
  g_free(stderr_text);
  return TRUE;
}

/* Runtime boundary: delegate text clipboard writes to wl-copy.
 * g_spawn_command_line_sync does not interpret shell pipe operators, so the
 * pipeline must be passed through /bin/sh -c.
 */
gboolean shaula_clipboard_copy_text(const char *text, GError **error) {
  gchar *quoted = g_shell_quote(text != NULL ? text : "");
  gchar *shell_cmd =
      g_strdup_printf("printf %%s %s | wl-copy --type text/plain", quoted);
  gchar *argv[] = {"/bin/sh", "-c", shell_cmd, NULL};
  int status = 1;
  gboolean ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                              NULL, NULL, &status, error);
  g_free(shell_cmd);
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
