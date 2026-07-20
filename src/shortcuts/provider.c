#include "provider_core.h"
#include "state.h"

#include "runtime/helper_resolution.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PORTAL_DEST "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT "/org/freedesktop/portal/desktop"
#define GLOBAL_SHORTCUTS_IFACE "org.freedesktop.portal.GlobalShortcuts"
#define REQUEST_IFACE "org.freedesktop.portal.Request"
#define SESSION_IFACE "org.freedesktop.portal.Session"
#define PROVIDER_NAME "dev.shaula.ShortcutProvider"
#define PROVIDER_OBJECT "/dev/shaula/ShortcutProvider"
#define PROVIDER_IFACE "dev.shaula.ShortcutProvider"

static const char provider_introspection_xml[] =
    "<node>"
    " <interface name='dev.shaula.ShortcutProvider'>"
    "  <method name='GetStatus'>"
    "   <arg name='status' type='a{sv}' direction='out'/>"
    "  </method>"
    "  <method name='Reconfigure'/>"
    "  <method name='Stop'/>"
    " </interface>"
    "</node>";

typedef struct {
  const char *id;
  const char *description;
  const char *preferred_trigger;
} ShortcutSpec;

static const ShortcutSpec shortcut_specs[] = {
    {"quick", "Quick capture", "CTRL+SHIFT+1"},
    {"area", "Area capture", "CTRL+SHIFT+2"},
    {"fullscreen", "Fullscreen capture", "CTRL+SHIFT+3"},
    {"all_screens", "All-screens capture", "CTRL+SHIFT+4"},
};

typedef struct {
  GMainLoop *loop;
  guint response;
  gboolean responded;
  gboolean timed_out;
  GVariant *results;
} PortalRequest;

typedef struct {
  GMainLoop *loop;
  GDBusConnection *bus;
  GDBusNodeInfo *introspection;
  guint object_registration;
  guint activated_subscription;
  guint deactivated_subscription;
  guint changed_subscription;
  guint session_closed_subscription;
  guint portal_owner_subscription;
  guint reconnect_source;
  gboolean stopping;
  guint portal_version;
  char *session_handle;
  char *shaula_binary;
  GSubprocess *capture_process;
  ShaulaShortcutProviderGate gate;
  ShaulaShortcutProviderState persisted;
} Provider;

static guint request_timeout_ms(void) {
  const char *raw = g_getenv("SHAULA_SHORTCUT_PORTAL_TIMEOUT_MS");
  if (raw == NULL || raw[0] == '\0')
    return 120000U;
  guint64 value = g_ascii_strtoull(raw, NULL, 10);
  return value > 0U && value <= G_MAXUINT ? (guint)value : 120000U;
}

static char *token_new(const char *prefix) {
  return g_strdup_printf("%s_%u_%u", prefix, (guint)getpid(), g_random_int());
}

static char *sender_path_element(GDBusConnection *connection) {
  const char *unique = g_dbus_connection_get_unique_name(connection);
  if (unique == NULL || unique[0] == '\0')
    return NULL;
  if (unique[0] == ':')
    unique++;
  char *sender = g_strdup(unique);
  for (char *cursor = sender; cursor[0] != '\0'; cursor++) {
    if (cursor[0] == '.')
      cursor[0] = '_';
  }
  return sender;
}

static char *predicted_request_path(GDBusConnection *connection,
                                    const char *token) {
  g_autofree char *sender = sender_path_element(connection);
  return sender != NULL
             ? g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s",
                               sender, token)
             : NULL;
}

static void portal_request_clear(PortalRequest *request) {
  g_clear_pointer(&request->results, g_variant_unref);
  if (request->loop != NULL)
    g_main_loop_unref(request->loop);
  *request = (PortalRequest){0};
}

static void on_request_response(GDBusConnection *connection,
                                const gchar *sender_name,
                                const gchar *object_path,
                                const gchar *interface_name,
                                const gchar *signal_name,
                                GVariant *parameters, gpointer user_data) {
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  PortalRequest *request = user_data;
  GVariant *results = NULL;
  g_variant_get(parameters, "(u@a{sv})", &request->response, &results);
  request->responded = TRUE;
  request->results = results;
  g_main_loop_quit(request->loop);
}

