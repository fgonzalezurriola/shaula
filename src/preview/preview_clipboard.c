#include "preview_clipboard.h"

#include <gio/gio.h>
#include <string.h>

#define SHAULA_PREVIEW_CLIPBOARD_ERROR_DOMAIN "shaula-preview-clipboard"

enum {
  SHAULA_PREVIEW_CLIPBOARD_ERROR_MISSING_PNG_PATH = 1,
  SHAULA_PREVIEW_CLIPBOARD_ERROR_COPY_IMAGE_FAILED = 2,
  SHAULA_PREVIEW_CLIPBOARD_ERROR_COPY_TEXT_FAILED = 3,
};

static GQuark shaula_preview_clipboard_error_quark(void) {
  return g_quark_from_static_string(SHAULA_PREVIEW_CLIPBOARD_ERROR_DOMAIN);
}

static void set_clipboard_error(GError **error, int code, const char *message) {
  g_set_error_literal(error, shaula_preview_clipboard_error_quark(), code,
                      message);
}

/* Returns a GLib-owned executable path; the caller releases it with g_free().
 */
static char *resolve_shaula_cli(void) {
  g_autofree char *executable = g_file_read_link("/proc/self/exe", NULL);
  if (executable != NULL) {
    g_autofree char *directory = g_path_get_dirname(executable);
    g_autofree char *candidate = g_build_filename(directory, "shaula", NULL);
    if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE))
      return g_steal_pointer(&candidate);
  }

  return g_strdup("shaula");
}

/*
 * Runs one synchronous process boundary without exposing child stdout. This is
 * an intentional hardening over the old Zig spawn flags: nested Shaula `--json`
 * output must not leak into the Preview helper's own stdout contract.
 */
static gboolean run_subprocess(const char *const *argv, GSubprocessFlags flags,
                               GBytes *stdin_bytes, gboolean capture_stderr,
                               int failure_code, const char *failure_message,
                               GError **error) {
  g_autoptr(GSubprocess) subprocess = g_subprocess_newv(argv, flags, error);
  if (subprocess == NULL)
    return FALSE;

  g_autoptr(GBytes) stderr_bytes = NULL;
  g_autoptr(GError) communicate_error = NULL;
  if (!g_subprocess_communicate(subprocess, stdin_bytes, NULL, NULL,
                                capture_stderr ? &stderr_bytes : NULL,
                                &communicate_error)) {
    g_propagate_error(error, g_steal_pointer(&communicate_error));
    return FALSE;
  }

  if (!g_subprocess_get_successful(subprocess)) {
    set_clipboard_error(error, failure_code, failure_message);
    return FALSE;
  }

  return TRUE;
}

gboolean shaula_clipboard_copy_png_file(const char *path, GError **error) {
  if (path == NULL || path[0] == '\0') {
    set_clipboard_error(error, SHAULA_PREVIEW_CLIPBOARD_ERROR_MISSING_PNG_PATH,
                        "missing PNG path");
    return FALSE;
  }

  g_autofree char *shaula = resolve_shaula_cli();
  const char *argv[] = {shaula, "clipboard", "copy-image", "--input",
                        path,   "--json",    NULL};
  return run_subprocess(
      argv, G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
      NULL, TRUE, SHAULA_PREVIEW_CLIPBOARD_ERROR_COPY_IMAGE_FAILED,
      "shaula clipboard copy-image failed", error);
}

gboolean shaula_clipboard_copy_text(const char *text, GError **error) {
  const char *input = text != NULL ? text : "";
  g_autoptr(GBytes) stdin_bytes = g_bytes_new(input, strlen(input));
  const char *argv[] = {"wl-copy", "--type", "text/plain", NULL};
  return run_subprocess(
      argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
      stdin_bytes, FALSE, SHAULA_PREVIEW_CLIPBOARD_ERROR_COPY_TEXT_FAILED,
      "wl-copy text failed", error);
}
