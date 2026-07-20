#include "state.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *xdg_home(const char *environment, const char *fallback) {
  const char *configured = g_getenv(environment);
  if (configured != NULL && configured[0] != '\0')
    return g_strdup(configured);
  const char *home = g_getenv("HOME");
  return home != NULL && home[0] != '\0'
             ? g_build_filename(home, fallback, NULL)
             : NULL;
}

char *shaula_shortcut_setup_state_path(void) {
  g_autofree char *config = xdg_home("XDG_CONFIG_HOME", ".config");
  return config != NULL
             ? g_build_filename(config, "shaula", "setup-state.ini", NULL)
             : NULL;
}

char *shaula_shortcut_provider_state_path(void) {
  g_autofree char *state = xdg_home("XDG_STATE_HOME", ".local/state");
  return state != NULL
             ? g_build_filename(state, "shaula", "shortcut-provider.ini", NULL)
             : NULL;
}

char *shaula_shortcut_autostart_path(void) {
  g_autofree char *config = xdg_home("XDG_CONFIG_HOME", ".config");
  return config != NULL
             ? g_build_filename(config, "autostart",
                                "dev.shaula.ShortcutProvider.desktop", NULL)
             : NULL;
}

static gboolean atomic_write(const char *path, const char *contents, mode_t mode,
                             GError **error) {
  if (path == NULL || contents == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "shortcut state path is unavailable");
    return FALSE;
  }
  g_autofree char *parent = g_path_get_dirname(path);
  if (g_mkdir_with_parents(parent, 0700) != 0) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                "could not create %s", parent);
    return FALSE;
  }
  g_autofree char *temporary =
      g_strdup_printf("%s.shaula-tmp.%ld", path, (long)getpid());
  if (!g_file_set_contents(temporary, contents, -1, error))
    return FALSE;
  if (g_chmod(temporary, mode) != 0) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                "could not set permissions on %s", temporary);
    (void)g_unlink(temporary);
    return FALSE;
  }
  if (g_rename(temporary, path) != 0) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                "could not replace %s", path);
    (void)g_unlink(temporary);
    return FALSE;
  }
  return TRUE;
}

static ShaulaShortcutChoice choice_from_token(const char *token) {
  if (g_strcmp0(token, "enabled") == 0)
    return SHAULA_SHORTCUT_CHOICE_ENABLED;
  if (g_strcmp0(token, "declined") == 0)
    return SHAULA_SHORTCUT_CHOICE_DECLINED;
  return SHAULA_SHORTCUT_CHOICE_UNSET;
}

static ShaulaShortcutBackend backend_from_token(const char *token) {
  if (g_strcmp0(token, "portal") == 0)
    return SHAULA_SHORTCUT_BACKEND_PORTAL;
  if (g_strcmp0(token, "niri") == 0)
    return SHAULA_SHORTCUT_BACKEND_NIRI;
  return SHAULA_SHORTCUT_BACKEND_NONE;
}

static gboolean token_is_one_of(const char *token, const char *const *values,
                                gsize count) {
  for (gsize i = 0; i < count; i++) {
    if (g_strcmp0(token, values[i]) == 0)
      return TRUE;
  }
  return FALSE;
}

static gboolean require_key(GKeyFile *file, const char *group, const char *key,
                            GError **error) {
  if (g_key_file_has_key(file, group, key, NULL))
    return TRUE;
  g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND,
              "shortcut state is missing [%s] %s", group, key);
  return FALSE;
}

