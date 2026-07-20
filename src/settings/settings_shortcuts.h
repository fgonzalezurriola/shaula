#ifndef SHAULA_SETTINGS_SHORTCUTS_H
#define SHAULA_SETTINGS_SHORTCUTS_H

#include "shortcuts/shortcuts.h"

#include <glib.h>

const char *shaula_settings_shortcut_state_text(ShaulaShortcutState state);
const char *shaula_settings_shortcut_backend_text(ShaulaShortcutBackend backend);
gboolean shaula_settings_shortcut_state_is_warning(ShaulaShortcutState state);
gboolean shaula_settings_shortcut_can_enable(const ShaulaShortcutStatus *status);
gboolean shaula_settings_shortcut_can_disable(const ShaulaShortcutStatus *status);
gboolean shaula_settings_shortcut_can_repair(const ShaulaShortcutStatus *status);
const char *shaula_settings_shortcut_registration_text(
    const ShaulaShortcutStatus *status);

#endif
