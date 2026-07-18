#include "provider_handoff.h"

#include <gdk/gdk.h>
#include <glib-unix.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SHAULA_PROVIDER_MAX_HEADER_LINE 256U
#define SHAULA_PROVIDER_MAX_PNG_BYTES (128U * 1024U * 1024U)
#define SHAULA_PROVIDER_MAX_TEXT_BYTES (4U * 1024U * 1024U)
#define SHAULA_PROVIDER_HANDOFF_TIMEOUT_MS 5000U

#define SHAULA_PROVIDER_EXIT_USAGE 2
#define SHAULA_PROVIDER_EXIT_PROTOCOL 31
#define SHAULA_PROVIDER_EXIT_UNAVAILABLE 35
#define SHAULA_PROVIDER_EXIT_UNKNOWN 99

typedef struct {
  GMainLoop *loop;
  GdkClipboard *clipboard;
  GdkContentProvider *provider;
  GDBusConnection *bus;
  ShaulaClipboardProviderHandoff *handoff;
  char *previous_owner;
  char *previous_token;
  gboolean previous_prepared;
  gboolean ready;
  gboolean name_acquired;
  int exit_code;
} ProviderState;

typedef struct {
  char *mime;
  GBytes *bytes;
} ProviderPayload;

static void provider_payload_clear(ProviderPayload *payload) {
  g_clear_pointer(&payload->mime, g_free);
  g_clear_pointer(&payload->bytes, g_bytes_unref);
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(ProviderPayload, provider_payload_clear)

static gboolean read_exact(int fd, guint8 *data, gsize length) {
  gsize offset = 0U;
  while (offset < length) {
    ssize_t count = read(fd, data + offset, length - offset);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      return FALSE;
    }
    if (count == 0)
      return FALSE;
    offset += (gsize)count;
  }
  return TRUE;
}

static gboolean read_line(int fd, char *buffer, gsize capacity) {
  if (buffer == NULL || capacity < 2U)
    return FALSE;
  gsize length = 0U;
  while (length + 1U < capacity) {
    char byte = '\0';
    ssize_t count = read(fd, &byte, 1);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      return FALSE;
    }
    if (count == 0)
      return FALSE;
    buffer[length++] = byte;
    if (byte == '\n') {
      buffer[length] = '\0';
      return TRUE;
    }
  }
  return FALSE;
}

/*
 * Parse the complete private stdin protocol before touching GTK. This prevents
 * a malformed caller from claiming clipboard ownership and keeps protocol
 * failures deterministic and stderr-only.
 */
static gboolean read_payload(ProviderPayload *out) {
  char line[SHAULA_PROVIDER_MAX_HEADER_LINE];
  if (!read_line(STDIN_FILENO, line, sizeof(line)) ||
      !g_str_equal(line, "SHAULA-CLIPBOARD/1\n"))
    return FALSE;

  if (!read_line(STDIN_FILENO, line, sizeof(line)) ||
      !g_str_has_prefix(line, "mime:"))
    return FALSE;
  g_strchomp(line);
  out->mime = g_strdup(line + strlen("mime:"));
  if (out->mime == NULL ||
      (!g_str_equal(out->mime, "image/png") &&
       !g_str_equal(out->mime, "text/plain;charset=utf-8")))
    return FALSE;

  if (!read_line(STDIN_FILENO, line, sizeof(line)) ||
      !g_str_has_prefix(line, "length:"))
    return FALSE;
  g_strchomp(line);
  const char *length_text = line + strlen("length:");
  char *end = NULL;
  guint64 parsed = g_ascii_strtoull(length_text, &end, 10);
  const guint64 maximum = g_str_equal(out->mime, "image/png")
                              ? SHAULA_PROVIDER_MAX_PNG_BYTES
                              : SHAULA_PROVIDER_MAX_TEXT_BYTES;
  if (end == length_text || *end != '\0' || parsed == 0U ||
      parsed > maximum || parsed > G_MAXSIZE)
    return FALSE;

  if (!read_line(STDIN_FILENO, line, sizeof(line)) ||
      !g_str_equal(line, "\n"))
    return FALSE;

  guint8 *data = g_try_malloc((gsize)parsed);
  if (data == NULL || !read_exact(STDIN_FILENO, data, (gsize)parsed)) {
    g_free(data);
    return FALSE;
  }
  if (g_str_equal(out->mime, "text/plain;charset=utf-8") &&
      !g_utf8_validate((const char *)data, (gssize)parsed, NULL)) {
    g_free(data);
    return FALSE;
  }
  out->bytes = g_bytes_new_take(data, (gsize)parsed);
  return out->bytes != NULL;
}

