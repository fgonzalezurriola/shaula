#include "preview_properties_hud.h"

#include <string.h>

void shaula_properties_hud_state_init(ShaulaPropertiesHudState *hud) {
  if (hud == NULL)
    return;
  memset(hud, 0, sizeof(*hud));
  hud->active_panel = SHAULA_PROPERTIES_PANEL_NONE;
  hud->spotlight_index = -1;
}

gboolean shaula_properties_hud_set_panel(ShaulaPropertiesHudState *hud,
                                         ShaulaPropertiesPanel panel) {
  if (hud == NULL || hud->active_panel == panel)
    return FALSE;
  hud->active_panel = panel;
  if (panel != SHAULA_PROPERTIES_PANEL_SPOTLIGHT)
    hud->spotlight_index = -1;
  return TRUE;
}
