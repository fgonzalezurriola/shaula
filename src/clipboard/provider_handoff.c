#include "provider_handoff.h"

#include <string.h>

#define SHAULA_HANDOFF_INTERFACE "dev.shaula.ClipboardProvider1"
#define SHAULA_HANDOFF_CALL_TIMEOUT_MS 2000

struct ShaulaClipboardProviderHandoff {
  GDBusConnection *connection;
  guint registration_id;
  ShaulaClipboardProviderHandoffCallbacks callbacks;
  gpointer user_data;
  guint timeout_ms;
  guint timeout_id;
  char *pending_sender;
  char *pending_token;
  char *pending_marker;
};

static const char handoff_xml[] =
    "<node>"
    "  <interface name='" SHAULA_HANDOFF_INTERFACE "'>"
    "    <method name='PrepareReplacement'>"
    "      <arg type='s' name='token' direction='in'/>"
    "      <arg type='s' name='marker_mime' direction='in'/>"
    "      <arg type='b' name='accepted' direction='out'/>"
    "    </method>"
    "    <method name='CommitReplacement'>"
    "      <arg type='s' name='token' direction='in'/>"
    "      <arg type='b' name='accepted' direction='out'/>"
    "    </method>"
    "    <method name='AbortReplacement'>"
    "      <arg type='s' name='token' direction='in'/>"
    "      <arg type='b' name='accepted' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *handoff_node_info(void) {
  static gsize initialized = 0;
  static GDBusNodeInfo *node_info = NULL;

  if (g_once_init_enter(&initialized)) {
    g_autoptr(GError) error = NULL;
    node_info = g_dbus_node_info_new_for_xml(handoff_xml, &error);
    if (node_info == NULL)
      g_error("invalid clipboard handoff introspection: %s",
              error != NULL ? error->message : "unknown error");
    g_once_init_leave(&initialized, 1);
  }
  return node_info;
}

static void clear_pending(ShaulaClipboardProviderHandoff *handoff,
                          gboolean remove_timeout) {
  if (remove_timeout && handoff->timeout_id != 0U)
    g_source_remove(handoff->timeout_id);
  handoff->timeout_id = 0U;
  g_clear_pointer(&handoff->pending_sender, g_free);
  g_clear_pointer(&handoff->pending_token, g_free);
  g_clear_pointer(&handoff->pending_marker, g_free);
}

static gboolean pending_matches(const ShaulaClipboardProviderHandoff *handoff,
                                const char *sender, const char *token) {
  return handoff->pending_sender != NULL && handoff->pending_token != NULL &&
         g_strcmp0(handoff->pending_sender, sender) == 0 &&
         g_strcmp0(handoff->pending_token, token) == 0;
}

static gboolean handoff_timeout(gpointer user_data) {
  ShaulaClipboardProviderHandoff *handoff = user_data;
  g_autofree char *marker = g_strdup(handoff->pending_marker);

  handoff->timeout_id = 0U;
  clear_pending(handoff, FALSE);
  if (handoff->callbacks.timed_out != NULL)
    handoff->callbacks.timed_out(marker, handoff->user_data);
  return G_SOURCE_REMOVE;
}

static gboolean marker_is_valid(const char *marker) {
  static const char prefix[] =
      "application/x-shaula-clipboard-provider-";
  return marker != NULL && g_str_has_prefix(marker, prefix) &&
         strlen(marker) > strlen(prefix) && strchr(marker, '/') == marker + 11;
}

