#ifndef SHAULA_SHORTCUTS_SHORTCUTS_H
#define SHAULA_SHORTCUTS_SHORTCUTS_H

#include <glib.h>
#include <stdint.h>

typedef int32_t ShaulaShortcutBackend;
enum {
  SHAULA_SHORTCUT_BACKEND_NONE = 0,
  SHAULA_SHORTCUT_BACKEND_PORTAL = 1,
  SHAULA_SHORTCUT_BACKEND_NIRI = 2,
};

typedef int32_t ShaulaShortcutState;
enum {
  SHAULA_SHORTCUT_STATE_DISABLED = 0,
  SHAULA_SHORTCUT_STATE_ACTIVE = 1,
  SHAULA_SHORTCUT_STATE_PERMISSION_PENDING = 2,
  SHAULA_SHORTCUT_STATE_PERMISSION_DENIED = 3,
  SHAULA_SHORTCUT_STATE_CONFLICT = 4,
  SHAULA_SHORTCUT_STATE_UNSUPPORTED = 5,
  SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE = 6,
  SHAULA_SHORTCUT_STATE_RECONNECTING = 7,
  SHAULA_SHORTCUT_STATE_CONFIG_INVALID = 8,
};

typedef int32_t ShaulaShortcutChoice;
enum {
  SHAULA_SHORTCUT_CHOICE_UNSET = 0,
  SHAULA_SHORTCUT_CHOICE_ENABLED = 1,
  SHAULA_SHORTCUT_CHOICE_DECLINED = 2,
};

typedef int32_t ShaulaShortcutResult;
enum {
  SHAULA_SHORTCUT_RESULT_OK = 0,
  SHAULA_SHORTCUT_RESULT_IO_FAILED = 1,
  SHAULA_SHORTCUT_RESULT_CONFLICT = 2,
  SHAULA_SHORTCUT_RESULT_CONFIG_INVALID = 3,
  SHAULA_SHORTCUT_RESULT_PROVIDER_FAILED = 4,
};

typedef struct {
  gboolean setup_completed;
  ShaulaShortcutChoice choice;
  gboolean enabled_requested;
  ShaulaShortcutBackend backend;
  ShaulaShortcutState state;
  gboolean autostart_installed;
  gboolean provider_running;
  guint portal_version;
  gboolean activation_ready;
  char *detail;
  char *error_code;
  char *triggers[4];
} ShaulaShortcutStatus;

typedef struct {
  gboolean dry_run;
  gboolean force;
  gboolean remember_choice;
} ShaulaShortcutOptions;

void shaula_shortcut_status_init(ShaulaShortcutStatus *status);
void shaula_shortcut_status_clear(ShaulaShortcutStatus *status);

/*
 * The shortcut module is the sole external seam for Settings and CLI setup.
 * It selects and operates the portal or Niri adapter internally and maps every
 * stable failure to a deterministic status and ERR_* token.
 */
ShaulaShortcutResult
shaula_shortcuts_query(ShaulaShortcutStatus *status, char **error_text);
ShaulaShortcutResult
shaula_shortcuts_enable(const ShaulaShortcutOptions *options,
                        ShaulaShortcutStatus *status, char **error_text);
ShaulaShortcutResult
shaula_shortcuts_disable(const ShaulaShortcutOptions *options,
                         ShaulaShortcutStatus *status, char **error_text);
ShaulaShortcutResult
shaula_shortcuts_repair(const ShaulaShortcutOptions *options,
                        ShaulaShortcutStatus *status, char **error_text);

/* First-run state is explicit and independent from installed shortcut files. */
gboolean shaula_shortcuts_mark_declined(char **error_text);
gboolean shaula_shortcuts_mark_setup_complete(char **error_text);
gboolean shaula_shortcuts_reset_setup_state(char **error_text);
gboolean shaula_shortcuts_setup_completed(void);

const char *shaula_shortcut_backend_token(ShaulaShortcutBackend backend);
const char *shaula_shortcut_state_token(ShaulaShortcutState state);
const char *shaula_shortcut_choice_token(ShaulaShortcutChoice choice);

#endif