static gboolean on_request_timeout(gpointer user_data) {
  PortalRequest *request = user_data;
  request->timed_out = TRUE;
  g_main_loop_quit(request->loop);
  return G_SOURCE_REMOVE;
}

/*
 * Executes one request-style portal method. Subscription happens before the
 * method call, preserving the Request path race contract. Timeout maps to
 * ERR_IPC_TIMEOUT and the Request object is closed best-effort.
 */
static gboolean portal_request_call(Provider *provider, const char *method,
                                    const char *handle_token,
                                    GVariant *parameters,
                                    PortalRequest *request, GError **error) {
  *request = (PortalRequest){.loop = g_main_loop_new(NULL, FALSE)};
  g_autofree char *predicted =
      predicted_request_path(provider->bus, handle_token);
  if (predicted == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "portal request path could not be predicted");
    return FALSE;
  }

  guint predicted_subscription = g_dbus_connection_signal_subscribe(
      provider->bus, PORTAL_DEST, REQUEST_IFACE, "Response", predicted, NULL,
      G_DBUS_SIGNAL_FLAGS_NONE, on_request_response, request, NULL);
  GVariant *reply = g_dbus_connection_call_sync(
      provider->bus, PORTAL_DEST, PORTAL_OBJECT, GLOBAL_SHORTCUTS_IFACE, method,
      parameters, G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
      error);
  if (reply == NULL) {
    g_dbus_connection_signal_unsubscribe(provider->bus,
                                         predicted_subscription);
    return FALSE;
  }

  const char *returned_borrowed = NULL;
  g_variant_get(reply, "(&o)", &returned_borrowed);
  g_autofree char *returned = g_strdup(returned_borrowed);
  g_variant_unref(reply);
  guint returned_subscription = 0;
  if (returned != NULL && !g_str_equal(returned, predicted)) {
    returned_subscription = g_dbus_connection_signal_subscribe(
        provider->bus, PORTAL_DEST, REQUEST_IFACE, "Response", returned, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_request_response, request, NULL);
  }

  guint timeout_source =
      g_timeout_add(request_timeout_ms(), on_request_timeout, request);
  g_main_loop_run(request->loop);
  if (!request->timed_out && timeout_source != 0U)
    g_source_remove(timeout_source);
  if (request->timed_out && returned != NULL) {
    (void)g_dbus_connection_call_sync(
        provider->bus, PORTAL_DEST, returned, REQUEST_IFACE, "Close", NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
  }
  if (returned_subscription != 0U)
    g_dbus_connection_signal_unsubscribe(provider->bus,
                                         returned_subscription);
  g_dbus_connection_signal_unsubscribe(provider->bus, predicted_subscription);
  if (request->timed_out) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                        "portal request timed out");
    return FALSE;
  }
  if (!request->responded) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "portal request returned no response");
    return FALSE;
  }
  return TRUE;
}

static void provider_write_state(Provider *provider, ShaulaShortcutState state,
                                 const char *error_code, const char *detail) {
  provider->persisted.state = state;
  provider->persisted.portal_version = provider->portal_version;
  g_set_str(&provider->persisted.error_code,
            error_code != NULL ? error_code : "");
  g_set_str(&provider->persisted.detail, detail != NULL ? detail : "");
  g_autoptr(GError) error = NULL;
  if (!shaula_shortcut_provider_state_save(&provider->persisted, &error))
    g_printerr("shortcut provider state write failed: %s\n",
               error != NULL ? error->message : "unknown error");
}

static gboolean provider_query_portal_version(Provider *provider,
                                              GError **error) {
  GVariant *reply = g_dbus_connection_call_sync(
      provider->bus, PORTAL_DEST, PORTAL_OBJECT,
      "org.freedesktop.DBus.Properties", "Get",
      g_variant_new("(ss)", GLOBAL_SHORTCUTS_IFACE, "version"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, error);
  if (reply == NULL)
    return FALSE;
  GVariant *value = NULL;
  g_variant_get(reply, "(v)", &value);
  g_variant_unref(reply);
  if (value == NULL || !g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) {
    g_clear_pointer(&value, g_variant_unref);
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "portal version response was invalid");
    return FALSE;
  }
  provider->portal_version = g_variant_get_uint32(value);
  g_variant_unref(value);
  if (provider->portal_version < 1U) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                        "GlobalShortcuts portal version is unsupported");
    return FALSE;
  }
  return TRUE;
}

