#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SHAULA_EXIT_USAGE 2
#define SHAULA_EXIT_IPC_TIMEOUT 23
#define SHAULA_EXIT_BACKEND_UNAVAILABLE 30
#define SHAULA_EXIT_SELECTION_CANCELLED 33
#define SHAULA_EXIT_UNKNOWN 99

#define PORTAL_DEST "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT "/org/freedesktop/portal/desktop"
#define SCREENSHOT_IFACE "org.freedesktop.portal.Screenshot"
#define REQUEST_IFACE "org.freedesktop.portal.Request"

typedef struct {
  const char *backend;
  const char *mode;
  const char *output;
} ShaulaPortalArgs;

typedef struct {
  GMainLoop *loop;
  guint response;
  gboolean responded;
  gboolean timed_out;
  char *uri;
} ShaulaRequestState;

static gboolean parse_args(int argc, char **argv, ShaulaPortalArgs *out) {
  memset(out, 0, sizeof(*out));
  for (int i = 1; i < argc; i++) {
    if (g_strcmp0(argv[i], "--backend") == 0 && i + 1 < argc) {
      out->backend = argv[++i];
    } else if (g_strcmp0(argv[i], "--mode") == 0 && i + 1 < argc) {
      out->mode = argv[++i];
    } else if (g_strcmp0(argv[i], "--geometry") == 0 && i + 1 < argc) {
      i++;
    } else if (g_strcmp0(argv[i], "--output") == 0 && i + 1 < argc) {
      out->output = argv[++i];
    } else {
      return FALSE;
    }
  }
  return out->backend != NULL && out->mode != NULL && out->output != NULL &&
         g_strcmp0(out->backend, "portal-screenshot") == 0;
}

static gboolean mode_is_area(const char *mode) {
  return g_strcmp0(mode, "area") == 0 || g_strcmp0(mode, "quick") == 0;
}

static gboolean mode_is_screen(const char *mode) {
  return g_strcmp0(mode, "fullscreen") == 0 ||
         g_strcmp0(mode, "focused") == 0 ||
         g_strcmp0(mode, "all-screens") == 0 ||
         g_strcmp0(mode, "all_screens") == 0;
}

static guint timeout_ms(void) {
  const char *raw = g_getenv("SHAULA_PORTAL_TIMEOUT_MS");
  if (raw == NULL || *raw == '\0')
    return 120000;
  guint64 parsed = g_ascii_strtoull(raw, NULL, 10);
  if (parsed == 0 || parsed > G_MAXUINT)
    return 120000;
  return (guint)parsed;
}

static char *sender_path_element(GDBusConnection *connection) {
  const char *unique = g_dbus_connection_get_unique_name(connection);
  if (unique == NULL || *unique == '\0')
    return NULL;
  if (*unique == ':')
    unique++;
  char *sender = g_strdup(unique);
  for (char *p = sender; *p != '\0'; p++) {
    if (*p == '.')
      *p = '_';
  }
  return sender;
}

static guint32 available_targets(GDBusConnection *connection) {
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      connection, PORTAL_DEST, PORTAL_OBJECT, "org.freedesktop.DBus.Properties",
      "Get", g_variant_new("(ss)", SCREENSHOT_IFACE, "AvailableTargets"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &error);
  if (reply == NULL) {
    g_clear_error(&error);
    return 0;
  }
  GVariant *variant = NULL;
  g_variant_get(reply, "(v)", &variant);
  guint32 value = 0;
  if (variant != NULL && g_variant_is_of_type(variant, G_VARIANT_TYPE_UINT32))
    value = g_variant_get_uint32(variant);
  g_clear_pointer(&variant, g_variant_unref);
  g_variant_unref(reply);
  return value;
}

static guint32 target_for_mode(GDBusConnection *connection, const char *mode) {
  guint32 targets = available_targets(connection);
  if (mode_is_area(mode) && (targets & 4u) != 0)
    return 4u;
  if (mode_is_screen(mode) && (targets & 1u) != 0)
    return 1u;
  return 0u;
}

static gboolean ensure_parent_dir(const char *path, GError **error) {
  char *dir = g_path_get_dirname(path);
  if (dir == NULL || g_strcmp0(dir, ".") == 0) {
    g_free(dir);
    return TRUE;
  }
  if (g_mkdir_with_parents(dir, 0755) != 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "could not create output directory");
    g_free(dir);
    return FALSE;
  }
  g_free(dir);
  return TRUE;
}

static gboolean copy_uri_to_output(const char *uri, const char *output,
                                   GError **error) {
  if (!ensure_parent_dir(output, error))
    return FALSE;
  GFile *source = g_file_new_for_uri(uri);
  GFile *dest = g_file_new_for_path(output);
  gboolean ok = g_file_copy(source, dest, G_FILE_COPY_OVERWRITE, NULL, NULL,
                            NULL, error);
  g_object_unref(source);
  g_object_unref(dest);
  return ok;
}

static void on_response(GDBusConnection *connection, const gchar *sender_name,
                        const gchar *object_path, const gchar *interface_name,
                        const gchar *signal_name, GVariant *parameters,
                        gpointer user_data) {
  (void)connection;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  ShaulaRequestState *state = user_data;
  GVariant *results = NULL;
  g_variant_get(parameters, "(u@a{sv})", &state->response, &results);
  state->responded = TRUE;
  if (state->response == 0 && results != NULL) {
    GVariant *uri_value = g_variant_lookup_value(results, "uri",
                                                 G_VARIANT_TYPE_STRING);
    if (uri_value != NULL) {
      state->uri = g_strdup(g_variant_get_string(uri_value, NULL));
      g_variant_unref(uri_value);
    }
  }
  g_clear_pointer(&results, g_variant_unref);
  g_main_loop_quit(state->loop);
}