static ShaulaShortcutState shortcut_state_from_token(const char *token) {
  if (g_strcmp0(token, "active") == 0)
    return SHAULA_SHORTCUT_STATE_ACTIVE;
  if (g_strcmp0(token, "permission_pending") == 0)
    return SHAULA_SHORTCUT_STATE_PERMISSION_PENDING;
  if (g_strcmp0(token, "permission_denied") == 0)
    return SHAULA_SHORTCUT_STATE_PERMISSION_DENIED;
  if (g_strcmp0(token, "conflict") == 0)
    return SHAULA_SHORTCUT_STATE_CONFLICT;
  if (g_strcmp0(token, "unsupported") == 0)
    return SHAULA_SHORTCUT_STATE_UNSUPPORTED;
  if (g_strcmp0(token, "provider_unavailable") == 0)
    return SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE;
  if (g_strcmp0(token, "reconnecting") == 0)
    return SHAULA_SHORTCUT_STATE_RECONNECTING;
  if (g_strcmp0(token, "configuration_invalid") == 0)
    return SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
  return SHAULA_SHORTCUT_STATE_DISABLED;
}

void shaula_shortcut_setup_state_init(ShaulaShortcutSetupState *state) {
  g_return_if_fail(state != NULL);
  *state = (ShaulaShortcutSetupState){0};
}

gboolean shaula_shortcut_setup_state_load(ShaulaShortcutSetupState *state,
                                          GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  shaula_shortcut_setup_state_init(state);
  g_autofree char *path = shaula_shortcut_setup_state_path();
  if (path == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "setup state path is unavailable");
    return FALSE;
  }
  if (!g_file_test(path, G_FILE_TEST_EXISTS))
    return TRUE;
  g_autoptr(GKeyFile) file = g_key_file_new();
  if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, error))
    return FALSE;
  if (!require_key(file, "setup", "completed", error) ||
      !require_key(file, "setup", "shortcuts_choice", error) ||
      !require_key(file, "setup", "shortcut_backend", error))
    return FALSE;
  g_autoptr(GError) parse_error = NULL;
  state->completed =
      g_key_file_get_boolean(file, "setup", "completed", &parse_error);
  if (parse_error != NULL) {
    g_propagate_error(error, g_steal_pointer(&parse_error));
    return FALSE;
  }
  g_autofree char *choice =
      g_key_file_get_string(file, "setup", "shortcuts_choice", error);
  if (choice == NULL)
    return FALSE;
  g_autofree char *backend =
      g_key_file_get_string(file, "setup", "shortcut_backend", error);
  if (backend == NULL)
    return FALSE;
  static const char *const choices[] = {"unset", "enabled", "declined"};
  static const char *const backends[] = {"none", "portal", "niri"};
  if (!token_is_one_of(choice, choices, G_N_ELEMENTS(choices)) ||
      !token_is_one_of(backend, backends, G_N_ELEMENTS(backends))) {
    g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                        "shortcut setup state contains an invalid token");
    return FALSE;
  }
  state->choice = choice_from_token(choice);
  state->backend = backend_from_token(backend);
  return TRUE;
}

gboolean shaula_shortcut_setup_state_save(
    const ShaulaShortcutSetupState *state, gboolean dry_run, GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  if (dry_run)
    return TRUE;
  g_autoptr(GKeyFile) file = g_key_file_new();
  g_key_file_set_boolean(file, "setup", "completed", state->completed);
  g_key_file_set_string(file, "setup", "shortcuts_choice",
                        shaula_shortcut_choice_token(state->choice));
  g_key_file_set_string(file, "setup", "shortcut_backend",
                        shaula_shortcut_backend_token(state->backend));
  gsize length = 0;
  g_autofree char *contents = g_key_file_to_data(file, &length, error);
  if (contents == NULL)
    return FALSE;
  g_autofree char *path = shaula_shortcut_setup_state_path();
  return atomic_write(path, contents, 0600, error);
}

gboolean shaula_shortcut_setup_state_remove(gboolean dry_run, GError **error) {
  g_autofree char *path = shaula_shortcut_setup_state_path();
  if (path == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "setup state path is unavailable");
    return FALSE;
  }
  if (dry_run || !g_file_test(path, G_FILE_TEST_EXISTS))
    return TRUE;
  if (g_unlink(path) != 0) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                "could not remove %s", path);
    return FALSE;
  }
  return TRUE;
}

