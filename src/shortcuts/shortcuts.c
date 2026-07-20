#include "shortcuts.h"

#include "niri_adapter.h"
#include "portal_adapter.h"
#include "state.h"

static gboolean portal_state_is_viable(ShaulaShortcutState state) {
  return state == SHAULA_SHORTCUT_STATE_ACTIVE ||
         state == SHAULA_SHORTCUT_STATE_PERMISSION_PENDING ||
         state == SHAULA_SHORTCUT_STATE_PERMISSION_DENIED ||
         state == SHAULA_SHORTCUT_STATE_RECONNECTING;
}

static gboolean portal_state_needs_fallback(ShaulaShortcutState state) {
  return state == SHAULA_SHORTCUT_STATE_UNSUPPORTED ||
         state == SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE ||
         state == SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
}

static void set_error(char **error_text, const char *message) {
  if (error_text == NULL)
    return;
  g_clear_pointer(error_text, g_free);
  *error_text = g_strdup(message);
}

void shaula_shortcut_status_init(ShaulaShortcutStatus *status) {
  g_return_if_fail(status != NULL);
  *status = (ShaulaShortcutStatus){
      .choice = SHAULA_SHORTCUT_CHOICE_UNSET,
      .backend = SHAULA_SHORTCUT_BACKEND_NONE,
      .state = SHAULA_SHORTCUT_STATE_DISABLED,
  };
}

void shaula_shortcut_status_clear(ShaulaShortcutStatus *status) {
  if (status == NULL)
    return;
  g_clear_pointer(&status->detail, g_free);
  g_clear_pointer(&status->error_code, g_free);
  for (guint i = 0; i < G_N_ELEMENTS(status->triggers); i++)
    g_clear_pointer(&status->triggers[i], g_free);
  shaula_shortcut_status_init(status);
}

const char *shaula_shortcut_backend_token(ShaulaShortcutBackend backend) {
  switch (backend) {
  case SHAULA_SHORTCUT_BACKEND_PORTAL:
    return "portal";
  case SHAULA_SHORTCUT_BACKEND_NIRI:
    return "niri";
  default:
    return "none";
  }
}

const char *shaula_shortcut_state_token(ShaulaShortcutState state) {
  switch (state) {
  case SHAULA_SHORTCUT_STATE_ACTIVE:
    return "active";
  case SHAULA_SHORTCUT_STATE_PERMISSION_PENDING:
    return "permission_pending";
  case SHAULA_SHORTCUT_STATE_PERMISSION_DENIED:
    return "permission_denied";
  case SHAULA_SHORTCUT_STATE_CONFLICT:
    return "conflict";
  case SHAULA_SHORTCUT_STATE_UNSUPPORTED:
    return "unsupported";
  case SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE:
    return "provider_unavailable";
  case SHAULA_SHORTCUT_STATE_RECONNECTING:
    return "reconnecting";
  case SHAULA_SHORTCUT_STATE_CONFIG_INVALID:
    return "configuration_invalid";
  default:
    return "disabled";
  }
}

const char *shaula_shortcut_choice_token(ShaulaShortcutChoice choice) {
  switch (choice) {
  case SHAULA_SHORTCUT_CHOICE_ENABLED:
    return "enabled";
  case SHAULA_SHORTCUT_CHOICE_DECLINED:
    return "declined";
  default:
    return "unset";
  }
}

static ShaulaShortcutResult load_setup_state(ShaulaShortcutSetupState *state,
                                             char **error_text) {
  shaula_shortcut_setup_state_init(state);
  g_autoptr(GError) error = NULL;
  if (!shaula_shortcut_setup_state_load(state, &error)) {
    set_error(error_text,
              error != NULL ? error->message
                            : "ERR_SHORTCUT_CONFIGURATION_INVALID: setup state unreadable");
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }
  return SHAULA_SHORTCUT_RESULT_OK;
}

static ShaulaShortcutResult save_setup_state(
    ShaulaShortcutChoice choice, ShaulaShortcutBackend backend,
    gboolean completed, gboolean dry_run, char **error_text) {
  ShaulaShortcutSetupState state = {
      .completed = completed,
      .choice = choice,
      .backend = backend,
  };
  g_autoptr(GError) error = NULL;
  if (!shaula_shortcut_setup_state_save(&state, dry_run, &error)) {
    set_error(error_text,
              error != NULL ? error->message
                            : "ERR_SHORTCUT_CONFIGURATION_INVALID: setup state write failed");
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }
  return SHAULA_SHORTCUT_RESULT_OK;
}

static void overlay_setup_state(ShaulaShortcutStatus *status,
                                const ShaulaShortcutSetupState *setup) {
  status->setup_completed = setup->completed;
  status->choice = setup->choice;
  status->enabled_requested = setup->choice == SHAULA_SHORTCUT_CHOICE_ENABLED;
}

