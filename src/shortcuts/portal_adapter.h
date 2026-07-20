#ifndef SHAULA_SHORTCUTS_PORTAL_ADAPTER_H
#define SHAULA_SHORTCUTS_PORTAL_ADAPTER_H

#include "shortcuts.h"

ShaulaShortcutResult
shaula_shortcut_portal_query(ShaulaShortcutStatus *status, char **error_text);
ShaulaShortcutResult
shaula_shortcut_portal_enable(const ShaulaShortcutOptions *options,
                              ShaulaShortcutStatus *status, char **error_text);
ShaulaShortcutResult
shaula_shortcut_portal_disable(const ShaulaShortcutOptions *options,
                               ShaulaShortcutStatus *status, char **error_text);
ShaulaShortcutResult
shaula_shortcut_portal_repair(const ShaulaShortcutOptions *options,
                              ShaulaShortcutStatus *status, char **error_text);

#endif
