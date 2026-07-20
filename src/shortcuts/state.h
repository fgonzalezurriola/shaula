#ifndef SHAULA_SHORTCUTS_STATE_H
#define SHAULA_SHORTCUTS_STATE_H

#include "shortcuts.h"

#include <glib.h>

typedef struct {
  gboolean completed;
  ShaulaShortcutChoice choice;
  ShaulaShortcutBackend backend;
} ShaulaShortcutSetupState;

typedef struct {
  ShaulaShortcutState state;
  guint portal_version;
  gboolean activation_ready;
  char *detail;
  char *error_code;
  char *triggers[4];
} ShaulaShortcutProviderState;

char *shaula_shortcut_setup_state_path(void);
char *shaula_shortcut_provider_state_path(void);
char *shaula_shortcut_autostart_path(void);

void shaula_shortcut_setup_state_init(ShaulaShortcutSetupState *state);
gboolean shaula_shortcut_setup_state_load(ShaulaShortcutSetupState *state,
                                          GError **error);
gboolean shaula_shortcut_setup_state_save(
    const ShaulaShortcutSetupState *state, gboolean dry_run, GError **error);
gboolean shaula_shortcut_setup_state_remove(gboolean dry_run, GError **error);

void shaula_shortcut_provider_state_init(ShaulaShortcutProviderState *state);
void shaula_shortcut_provider_state_clear(ShaulaShortcutProviderState *state);
gboolean shaula_shortcut_provider_state_load(ShaulaShortcutProviderState *state,
                                             GError **error);
gboolean shaula_shortcut_provider_state_save(
    const ShaulaShortcutProviderState *state, GError **error);
gboolean shaula_shortcut_provider_state_remove(gboolean dry_run,
                                               GError **error);

/* Writes a user-owned XDG autostart entry with an absolute provider path. */
gboolean shaula_shortcut_autostart_install(const char *provider_path,
                                           gboolean dry_run, gboolean *changed,
                                           GError **error);
gboolean shaula_shortcut_autostart_remove(gboolean dry_run, gboolean *changed,
                                          GError **error);
gboolean shaula_shortcut_autostart_installed(void);

#endif
