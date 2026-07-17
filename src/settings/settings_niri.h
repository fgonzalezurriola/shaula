#ifndef SHAULA_SETTINGS_NIRI_H
#define SHAULA_SETTINGS_NIRI_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  gboolean detected;
  char *config_path;
  gboolean shortcuts_installed;
  gboolean shortcuts_conflict;
} ShaulaSettingsNiriStatus;

typedef enum {
  SHAULA_SETTINGS_NIRI_OK,
  SHAULA_SETTINGS_NIRI_COMMAND_FAILED,
  SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID,
} ShaulaSettingsNiriResult;

/* Initializes or releases the owned config path. Clear is idempotent. */
void shaula_settings_niri_status_init(ShaulaSettingsNiriStatus *status);
void shaula_settings_niri_status_clear(ShaulaSettingsNiriStatus *status);

/* Settings Niri protocol boundary. shaula_bin and status are required;
 * error_text is optional. The binary path is borrowed. On failure, error_text
 * receives a GLib-owned diagnostic suitable for the GTK adapter. Status
 * parsing is strict so malformed helper JSON cannot silently present a
 * safe-to-install state.
 */
ShaulaSettingsNiriResult shaula_settings_niri_load(
    const char *shaula_bin, ShaulaSettingsNiriStatus *status,
    char **error_text);
ShaulaSettingsNiriResult shaula_settings_niri_install(
    const char *shaula_bin, gboolean force, char **error_text);
ShaulaSettingsNiriResult shaula_settings_niri_remove(const char *shaula_bin,
                                                     char **error_text);

/* Parses the Config save result without exposing its JSON grammar to GTK.
 * json and changed are required; changed is initialized on every outcome.
 */
ShaulaSettingsNiriResult shaula_settings_niri_rule_changed(const char *json,
                                                           gboolean *changed);

G_END_DECLS

#endif
