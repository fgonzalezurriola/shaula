#include "clipboard.h"

#include "runtime/helper_resolution.h"
#include "runtime/process_exec.h"

#include <glib.h>
#include <string.h>

#define SHAULA_CLIPBOARD_MAX_PNG_BYTES (128U * 1024U * 1024U)
#define SHAULA_CLIPBOARD_MAX_TEXT_BYTES (4U * 1024U * 1024U)
#define SHAULA_CLIPBOARD_READY_LINE "READY shaula-clipboard/1\n"
#define SHAULA_PROVIDER_EXIT_UNAVAILABLE 35U

static gboolean env_flag_disabled(const char *name) {
  const char *value = g_getenv(name);
  return value != NULL &&
         (g_str_equal(value, "0") || g_ascii_strcasecmp(value, "false") == 0 ||
          g_ascii_strcasecmp(value, "no") == 0);
}

static guint ready_timeout_ms(void) {
  const char *value = g_getenv("SHAULA_CLIPBOARD_READY_TIMEOUT_MS");
  if (value == NULL || *value == '\0')
    return 5000U;
  char *end = NULL;
  guint64 parsed = g_ascii_strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0U || parsed > 60000U)
    return 5000U;
  return (guint)parsed;
}

static gboolean process_status_is_spawn_failure(ShaulaProcessStatus status) {
  switch (status) {
  case SHAULA_PROCESS_STATUS_FILE_NOT_FOUND:
  case SHAULA_PROCESS_STATUS_ACCESS_DENIED:
  case SHAULA_PROCESS_STATUS_INVALID_EXECUTABLE:
  case SHAULA_PROCESS_STATUS_IS_DIRECTORY:
  case SHAULA_PROCESS_STATUS_NOT_DIRECTORY:
  case SHAULA_PROCESS_STATUS_FILE_BUSY:
  case SHAULA_PROCESS_STATUS_SYMLINK_LOOP:
  case SHAULA_PROCESS_STATUS_FD_QUOTA:
  case SHAULA_PROCESS_STATUS_RESOURCE_LIMIT:
  case SHAULA_PROCESS_STATUS_SPAWN_ERROR:
  case SHAULA_PROCESS_STATUS_NAME_TOO_LONG:
  case SHAULA_PROCESS_STATUS_FILESYSTEM_ERROR:
  case SHAULA_PROCESS_STATUS_PROCESS_FD_QUOTA:
  case SHAULA_PROCESS_STATUS_SYSTEM_RESOURCES:
  case SHAULA_PROCESS_STATUS_PERMISSION_DENIED:
    return TRUE;
  case SHAULA_PROCESS_STATUS_OK:
  case SHAULA_PROCESS_STATUS_INVALID_ARGUMENT:
  case SHAULA_PROCESS_STATUS_OUT_OF_MEMORY:
  case SHAULA_PROCESS_STATUS_STREAM_TOO_LONG:
  case SHAULA_PROCESS_STATUS_IO_ERROR:
    return FALSE;
  }
  return FALSE;
}

/*
 * Publish through the shared detached-provider process boundary. Every failed
 * readiness outcome returns only after the provider has been terminated and
 * reaped, so a timed-out or malformed child cannot later claim the clipboard.
 * Successful providers are orphaned by the short-lived launcher and therefore
 * remain alive after the initiating CLI or Preview process exits.
 */