static gboolean provider_create_session(Provider *provider, guint *response,
                                        GError **error) {
  g_autofree char *handle_token = token_new("shaula_create");
  g_autofree char *session_token = token_new("shaula_session");
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string(handle_token));
  g_variant_builder_add(&options, "{sv}", "session_handle_token",
                        g_variant_new_string(session_token));
  PortalRequest request = {0};
  gboolean called = portal_request_call(
      provider, "CreateSession", handle_token,
      g_variant_new("(a{sv})", &options), &request, error);
  if (!called) {
    portal_request_clear(&request);
    return FALSE;
  }
  *response = request.response;
  if (request.response == 0U && request.results != NULL) {
    GVariant *handle = g_variant_lookup_value(request.results, "session_handle",
                                              G_VARIANT_TYPE_STRING);
    if (handle != NULL) {
      g_set_str(&provider->session_handle,
                g_variant_get_string(handle, NULL));
      g_variant_unref(handle);
    }
  }
  gboolean valid = request.response != 0U ||
                   (provider->session_handle != NULL &&
                    g_variant_is_object_path(provider->session_handle));
  portal_request_clear(&request);
  if (!valid) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "portal session handle was invalid");
    return FALSE;
  }
  return TRUE;
}

static GVariant *shortcut_request_array(void) {
  GVariantBuilder shortcuts;
  g_variant_builder_init(&shortcuts, G_VARIANT_TYPE("a(sa{sv})"));
  for (guint i = 0; i < G_N_ELEMENTS(shortcut_specs); i++) {
    GVariantBuilder properties;
    g_variant_builder_init(&properties, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&properties, "{sv}", "description",
                          g_variant_new_string(shortcut_specs[i].description));
    g_variant_builder_add(
        &properties, "{sv}", "preferred_trigger",
        g_variant_new_string(shortcut_specs[i].preferred_trigger));
    g_variant_builder_add(&shortcuts, "(s@a{sv})", shortcut_specs[i].id,
                          g_variant_builder_end(&properties));
  }
  return g_variant_builder_end(&shortcuts);
}

static int shortcut_index(const char *id) {
  for (guint i = 0; i < G_N_ELEMENTS(shortcut_specs); i++) {
    if (g_strcmp0(id, shortcut_specs[i].id) == 0)
      return (int)i;
  }
  return -1;
}

static guint provider_apply_shortcut_list(Provider *provider,
                                          GVariant *shortcuts) {
  for (guint i = 0; i < G_N_ELEMENTS(provider->persisted.triggers); i++)
    g_clear_pointer(&provider->persisted.triggers[i], g_free);
  if (shortcuts == NULL ||
      !g_variant_is_of_type(shortcuts, G_VARIANT_TYPE("a(sa{sv})")))
    return 0U;

  gboolean seen[G_N_ELEMENTS(shortcut_specs)] = {FALSE};
  GVariantIter iterator;
  g_variant_iter_init(&iterator, shortcuts);
  const char *id = NULL;
  GVariant *properties = NULL;
  while (g_variant_iter_next(&iterator, "(&s@a{sv})", &id, &properties)) {
    int index = shortcut_index(id);
    if (index >= 0 && !seen[index]) {
      seen[index] = TRUE;
      GVariant *trigger = g_variant_lookup_value(
          properties, "trigger_description", G_VARIANT_TYPE_STRING);
      provider->persisted.triggers[index] =
          g_strdup(trigger != NULL ? g_variant_get_string(trigger, NULL)
                                   : shortcut_specs[index].preferred_trigger);
      g_clear_pointer(&trigger, g_variant_unref);
    }
    g_variant_unref(properties);
  }
  guint count = 0U;
  for (guint i = 0; i < G_N_ELEMENTS(seen); i++)
    count += seen[i] ? 1U : 0U;
  return count;
}

