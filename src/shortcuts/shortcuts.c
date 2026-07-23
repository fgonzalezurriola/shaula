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

static const char *const default_triggers[SHAULA_SHORTCUT_COUNT] = {
    "Ctrl+Shift+1", "Ctrl+Shift+2", "Ctrl+Shift+3", "Ctrl+Shift+4"};

static const char *canonical_named_key(const char *token) {
  static const struct {
    const char *input;
    const char *canonical;
  } names[] = {{"tab", "Tab"},
               {"space", "Space"},
               {"enter", "Enter"},
               {"return", "Enter"},
               {"escape", "Escape"},
               {"esc", "Escape"},
               {"print", "Print"},
               {"insert", "Insert"},
               {"delete", "Delete"},
               {"backspace", "Backspace"},
               {"home", "Home"},
               {"end", "End"},
               {"pageup", "PageUp"},
               {"pagedown", "PageDown"},
               {"up", "Up"},
               {"down", "Down"},
               {"left", "Left"},
               {"right", "Right"}};
  for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
    if (g_ascii_strcasecmp(token, names[i].input) == 0)
      return names[i].canonical;
  }
  return NULL;
}

static char *normalize_key(const char *token) {
  if (token == NULL || token[0] == '\0')
    return NULL;
  if (token[1] == '\0' && g_ascii_isalnum(token[0])) {
    char key[2] = {(char)g_ascii_toupper(token[0]), '\0'};
    return g_strdup(key);
  }
  if ((token[0] == 'f' || token[0] == 'F') && token[1] != '\0') {
    char *end = NULL;
    guint64 value = g_ascii_strtoull(token + 1, &end, 10);
    if (end != NULL && end[0] == '\0' && value >= 1U && value <= 35U)
      return g_strdup_printf("F%" G_GUINT64_FORMAT, value);
  }
  const char *named = canonical_named_key(token);
  return named != NULL ? g_strdup(named) : NULL;
}

static gboolean normalize_trigger(const char *input, char **normalized,
                                  char **error_text) {
  g_return_val_if_fail(normalized != NULL, FALSE);
  g_clear_pointer(normalized, g_free);
  g_autofree char *trimmed = g_strdup(input != NULL ? input : "");
  g_strstrip(trimmed);
  if (trimmed[0] == '\0') {
    set_error(error_text,
              "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut cannot be empty");
    return FALSE;
  }

  g_auto(GStrv) parts = g_strsplit(trimmed, "+", -1);
  gboolean ctrl = FALSE;
  gboolean alt = FALSE;
  gboolean shift = FALSE;
  gboolean super = FALSE;
  g_autofree char *key = NULL;
  for (guint i = 0; parts[i] != NULL; i++) {
    char *part = g_strstrip(parts[i]);
    if (part[0] == '\0') {
      set_error(error_text,
                "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut contains an empty component");
      return FALSE;
    }
    if (g_ascii_strcasecmp(part, "ctrl") == 0 ||
        g_ascii_strcasecmp(part, "control") == 0) {
      if (ctrl) {
        set_error(error_text,
                  "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut repeats Ctrl");
        return FALSE;
      }
      ctrl = TRUE;
    } else if (g_ascii_strcasecmp(part, "alt") == 0) {
      if (alt) {
        set_error(error_text,
                  "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut repeats Alt");
        return FALSE;
      }
      alt = TRUE;
    } else if (g_ascii_strcasecmp(part, "shift") == 0) {
      if (shift) {
        set_error(error_text,
                  "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut repeats Shift");
        return FALSE;
      }
      shift = TRUE;
    } else if (g_ascii_strcasecmp(part, "super") == 0 ||
               g_ascii_strcasecmp(part, "meta") == 0) {
      if (super) {
        set_error(error_text,
                  "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut repeats Super");
        return FALSE;
      }
      super = TRUE;
    } else {
      if (key != NULL) {
        set_error(error_text,
                  "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut must contain one key");
        return FALSE;
      }
      key = normalize_key(part);
      if (key == NULL) {
        set_error(error_text,
                  "ERR_SHORTCUT_CONFIGURATION_INVALID: unsupported shortcut key");
        return FALSE;
      }
    }
  }
  if (key == NULL || (!ctrl && !alt && !shift && !super)) {
    set_error(error_text,
              "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut needs a modifier and one key");
    return FALSE;
  }

  GString *result = g_string_new(NULL);
  if (ctrl)
    g_string_append(result, "Ctrl+");
  if (alt)
    g_string_append(result, "Alt+");
  if (shift)
    g_string_append(result, "Shift+");
  if (super)
    g_string_append(result, "Super+");
  g_string_append(result, key);
  *normalized = g_string_free(result, FALSE);
  return TRUE;
}

