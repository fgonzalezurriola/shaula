#ifndef SHAULA_SHORTCUTS_PROVIDER_CORE_H
#define SHAULA_SHORTCUTS_PROVIDER_CORE_H

#include <glib.h>
#include <stdint.h>

typedef int32_t ShaulaShortcutAction;
enum {
  SHAULA_SHORTCUT_ACTION_INVALID = -1,
  SHAULA_SHORTCUT_ACTION_QUICK = 0,
  SHAULA_SHORTCUT_ACTION_AREA = 1,
  SHAULA_SHORTCUT_ACTION_FULLSCREEN = 2,
  SHAULA_SHORTCUT_ACTION_ALL_SCREENS = 3,
};

typedef int32_t ShaulaProviderNameResult;
enum {
  SHAULA_PROVIDER_NAME_ACQUIRED = 0,
  SHAULA_PROVIDER_NAME_ALREADY_RUNNING = 1,
  SHAULA_PROVIDER_NAME_FAILED = 2,
};

typedef struct {
  gboolean capture_running;
  char *active_shortcut_id;
  guint64 active_timestamp;
  guint reconnect_attempt;
} ShaulaShortcutProviderGate;

void shaula_shortcut_provider_gate_init(ShaulaShortcutProviderGate *gate);
void shaula_shortcut_provider_gate_clear(ShaulaShortcutProviderGate *gate);
ShaulaShortcutAction shaula_shortcut_action_from_id(const char *shortcut_id);
const char *shaula_shortcut_action_id(ShaulaShortcutAction action);
char **shaula_shortcut_action_argv(const char *shaula_binary,
                                  ShaulaShortcutAction action);

/* Returns false for duplicate, repeated, unknown, or concurrent activations. */
gboolean shaula_shortcut_provider_should_dispatch(
    ShaulaShortcutProviderGate *gate, const char *shortcut_id,
    guint64 timestamp);
void shaula_shortcut_provider_capture_finished(ShaulaShortcutProviderGate *gate);
void shaula_shortcut_provider_deactivated(ShaulaShortcutProviderGate *gate,
                                          const char *shortcut_id);

/* Reconnect delays are deterministic: 1, 2, 4, 8, 16, then 30 seconds. */
guint shaula_shortcut_provider_next_reconnect_delay_ms(
    ShaulaShortcutProviderGate *gate);
void shaula_shortcut_provider_reconnected(ShaulaShortcutProviderGate *gate);

ShaulaProviderNameResult shaula_shortcut_provider_name_result(guint32 reply);

#endif
