#ifndef SHAULA_SHORTCUTS_NIRI_ADAPTER_H
#define SHAULA_SHORTCUTS_NIRI_ADAPTER_H

#include "shortcuts.h"

ShaulaShortcutResult
shaula_shortcut_niri_query(ShaulaShortcutStatus *status, char **error_text);
ShaulaShortcutResult
shaula_shortcut_niri_enable(const ShaulaShortcutOptions *options,
                            ShaulaShortcutStatus *status, char **error_text);
ShaulaShortcutResult
shaula_shortcut_niri_disable(const ShaulaShortcutOptions *options,
                             ShaulaShortcutStatus *status, char **error_text);

#endif

