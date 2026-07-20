#include "portal_adapter.h"

#include "state.h"
#include "runtime/helper_resolution.h"

#include <gio/gio.h>
#include <glib.h>

#define PROVIDER_NAME "dev.shaula.ShortcutProvider"
#define PROVIDER_OBJECT "/dev/shaula/ShortcutProvider"
#define PROVIDER_IFACE "dev.shaula.ShortcutProvider"

static const char *const preferred_triggers[] = {
    "Ctrl+Shift+1", "Ctrl+Shift+2", "Ctrl+Shift+3", "Ctrl+Shift+4"};

static void set_error(char **error_text, const char *message) {
  if (error_text == NULL)
    return;
  g_clear_pointer(error_text, g_free);
  *error_text = g_strdup(message);
}

static void initialize_portal_status(ShaulaShortcutStatus *status) {
  shaula_shortcut_status_clear(status);
  status->backend = SHAULA_SHORTCUT_BACKEND_PORTAL;
  status->autostart_installed = shaula_shortcut_autostart_installed();
  for (guint i = 0; i < G_N_ELEMENTS(preferred_triggers); i++)
    status->triggers[i] = g_strdup(preferred_triggers[i]);
}

static ShaulaShortcutState test_state(void) {
  const char *value = g_getenv("SHAULA_SHORTCUTS_TEST_PORTAL");
  if (g_strcmp0(value, "active") == 0)
    return SHAULA_SHORTCUT_STATE_ACTIVE;
  if (g_strcmp0(value, "pending") == 0)
    return SHAULA_SHORTCUT_STATE_PERMISSION_PENDING;
  if (g_strcmp0(value, "denied") == 0)
    return SHAULA_SHORTCUT_STATE_PERMISSION_DENIED;
  if (g_strcmp0(value, "reconnecting") == 0)
    return SHAULA_SHORTCUT_STATE_RECONNECTING;
  if (g_strcmp0(value, "provider-unavailable") == 0)
    return SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE;
  if (g_strcmp0(value, "invalid") == 0)
    return SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
  if (g_strcmp0(value, "unsupported") == 0)
    return SHAULA_SHORTCUT_STATE_UNSUPPORTED;
  return SHAULA_SHORTCUT_STATE_DISABLED;
}

static gboolean test_mode_enabled(void) {
  const char *value = g_getenv("SHAULA_SHORTCUTS_TEST_PORTAL");
  return value != NULL && value[0] != '\0';
}

static gboolean provider_has_owner(GDBusConnection *bus) {
  if (bus == NULL)
    return FALSE;
  GVariant *reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "NameHasOwner",
      g_variant_new("(s)", PROVIDER_NAME), G_VARIANT_TYPE("(b)"),
      G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
  if (reply == NULL)
    return FALSE;
  gboolean owner = FALSE;
  g_variant_get(reply, "(b)", &owner);
  g_variant_unref(reply);
  return owner;
}

static void copy_provider_state(ShaulaShortcutStatus *status,
                                const ShaulaShortcutProviderState *provider) {
  status->state = provider->state;
  status->portal_version = provider->portal_version;
  status->activation_ready = provider->activation_ready;
  status->detail = g_strdup(provider->detail);
  status->error_code = g_strdup(provider->error_code);
  for (guint i = 0; i < G_N_ELEMENTS(status->triggers); i++) {
    if (provider->triggers[i] != NULL && provider->triggers[i][0] != '\0') {
      g_free(status->triggers[i]);
      status->triggers[i] = g_strdup(provider->triggers[i]);
    }
  }
}

