#include "settings_process.h"

#include "process_exec.h"

gboolean shaula_settings_run_command(char *const *argv, gchar **stdout_text,
                                     gchar **stderr_text, int *exit_code) {
  g_return_val_if_fail(argv != NULL && argv[0] != NULL, FALSE);

  if (stdout_text != NULL)
    g_clear_pointer(stdout_text, g_free);
  if (stderr_text != NULL)
    g_clear_pointer(stderr_text, g_free);

  int runtime_exit_code = 127;
  ShaulaProcessStatus status = shaula_process_run_sync(
      (const char *const *)argv, NULL, stdout_text, stderr_text,
      &runtime_exit_code);
  if (status != SHAULA_PROCESS_STATUS_OK) {
    if (stderr_text != NULL)
      *stderr_text = g_strdup_printf("could not start %s", argv[0]);
    if (exit_code != NULL)
      *exit_code = 127;
    return FALSE;
  }
  if (exit_code != NULL)
    *exit_code = runtime_exit_code == 0 ? 0 : 1;
  return TRUE;
}