static gboolean provider_list_shortcuts(Provider *provider, guint *bound_count,
                                        GError **error) {
  g_autofree char *handle_token = token_new("shaula_list");
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string(handle_token));
  PortalRequest request = {0};
  gboolean called = portal_request_call(
      provider, "ListShortcuts", handle_token,
      g_variant_new("(o@a{sv})", provider->session_handle,
                    g_variant_builder_end(&options)),
      &request, error);
  if (!called) {
    portal_request_clear(&request);
    return FALSE;
  }
  if (request.response != 0U) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "ListShortcuts was rejected with response %u", request.response);
    portal_request_clear(&request);
    return FALSE;
  }
  GVariant *shortcuts = request.results != NULL
                            ? g_variant_lookup_value(
                                  request.results, "shortcuts",
                                  G_VARIANT_TYPE("a(sa{sv})"))
                            : NULL;
  if (shortcuts == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "ListShortcuts returned no shortcut list");
    portal_request_clear(&request);
    return FALSE;
  }
  *bound_count = provider_apply_shortcut_list(provider, shortcuts);
  g_variant_unref(shortcuts);
  portal_request_clear(&request);
  return TRUE;
}

static gboolean provider_bind_shortcuts(Provider *provider, guint *response,
                                        guint *bound_count, GError **error) {
  g_autofree char *handle_token = token_new("shaula_bind");
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string(handle_token));
  PortalRequest request = {0};
  gboolean called = portal_request_call(
      provider, "BindShortcuts", handle_token,
      g_variant_new("(o@a(sa{sv})s@a{sv})", provider->session_handle,
                    shortcut_request_array(), "",
                    g_variant_builder_end(&options)),
      &request, error);
  if (!called) {
    portal_request_clear(&request);
    return FALSE;
  }
  *response = request.response;
  if (request.response == 0U) {
    GVariant *shortcuts = request.results != NULL
                              ? g_variant_lookup_value(
                                    request.results, "shortcuts",
                                    G_VARIANT_TYPE("a(sa{sv})"))
                              : NULL;
    if (shortcuts == NULL) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                          "BindShortcuts returned no shortcut list");
      portal_request_clear(&request);
      return FALSE;
    }
    *bound_count = provider_apply_shortcut_list(provider, shortcuts);
    g_variant_unref(shortcuts);
  }
  portal_request_clear(&request);
  return TRUE;
}

static void provider_unsubscribe_session(Provider *provider) {
  guint *subscriptions[] = {
      &provider->activated_subscription, &provider->deactivated_subscription,
      &provider->changed_subscription, &provider->session_closed_subscription};
  for (guint i = 0; i < G_N_ELEMENTS(subscriptions); i++) {
    if (*subscriptions[i] != 0U && provider->bus != NULL)
      g_dbus_connection_signal_unsubscribe(provider->bus, *subscriptions[i]);
    *subscriptions[i] = 0U;
  }
}

static void provider_close_session(Provider *provider, gboolean call_close) {
  provider_unsubscribe_session(provider);
  if (call_close && provider->bus != NULL && provider->session_handle != NULL) {
    (void)g_dbus_connection_call_sync(
        provider->bus, PORTAL_DEST, provider->session_handle, SESSION_IFACE,
        "Close", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
  }
  g_clear_pointer(&provider->session_handle, g_free);
  provider->persisted.activation_ready = FALSE;
}

static void on_capture_finished(GObject *source, GAsyncResult *result,
                                gpointer user_data) {
  Provider *provider = user_data;
  g_autoptr(GError) error = NULL;
  (void)g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error);
  g_clear_object(&provider->capture_process);
  shaula_shortcut_provider_capture_finished(&provider->gate);
}