static void on_method_call(GDBusConnection *connection, const gchar *sender,
                           const gchar *object_path,
                           const gchar *interface_name,
                           const gchar *method_name, GVariant *parameters,
                           GDBusMethodInvocation *invocation,
                           gpointer user_data) {
  (void)connection;
  (void)object_path;
  (void)interface_name;
  ShaulaClipboardProviderHandoff *handoff = user_data;

  if (g_str_equal(method_name, "PrepareReplacement")) {
    const char *token = NULL;
    const char *marker = NULL;
    g_variant_get(parameters, "(&s&s)", &token, &marker);
    gboolean accepted =
        handoff->pending_sender == NULL && token != NULL && *token != '\0' &&
        marker_is_valid(marker);
    if (accepted) {
      handoff->pending_sender = g_strdup(sender);
      handoff->pending_token = g_strdup(token);
      handoff->pending_marker = g_strdup(marker);
      accepted = handoff->pending_sender != NULL &&
                 handoff->pending_token != NULL &&
                 handoff->pending_marker != NULL;
      if (accepted) {
        handoff->timeout_id =
            g_timeout_add(handoff->timeout_ms, handoff_timeout, handoff);
        accepted = handoff->timeout_id != 0U;
      }
      if (!accepted)
        clear_pending(handoff, TRUE);
    }
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", accepted));
    if (accepted && handoff->callbacks.prepared != NULL)
      handoff->callbacks.prepared(marker, handoff->user_data);
    return;
  }

  const char *token = NULL;
  g_variant_get(parameters, "(&s)", &token);
  gboolean accepted = pending_matches(handoff, sender, token);
  if (g_str_equal(method_name, "CommitReplacement")) {
    if (accepted)
      clear_pending(handoff, TRUE);
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", accepted));
    if (accepted && handoff->callbacks.committed != NULL)
      handoff->callbacks.committed(handoff->user_data);
    return;
  }

  if (g_str_equal(method_name, "AbortReplacement")) {
    g_autofree char *marker =
        accepted ? g_strdup(handoff->pending_marker) : NULL;
    if (accepted)
      clear_pending(handoff, TRUE);
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", accepted));
    if (accepted && handoff->callbacks.aborted != NULL)
      handoff->callbacks.aborted(marker, handoff->user_data);
    return;
  }

  g_dbus_method_invocation_return_dbus_error(
      invocation, "dev.shaula.ClipboardProvider.Error.UnknownMethod",
      "unknown clipboard handoff method");
}

static const GDBusInterfaceVTable handoff_vtable = {
    .method_call = on_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

ShaulaClipboardProviderHandoff *shaula_clipboard_provider_handoff_new(
    GDBusConnection *connection,
    const ShaulaClipboardProviderHandoffCallbacks *callbacks,
    gpointer user_data, guint timeout_ms, GError **error) {
  if (connection == NULL || timeout_ms == 0U) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "invalid clipboard handoff arguments");
    return NULL;
  }

  ShaulaClipboardProviderHandoff *handoff =
      g_try_new0(ShaulaClipboardProviderHandoff, 1);
  if (handoff == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                        "could not allocate clipboard handoff state");
    return NULL;
  }
  handoff->connection = g_object_ref(connection);
  if (callbacks != NULL)
    handoff->callbacks = *callbacks;
  handoff->user_data = user_data;
  handoff->timeout_ms = timeout_ms;

  GDBusNodeInfo *node_info = handoff_node_info();
  handoff->registration_id = g_dbus_connection_register_object(
      connection, SHAULA_CLIPBOARD_PROVIDER_OBJECT_PATH,
      node_info->interfaces[0], &handoff_vtable, handoff, NULL, error);
  if (handoff->registration_id == 0U) {
    shaula_clipboard_provider_handoff_free(handoff);
    return NULL;
  }
  return handoff;
}

void shaula_clipboard_provider_handoff_free(
    ShaulaClipboardProviderHandoff *handoff) {
  if (handoff == NULL)
    return;
  clear_pending(handoff, TRUE);
  if (handoff->registration_id != 0U && handoff->connection != NULL)
    g_dbus_connection_unregister_object(handoff->connection,
                                        handoff->registration_id);
  g_clear_object(&handoff->connection);
  g_free(handoff);
}

const char *shaula_clipboard_provider_handoff_pending_marker(
    const ShaulaClipboardProviderHandoff *handoff) {
  return handoff != NULL ? handoff->pending_marker : NULL;
}

gboolean shaula_clipboard_provider_handoff_has_pending(
    const ShaulaClipboardProviderHandoff *handoff) {
  return handoff != NULL && handoff->pending_sender != NULL;
}

static gboolean is_legacy_handoff_error(const GError *error) {
  if (error == NULL || !g_dbus_error_is_remote_error(error))
    return FALSE;
  g_autofree char *remote_name = g_dbus_error_get_remote_error(error);
  return g_strcmp0(remote_name, "org.freedesktop.DBus.Error.UnknownMethod") ==
             0 ||
         g_strcmp0(remote_name, "org.freedesktop.DBus.Error.UnknownObject") ==
             0 ||
         g_strcmp0(remote_name,
                   "org.freedesktop.DBus.Error.UnknownInterface") == 0;
}

static gboolean call_peer(GDBusConnection *connection, const char *owner,
                          const char *method, GVariant *parameters,
                          GError **error) {
  g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
      connection, owner, SHAULA_CLIPBOARD_PROVIDER_OBJECT_PATH,
      SHAULA_HANDOFF_INTERFACE, method, parameters, G_VARIANT_TYPE("(b)"),
      G_DBUS_CALL_FLAGS_NONE, SHAULA_HANDOFF_CALL_TIMEOUT_MS, NULL, error);
  if (reply == NULL)
    return FALSE;
  gboolean accepted = FALSE;
  g_variant_get(reply, "(b)", &accepted);
  if (!accepted && error != NULL)
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "clipboard provider rejected %s", method);
  return accepted;
}

