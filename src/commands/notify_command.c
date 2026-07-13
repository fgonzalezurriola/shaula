#include "commands.h"

#include "notify/request.h"
#include "runtime/process_exec.h"
#include "support.h"

#include <glib.h>
#include <string.h>

static ShaulaNotifySpan notify_span(const char *value) {
  return (ShaulaNotifySpan){.data = (const guint8 *)value,
                            .length = value != NULL ? strlen(value) : 0};
}

static char **notify_argv_new(const ShaulaNotifySendArgs *args) {
  char **argv = g_new0(char *, args->length + 1);
  for (gsize i = 0; i < args->length; i++)
    argv[i] = g_strndup((const char *)args->items[i].data,
                        args->items[i].length);
  return argv;
}

static gboolean notify_request_run(const char *summary, const char *body,
                                   const char *image_path, guint timeout_ms,
                                   gboolean transient, gboolean with_action,
                                   char **action_output) {
  ShaulaNotifyRequest request;
  shaula_notify_request_init(&request);
  request.summary = notify_span(summary);
  request.body = notify_span(body);
  request.urgency = SHAULA_NOTIFY_URGENCY_NORMAL;
  request.timeout_ms = timeout_ms;
  request.transient = transient ? 1 : 0;
  if (image_path != NULL) {
    request.has_image_path = 1;
    request.image_path = notify_span(image_path);
  }
  if (with_action) {
    request.has_action = 1;
    request.action_id = notify_span("default");
    request.action_label = notify_span("Show in folder");
  }

  for (guint attempt = 0; attempt < (image_path != NULL ? 2U : 1U);
       attempt++) {
    ShaulaNotifySendArgs args;
    shaula_notify_send_args_init(&args);
    ShaulaNotifyImageMode image_mode =
        attempt == 0 ? SHAULA_NOTIFY_IMAGE_MODE_HINT
                     : SHAULA_NOTIFY_IMAGE_MODE_ICON;
    if (shaula_notify_send_args_build(&request, image_mode, &args) !=
        SHAULA_NOTIFY_STATUS_OK) {
      shaula_notify_send_args_clear(&args);
      return FALSE;
    }

    g_auto(GStrv) argv = notify_argv_new(&args);
    g_autofree char *stdout_text = NULL;
    int exit_code = 0;
    gboolean spawned =
        shaula_process_run_sync((const char *const *)argv, NULL,
                                action_output != NULL ? &stdout_text : NULL,
                                NULL, &exit_code) ==
        SHAULA_PROCESS_STATUS_OK;
    shaula_notify_send_args_clear(&args);
    if (spawned && exit_code == 0) {
      if (action_output != NULL) {
        g_strstrip(stdout_text);
        *action_output = g_steal_pointer(&stdout_text);
      }
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean reveal_file(const char *path) {
  g_autofree char *absolute = g_canonicalize_filename(path, NULL);
  ShaulaNotifyOwnedBytes uri = {0};
  if (shaula_notify_file_uri_build(notify_span(absolute), &uri) !=
      SHAULA_NOTIFY_STATUS_OK)
    return FALSE;
  g_autofree char *uri_text =
      g_strndup((const char *)uri.data, uri.length);
  shaula_notify_owned_bytes_clear(&uri);
  g_autofree char *items = g_strdup_printf("['%s']", uri_text);
  char *gdbus_argv[] = {
      "gdbus",
      "call",
      "--session",
      "--dest",
      "org.freedesktop.FileManager1",
      "--object-path",
      "/org/freedesktop/FileManager1",
      "--method",
      "org.freedesktop.FileManager1.ShowItems",
      items,
      "",
      NULL,
  };
  int exit_code = 0;
  if (shaula_process_run_sync((const char *const *)gdbus_argv, NULL, NULL,
                              NULL, &exit_code) ==
          SHAULA_PROCESS_STATUS_OK &&
      exit_code == 0)
    return TRUE;

  g_autofree char *parent = g_path_get_dirname(absolute);
  char *open_argv[] = {"xdg-open", parent, NULL};
  return shaula_process_run_sync((const char *const *)open_argv, NULL, NULL,
                                 NULL, &exit_code) ==
             SHAULA_PROCESS_STATUS_OK &&
         exit_code == 0;
}

static int notify_test(const char *kind) {
  const char *summary = "Screenshot captured";
  const char *body = "You can paste the image from the clipboard.";
  const char *image_path = NULL;
  guint timeout_ms = 2500;
  gboolean transient = TRUE;
  if (g_str_equal(kind, "saved")) {
    body = "Saved to screenshots folder.";
    image_path = "/tmp/shaula-notify-test.png";
    timeout_ms = 6000;
  } else if (g_str_equal(kind, "error")) {
    summary = "Could not copy screenshot";
    body = "Copy failed";
    timeout_ms = 5000;
    transient = FALSE;
  }

  gboolean delivered = notify_request_run(summary, body, image_path, timeout_ms,
                                           transient, FALSE, NULL);
  g_autofree char *kind_json = shaula_command_json_string(kind);
  g_autofree char *result =
      g_strdup_printf("{\"kind\":%s,\"delivered\":%s}", kind_json,
                      shaula_command_json_bool(delivered));
  g_autofree char *timestamp = shaula_command_json_timestamp();
  g_print("{\"ok\":%s,\"contract_version\":\"1.0.0\","
          "\"command\":\"notify test\",\"timestamp\":\"%s\","
          "\"result\":%s,\"warnings\":[]}\n",
          shaula_command_json_bool(delivered), timestamp, result);
  return 0;
}

int shaula_notify_command_run(int argc, char **argv) {
  if (argc >= 4 && g_str_equal(argv[2], "__saved-action-listener")) {
    g_autofree char *absolute = g_canonicalize_filename(argv[3], NULL);
    const char *image_path = argc >= 5 ? argv[4] : NULL;
    g_autofree char *action = NULL;
    if (notify_request_run("Screenshot captured",
                           "Saved to screenshots folder.", image_path, 6000,
                           TRUE, TRUE, &action) &&
        action != NULL &&
        (g_str_equal(action, "default") ||
         g_str_equal(action, "show-in-folder") ||
         g_str_equal(action, "reveal-file")))
      (void)reveal_file(absolute);
    return 0;
  }

  if (argc >= 4 && g_str_equal(argv[2], "reveal-file")) {
    (void)reveal_file(argv[3]);
    return 0;
  }

  if (argc < 3 || !g_str_equal(argv[2], "test"))
    return shaula_command_write_error(
        "notify test", "ERR_CLI_USAGE",
        "usage: shaula notify test [--kind copied|saved|error]", "{}");

  const char *kind = "copied";
  for (int i = 3; i < argc; i++) {
    if (!g_str_equal(argv[i], "--kind"))
      return shaula_command_write_error("notify test", "ERR_CLI_USAGE",
                                        "unsupported flag", "{}");
    if (i + 1 >= argc)
      return shaula_command_write_error(
          "notify test", "ERR_CLI_USAGE",
          "--kind requires copied, saved, or error", "{}");
    kind = argv[++i];
    if (!g_str_equal(kind, "copied") && !g_str_equal(kind, "saved") &&
        !g_str_equal(kind, "error"))
      return shaula_command_write_error(
          "notify test", "ERR_CLI_USAGE",
          "--kind must be copied, saved, or error", "{}");
  }
  return notify_test(kind);
}