static void on_shortcut_activated(GDBusConnection *connection,
                                  const gchar *sender_name,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *signal_name,
                                  GVariant *parameters, gpointer user_data) {
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  Provider *provider = user_data;
  const char *session = NULL;
  const char *shortcut_id = NULL;
  guint64 timestamp = 0U;
  GVariant *options = NULL;
  g_variant_get(parameters, "(&o&st@a{sv})", &session, &shortcut_id, &timestamp,
                &options);
  g_clear_pointer(&options, g_variant_unref);
  if (provider->stopping ||
      g_strcmp0(session, provider->session_handle) != 0 ||
      !shaula_shortcut_provider_should_dispatch(&provider->gate, shortcut_id,
                                                timestamp))
    return;

  ShaulaShortcutAction action = shaula_shortcut_action_from_id(shortcut_id);
  g_auto(GStrv) argv =
      shaula_shortcut_action_argv(provider->shaula_binary, action);
  if (argv == NULL) {
    shaula_shortcut_provider_capture_finished(&provider->gate);
    return;
  }
  g_autoptr(GError) error = NULL;
  provider->capture_process = g_subprocess_newv(
      (const gchar *const *)argv,
      G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
      &error);
  if (provider->capture_process == NULL) {
    g_printerr("shortcut capture launch failed: %s\n",
               error != NULL ? error->message : "unknown error");
    shaula_shortcut_provider_capture_finished(&provider->gate);
    return;
  }
  g_subprocess_wait_async(provider->capture_process, NULL, on_capture_finished,
                          provider);
}

static void on_shortcut_deactivated(GDBusConnection *connection,
                                    const gchar *sender_name,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *signal_name,
                                    GVariant *parameters, gpointer user_data) {
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  Provider *provider = user_data;
  const char *session = NULL;
  const char *shortcut_id = NULL;
  guint64 timestamp = 0U;
  GVariant *options = NULL;
  g_variant_get(parameters, "(&o&st@a{sv})", &session, &shortcut_id, &timestamp,
                &options);
  (void)timestamp;
  g_clear_pointer(&options, g_variant_unref);
  if (g_strcmp0(session, provider->session_handle) == 0)
    shaula_shortcut_provider_deactivated(&provider->gate, shortcut_id);
}

static void provider_publish_bound_state(Provider *provider, guint bound_count) {
  provider->persisted.activation_ready =
      provider->activated_subscription != 0U &&
      provider->deactivated_subscription != 0U &&
      provider->session_closed_subscription != 0U;
  if (bound_count == G_N_ELEMENTS(shortcut_specs) &&
      provider->persisted.activation_ready) {
    provider_write_state(provider, SHAULA_SHORTCUT_STATE_ACTIVE, "",
                         "Portal session is active.");
  } else {
    provider_write_state(provider, SHAULA_SHORTCUT_STATE_PERMISSION_DENIED,
                         "ERR_SHORTCUT_PERMISSION_DENIED",
                         "The desktop did not approve all four shortcuts.");
  }
}

static void on_shortcuts_changed(GDBusConnection *connection,
                                 const gchar *sender_name,
                                 const gchar *object_path,
                                 const gchar *interface_name,
                                 const gchar *signal_name,
                                 GVariant *parameters, gpointer user_data) {
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  Provider *provider = user_data;
  const char *session = NULL;
  GVariant *shortcuts = NULL;
  g_variant_get(parameters, "(&o@a(sa{sv}))", &session, &shortcuts);
  if (g_strcmp0(session, provider->session_handle) == 0) {
    guint count = provider_apply_shortcut_list(provider, shortcuts);
    provider_publish_bound_state(provider, count);
  }
  g_clear_pointer(&shortcuts, g_variant_unref);
}

static gboolean provider_connect_portal(Provider *provider, GError **error);
static void provider_schedule_reconnect(Provider *provider);

static void on_session_closed(GDBusConnection *connection,
                              const gchar *sender_name,
                              const gchar *object_path,
                              const gchar *interface_name,
                              const gchar *signal_name, GVariant *parameters,
                              gpointer user_data) {
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  (void)parameters;
  Provider *provider = user_data;
  if (provider->stopping)
    return;
  provider_close_session(provider, FALSE);
  provider_schedule_reconnect(provider);
}

static void provider_subscribe_session(Provider *provider) {
  provider->activated_subscription = g_dbus_connection_signal_subscribe(
      provider->bus, PORTAL_DEST, GLOBAL_SHORTCUTS_IFACE, "Activated",
      PORTAL_OBJECT, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_shortcut_activated,
      provider, NULL);
  provider->deactivated_subscription = g_dbus_connection_signal_subscribe(
      provider->bus, PORTAL_DEST, GLOBAL_SHORTCUTS_IFACE, "Deactivated",
      PORTAL_OBJECT, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_shortcut_deactivated,
      provider, NULL);
  provider->changed_subscription = g_dbus_connection_signal_subscribe(
      provider->bus, PORTAL_DEST, GLOBAL_SHORTCUTS_IFACE, "ShortcutsChanged",
      PORTAL_OBJECT, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_shortcuts_changed,
      provider, NULL);
  provider->session_closed_subscription = g_dbus_connection_signal_subscribe(
      provider->bus, PORTAL_DEST, SESSION_IFACE, "Closed",
      provider->session_handle, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
      on_session_closed, provider, NULL);
}

