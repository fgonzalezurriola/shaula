#ifndef SHAULA_CLIPBOARD_PROVIDER_HANDOFF_H
#define SHAULA_CLIPBOARD_PROVIDER_HANDOFF_H

#include <gio/gio.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHAULA_CLIPBOARD_PROVIDER_BUS_NAME "dev.shaula.ClipboardProvider"
#define SHAULA_CLIPBOARD_PROVIDER_OBJECT_PATH "/dev/shaula/ClipboardProvider"

typedef struct ShaulaClipboardProviderHandoff
    ShaulaClipboardProviderHandoff;

typedef struct {
  void (*prepared)(const char *marker_mime, gpointer user_data);
  void (*committed)(gpointer user_data);
  void (*aborted)(const char *marker_mime, gpointer user_data);
  void (*timed_out)(const char *marker_mime, gpointer user_data);
} ShaulaClipboardProviderHandoffCallbacks;

typedef enum {
  SHAULA_CLIPBOARD_HANDOFF_NONE = 0,
  SHAULA_CLIPBOARD_HANDOFF_PREPARED = 1,
  SHAULA_CLIPBOARD_HANDOFF_FAILED = 2,
} ShaulaClipboardHandoffPrepareStatus;

/*
 * Registers the per-provider peer object used by ADR-0003 replacement. The
 * object is addressed through each process's unique bus name, so replacing the
 * well-known lease never makes the old provider unreachable before readiness.
 */
ShaulaClipboardProviderHandoff *shaula_clipboard_provider_handoff_new(
    GDBusConnection *connection,
    const ShaulaClipboardProviderHandoffCallbacks *callbacks,
    gpointer user_data, guint timeout_ms, GError **error);
void shaula_clipboard_provider_handoff_free(
    ShaulaClipboardProviderHandoff *handoff);

/* Borrowed until the next handoff callback or handoff destruction. */
const char *shaula_clipboard_provider_handoff_pending_marker(
    const ShaulaClipboardProviderHandoff *handoff);
gboolean shaula_clipboard_provider_handoff_has_pending(
    const ShaulaClipboardProviderHandoff *handoff);

/*
 * Asks the current well-known owner to remain alive for marker_mime. On
 * PREPARED, out_owner and out_token are GLib-owned and must be freed by the
 * caller. NONE means no previous Shaula provider owns the lease.
 */
ShaulaClipboardHandoffPrepareStatus
shaula_clipboard_provider_handoff_prepare_previous(
    GDBusConnection *connection, const char *marker_mime, char **out_owner,
    char **out_token, GError **error);

/* Complete or abort a previously prepared peer handoff. */
gboolean shaula_clipboard_provider_handoff_commit_previous(
    GDBusConnection *connection, const char *owner, const char *token,
    GError **error);
gboolean shaula_clipboard_provider_handoff_abort_previous(
    GDBusConnection *connection, const char *owner, const char *token,
    GError **error);

#ifdef __cplusplus
}
#endif

#endif