static ShaulaClipboardStatus publish_bytes(const char *mime,
                                           const guint8 *data, gsize length,
                                           gsize maximum_length) {
  if (mime == NULL || data == NULL || length == 0U)
    return SHAULA_CLIPBOARD_STATUS_INVALID_ARGUMENT;
  if (length > maximum_length)
    return SHAULA_CLIPBOARD_STATUS_PAYLOAD_TOO_LARGE;
  if (env_flag_disabled("SHAULA_CLIPBOARD_AVAILABLE"))
    return SHAULA_CLIPBOARD_STATUS_UNAVAILABLE;

  g_autofree char *helper = shaula_executable_resolve_helper(
      "SHAULA_CLIPBOARD_PROVIDER_BIN", "shaula-clipboard-provider");
  if (helper == NULL)
    return SHAULA_CLIPBOARD_STATUS_UNAVAILABLE;

  g_autofree char *header = g_strdup_printf(
      "SHAULA-CLIPBOARD/1\nmime:%s\nlength:%" G_GSIZE_FORMAT "\n\n", mime,
      length);
  if (header == NULL)
    return SHAULA_CLIPBOARD_STATUS_IO_FAILED;

  const char *argv[] = {helper, NULL};
  const ShaulaProcessInputChunk input[] = {
      {.data = header, .length = strlen(header)},
      {.data = data, .length = length},
  };
  ShaulaProcessReadyResult result = {0};
  ShaulaProcessStatus process_status = shaula_process_spawn_ready_detached(
      argv, input, G_N_ELEMENTS(input), SHAULA_CLIPBOARD_READY_LINE,
      strlen(SHAULA_CLIPBOARD_READY_LINE), ready_timeout_ms(), &result);
  if (process_status != SHAULA_PROCESS_STATUS_OK) {
    return process_status_is_spawn_failure(process_status)
               ? SHAULA_CLIPBOARD_STATUS_UNAVAILABLE
               : SHAULA_CLIPBOARD_STATUS_IO_FAILED;
  }

  switch (result.ready_status) {
  case SHAULA_PROCESS_READY_OK:
    return SHAULA_CLIPBOARD_STATUS_OK;
  case SHAULA_PROCESS_READY_TIMEOUT:
    return SHAULA_CLIPBOARD_STATUS_TIMEOUT;
  case SHAULA_PROCESS_READY_PROTOCOL_INVALID:
    return SHAULA_CLIPBOARD_STATUS_PROTOCOL_INVALID;
  case SHAULA_PROCESS_READY_CHILD_EXITED:
    if (result.term_kind == SHAULA_PROCESS_TERM_EXITED &&
        result.term_value == SHAULA_PROVIDER_EXIT_UNAVAILABLE)
      return SHAULA_CLIPBOARD_STATUS_UNAVAILABLE;
    return SHAULA_CLIPBOARD_STATUS_PROVIDER_FAILED;
  case SHAULA_PROCESS_READY_IO_ERROR:
    return SHAULA_CLIPBOARD_STATUS_IO_FAILED;
  }
  return SHAULA_CLIPBOARD_STATUS_PROVIDER_FAILED;
}

ShaulaClipboardStatus shaula_clipboard_publish_png_file(const char *path) {
  if (path == NULL || *path == '\0')
    return SHAULA_CLIPBOARD_STATUS_INVALID_ARGUMENT;
  g_autofree gchar *contents = NULL;
  gsize length = 0U;
  if (!g_file_get_contents(path, &contents, &length, NULL))
    return SHAULA_CLIPBOARD_STATUS_READ_FAILED;
  if (length == 0U)
    return SHAULA_CLIPBOARD_STATUS_READ_FAILED;
  return publish_bytes("image/png", (const guint8 *)contents, length,
                       SHAULA_CLIPBOARD_MAX_PNG_BYTES);
}

ShaulaClipboardStatus shaula_clipboard_publish_text(const char *text,
                                                    size_t length) {
  if (text == NULL || length == 0U ||
      !g_utf8_validate(text, (gssize)length, NULL))
    return SHAULA_CLIPBOARD_STATUS_INVALID_ARGUMENT;
  return publish_bytes("text/plain;charset=utf-8", (const guint8 *)text,
                       (gsize)length, SHAULA_CLIPBOARD_MAX_TEXT_BYTES);
}

int32_t shaula_clipboard_provider_available(void) {
  if (env_flag_disabled("SHAULA_CLIPBOARD_AVAILABLE"))
    return 0;
  g_autofree char *helper = shaula_executable_resolve_helper(
      "SHAULA_CLIPBOARD_PROVIDER_BIN", "shaula-clipboard-provider");
  if (helper == NULL)
    return 0;
  if (strchr(helper, G_DIR_SEPARATOR) != NULL)
    return g_file_test(helper, G_FILE_TEST_IS_EXECUTABLE) ? 1 : 0;
  g_autofree char *resolved = g_find_program_in_path(helper);
  return resolved != NULL ? 1 : 0;
}

const char *shaula_clipboard_status_token(ShaulaClipboardStatus status) {
  switch (status) {
  case SHAULA_CLIPBOARD_STATUS_OK:
    return "ok";
  case SHAULA_CLIPBOARD_STATUS_INVALID_ARGUMENT:
    return "invalid_argument";
  case SHAULA_CLIPBOARD_STATUS_UNAVAILABLE:
    return "unavailable";
  case SHAULA_CLIPBOARD_STATUS_READ_FAILED:
    return "read_failed";
  case SHAULA_CLIPBOARD_STATUS_PAYLOAD_TOO_LARGE:
    return "payload_too_large";
  case SHAULA_CLIPBOARD_STATUS_SPAWN_FAILED:
    return "spawn_failed";
  case SHAULA_CLIPBOARD_STATUS_IO_FAILED:
    return "io_failed";
  case SHAULA_CLIPBOARD_STATUS_TIMEOUT:
    return "timeout";
  case SHAULA_CLIPBOARD_STATUS_PROTOCOL_INVALID:
    return "protocol_invalid";
  case SHAULA_CLIPBOARD_STATUS_PROVIDER_FAILED:
    return "provider_failed";
  default:
    return "unknown";
  }
}