/* Establishes and verifies CreateSession, BindShortcuts, ListShortcuts, and the
 * activation signal subscriptions before reporting the portal backend active. */
static gboolean provider_connect_portal(Provider *provider, GError **error) {
  provider_close_session(provider, FALSE);
  provider->portal_version = 0U;
  provider_write_state(provider, SHAULA_SHORTCUT_STATE_PERMISSION_PENDING, "",
                       "Waiting for desktop shortcut approval.");
  if (!provider_query_portal_version(provider, error))
    return FALSE;

  guint create_response = 0U;
  if (!provider_create_session(provider, &create_response, error))
    return FALSE;
  if (create_response == 1U) {
    provider_write_state(provider, SHAULA_SHORTCUT_STATE_PERMISSION_DENIED,
                         "ERR_SHORTCUT_PERMISSION_DENIED",
                         "The desktop declined the shortcut session.");
    return TRUE;
  }
  if (create_response != 0U) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "CreateSession failed with response %u", create_response);
    return FALSE;
  }

  provider_subscribe_session(provider);
  guint bind_response = 0U;
  guint bound_count = 0U;
  if (!provider_bind_shortcuts(provider, &bind_response, &bound_count, error))
    return FALSE;
  if (bind_response == 1U) {
    provider_publish_bound_state(provider, 0U);
    return TRUE;
  }
  if (bind_response != 0U) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "BindShortcuts failed with response %u", bind_response);
    return FALSE;
  }

  guint listed_count = 0U;
  if (!provider_list_shortcuts(provider, &listed_count, error))
    return FALSE;
  provider_publish_bound_state(provider, MIN(bound_count, listed_count));
  shaula_shortcut_provider_reconnected(&provider->gate);
  return TRUE;
}

static gboolean on_reconnect(gpointer user_data) {
  Provider *provider = user_data;
  provider->reconnect_source = 0U;
  if (provider->stopping)
    return G_SOURCE_REMOVE;
  g_autoptr(GError) error = NULL;
  if (!provider_connect_portal(provider, &error)) {
    provider_write_state(provider, SHAULA_SHORTCUT_STATE_RECONNECTING,
                         "ERR_SHORTCUT_SESSION_LOST",
                         error != NULL ? error->message
                                       : "Portal session reconnect failed.");
    provider_schedule_reconnect(provider);
  }
  return G_SOURCE_REMOVE;
}

static void provider_schedule_reconnect(Provider *provider) {
  if (provider->stopping || provider->reconnect_source != 0U)
    return;
  provider_write_state(provider, SHAULA_SHORTCUT_STATE_RECONNECTING,
                       "ERR_SHORTCUT_SESSION_LOST",
                       "Portal session was lost; reconnecting.");
  guint delay =
      shaula_shortcut_provider_next_reconnect_delay_ms(&provider->gate);
  provider->reconnect_source = g_timeout_add(delay, on_reconnect, provider);
}

static void on_portal_owner_changed(GDBusConnection *connection,
                                    const gchar *sender_name,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *signal_name,
                                    GVariant *parameters, gpointer user_data) {
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  Provider *provider = user_data;
  const char *name = NULL;
  const char *old_owner = NULL;
  const char *new_owner = NULL;
  g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
  (void)old_owner;
  if (!g_str_equal(name, PORTAL_DEST) || provider->stopping)
    return;
  provider_close_session(provider, FALSE);
  if (new_owner[0] == '\0') {
    provider_schedule_reconnect(provider);
  } else {
    if (provider->reconnect_source != 0U) {
      g_source_remove(provider->reconnect_source);
      provider->reconnect_source = 0U;
    }
    provider->reconnect_source = g_idle_add(on_reconnect, provider);
  }
}

