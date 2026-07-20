#include "settings_shortcuts.h"

const char *shaula_settings_shortcut_state_text(ShaulaShortcutState state) {
  switch (state) {
  case SHAULA_SHORTCUT_STATE_ACTIVE:
    return "Enabled and active";
  case SHAULA_SHORTCUT_STATE_PERMISSION_PENDING:
    return "Waiting for desktop approval";
  case SHAULA_SHORTCUT_STATE_PERMISSION_DENIED:
    return "Desktop permission denied";
  case SHAULA_SHORTCUT_STATE_CONFLICT:
    return "Shortcut conflict";
  case SHAULA_SHORTCUT_STATE_UNSUPPORTED:
    return "Automatic global shortcuts unavailable";
  case SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE:
    return "Shortcut service unavailable";
  case SHAULA_SHORTCUT_STATE_RECONNECTING:
    return "Reconnecting";
  case SHAULA_SHORTCUT_STATE_CONFIG_INVALID:
    return "Configuration needs repair";
  default:
    return "Disabled";
  }
}

const char *shaula_settings_shortcut_backend_text(
    ShaulaShortcutBackend backend) {
  switch (backend) {
  case SHAULA_SHORTCUT_BACKEND_PORTAL:
    return "Desktop shortcut service";
  case SHAULA_SHORTCUT_BACKEND_NIRI:
    return "Niri managed keybindings";
  default:
    return "None";
  }
}

gboolean shaula_settings_shortcut_state_is_warning(ShaulaShortcutState state) {
  return state == SHAULA_SHORTCUT_STATE_PERMISSION_DENIED ||
         state == SHAULA_SHORTCUT_STATE_CONFLICT ||
         state == SHAULA_SHORTCUT_STATE_UNSUPPORTED ||
         state == SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE ||
         state == SHAULA_SHORTCUT_STATE_RECONNECTING ||
         state == SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
}

gboolean shaula_settings_shortcut_can_enable(
    const ShaulaShortcutStatus *status) {
  g_return_val_if_fail(status != NULL, FALSE);
  return status->state != SHAULA_SHORTCUT_STATE_ACTIVE &&
         status->state != SHAULA_SHORTCUT_STATE_PERMISSION_PENDING;
}

gboolean shaula_settings_shortcut_can_disable(
    const ShaulaShortcutStatus *status) {
  g_return_val_if_fail(status != NULL, FALSE);
  return !status->setup_completed || status->enabled_requested ||
         status->state == SHAULA_SHORTCUT_STATE_ACTIVE ||
         status->state == SHAULA_SHORTCUT_STATE_PERMISSION_PENDING ||
         status->state == SHAULA_SHORTCUT_STATE_PERMISSION_DENIED ||
         status->state == SHAULA_SHORTCUT_STATE_RECONNECTING;
}

gboolean shaula_settings_shortcut_can_repair(
    const ShaulaShortcutStatus *status) {
  g_return_val_if_fail(status != NULL, FALSE);
  return status->enabled_requested ||
         shaula_settings_shortcut_state_is_warning(status->state);
}

const char *shaula_settings_shortcut_registration_text(
    const ShaulaShortcutStatus *status) {
  g_return_val_if_fail(status != NULL, "Not installed");
  if (status->backend == SHAULA_SHORTCUT_BACKEND_PORTAL)
    return status->autostart_installed
               ? (status->provider_running ? "Registered and running"
                                           : "Registered; provider not running")
               : "Not registered";
  if (status->backend == SHAULA_SHORTCUT_BACKEND_NIRI)
    return status->state == SHAULA_SHORTCUT_STATE_ACTIVE
               ? "Managed keybindings installed"
               : "Managed keybindings not installed";
  return "Not installed";
}