static GdkContentProvider *provider_for_payload(const ProviderPayload *payload,
                                                const char *marker_mime) {
  GdkContentProvider *providers[3] = {NULL, NULL, NULL};
  gsize count = 0U;

  if (g_str_equal(payload->mime, "image/png")) {
    providers[count++] =
        gdk_content_provider_new_for_bytes("image/png", payload->bytes);
  } else {
    providers[count++] = gdk_content_provider_new_for_bytes(
        "text/plain;charset=utf-8", payload->bytes);
    providers[count++] =
        gdk_content_provider_new_for_bytes("text/plain", payload->bytes);
  }

  g_autoptr(GBytes) marker_bytes = g_bytes_new_static("1", 1U);
  providers[count++] =
      gdk_content_provider_new_for_bytes(marker_mime, marker_bytes);
  for (gsize index = 0; index < count; index += 1) {
    if (providers[index] == NULL) {
      for (gsize cleanup = 0; cleanup < count; cleanup += 1)
        g_clear_object(&providers[cleanup]);
      return NULL;
    }
  }

  /* gdk_content_provider_new_union() takes ownership of every provider. */
  return gdk_content_provider_new_union(providers, count);
}

static gboolean clipboard_has_mime(GdkClipboard *clipboard,
                                   const char *mime) {
  if (clipboard == NULL || mime == NULL)
    return FALSE;
  GdkContentFormats *formats = gdk_clipboard_get_formats(clipboard);
  return formats != NULL &&
         gdk_content_formats_contain_mime_type(formats, mime);
}

static void quit_provider(ProviderState *state, int exit_code) {
  state->exit_code = exit_code;
  g_main_loop_quit(state->loop);
}

static void restore_previous_or_exit(ProviderState *state,
                                     const char *marker_mime,
                                     gboolean replacement_owned_name) {
  if (gdk_clipboard_is_local(state->clipboard))
    return;
  if (!clipboard_has_mime(state->clipboard, marker_mime)) {
    quit_provider(state, 0);
    return;
  }
  if (replacement_owned_name) {
    quit_provider(state, 0);
    return;
  }
  if (!gdk_clipboard_set_content(state->clipboard, state->provider)) {
    fprintf(stderr, "clipboard ownership restore failed\n");
    quit_provider(state, SHAULA_PROVIDER_EXIT_UNAVAILABLE);
  }
}

static void on_handoff_prepared(const char *marker_mime,
                                gpointer user_data) {
  (void)marker_mime;
  (void)user_data;
}

static void on_handoff_committed(gpointer user_data) {
  ProviderState *state = user_data;
  quit_provider(state, 0);
}

static void on_handoff_aborted(const char *marker_mime, gpointer user_data) {
  ProviderState *state = user_data;
  restore_previous_or_exit(state, marker_mime, FALSE);
}

static gboolean replacement_owns_bus_name(ProviderState *state) {
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
      state->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "GetNameOwner",
      g_variant_new("(s)", SHAULA_CLIPBOARD_PROVIDER_BUS_NAME),
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
  if (reply == NULL)
    return FALSE;

  const char *owner = NULL;
  g_variant_get(reply, "(&s)", &owner);
  return owner != NULL &&
         g_strcmp0(owner, g_dbus_connection_get_unique_name(state->bus)) != 0;
}

static void on_handoff_timed_out(const char *marker_mime,
                                 gpointer user_data) {
  ProviderState *state = user_data;
  restore_previous_or_exit(state, marker_mime,
                           replacement_owns_bus_name(state));
}

static void on_clipboard_changed(GdkClipboard *clipboard, gpointer user_data) {
  ProviderState *state = user_data;
  if (gdk_clipboard_is_local(clipboard))
    return;

  const char *pending_marker =
      shaula_clipboard_provider_handoff_pending_marker(state->handoff);
  if (pending_marker != NULL && clipboard_has_mime(clipboard, pending_marker))
    return;

  quit_provider(state, 0);
}

static void on_display_closed(GdkDisplay *display, gboolean is_error,
                              gpointer user_data) {
  (void)display;
  (void)is_error;
  ProviderState *state = user_data;
  quit_provider(state, 0);
}