ShaulaShortcutResult
shaula_shortcuts_query(ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);
  ShaulaShortcutSetupState setup;
  ShaulaShortcutResult loaded = load_setup_state(&setup, error_text);
  if (loaded != SHAULA_SHORTCUT_RESULT_OK) {
    shaula_shortcut_status_clear(status);
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code = g_strdup("ERR_SHORTCUT_CONFIGURATION_INVALID");
    return loaded;
  }

  ShaulaShortcutResult result = SHAULA_SHORTCUT_RESULT_OK;
  if (setup.backend == SHAULA_SHORTCUT_BACKEND_PORTAL ||
      shaula_shortcut_autostart_installed()) {
    result = shaula_shortcut_portal_query(status, error_text);
  } else if (setup.backend == SHAULA_SHORTCUT_BACKEND_NIRI) {
    result = shaula_shortcut_niri_query(status, error_text);
  } else {
    result = shaula_shortcut_niri_query(status, error_text);
    if (result == SHAULA_SHORTCUT_RESULT_OK &&
        status->state != SHAULA_SHORTCUT_STATE_ACTIVE &&
        status->state != SHAULA_SHORTCUT_STATE_CONFLICT &&
        status->state != SHAULA_SHORTCUT_STATE_CONFIG_INVALID) {
      shaula_shortcut_status_clear(status);
      if (setup.choice == SHAULA_SHORTCUT_CHOICE_ENABLED) {
        status->state = SHAULA_SHORTCUT_STATE_UNSUPPORTED;
        status->error_code = g_strdup("ERR_SHORTCUTS_UNSUPPORTED");
        status->detail = g_strdup(
            "Automatic global shortcuts are unavailable on this desktop.");
      }
    }
  }
  overlay_setup_state(status, &setup);
  return result;
}

static ShaulaShortcutResult persist_enable_status(
    const ShaulaShortcutOptions *options, ShaulaShortcutStatus *status,
    ShaulaShortcutResult operation, char **error_text) {
  if (!options->remember_choice)
    return operation;
  ShaulaShortcutBackend backend =
      status->state == SHAULA_SHORTCUT_STATE_UNSUPPORTED
          ? SHAULA_SHORTCUT_BACKEND_NONE
          : status->backend;
  ShaulaShortcutResult saved = save_setup_state(
      SHAULA_SHORTCUT_CHOICE_ENABLED, backend, TRUE, options->dry_run,
      error_text);
  status->setup_completed = TRUE;
  status->choice = SHAULA_SHORTCUT_CHOICE_ENABLED;
  status->enabled_requested = TRUE;
  return saved == SHAULA_SHORTCUT_RESULT_OK ? operation : saved;
}

ShaulaShortcutResult
shaula_shortcuts_enable(const ShaulaShortcutOptions *options,
                        ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  const ShaulaShortcutOptions defaults = {.remember_choice = TRUE};
  if (options == NULL)
    options = &defaults;
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);

  ShaulaShortcutResult portal =
      shaula_shortcut_portal_enable(options, status, error_text);
  if (portal_state_is_viable(status->state))
    return persist_enable_status(options, status, portal, error_text);
  if (!portal_state_needs_fallback(status->state) &&
      portal != SHAULA_SHORTCUT_RESULT_PROVIDER_FAILED &&
      portal != SHAULA_SHORTCUT_RESULT_CONFIG_INVALID)
    return persist_enable_status(options, status, portal, error_text);

  ShaulaShortcutOptions cleanup = *options;
  cleanup.remember_choice = FALSE;
  (void)shaula_shortcut_portal_disable(&cleanup, status, NULL);

  ShaulaShortcutResult niri =
      shaula_shortcut_niri_enable(options, status, error_text);
  if (status->state == SHAULA_SHORTCUT_STATE_UNSUPPORTED) {
    shaula_shortcut_status_clear(status);
    status->state = SHAULA_SHORTCUT_STATE_UNSUPPORTED;
    status->backend = SHAULA_SHORTCUT_BACKEND_NONE;
    status->error_code = g_strdup("ERR_SHORTCUTS_UNSUPPORTED");
    status->detail = g_strdup(
        "Automatic global shortcuts are unavailable. Desktop launcher actions remain available.");
    niri = SHAULA_SHORTCUT_RESULT_OK;
  }
  return persist_enable_status(options, status, niri, error_text);
}