static gboolean on_timeout(gpointer user_data) {
  ShaulaRequestState *state = user_data;
  state->timed_out = TRUE;
  g_main_loop_quit(state->loop);
  return G_SOURCE_REMOVE;
}

static int portal_capture(const ShaulaPortalArgs *args) {
  if (!mode_is_area(args->mode) && !mode_is_screen(args->mode)) {
    fprintf(stderr, "portal-screenshot unsupported mode: %s\n", args->mode);
    return SHAULA_EXIT_USAGE;
  }

  GError *error = NULL;
  GDBusConnection *connection =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (connection == NULL) {
    fprintf(stderr, "portal session bus unavailable: %s\n",
            error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return SHAULA_EXIT_BACKEND_UNAVAILABLE;
  }

  char token[96];
  g_snprintf(token, sizeof(token), "shaula_%u_%u", (guint)getpid(),
             g_random_int());
  char *sender = sender_path_element(connection);
  if (sender == NULL) {
    g_object_unref(connection);
    return SHAULA_EXIT_BACKEND_UNAVAILABLE;
  }
  char *predicted_handle = g_strdup_printf(
      "/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
  g_free(sender);

  ShaulaRequestState state = {0};
  state.loop = g_main_loop_new(NULL, FALSE);

  guint subscription = g_dbus_connection_signal_subscribe(
      connection, PORTAL_DEST, REQUEST_IFACE, "Response", predicted_handle, NULL,
      G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_response, &state, NULL);

  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string(token));
  g_variant_builder_add(&options, "{sv}", "modal", g_variant_new_boolean(TRUE));
  if (mode_is_area(args->mode))
    g_variant_builder_add(&options, "{sv}", "interactive",
                          g_variant_new_boolean(TRUE));
  guint32 target = target_for_mode(connection, args->mode);
  if (target != 0)
    g_variant_builder_add(&options, "{sv}", "target",
                          g_variant_new_uint32(target));

  GVariant *reply = g_dbus_connection_call_sync(
      connection, PORTAL_DEST, PORTAL_OBJECT, SCREENSHOT_IFACE, "Screenshot",
      g_variant_new("(sa{sv})", "", &options), G_VARIANT_TYPE("(o)"),
      G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
  if (reply == NULL) {
    fprintf(stderr, "portal screenshot request failed: %s\n",
            error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    g_dbus_connection_signal_unsubscribe(connection, subscription);
    g_main_loop_unref(state.loop);
    g_free(predicted_handle);
    g_object_unref(connection);
    return SHAULA_EXIT_BACKEND_UNAVAILABLE;
  }

  const char *returned_handle_borrowed = NULL;
  g_variant_get(reply, "(&o)", &returned_handle_borrowed);
  char *returned_handle = returned_handle_borrowed != NULL
                              ? g_strdup(returned_handle_borrowed)
                              : NULL;
  guint returned_subscription = 0;
  if (returned_handle != NULL && g_strcmp0(returned_handle, predicted_handle) != 0) {
    returned_subscription = g_dbus_connection_signal_subscribe(
        connection, PORTAL_DEST, REQUEST_IFACE, "Response", returned_handle,
        NULL, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_response, &state, NULL);
  }
  g_variant_unref(reply);

  guint timeout_id = g_timeout_add(timeout_ms(), on_timeout, &state);
  g_main_loop_run(state.loop);
  if (!state.timed_out && timeout_id != 0)
    g_source_remove(timeout_id);

  if (state.timed_out && returned_handle != NULL) {
    g_dbus_connection_call_sync(connection, PORTAL_DEST, returned_handle,
                                REQUEST_IFACE, "Close", NULL, NULL,
                                G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
  }

  if (returned_subscription != 0)
    g_dbus_connection_signal_unsubscribe(connection, returned_subscription);
  g_dbus_connection_signal_unsubscribe(connection, subscription);
  g_main_loop_unref(state.loop);
  g_free(predicted_handle);
  g_free(returned_handle);
  g_object_unref(connection);

  if (state.timed_out) {
    g_free(state.uri);
    return SHAULA_EXIT_IPC_TIMEOUT;
  }
  if (!state.responded) {
    g_free(state.uri);
    return SHAULA_EXIT_BACKEND_UNAVAILABLE;
  }
  if (state.response == 1) {
    g_free(state.uri);
    return SHAULA_EXIT_SELECTION_CANCELLED;
  }
  if (state.response != 0 || state.uri == NULL || *state.uri == '\0') {
    g_free(state.uri);
    return SHAULA_EXIT_UNKNOWN;
  }

  gboolean copied = copy_uri_to_output(state.uri, args->output, &error);
  g_free(state.uri);
  if (!copied) {
    fprintf(stderr, "portal screenshot copy failed: %s\n",
            error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return SHAULA_EXIT_UNKNOWN;
  }
  return 0;
}

int main(int argc, char **argv) {
  ShaulaPortalArgs args;
  if (!parse_args(argc, argv, &args)) {
    fprintf(stderr,
            "usage: shaula-portal-screenshot --backend portal-screenshot "
            "--mode <mode> --output <path>\n");
    return SHAULA_EXIT_USAGE;
  }
  return portal_capture(&args);
}