static gboolean read_live_status(GDBusConnection *bus,
                                 ShaulaShortcutStatus *status) {
  GVariant *reply = g_dbus_connection_call_sync(
      bus, PROVIDER_NAME, PROVIDER_OBJECT, PROVIDER_IFACE, "GetStatus", NULL,
      G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 1500, NULL, NULL);
  if (reply == NULL)
    return FALSE;
  GVariant *dictionary = NULL;
  g_variant_get(reply, "(@a{sv})", &dictionary);
  g_variant_unref(reply);
  if (dictionary == NULL)
    return FALSE;

  GVariant *state_value =
      g_variant_lookup_value(dictionary, "state", G_VARIANT_TYPE_STRING);
  GVariant *version_value = g_variant_lookup_value(
      dictionary, "portal_version", G_VARIANT_TYPE_UINT32);
  GVariant *ready_value = g_variant_lookup_value(
      dictionary, "activation_ready", G_VARIANT_TYPE_BOOLEAN);
  GVariant *detail_value =
      g_variant_lookup_value(dictionary, "detail", G_VARIANT_TYPE_STRING);
  GVariant *error_value =
      g_variant_lookup_value(dictionary, "error_code", G_VARIANT_TYPE_STRING);
  GVariant *triggers_value =
      g_variant_lookup_value(dictionary, "triggers", G_VARIANT_TYPE_STRING_ARRAY);
  if (state_value == NULL || version_value == NULL || ready_value == NULL ||
      detail_value == NULL || error_value == NULL || triggers_value == NULL) {
    g_clear_pointer(&state_value, g_variant_unref);
    g_clear_pointer(&version_value, g_variant_unref);
    g_clear_pointer(&ready_value, g_variant_unref);
    g_clear_pointer(&detail_value, g_variant_unref);
    g_clear_pointer(&error_value, g_variant_unref);
    g_clear_pointer(&triggers_value, g_variant_unref);
    g_variant_unref(dictionary);
    return FALSE;
  }

  const char *state_token = g_variant_get_string(state_value, NULL);
  if (g_str_equal(state_token, "active"))
    status->state = SHAULA_SHORTCUT_STATE_ACTIVE;
  else if (g_str_equal(state_token, "permission_pending"))
    status->state = SHAULA_SHORTCUT_STATE_PERMISSION_PENDING;
  else if (g_str_equal(state_token, "permission_denied"))
    status->state = SHAULA_SHORTCUT_STATE_PERMISSION_DENIED;
  else if (g_str_equal(state_token, "reconnecting"))
    status->state = SHAULA_SHORTCUT_STATE_RECONNECTING;
  else if (g_str_equal(state_token, "provider_unavailable"))
    status->state = SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE;
  else if (g_str_equal(state_token, "configuration_invalid"))
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
  else if (g_str_equal(state_token, "unsupported"))
    status->state = SHAULA_SHORTCUT_STATE_UNSUPPORTED;
  else
    status->state = SHAULA_SHORTCUT_STATE_DISABLED;
  status->portal_version = g_variant_get_uint32(version_value);
  status->activation_ready = g_variant_get_boolean(ready_value);
  g_set_str(&status->detail, g_variant_get_string(detail_value, NULL));
  const char *error_code = g_variant_get_string(error_value, NULL);
  if (error_code[0] == '\0')
    g_clear_pointer(&status->error_code, g_free);
  else
    g_set_str(&status->error_code, error_code);
  gsize trigger_count = 0U;
  g_auto(GStrv) triggers = g_variant_dup_strv(triggers_value, &trigger_count);
  for (guint i = 0; i < G_N_ELEMENTS(status->triggers) && i < trigger_count; i++)
    g_set_str(&status->triggers[i], triggers[i]);

  g_variant_unref(state_value);
  g_variant_unref(version_value);
  g_variant_unref(ready_value);
  g_variant_unref(detail_value);
  g_variant_unref(error_value);
  g_variant_unref(triggers_value);
  g_variant_unref(dictionary);
  return TRUE;
}