ShaulaShortcutResult
shaula_shortcuts_disable(const ShaulaShortcutOptions *options,
                         ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  const ShaulaShortcutOptions defaults = {.remember_choice = TRUE};
  if (options == NULL)
    options = &defaults;
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);

  ShaulaShortcutOptions internal = *options;
  internal.remember_choice = FALSE;
  ShaulaShortcutStatus portal_status;
  ShaulaShortcutStatus niri_status;
  shaula_shortcut_status_init(&portal_status);
  shaula_shortcut_status_init(&niri_status);
  g_autofree char *portal_error = NULL;
  g_autofree char *niri_error = NULL;
  ShaulaShortcutResult portal = shaula_shortcut_portal_disable(
      &internal, &portal_status, &portal_error);
  ShaulaShortcutResult niri =
      shaula_shortcut_niri_disable(&internal, &niri_status, &niri_error);

  ShaulaShortcutResult operation =
      portal != SHAULA_SHORTCUT_RESULT_OK ? portal : niri;
  if (operation != SHAULA_SHORTCUT_RESULT_OK) {
    set_error(error_text, portal_error != NULL ? portal_error : niri_error);
    shaula_shortcut_status_clear(status);
    if (portal != SHAULA_SHORTCUT_RESULT_OK) {
      *status = portal_status;
      shaula_shortcut_status_init(&portal_status);
    } else {
      *status = niri_status;
      shaula_shortcut_status_init(&niri_status);
    }
    shaula_shortcut_status_clear(&portal_status);
    shaula_shortcut_status_clear(&niri_status);
    return operation;
  }

  shaula_shortcut_status_clear(status);
  status->state = SHAULA_SHORTCUT_STATE_DISABLED;
  status->detail = g_strdup(options->dry_run
                                ? "Global shortcuts would be disabled."
                                : "Global shortcuts are disabled.");
  shaula_shortcut_status_clear(&portal_status);
  shaula_shortcut_status_clear(&niri_status);

  if (options->remember_choice) {
    ShaulaShortcutResult saved = save_setup_state(
        SHAULA_SHORTCUT_CHOICE_DECLINED, SHAULA_SHORTCUT_BACKEND_NONE, TRUE,
        options->dry_run, error_text);
    status->setup_completed = TRUE;
    status->choice = SHAULA_SHORTCUT_CHOICE_DECLINED;
    if (saved != SHAULA_SHORTCUT_RESULT_OK)
      return saved;
  }
  return operation;
}

ShaulaShortcutResult
shaula_shortcuts_repair(const ShaulaShortcutOptions *options,
                        ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  const ShaulaShortcutOptions defaults = {.remember_choice = TRUE};
  if (options == NULL)
    options = &defaults;

  ShaulaShortcutResult queried = shaula_shortcuts_query(status, error_text);
  if (queried != SHAULA_SHORTCUT_RESULT_OK &&
      status->state == SHAULA_SHORTCUT_STATE_CONFIG_INVALID)
    return queried;
  if (status->backend == SHAULA_SHORTCUT_BACKEND_PORTAL) {
    ShaulaShortcutResult repaired =
        shaula_shortcut_portal_repair(options, status, error_text);
    if (portal_state_is_viable(status->state))
      return persist_enable_status(options, status, repaired, error_text);
  } else if (status->backend == SHAULA_SHORTCUT_BACKEND_NIRI) {
    ShaulaShortcutResult repaired =
        shaula_shortcut_niri_enable(options, status, error_text);
    return persist_enable_status(options, status, repaired, error_text);
  }
  return shaula_shortcuts_enable(options, status, error_text);
}

gboolean shaula_shortcuts_mark_declined(char **error_text) {
  return save_setup_state(SHAULA_SHORTCUT_CHOICE_DECLINED,
                          SHAULA_SHORTCUT_BACKEND_NONE, TRUE, FALSE,
                          error_text) == SHAULA_SHORTCUT_RESULT_OK;
}

gboolean shaula_shortcuts_mark_setup_complete(char **error_text) {
  ShaulaShortcutSetupState state;
  if (load_setup_state(&state, error_text) != SHAULA_SHORTCUT_RESULT_OK)
    return FALSE;
  state.completed = TRUE;
  g_autoptr(GError) error = NULL;
  if (!shaula_shortcut_setup_state_save(&state, FALSE, &error)) {
    set_error(error_text,
              error != NULL ? error->message
                            : "ERR_SHORTCUT_CONFIGURATION_INVALID: setup state write failed");
    return FALSE;
  }
  return TRUE;
}

gboolean shaula_shortcuts_reset_setup_state(char **error_text) {
  g_autoptr(GError) error = NULL;
  if (!shaula_shortcut_setup_state_remove(FALSE, &error)) {
    set_error(error_text,
              error != NULL ? error->message
                            : "ERR_SHORTCUT_CONFIGURATION_INVALID: setup state removal failed");
    return FALSE;
  }
  return TRUE;
}

gboolean shaula_shortcuts_setup_completed(void) {
  ShaulaShortcutSetupState state;
  g_autoptr(GError) error = NULL;
  return shaula_shortcut_setup_state_load(&state, &error) && state.completed;
}
