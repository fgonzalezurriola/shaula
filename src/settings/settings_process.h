#ifndef SHAULA_SETTINGS_PROCESS_H
#define SHAULA_SETTINGS_PROCESS_H

#include <glib.h>

#include "settings_config.h"

G_BEGIN_DECLS

/*
 * Builds the exact `shaula config save --json` argv used by the native Settings
 * helper. The returned vector and every element are GLib-owned; release them
 * with g_strfreev(). shaula_bin and config are borrowed for the call only.
 */
char **shaula_settings_build_save_argv(const char *shaula_bin,
                                       const ShaulaSettingsConfig *config)
    G_GNUC_WARN_UNUSED_RESULT;

/*
 * Runs one synchronous argv boundary without shell interpretation. Existing
 * GLib-owned values in stdout_text and stderr_text are cleared before use, and
 * the outputs receive new GLib-owned buffers when requested. Spawn failures map
 * to exit code 127 and copy the GLib spawn message into stderr_text; completed
 * nonzero or signalled children map to exit code 1, preserving the existing GTK
 * Settings adapter contract.
 */
gboolean shaula_settings_run_command(char *const *argv, gchar **stdout_text,
                                     gchar **stderr_text, int *exit_code);

G_END_DECLS

#endif