ShaulaShortcutResult
shaula_shortcut_portal_query(ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  initialize_portal_status(status);
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);

  if (test_mode_enabled()) {
    status->state = test_state();
    status->provider_running =
        status->state != SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE &&
        status->state != SHAULA_SHORTCUT_STATE_UNSUPPORTED;
    status->activation_ready = status->state == SHAULA_SHORTCUT_STATE_ACTIVE;
    status->portal_version = status->provider_running ? 2U : 0U;
    status->detail = g_strdup("Isolated portal adapter test state.");
    if (status->state == SHAULA_SHORTCUT_STATE_UNSUPPORTED)
      status->error_code = g_strdup("ERR_SHORTCUTS_UNSUPPORTED");
    else if (status->state == SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE)
      status->error_code =
          g_strdup("ERR_SHORTCUT_PROVIDER_UNAVAILABLE");
    else if (status->state == SHAULA_SHORTCUT_STATE_PERMISSION_DENIED)
      status->error_code = g_strdup("ERR_SHORTCUT_PERMISSION_DENIED");
    else if (status->state == SHAULA_SHORTCUT_STATE_RECONNECTING)
      status->error_code = g_strdup("ERR_SHORTCUT_SESSION_LOST");
    return SHAULA_SHORTCUT_RESULT_OK;
  }

  g_autoptr(GError) bus_error = NULL;
  g_autoptr(GDBusConnection) bus =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &bus_error);
  status->provider_running = provider_has_owner(bus);

  ShaulaShortcutProviderState provider;
  shaula_shortcut_provider_state_init(&provider);
  g_autoptr(GError) state_error = NULL;
  gboolean state_loaded =
      shaula_shortcut_provider_state_load(&provider, &state_error);
  if (state_loaded)
    copy_provider_state(status, &provider);
  shaula_shortcut_provider_state_clear(&provider);

  if (status->provider_running && read_live_status(bus, status))
    return SHAULA_SHORTCUT_RESULT_OK;
  if (status->provider_running) {
    status->state = SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE;
    g_set_str(&status->error_code, "ERR_SHORTCUT_PROVIDER_UNAVAILABLE");
    g_set_str(&status->detail,
              "The shortcut provider is running but did not return status.");
    set_error(error_text,
              "ERR_SHORTCUT_PROVIDER_UNAVAILABLE: provider status unavailable");
    return SHAULA_SHORTCUT_RESULT_PROVIDER_FAILED;
  }
  if (!status->autostart_installed) {
    status->state = SHAULA_SHORTCUT_STATE_DISABLED;
    g_clear_pointer(&status->error_code, g_free);
    g_set_str(&status->detail, "Portal shortcuts are disabled.");
    return SHAULA_SHORTCUT_RESULT_OK;
  }
  if (!state_loaded) {
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    g_set_str(&status->error_code, "ERR_SHORTCUT_CONFIGURATION_INVALID");
    g_set_str(&status->detail,
              state_error != NULL ? state_error->message
                                  : "Provider state is invalid.");
    set_error(error_text,
              "ERR_SHORTCUT_CONFIGURATION_INVALID: provider state is invalid");
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }
  if (status->state == SHAULA_SHORTCUT_STATE_UNSUPPORTED ||
      status->state == SHAULA_SHORTCUT_STATE_CONFIG_INVALID)
    return status->state == SHAULA_SHORTCUT_STATE_CONFIG_INVALID
               ? SHAULA_SHORTCUT_RESULT_CONFIG_INVALID
               : SHAULA_SHORTCUT_RESULT_OK;

  status->state = SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE;
  g_set_str(&status->error_code, "ERR_SHORTCUT_PROVIDER_UNAVAILABLE");
  if (bus == NULL)
    g_set_str(&status->detail,
              bus_error != NULL ? bus_error->message
                                : "The session bus is unavailable.");
  else
    g_set_str(&status->detail,
              "Portal shortcuts are enabled, but the provider is not running.");
  return SHAULA_SHORTCUT_RESULT_OK;
}