void shaula_shortcut_provider_state_init(ShaulaShortcutProviderState *state) {
  g_return_if_fail(state != NULL);
  *state = (ShaulaShortcutProviderState){
      .state = SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE,
  };
}

void shaula_shortcut_provider_state_clear(ShaulaShortcutProviderState *state) {
  if (state == NULL)
    return;
  g_clear_pointer(&state->detail, g_free);
  g_clear_pointer(&state->error_code, g_free);
  for (guint i = 0; i < G_N_ELEMENTS(state->triggers); i++)
    g_clear_pointer(&state->triggers[i], g_free);
  shaula_shortcut_provider_state_init(state);
}

gboolean shaula_shortcut_provider_state_load(ShaulaShortcutProviderState *state,
                                             GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  ShaulaShortcutProviderState parsed;
  shaula_shortcut_provider_state_init(&parsed);
  g_autofree char *path = shaula_shortcut_provider_state_path();
  if (path == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "provider state path is unavailable");
    return FALSE;
  }
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    shaula_shortcut_provider_state_clear(state);
    *state = parsed;
    return TRUE;
  }
  g_autoptr(GKeyFile) file = g_key_file_new();
  if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, error))
    return FALSE;
  static const char *const names[] = {"quick", "area", "fullscreen",
                                      "all_screens"};
  if (!require_key(file, "provider", "state", error) ||
      !require_key(file, "provider", "portal_version", error) ||
      !require_key(file, "provider", "activation_ready", error) ||
      !require_key(file, "provider", "detail", error) ||
      !require_key(file, "provider", "error_code", error))
    return FALSE;
  for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
    if (!require_key(file, "shortcuts", names[i], error))
      return FALSE;
  }
  g_autofree char *state_token =
      g_key_file_get_string(file, "provider", "state", error);
  if (state_token == NULL)
    return FALSE;
  static const char *const states[] = {
      "disabled",          "active",       "permission_pending",
      "permission_denied", "conflict",     "unsupported",
      "provider_unavailable", "reconnecting", "configuration_invalid"};
  if (!token_is_one_of(state_token, states, G_N_ELEMENTS(states))) {
    g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                        "shortcut provider state contains an invalid token");
    return FALSE;
  }
  parsed.state = shortcut_state_from_token(state_token);
  g_autoptr(GError) parse_error = NULL;
  guint64 portal_version =
      g_key_file_get_uint64(file, "provider", "portal_version", &parse_error);
  if (parse_error != NULL || portal_version > G_MAXUINT) {
    if (parse_error != NULL)
      g_propagate_error(error, g_steal_pointer(&parse_error));
    else
      g_set_error_literal(error, G_KEY_FILE_ERROR,
                          G_KEY_FILE_ERROR_INVALID_VALUE,
                          "shortcut provider version is out of range");
    return FALSE;
  }
  parsed.portal_version = (guint)portal_version;
  parsed.activation_ready = g_key_file_get_boolean(
      file, "provider", "activation_ready", &parse_error);
  if (parse_error != NULL) {
    g_propagate_error(error, g_steal_pointer(&parse_error));
    return FALSE;
  }
  parsed.detail = g_key_file_get_string(file, "provider", "detail", error);
  if (parsed.detail == NULL)
    return FALSE;
  parsed.error_code =
      g_key_file_get_string(file, "provider", "error_code", error);
  if (parsed.error_code == NULL)
    return FALSE;
  for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
    parsed.triggers[i] =
        g_key_file_get_string(file, "shortcuts", names[i], error);
    if (parsed.triggers[i] == NULL) {
      shaula_shortcut_provider_state_clear(&parsed);
      return FALSE;
    }
  }
  shaula_shortcut_provider_state_clear(state);
  *state = parsed;
  return TRUE;
}