static void commit_previous_after_readiness(ProviderState *state) {
  if (!state->previous_prepared)
    return;
  g_autoptr(GError) error = NULL;
  if (!shaula_clipboard_provider_handoff_commit_previous(
          state->bus, state->previous_owner, state->previous_token, &error)) {
    fprintf(stderr, "clipboard replacement commit failed: %s\n",
            error != NULL ? error->message : "unknown error");
    return;
  }
  state->previous_prepared = FALSE;
}

static void on_bus_name_acquired(GDBusConnection *connection,
                                 const gchar *name, gpointer user_data) {
  (void)connection;
  (void)name;
  ProviderState *state = user_data;
  if (!gdk_clipboard_is_local(state->clipboard)) {
    fprintf(stderr, "clipboard ownership was lost before readiness\n");
    quit_provider(state, SHAULA_PROVIDER_EXIT_UNAVAILABLE);
    return;
  }

  state->name_acquired = TRUE;
  if (state->ready)
    return;

  state->ready = TRUE;
  fputs("READY shaula-clipboard/1\n", stdout);
  fflush(stdout);
  commit_previous_after_readiness(state);
}

static void on_bus_name_lost(GDBusConnection *connection, const gchar *name,
                             gpointer user_data) {
  (void)connection;
  (void)name;
  ProviderState *state = user_data;
  state->name_acquired = FALSE;
  if (shaula_clipboard_provider_handoff_has_pending(state->handoff))
    return;
  if (!state->ready) {
    fprintf(stderr, "clipboard replacement lease unavailable\n");
    quit_provider(state, SHAULA_PROVIDER_EXIT_UNAVAILABLE);
    return;
  }
  quit_provider(state, 0);
}

static gboolean on_termination_signal(gpointer user_data) {
  ProviderState *state = user_data;
  quit_provider(state, 0);
  return G_SOURCE_REMOVE;
}

static void abort_previous_if_needed(ProviderState *state) {
  if (!state->previous_prepared || state->ready)
    return;
  g_autoptr(GError) error = NULL;
  if (!shaula_clipboard_provider_handoff_abort_previous(
          state->bus, state->previous_owner, state->previous_token, &error)) {
    fprintf(stderr, "clipboard replacement abort failed: %s\n",
            error != NULL ? error->message : "unknown error");
  }
  state->previous_prepared = FALSE;
}

/*
 * ADR-0003 replacement order is explicit: prepare the old provider, claim the
 * Wayland clipboard with a unique marker, replace the session-bus lease, emit
 * readiness, and only then commit the old provider's exit. External clipboard
 * formats do not carry the marker and still terminate the active provider.
 */