ShaulaClipboardHandoffPrepareStatus
shaula_clipboard_provider_handoff_prepare_previous(
    GDBusConnection *connection, const char *marker_mime, char **out_owner,
    char **out_token, GError **error) {
  if (out_owner != NULL)
    *out_owner = NULL;
  if (out_token != NULL)
    *out_token = NULL;
  if (connection == NULL || !marker_is_valid(marker_mime) ||
      out_owner == NULL || out_token == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "invalid previous-provider handoff arguments");
    return SHAULA_CLIPBOARD_HANDOFF_FAILED;
  }

  g_autoptr(GError) owner_error = NULL;
  g_autoptr(GVariant) owner_reply = g_dbus_connection_call_sync(
      connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "GetNameOwner",
      g_variant_new("(s)", SHAULA_CLIPBOARD_PROVIDER_BUS_NAME),
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE,
      SHAULA_HANDOFF_CALL_TIMEOUT_MS, NULL, &owner_error);
  if (owner_reply == NULL) {
    if (g_error_matches(owner_error, G_DBUS_ERROR,
                        G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
        g_error_matches(owner_error, G_DBUS_ERROR,
                        G_DBUS_ERROR_SERVICE_UNKNOWN))
      return SHAULA_CLIPBOARD_HANDOFF_NONE;
    g_propagate_error(error, g_steal_pointer(&owner_error));
    return SHAULA_CLIPBOARD_HANDOFF_FAILED;
  }

  const char *owner_borrowed = NULL;
  g_variant_get(owner_reply, "(&s)", &owner_borrowed);
  const char *self = g_dbus_connection_get_unique_name(connection);
  if (owner_borrowed == NULL || g_strcmp0(owner_borrowed, self) == 0)
    return SHAULA_CLIPBOARD_HANDOFF_NONE;

  g_autofree char *token = g_uuid_string_random();
  if (token == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                        "could not allocate clipboard handoff token");
    return SHAULA_CLIPBOARD_HANDOFF_FAILED;
  }
  g_autoptr(GError) prepare_error = NULL;
  g_autoptr(GVariant) prepare_reply = g_dbus_connection_call_sync(
      connection, owner_borrowed, SHAULA_CLIPBOARD_PROVIDER_OBJECT_PATH,
      SHAULA_HANDOFF_INTERFACE, "PrepareReplacement",
      g_variant_new("(ss)", token, marker_mime), G_VARIANT_TYPE("(b)"),
      G_DBUS_CALL_FLAGS_NONE, SHAULA_HANDOFF_CALL_TIMEOUT_MS, NULL,
      &prepare_error);
  if (prepare_reply == NULL) {
    if (is_legacy_handoff_error(prepare_error))
      return SHAULA_CLIPBOARD_HANDOFF_NONE;
    g_propagate_error(error, g_steal_pointer(&prepare_error));
    return SHAULA_CLIPBOARD_HANDOFF_FAILED;
  }
  gboolean accepted = FALSE;
  g_variant_get(prepare_reply, "(b)", &accepted);
  if (!accepted) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "clipboard provider rejected PrepareReplacement");
    return SHAULA_CLIPBOARD_HANDOFF_FAILED;
  }

  *out_owner = g_strdup(owner_borrowed);
  *out_token = g_steal_pointer(&token);
  if (*out_owner == NULL || *out_token == NULL) {
    g_clear_pointer(out_owner, g_free);
    g_clear_pointer(out_token, g_free);
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                        "could not retain clipboard handoff peer");
    return SHAULA_CLIPBOARD_HANDOFF_FAILED;
  }
  return SHAULA_CLIPBOARD_HANDOFF_PREPARED;
}

gboolean shaula_clipboard_provider_handoff_commit_previous(
    GDBusConnection *connection, const char *owner, const char *token,
    GError **error) {
  if (connection == NULL || owner == NULL || token == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "invalid clipboard handoff commit arguments");
    return FALSE;
  }
  return call_peer(connection, owner, "CommitReplacement",
                   g_variant_new("(s)", token), error);
}

gboolean shaula_clipboard_provider_handoff_abort_previous(
    GDBusConnection *connection, const char *owner, const char *token,
    GError **error) {
  if (connection == NULL || owner == NULL || token == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "invalid clipboard handoff abort arguments");
    return FALSE;
  }
  return call_peer(connection, owner, "AbortReplacement",
                   g_variant_new("(s)", token), error);
}
