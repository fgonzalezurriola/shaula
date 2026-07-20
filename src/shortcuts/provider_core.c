#include "provider_core.h"

#include <string.h>

void shaula_shortcut_provider_gate_init(ShaulaShortcutProviderGate *gate) {
  g_return_if_fail(gate != NULL);
  *gate = (ShaulaShortcutProviderGate){0};
}

void shaula_shortcut_provider_gate_clear(ShaulaShortcutProviderGate *gate) {
  if (gate == NULL)
    return;
  g_clear_pointer(&gate->active_shortcut_id, g_free);
  shaula_shortcut_provider_gate_init(gate);
}

ShaulaShortcutAction shaula_shortcut_action_from_id(const char *shortcut_id) {
  if (g_strcmp0(shortcut_id, "quick") == 0)
    return SHAULA_SHORTCUT_ACTION_QUICK;
  if (g_strcmp0(shortcut_id, "area") == 0)
    return SHAULA_SHORTCUT_ACTION_AREA;
  if (g_strcmp0(shortcut_id, "fullscreen") == 0)
    return SHAULA_SHORTCUT_ACTION_FULLSCREEN;
  if (g_strcmp0(shortcut_id, "all_screens") == 0)
    return SHAULA_SHORTCUT_ACTION_ALL_SCREENS;
  return SHAULA_SHORTCUT_ACTION_INVALID;
}

const char *shaula_shortcut_action_id(ShaulaShortcutAction action) {
  switch (action) {
  case SHAULA_SHORTCUT_ACTION_QUICK:
    return "quick";
  case SHAULA_SHORTCUT_ACTION_AREA:
    return "area";
  case SHAULA_SHORTCUT_ACTION_FULLSCREEN:
    return "fullscreen";
  case SHAULA_SHORTCUT_ACTION_ALL_SCREENS:
    return "all_screens";
  default:
    return NULL;
  }
}

char **shaula_shortcut_action_argv(const char *shaula_binary,
                                  ShaulaShortcutAction action) {
  if (shaula_binary == NULL || shaula_binary[0] == '\0')
    return NULL;
  const char *mode = NULL;
  gboolean save = FALSE;
  switch (action) {
  case SHAULA_SHORTCUT_ACTION_QUICK:
    mode = "quick";
    break;
  case SHAULA_SHORTCUT_ACTION_AREA:
    mode = "area";
    break;
  case SHAULA_SHORTCUT_ACTION_FULLSCREEN:
    mode = "fullscreen";
    save = TRUE;
    break;
  case SHAULA_SHORTCUT_ACTION_ALL_SCREENS:
    mode = "all-screens";
    save = TRUE;
    break;
  default:
    return NULL;
  }

  char **argv = g_new0(char *, save ? 7U : 6U);
  argv[0] = g_strdup(shaula_binary);
  argv[1] = g_strdup("capture");
  argv[2] = g_strdup(mode);
  argv[3] = g_strdup("--json");
  if (save)
    argv[4] = g_strdup("--save");
  return argv;
}

gboolean shaula_shortcut_provider_should_dispatch(
    ShaulaShortcutProviderGate *gate, const char *shortcut_id,
    guint64 timestamp) {
  g_return_val_if_fail(gate != NULL, FALSE);
  if (shaula_shortcut_action_from_id(shortcut_id) ==
          SHAULA_SHORTCUT_ACTION_INVALID ||
      gate->capture_running ||
      g_strcmp0(gate->active_shortcut_id, shortcut_id) == 0)
    return FALSE;
  g_set_str(&gate->active_shortcut_id, shortcut_id);
  gate->active_timestamp = timestamp;
  gate->capture_running = TRUE;
  return TRUE;
}

void shaula_shortcut_provider_capture_finished(ShaulaShortcutProviderGate *gate) {
  g_return_if_fail(gate != NULL);
  gate->capture_running = FALSE;
}

void shaula_shortcut_provider_deactivated(ShaulaShortcutProviderGate *gate,
                                          const char *shortcut_id) {
  g_return_if_fail(gate != NULL);
  if (g_strcmp0(gate->active_shortcut_id, shortcut_id) == 0) {
    g_clear_pointer(&gate->active_shortcut_id, g_free);
    gate->active_timestamp = 0;
  }
}

guint shaula_shortcut_provider_next_reconnect_delay_ms(
    ShaulaShortcutProviderGate *gate) {
  g_return_val_if_fail(gate != NULL, 30000U);
  static const guint delays[] = {1000U, 2000U, 4000U, 8000U, 16000U, 30000U};
  guint index = MIN(gate->reconnect_attempt, G_N_ELEMENTS(delays) - 1U);
  if (gate->reconnect_attempt < G_MAXUINT)
    gate->reconnect_attempt++;
  return delays[index];
}

void shaula_shortcut_provider_reconnected(ShaulaShortcutProviderGate *gate) {
  g_return_if_fail(gate != NULL);
  gate->reconnect_attempt = 0;
}

ShaulaProviderNameResult shaula_shortcut_provider_name_result(guint32 reply) {
  switch (reply) {
  case 1U: /* DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER */
  case 4U: /* DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER */
    return SHAULA_PROVIDER_NAME_ACQUIRED;
  case 2U: /* DBUS_REQUEST_NAME_REPLY_IN_QUEUE */
  case 3U: /* DBUS_REQUEST_NAME_REPLY_EXISTS */
    return SHAULA_PROVIDER_NAME_ALREADY_RUNNING;
  default:
    return SHAULA_PROVIDER_NAME_FAILED;
  }
}