static int serve_payload(const ProviderPayload *payload) {
  gtk_init();
  GdkDisplay *display = gdk_display_get_default();
  if (display == NULL) {
    fprintf(stderr, "clipboard display unavailable\n");
    return SHAULA_PROVIDER_EXIT_UNAVAILABLE;
  }
  GdkClipboard *clipboard = gdk_display_get_clipboard(display);
  if (clipboard == NULL) {
    fprintf(stderr, "clipboard selection unavailable\n");
    return SHAULA_PROVIDER_EXIT_UNAVAILABLE;
  }
  g_autoptr(GError) bus_error = NULL;
  g_autoptr(GDBusConnection) bus =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &bus_error);
  if (bus == NULL) {
    fprintf(stderr, "clipboard session bus unavailable: %s\n",
            bus_error != NULL ? bus_error->message : "unknown error");
    return SHAULA_PROVIDER_EXIT_UNAVAILABLE;
  }

  ProviderState state = {
      .loop = g_main_loop_new(NULL, FALSE),
      .clipboard = clipboard,
      .provider = NULL,
      .bus = bus,
      .handoff = NULL,
      .previous_owner = NULL,
      .previous_token = NULL,
      .previous_prepared = FALSE,
      .ready = FALSE,
      .name_acquired = FALSE,
      .exit_code = SHAULA_PROVIDER_EXIT_UNKNOWN,
  };
  if (state.loop == NULL)
    return SHAULA_PROVIDER_EXIT_UNKNOWN;

  const ShaulaClipboardProviderHandoffCallbacks callbacks = {
      .prepared = on_handoff_prepared,
      .committed = on_handoff_committed,
      .aborted = on_handoff_aborted,
      .timed_out = on_handoff_timed_out,
  };
  state.handoff = shaula_clipboard_provider_handoff_new(
      bus, &callbacks, &state, SHAULA_PROVIDER_HANDOFF_TIMEOUT_MS, &bus_error);
  if (state.handoff == NULL) {
    fprintf(stderr, "clipboard handoff object unavailable: %s\n",
            bus_error != NULL ? bus_error->message : "unknown error");
    g_main_loop_unref(state.loop);
    return SHAULA_PROVIDER_EXIT_UNAVAILABLE;
  }

  g_autofree char *marker_uuid = g_uuid_string_random();
  g_autofree char *marker_mime =
      marker_uuid != NULL
          ? g_strdup_printf("application/x-shaula-clipboard-provider-%s",
                            marker_uuid)
          : NULL;
  if (marker_mime == NULL) {
    shaula_clipboard_provider_handoff_free(state.handoff);
    g_main_loop_unref(state.loop);
    return SHAULA_PROVIDER_EXIT_UNKNOWN;
  }

  ShaulaClipboardHandoffPrepareStatus prepare_status =
      shaula_clipboard_provider_handoff_prepare_previous(
          bus, marker_mime, &state.previous_owner, &state.previous_token,
          &bus_error);
  if (prepare_status == SHAULA_CLIPBOARD_HANDOFF_FAILED) {
    fprintf(stderr, "clipboard previous-provider handoff failed: %s\n",
            bus_error != NULL ? bus_error->message : "unknown error");
    shaula_clipboard_provider_handoff_free(state.handoff);
    g_main_loop_unref(state.loop);
    g_free(state.previous_owner);
    g_free(state.previous_token);
    return SHAULA_PROVIDER_EXIT_UNAVAILABLE;
  }
  state.previous_prepared =
      prepare_status == SHAULA_CLIPBOARD_HANDOFF_PREPARED;

  g_autoptr(GdkContentProvider) provider =
      provider_for_payload(payload, marker_mime);
  state.provider = provider;
  if (provider == NULL || !gdk_clipboard_set_content(clipboard, provider)) {
    fprintf(stderr, "clipboard ownership failed\n");
    abort_previous_if_needed(&state);
    shaula_clipboard_provider_handoff_free(state.handoff);
    g_main_loop_unref(state.loop);
    g_free(state.previous_owner);
    g_free(state.previous_token);
    return SHAULA_PROVIDER_EXIT_UNAVAILABLE;
  }

  g_signal_connect(clipboard, "changed", G_CALLBACK(on_clipboard_changed),
                   &state);
  g_signal_connect(display, "closed", G_CALLBACK(on_display_closed), &state);
  guint sigterm_id = g_unix_signal_add(SIGTERM, on_termination_signal, &state);
  guint sigint_id = g_unix_signal_add(SIGINT, on_termination_signal, &state);
  guint name_owner_id = g_bus_own_name_on_connection(
      bus, SHAULA_CLIPBOARD_PROVIDER_BUS_NAME,
      G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
          G_BUS_NAME_OWNER_FLAGS_REPLACE,
      on_bus_name_acquired, on_bus_name_lost, &state, NULL);
  if (name_owner_id == 0U) {
    abort_previous_if_needed(&state);
    shaula_clipboard_provider_handoff_free(state.handoff);
    g_main_loop_unref(state.loop);
    g_free(state.previous_owner);
    g_free(state.previous_token);
    return SHAULA_PROVIDER_EXIT_UNAVAILABLE;
  }

  g_main_loop_run(state.loop);

  abort_previous_if_needed(&state);
  g_bus_unown_name(name_owner_id);
  if (sigterm_id != 0U)
    g_source_remove(sigterm_id);
  if (sigint_id != 0U)
    g_source_remove(sigint_id);
  g_signal_handlers_disconnect_by_data(clipboard, &state);
  g_signal_handlers_disconnect_by_data(display, &state);
  shaula_clipboard_provider_handoff_free(state.handoff);
  g_main_loop_unref(state.loop);
  g_free(state.previous_owner);
  g_free(state.previous_token);
  return state.exit_code;
}

int main(int argc, char **argv) {
  (void)argv;
  if (argc != 1) {
    fprintf(stderr, "usage: shaula-clipboard-provider\n");
    return SHAULA_PROVIDER_EXIT_USAGE;
  }
  setvbuf(stdout, NULL, _IONBF, 0);

  g_auto(ProviderPayload) payload = {0};
  if (!read_payload(&payload)) {
    fprintf(stderr, "invalid clipboard provider protocol\n");
    return SHAULA_PROVIDER_EXIT_PROTOCOL;
  }
  return serve_payload(&payload);
}