gboolean shaula_shortcut_provider_state_save(
    const ShaulaShortcutProviderState *state, GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_autoptr(GKeyFile) file = g_key_file_new();
  g_key_file_set_string(file, "provider", "state",
                        shaula_shortcut_state_token(state->state));
  g_key_file_set_uint64(file, "provider", "portal_version",
                        state->portal_version);
  g_key_file_set_boolean(file, "provider", "activation_ready",
                         state->activation_ready);
  g_key_file_set_string(file, "provider", "detail",
                        state->detail != NULL ? state->detail : "");
  g_key_file_set_string(file, "provider", "error_code",
                        state->error_code != NULL ? state->error_code : "");
  static const char *const names[] = {"quick", "area", "fullscreen",
                                      "all_screens"};
  for (guint i = 0; i < G_N_ELEMENTS(names); i++)
    g_key_file_set_string(file, "shortcuts", names[i],
                          state->triggers[i] != NULL ? state->triggers[i] : "");
  gsize length = 0;
  g_autofree char *contents = g_key_file_to_data(file, &length, error);
  if (contents == NULL)
    return FALSE;
  g_autofree char *path = shaula_shortcut_provider_state_path();
  return atomic_write(path, contents, 0600, error);
}

gboolean shaula_shortcut_provider_state_remove(gboolean dry_run,
                                               GError **error) {
  g_autofree char *path = shaula_shortcut_provider_state_path();
  if (path == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "provider state path is unavailable");
    return FALSE;
  }
  if (dry_run || !g_file_test(path, G_FILE_TEST_EXISTS))
    return TRUE;
  if (g_unlink(path) != 0) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                "could not remove %s", path);
    return FALSE;
  }
  return TRUE;
}

static char *desktop_exec_quote(const char *path) {
  GString *quoted = g_string_new("\"");
  for (const char *cursor = path; cursor != NULL && *cursor != '\0'; cursor++) {
    if (*cursor == '\\' || *cursor == '\"')
      g_string_append_c(quoted, '\\');
    g_string_append_c(quoted, *cursor);
  }
  g_string_append_c(quoted, '\"');
  return g_string_free(quoted, FALSE);
}

gboolean shaula_shortcut_autostart_install(const char *provider_path,
                                           gboolean dry_run, gboolean *changed,
                                           GError **error) {
  g_return_val_if_fail(changed != NULL, FALSE);
  *changed = FALSE;
  if (provider_path == NULL || provider_path[0] != '/') {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "shortcut provider path must be absolute");
    return FALSE;
  }
  g_autofree char *quoted = desktop_exec_quote(provider_path);
  g_autofree char *contents = g_strdup_printf(
      "[Desktop Entry]\n"
      "Type=Application\n"
      "Name=Shaula Shortcut Provider\n"
      "Comment=Own Shaula's approved desktop capture shortcuts\n"
      "Exec=%s\n"
      "TryExec=%s\n"
      "Terminal=false\n"
      "NoDisplay=true\n"
      "X-GNOME-Autostart-enabled=true\n",
      quoted, provider_path);
  g_autofree char *path = shaula_shortcut_autostart_path();
  g_autofree char *current = NULL;
  if (path != NULL)
    (void)g_file_get_contents(path, &current, NULL, NULL);
  *changed = current == NULL || !g_str_equal(current, contents);
  if (!*changed || dry_run)
    return TRUE;
  return atomic_write(path, contents, 0644, error);
}

gboolean shaula_shortcut_autostart_remove(gboolean dry_run, gboolean *changed,
                                          GError **error) {
  g_return_val_if_fail(changed != NULL, FALSE);
  g_autofree char *path = shaula_shortcut_autostart_path();
  if (path == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "shortcut autostart path is unavailable");
    return FALSE;
  }
  *changed = g_file_test(path, G_FILE_TEST_EXISTS);
  if (!*changed || dry_run)
    return TRUE;
  if (g_unlink(path) != 0) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                "could not remove %s", path);
    return FALSE;
  }
  return TRUE;
}

gboolean shaula_shortcut_autostart_installed(void) {
  g_autofree char *path = shaula_shortcut_autostart_path();
  return path != NULL && g_file_test(path, G_FILE_TEST_IS_REGULAR);
}