static gboolean normalize_preferences(
    const ShaulaShortcutPreferences *input,
    ShaulaShortcutPreferences *normalized, char **error_text) {
  shaula_shortcut_preferences_init_defaults(normalized);
  for (guint i = 0; i < SHAULA_SHORTCUT_COUNT; i++) {
    g_clear_pointer(&normalized->triggers[i], g_free);
    if (!normalize_trigger(input != NULL ? input->triggers[i] : NULL,
                           &normalized->triggers[i], error_text)) {
      shaula_shortcut_preferences_clear(normalized);
      return FALSE;
    }
    for (guint previous = 0; previous < i; previous++) {
      if (g_ascii_strcasecmp(normalized->triggers[previous],
                             normalized->triggers[i]) == 0) {
        set_error(error_text,
                  "ERR_SHORTCUT_CONFIGURATION_INVALID: capture shortcuts must be unique");
        shaula_shortcut_preferences_clear(normalized);
        return FALSE;
      }
    }
  }
  return TRUE;
}

void shaula_shortcut_preferences_init_defaults(
    ShaulaShortcutPreferences *preferences) {
  g_return_if_fail(preferences != NULL);
  *preferences = (ShaulaShortcutPreferences){0};
  for (guint i = 0; i < SHAULA_SHORTCUT_COUNT; i++)
    preferences->triggers[i] = g_strdup(default_triggers[i]);
}

void shaula_shortcut_preferences_clear(
    ShaulaShortcutPreferences *preferences) {
  if (preferences == NULL)
    return;
  for (guint i = 0; i < SHAULA_SHORTCUT_COUNT; i++)
    g_clear_pointer(&preferences->triggers[i], g_free);
  *preferences = (ShaulaShortcutPreferences){0};
}