static char *provider_path_resolve(void) {
  g_autofree char *candidate = shaula_executable_resolve_helper(
      "SHAULA_SHORTCUT_PROVIDER", "shaula-shortcut-provider");
  if (candidate == NULL)
    return NULL;
  if (g_path_is_absolute(candidate))
    return g_steal_pointer(&candidate);
  return shaula_executable_find_program(candidate);
}

static gboolean provider_call(const char *method, GError **error) {
  g_autoptr(GDBusConnection) bus =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, error);
  if (bus == NULL)
    return FALSE;
  GVariant *reply = g_dbus_connection_call_sync(
      bus, PROVIDER_NAME, PROVIDER_OBJECT, PROVIDER_IFACE, method, NULL, NULL,
      G_DBUS_CALL_FLAGS_NONE, 3000, NULL, error);
  if (reply == NULL)
    return FALSE;
  g_variant_unref(reply);
  return TRUE;
}

static gboolean portal_state_is_terminal(ShaulaShortcutState state) {
  return state == SHAULA_SHORTCUT_STATE_ACTIVE ||
         state == SHAULA_SHORTCUT_STATE_PERMISSION_DENIED ||
         state == SHAULA_SHORTCUT_STATE_UNSUPPORTED ||
         state == SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE ||
         state == SHAULA_SHORTCUT_STATE_CONFIG_INVALID ||
         state == SHAULA_SHORTCUT_STATE_RECONNECTING;
}

ShaulaShortcutResult
shaula_shortcut_portal_enable(const ShaulaShortcutOptions *options,
                              ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  const ShaulaShortcutOptions defaults = {.remember_choice = TRUE};
  if (options == NULL)
    options = &defaults;
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);

  if (test_mode_enabled())
    return shaula_shortcut_portal_query(status, error_text);

  g_autofree char *provider_path = provider_path_resolve();
  if (provider_path == NULL || !g_file_test(provider_path, G_FILE_TEST_IS_EXECUTABLE)) {
    initialize_portal_status(status);
    status->state = SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE;
    status->error_code = g_strdup("ERR_SHORTCUT_PROVIDER_UNAVAILABLE");
    status->detail = g_strdup("The shortcut provider executable is unavailable.");
    set_error(error_text,
              "ERR_SHORTCUT_PROVIDER_UNAVAILABLE: provider executable unavailable");
    return SHAULA_SHORTCUT_RESULT_PROVIDER_FAILED;
  }

  gboolean autostart_changed = FALSE;
  g_autoptr(GError) error = NULL;
  if (!shaula_shortcut_autostart_install(provider_path, options->dry_run,
                                         &autostart_changed, &error)) {
    initialize_portal_status(status);
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code = g_strdup("ERR_SHORTCUT_CONFIGURATION_INVALID");
    status->detail = g_strdup(error != NULL ? error->message
                                           : "Autostart installation failed.");
    set_error(error_text,
              "ERR_SHORTCUT_CONFIGURATION_INVALID: autostart installation failed");
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }
  (void)autostart_changed;
  if (options->dry_run) {
    initialize_portal_status(status);
    status->autostart_installed = TRUE;
    status->state = SHAULA_SHORTCUT_STATE_PERMISSION_PENDING;
    status->detail = g_strdup("Portal shortcut approval would be requested.");
    return SHAULA_SHORTCUT_RESULT_OK;
  }

  ShaulaShortcutResult queried =
      shaula_shortcut_portal_query(status, error_text);
  if (status->provider_running &&
      status->state != SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE)
    return queried;

  (void)shaula_shortcut_provider_state_remove(FALSE, NULL);
  const char *argv[] = {provider_path, NULL};
  g_autoptr(GSubprocess) process = g_subprocess_newv(
      argv, G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
      &error);
  if (process == NULL) {
    initialize_portal_status(status);
    status->autostart_installed = TRUE;
    status->state = SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE;
    status->error_code = g_strdup("ERR_SHORTCUT_PROVIDER_UNAVAILABLE");
    status->detail = g_strdup(error != NULL ? error->message
                                           : "Provider launch failed.");
    set_error(error_text,
              "ERR_SHORTCUT_PROVIDER_UNAVAILABLE: provider launch failed");
    return SHAULA_SHORTCUT_RESULT_PROVIDER_FAILED;
  }

  for (guint attempt = 0; attempt < 20U; attempt++) {
    g_usleep(100000U);
    queried = shaula_shortcut_portal_query(status, error_text);
    if (status->provider_running &&
        portal_state_is_terminal(status->state))
      return queried;
    if (g_subprocess_get_if_exited(process))
      break;
  }
  queried = shaula_shortcut_portal_query(status, error_text);
  if (status->state == SHAULA_SHORTCUT_STATE_DISABLED) {
    status->state = SHAULA_SHORTCUT_STATE_PERMISSION_PENDING;
    g_set_str(&status->detail, "Waiting for desktop shortcut approval.");
  }
  return queried;
}

