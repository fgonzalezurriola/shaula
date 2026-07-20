#ifndef SHAULA_SETTINGS_NIRI_H
#define SHAULA_SETTINGS_NIRI_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  SHAULA_SETTINGS_NIRI_OK,
  SHAULA_SETTINGS_NIRI_PROTOCOL_INVALID,
} ShaulaSettingsNiriResult;

/* Parses the Config save result without exposing its JSON grammar to GTK.
 * json and changed are required; changed is initialized on every outcome.
 */
ShaulaSettingsNiriResult shaula_settings_niri_rule_changed(const char *json,
                                                           gboolean *changed);

G_END_DECLS

#endif