ShaulaShortcutResult shaula_shortcuts_preferences_load(
    ShaulaShortcutPreferences *preferences, char **error_text) {
  g_return_val_if_fail(preferences != NULL,
                       SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);
  ShaulaShortcutPreferences loaded;
  shaula_shortcut_preferences_init_defaults(&loaded);
  gboolean exists = FALSE;
  g_autoptr(GError) error = NULL;
  if (!shaula_shortcut_preferences_state_load(&loaded, &exists, &error)) {
    shaula_shortcut_preferences_clear(&loaded);
    set_error(error_text,
              error != NULL
                  ? error->message
                  : "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut preferences unreadable");
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }
  (void)exists;
  ShaulaShortcutPreferences normalized = {0};
  if (!normalize_preferences(&loaded, &normalized, error_text)) {
    shaula_shortcut_preferences_clear(&loaded);
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }
  shaula_shortcut_preferences_clear(&loaded);
  shaula_shortcut_preferences_clear(preferences);
  *preferences = normalized;
  return SHAULA_SHORTCUT_RESULT_OK;
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

static ShaulaShortcutResult save_preferences(
    const ShaulaShortcutPreferences *preferences, gboolean dry_run,
    ShaulaShortcutStatus *status, char **error_text) {
  g_autoptr(GError) save_error = NULL;
  if (shaula_shortcut_preferences_state_save(preferences, dry_run,
                                             &save_error))
    return SHAULA_SHORTCUT_RESULT_OK;
  shaula_shortcut_status_clear(status);
  status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
  status->error_code = g_strdup("ERR_SHORTCUT_CONFIGURATION_INVALID");
  status->detail = g_strdup(save_error != NULL ? save_error->message
                                               : "Preference write failed.");
  set_error(error_text,
            save_error != NULL
                ? save_error->message
                : "ERR_SHORTCUT_CONFIGURATION_INVALID: shortcut preference write failed");
  return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
}

ShaulaShortcutResult shaula_shortcuts_preferences_apply(
    const ShaulaShortcutPreferences *preferences,
    const ShaulaShortcutOptions *options, ShaulaShortcutStatus *status,
    char **error_text) {
  g_return_val_if_fail(preferences != NULL,
                       SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  const ShaulaShortcutOptions defaults = {.remember_choice = FALSE};
  if (options == NULL)
    options = &defaults;
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);

  ShaulaShortcutPreferences normalized = {0};
  if (!normalize_preferences(preferences, &normalized, error_text)) {
    shaula_shortcut_status_clear(status);
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code = g_strdup("ERR_SHORTCUT_CONFIGURATION_INVALID");
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }

  ShaulaShortcutSetupState setup;
  ShaulaShortcutResult setup_result = load_setup_state(&setup, error_text);
  if (setup_result != SHAULA_SHORTCUT_RESULT_OK) {
    shaula_shortcut_preferences_clear(&normalized);
    return setup_result;
  }

  if (setup.backend == SHAULA_SHORTCUT_BACKEND_NIRI) {
    ShaulaShortcutPreferences previous;
    shaula_shortcut_preferences_init_defaults(&previous);
    gboolean previous_valid =
        shaula_shortcuts_preferences_load(&previous, NULL) ==
        SHAULA_SHORTCUT_RESULT_OK;

    ShaulaShortcutOptions preflight = *options;
    preflight.dry_run = TRUE;
    ShaulaShortcutResult applied = shaula_shortcut_niri_apply_preferences(
        &preflight, &normalized, status, error_text);
    if (applied != SHAULA_SHORTCUT_RESULT_OK) {
      shaula_shortcut_preferences_clear(&previous);
      shaula_shortcut_preferences_clear(&normalized);
      overlay_setup_state(status, &setup);
      return applied;
    }
    if (options->dry_run) {
      shaula_shortcut_preferences_clear(&previous);
      shaula_shortcut_preferences_clear(&normalized);
      overlay_setup_state(status, &setup);
      return applied;
    }

    applied = shaula_shortcut_niri_apply_preferences(
        options, &normalized, status, error_text);
    if (applied != SHAULA_SHORTCUT_RESULT_OK) {
      shaula_shortcut_preferences_clear(&previous);
      shaula_shortcut_preferences_clear(&normalized);
      overlay_setup_state(status, &setup);
      return applied;
    }

    ShaulaShortcutResult saved =
        save_preferences(&normalized, FALSE, status, error_text);
    if (saved != SHAULA_SHORTCUT_RESULT_OK && previous_valid) {
      ShaulaShortcutOptions rollback = *options;
      rollback.remember_choice = FALSE;
      (void)shaula_shortcut_niri_apply_preferences(
          &rollback, &previous, status, NULL);
    }
    shaula_shortcut_preferences_clear(&previous);
    shaula_shortcut_preferences_clear(&normalized);
    overlay_setup_state(status, &setup);
    return saved == SHAULA_SHORTCUT_RESULT_OK ? applied : saved;
  }

  ShaulaShortcutResult saved =
      save_preferences(&normalized, options->dry_run, status, error_text);
  if (saved != SHAULA_SHORTCUT_RESULT_OK) {
    shaula_shortcut_preferences_clear(&normalized);
    return saved;
  }

  ShaulaShortcutResult queried = shaula_shortcuts_query(status, error_text);
  if (queried != SHAULA_SHORTCUT_RESULT_OK &&
      status->state == SHAULA_SHORTCUT_STATE_CONFIG_INVALID) {
    shaula_shortcut_preferences_clear(&normalized);
    return queried;
  }

  ShaulaShortcutResult applied = SHAULA_SHORTCUT_RESULT_OK;
  if (status->backend == SHAULA_SHORTCUT_BACKEND_PORTAL &&
      (status->autostart_installed || status->provider_running))
    applied =
        shaula_shortcut_portal_apply_preferences(options, status, error_text);
  shaula_shortcut_preferences_clear(&normalized);
  if (applied != SHAULA_SHORTCUT_RESULT_OK)
    return applied;
  return shaula_shortcuts_query(status, error_text);
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
    ShaulaShortcutPreferences preferences;
    shaula_shortcut_preferences_init_defaults(&preferences);
    ShaulaShortcutResult loaded =
        shaula_shortcuts_preferences_load(&preferences, error_text);
    if (loaded != SHAULA_SHORTCUT_RESULT_OK) {
      shaula_shortcut_preferences_clear(&preferences);
      return loaded;
    }
    ShaulaShortcutResult repaired = shaula_shortcut_niri_apply_preferences(
        options, &preferences, status, error_text);
    shaula_shortcut_preferences_clear(&preferences);
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