ShaulaShortcutResult
shaula_shortcut_portal_disable(const ShaulaShortcutOptions *options,
                               ShaulaShortcutStatus *status, char **error_text) {
  g_return_val_if_fail(status != NULL, SHAULA_SHORTCUT_RESULT_CONFIG_INVALID);
  const ShaulaShortcutOptions defaults = {.remember_choice = TRUE};
  if (options == NULL)
    options = &defaults;
  if (error_text != NULL)
    g_clear_pointer(error_text, g_free);

  if (!test_mode_enabled()) {
    g_autoptr(GError) ignored = NULL;
    (void)provider_call("Stop", &ignored);
  }
  gboolean changed = FALSE;
  g_autoptr(GError) error = NULL;
  if (!shaula_shortcut_autostart_remove(options->dry_run, &changed, &error) ||
      !shaula_shortcut_provider_state_remove(options->dry_run, &error)) {
    initialize_portal_status(status);
    status->state = SHAULA_SHORTCUT_STATE_CONFIG_INVALID;
    status->error_code = g_strdup("ERR_SHORTCUT_CONFIGURATION_INVALID");
    status->detail = g_strdup(error != NULL ? error->message
                                           : "Portal shortcut removal failed.");
    set_error(error_text,
              "ERR_SHORTCUT_CONFIGURATION_INVALID: portal shortcut removal failed");
    return SHAULA_SHORTCUT_RESULT_CONFIG_INVALID;
  }
  (void)changed;
  initialize_portal_status(status);
  status->autostart_installed = options->dry_run;
  status->state = SHAULA_SHORTCUT_STATE_DISABLED;
  status->detail = g_strdup(options->dry_run
                                ? "Portal shortcuts would be disabled."
                                : "Portal shortcuts are disabled.");
  return SHAULA_SHORTCUT_RESULT_OK;
}

ShaulaShortcutResult
shaula_shortcut_portal_repair(const ShaulaShortcutOptions *options,
                              ShaulaShortcutStatus *status, char **error_text) {
  ShaulaShortcutResult queried =
      shaula_shortcut_portal_query(status, error_text);
  if (test_mode_enabled() || !status->provider_running)
    return shaula_shortcut_portal_enable(options, status, error_text);
  g_autoptr(GError) error = NULL;
  if (!provider_call("Reconfigure", &error)) {
    set_error(error_text,
              error != NULL ? error->message
                            : "ERR_SHORTCUT_PROVIDER_UNAVAILABLE: repair failed");
    status->state = SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE;
    g_set_str(&status->error_code, "ERR_SHORTCUT_PROVIDER_UNAVAILABLE");
    return SHAULA_SHORTCUT_RESULT_PROVIDER_FAILED;
  }
  return queried == SHAULA_SHORTCUT_RESULT_CONFIG_INVALID
             ? queried
             : shaula_shortcut_portal_query(status, error_text);
}