static GVariant *provider_status_variant(Provider *provider) {
  GVariantBuilder status;
  g_variant_builder_init(&status, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(
      &status, "{sv}", "state",
      g_variant_new_string(
          shaula_shortcut_state_token(provider->persisted.state)));
  g_variant_builder_add(&status, "{sv}", "portal_version",
                        g_variant_new_uint32(provider->portal_version));
  g_variant_builder_add(&status, "{sv}", "activation_ready",
                        g_variant_new_boolean(
                            provider->persisted.activation_ready));
  g_variant_builder_add(
      &status, "{sv}", "detail",
      g_variant_new_string(provider->persisted.detail != NULL
                               ? provider->persisted.detail
                               : ""));
  g_variant_builder_add(
      &status, "{sv}", "error_code",
      g_variant_new_string(provider->persisted.error_code != NULL
                               ? provider->persisted.error_code
                               : ""));
  const char *triggers[G_N_ELEMENTS(shortcut_specs) + 1U] = {0};
  for (guint i = 0; i < G_N_ELEMENTS(shortcut_specs); i++)
    triggers[i] = provider->persisted.triggers[i] != NULL
                      ? provider->persisted.triggers[i]
                      : shortcut_specs[i].preferred_trigger;
  g_variant_builder_add(&status, "{sv}", "triggers",
                        g_variant_new_strv(triggers, -1));
  return g_variant_builder_end(&status);
}

static gboolean provider_reconfigure(Provider *provider, GError **error) {
  if (provider->session_handle == NULL) {
    provider_schedule_reconnect(provider);
    return TRUE;
  }
  if (provider->portal_version >= 2U) {
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GVariant *reply = g_dbus_connection_call_sync(
        provider->bus, PORTAL_DEST, PORTAL_OBJECT, GLOBAL_SHORTCUTS_IFACE,
        "ConfigureShortcuts",
        g_variant_new("(os@a{sv})", provider->session_handle, "",
                      g_variant_builder_end(&options)),
        NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, error);
    if (reply == NULL && error != NULL && *error != NULL)
      return FALSE;
    g_clear_pointer(&reply, g_variant_unref);
    provider_write_state(provider, SHAULA_SHORTCUT_STATE_PERMISSION_PENDING, "",
                         "Desktop shortcut configuration was opened.");
    return TRUE;
  }
  provider_close_session(provider, TRUE);
  return provider_connect_portal(provider, error);
}

static void on_provider_method_call(GDBusConnection *connection,
                                    const gchar *sender, const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *method_name,
                                    GVariant *parameters,
                                    GDBusMethodInvocation *invocation,
                                    gpointer user_data) {
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)interface_name;
  (void)parameters;
  Provider *provider = user_data;
  if (g_str_equal(method_name, "GetStatus")) {
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(@a{sv})", provider_status_variant(provider)));
    return;
  }
  if (g_str_equal(method_name, "Reconfigure")) {
    g_autoptr(GError) error = NULL;
    if (!provider_reconfigure(provider, &error)) {
      g_dbus_method_invocation_return_gerror(invocation, error);
      return;
    }
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }
  if (g_str_equal(method_name, "Stop")) {
    provider->stopping = TRUE;
    if (provider->reconnect_source != 0U) {
      g_source_remove(provider->reconnect_source);
      provider->reconnect_source = 0U;
    }
    provider_close_session(provider, TRUE);
    provider_write_state(provider, SHAULA_SHORTCUT_STATE_DISABLED, "",
                         "Shortcut provider stopped.");
    g_dbus_method_invocation_return_value(invocation, NULL);
    g_main_loop_quit(provider->loop);
    return;
  }
  g_dbus_method_invocation_return_error(
      invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
      "Unknown shortcut provider method: %s", method_name);
}

static const GDBusInterfaceVTable provider_vtable = {
    .method_call = on_provider_method_call,
};

static ShaulaProviderNameResult provider_request_name(GDBusConnection *bus,
                                                      GError **error) {
  GVariant *reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "RequestName",
      g_variant_new("(su)", PROVIDER_NAME, 4U), G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, 3000, NULL, error);
  if (reply == NULL)
    return SHAULA_PROVIDER_NAME_FAILED;
  guint32 value = 0U;
  g_variant_get(reply, "(u)", &value);
  g_variant_unref(reply);
  return shaula_shortcut_provider_name_result(value);
}

static void provider_clear(Provider *provider) {
  provider->stopping = TRUE;
  if (provider->reconnect_source != 0U)
    g_source_remove(provider->reconnect_source);
  provider_close_session(provider, TRUE);
  if (provider->portal_owner_subscription != 0U && provider->bus != NULL)
    g_dbus_connection_signal_unsubscribe(provider->bus,
                                         provider->portal_owner_subscription);
  if (provider->object_registration != 0U && provider->bus != NULL)
    g_dbus_connection_unregister_object(provider->bus,
                                        provider->object_registration);
  g_clear_object(&provider->capture_process);
  g_clear_pointer(&provider->introspection, g_dbus_node_info_unref);
  g_clear_object(&provider->bus);
  g_clear_pointer(&provider->loop, g_main_loop_unref);
  g_clear_pointer(&provider->session_handle, g_free);
  g_clear_pointer(&provider->shaula_binary, g_free);
  shaula_shortcut_provider_gate_clear(&provider->gate);
  shaula_shortcut_provider_state_clear(&provider->persisted);
}

int main(void) {
  Provider provider = {0};
  shaula_shortcut_provider_gate_init(&provider.gate);
  shaula_shortcut_provider_state_init(&provider.persisted);
  provider.shaula_binary =
      shaula_executable_resolve_helper("SHAULA_BIN", "shaula");
  provider.loop = g_main_loop_new(NULL, FALSE);

  g_autoptr(GError) error = NULL;
  provider.bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (provider.bus == NULL) {
    provider_write_state(&provider, SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE,
                         "ERR_SHORTCUT_PROVIDER_UNAVAILABLE",
                         error != NULL ? error->message
                                       : "Session bus is unavailable.");
    provider_clear(&provider);
    return 65;
  }

  ShaulaProviderNameResult name = provider_request_name(provider.bus, &error);
  if (name == SHAULA_PROVIDER_NAME_ALREADY_RUNNING) {
    provider_clear(&provider);
    return 0;
  }
  if (name != SHAULA_PROVIDER_NAME_ACQUIRED) {
    provider_write_state(&provider, SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE,
                         "ERR_SHORTCUT_PROVIDER_UNAVAILABLE",
                         error != NULL ? error->message
                                       : "Provider name could not be acquired.");
    provider_clear(&provider);
    return 65;
  }

  provider.introspection =
      g_dbus_node_info_new_for_xml(provider_introspection_xml, &error);
  if (provider.introspection == NULL) {
    provider_write_state(&provider, SHAULA_SHORTCUT_STATE_CONFIG_INVALID,
                         "ERR_CONFIG_INVALID", error->message);
    provider_clear(&provider);
    return 67;
  }
  provider.object_registration = g_dbus_connection_register_object(
      provider.bus, PROVIDER_OBJECT, provider.introspection->interfaces[0],
      &provider_vtable, &provider, NULL, &error);
  if (provider.object_registration == 0U) {
    provider_write_state(&provider, SHAULA_SHORTCUT_STATE_PROVIDER_UNAVAILABLE,
                         "ERR_SHORTCUT_PROVIDER_UNAVAILABLE", error->message);
    provider_clear(&provider);
    return 65;
  }

  provider.portal_owner_subscription = g_dbus_connection_signal_subscribe(
      provider.bus, "org.freedesktop.DBus", "org.freedesktop.DBus",
      "NameOwnerChanged", "/org/freedesktop/DBus", PORTAL_DEST,
      G_DBUS_SIGNAL_FLAGS_NONE, on_portal_owner_changed, &provider, NULL);

  if (!provider_connect_portal(&provider, &error)) {
    provider_write_state(
        &provider, SHAULA_SHORTCUT_STATE_UNSUPPORTED,
        "ERR_SHORTCUTS_UNSUPPORTED",
        error != NULL ? error->message
                      : "GlobalShortcuts portal session could not be created.");
    provider_clear(&provider);
    return 63;
  }
  g_main_loop_run(provider.loop);
  provider_clear(&provider);
  return 0;
}
