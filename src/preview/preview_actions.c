#include "preview_actions.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <sys/wait.h>
#include <stdio.h>

#include "preview_clipboard.h"
#include "preview_image_io.h"
#include "preview_paths.h"
#include "preview_render.h"
#include "preview_toolbar.h"

extern gboolean shaula_preview_notify(const char *summary, const char *body,
                                      const char *image_path, gboolean transient,
                                      int timeout_ms);

static gboolean env_enabled(const char *name, gboolean fallback) {
  const char *value = g_getenv(name);
  if (value == NULL || value[0] == '\0')
    return fallback;
  return g_strcmp0(value, "0") != 0 && g_ascii_strcasecmp(value, "false") != 0;
}

static gboolean success_notifications_enabled(void) {
  return env_enabled("SHAULA_NOTIFY_SUCCESS", TRUE);
}

static gboolean error_notifications_enabled(void) {
  return env_enabled("SHAULA_NOTIFY_ERRORS", TRUE);
}

static const char *thumbnail_path_or_null(const char *path) {
  if (!env_enabled("SHAULA_NOTIFY_THUMBNAILS", TRUE))
    return NULL;
  return path;
}

static void report_error(const char *context, GError *error) {
  if (error != NULL) {
    fprintf(stderr, "shaula-preview %s failed: %s\n", context, error->message);
    g_error_free(error);
  } else {
    fprintf(stderr, "shaula-preview %s failed\n", context);
  }
}

static void notify_save_failure(ShaulaPreviewState *state, const char *context,
                                GError *error) {
  char *body = g_strdup(error != NULL ? error->message : "Save failed");
  report_error(context, error);
  if (state != NULL && error_notifications_enabled())
    state->notified = shaula_preview_notify("Could not save screenshot", body,
                                            NULL, FALSE, 6000);
  else if (state != NULL)
    state->notified = TRUE;
  g_free(body);
}

static char *render_or_original_png(ShaulaPreviewState *state,
                                    gboolean *is_temp, GError **error) {
  *is_temp = FALSE;
  if (shaula_preview_state_has_modifications(state) ||
      !shaula_image_io_path_has_png_extension(state->document.path)) {
    *is_temp = TRUE;
    return shaula_render_composited_png_temp(state, error);
  }
  return g_strdup(state->document.path);
}

static char *shaula_notify_action_log_path(void) {
  const char *configured = g_getenv("SHAULA_NOTIFY_ACTION_LOG");
  if (configured != NULL && configured[0] != '\0')
    return g_strdup(configured);

  const char *state_home = g_getenv("XDG_STATE_HOME");
  if (state_home != NULL && state_home[0] != '\0') {
    char *dir = g_build_filename(state_home, "shaula", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *path = g_build_filename(dir, "notify-actions.log", NULL);
    g_free(dir);
    return path;
  }

  const char *home = g_get_home_dir();
  if (home != NULL && home[0] != '\0') {
    char *dir = g_build_filename(home, ".local", "state", "shaula", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *path = g_build_filename(dir, "notify-actions.log", NULL);
    g_free(dir);
    return path;
  }

  return g_strdup("/tmp/shaula-notify-actions.log");
}

static gboolean shaula_preview_notify_saved(const char *path,
                                            const char *image_path,
                                            gboolean transient,
                                            int timeout_ms) {
  char *absolute_path = g_canonicalize_filename(path, NULL);
  const char *body = "Saved to screenshots folder.";
  const char *shaula_bin = g_getenv("SHAULA_BIN");
  if (shaula_bin == NULL || shaula_bin[0] == '\0') {
    gboolean fallback_ok = shaula_preview_notify(
        "Screenshot captured", body, image_path, transient, timeout_ms);
    g_free(absolute_path);
    return fallback_ok;
  }

  char *log_path = shaula_notify_action_log_path();

  (void)timeout_ms;
  gchar *argv[12];
  int argc = 0;
  argv[argc++] = "sh";
  argv[argc++] = "-c";
  argv[argc++] = "log=\"$1\"; shift; exec \"$@\" >/dev/null 2>>\"$log\" &";
  argv[argc++] = "shaula-preview-notify-action-listener";
  argv[argc++] = (gchar *)log_path;
  argv[argc++] = (gchar *)shaula_bin;
  argv[argc++] = "notify";
  argv[argc++] = "__saved-action-listener";
  argv[argc++] = absolute_path;
  if (image_path != NULL && image_path[0] != '\0')
    argv[argc++] = (gchar *)image_path;
  argv[argc] = NULL;

  gint status = 1;
  GError *error = NULL;
  gboolean spawned =
      g_spawn_sync(NULL, argv, NULL,
                   G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                   NULL, NULL, NULL, NULL, &status, &error);
  if (!spawned) {
    if (error != NULL)
      g_error_free(error);
    gboolean fallback_ok = shaula_preview_notify(
        "Screenshot captured", body, image_path, transient, timeout_ms);
    g_free(log_path);
    g_free(absolute_path);
    return fallback_ok;
  }
  if (error != NULL)
    g_error_free(error);

  gboolean ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (!ok) {
    gboolean fallback_ok = shaula_preview_notify(
        "Screenshot captured", body, image_path, transient, timeout_ms);
    g_free(log_path);
    g_free(absolute_path);
    return fallback_ok;
  }
  g_free(log_path);
  g_free(absolute_path);
  return TRUE;
}

static gboolean ensure_writable_dir(const char *dir, GError **error) {
  if (dir == NULL || dir[0] == '\0')
    return FALSE;
  if (g_mkdir_with_parents(dir, 0700) != 0) {
    if (error != NULL)
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                  "Could not create %s: %s", dir, g_strerror(errno));
    return FALSE;
  }

  char *probe =
      g_strdup_printf("%s%c.shaula-write-probe-%" G_GINT64_FORMAT ".tmp", dir,
                      G_DIR_SEPARATOR, g_get_real_time());
  gboolean ok = g_file_set_contents(probe, "", 0, error);
  if (ok)
    g_unlink(probe);
  g_free(probe);
  return ok;
}

static char *quick_save_directory(GError **error) {
  const char *configured = g_getenv("SHAULA_SAVE_FOLDER");
  if (configured != NULL && configured[0] != '\0') {
    char *expanded = NULL;
    if (g_strcmp0(configured, "~") == 0) {
      const char *home = g_get_home_dir();
      if (home != NULL && home[0] != '\0')
        expanded = g_strdup(home);
    } else if (g_str_has_prefix(configured, "~/")) {
      const char *home = g_get_home_dir();
      if (home != NULL && home[0] != '\0')
        expanded = g_build_filename(home, configured + 2, NULL);
    } else if (g_path_is_absolute(configured)) {
      expanded = g_strdup(configured);
    }
    if (expanded != NULL) {
      if (ensure_writable_dir(expanded, error))
        return expanded;
      g_free(expanded);
      return NULL;
    }
  }

  const char *home = g_get_home_dir();
  if (home == NULL || home[0] == '\0') {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                "Home directory is not available");
    return NULL;
  }

  char *preferred = g_build_filename(home, "Pictures", "shaula", NULL);
  if (ensure_writable_dir(preferred, NULL))
    return preferred;
  g_free(preferred);

  char *fallback = g_build_filename(home, "shaula", NULL);
  if (ensure_writable_dir(fallback, error))
    return fallback;
  g_free(fallback);
  return NULL;
}

static char *timestamped_screenshot_basename(void) {
  GDateTime *now = g_date_time_new_now_local();
  char *stamp =
      now != NULL ? g_date_time_format(now, "%Y%m%d-%H%M%S")
                  : g_strdup_printf("%" G_GINT64_FORMAT, g_get_real_time());
  if (now != NULL)
    g_date_time_unref(now);
  char *name = g_strdup_printf("%s.png", stamp);
  g_free(stamp);
  return name;
}

static char *timestamped_quick_save_path(GError **error) {
  char *dir = quick_save_directory(error);
  if (dir == NULL)
    return NULL;

  char *basename = timestamped_screenshot_basename();
  char *stem = g_strndup(basename, strlen(basename) - strlen(".png"));
  char *path = NULL;
  for (int attempt = 0; attempt < 1000; attempt += 1) {
    char *filename = attempt == 0
                         ? g_strdup(basename)
                         : g_strdup_printf("%s-%d.png", stem, attempt + 1);
    path = g_build_filename(dir, filename, NULL);
    g_free(filename);
    if (!g_file_test(path, G_FILE_TEST_EXISTS))
      break;
    g_free(path);
    path = NULL;
  }
  g_free(stem);
  g_free(basename);
  g_free(dir);
  if (path == NULL)
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_EXIST,
                "Could not allocate a unique screenshot filename");
  return path;
}

static gboolean save_rendered_png_to_path(ShaulaPreviewState *state,
                                          const char *target, GError **error) {
  gboolean is_temp = FALSE;
  char *source = render_or_original_png(state, &is_temp, error);
  if (source == NULL)
    return FALSE;

  gboolean saved = TRUE;
  if (g_strcmp0(source, target) != 0)
    saved = shaula_image_io_copy_file_bytes(source, target, error);
  if (is_temp)
    g_unlink(source);
  g_free(source);
  return saved;
}

static void remember_real_save_path(ShaulaPreviewState *state,
                                    const char *target) {
  g_free(state->document.saved_path);
  state->document.saved_path = g_strdup(target);
  g_free(state->document.path);
  state->document.path = g_strdup(target);
}

void shaula_preview_action_set_tool(ShaulaPreviewState *state,
                                    ShaulaTool tool) {
  if (state == NULL)
    return;
  if (tool == SHAULA_TOOL_CROP && state->active_tool == SHAULA_TOOL_SELECT &&
      shaula_preview_apply_crop_to_selected_rect(state))
    return;

  if (state->active_tool == SHAULA_TOOL_ERASER && tool == SHAULA_TOOL_ERASER) {
    tool = state->previous_tool_before_eraser;
  } else if (tool == SHAULA_TOOL_ERASER &&
             state->active_tool != SHAULA_TOOL_ERASER) {
    state->previous_tool_before_eraser = state->active_tool;
  }

  if (state->active_tool == SHAULA_TOOL_ERASER &&
      tool != SHAULA_TOOL_ERASER)
    shaula_preview_commit_eraser_pending(state);

  shaula_preview_commit_history_gesture(state, TRUE);
  if (tool == SHAULA_TOOL_HAND) {
    if (state->operation != SHAULA_OPERATION_NONE)
      shaula_preview_cancel_operation(state);
  } else {
    shaula_preview_cancel_operation(state);
  }
  if (tool != SHAULA_TOOL_SELECT && tool != SHAULA_TOOL_HAND) {
    shaula_annotation_editor_clear_selection(state);
    shaula_preview_clear_region_selection(state);
  }
  state->active_tool = tool;
  if (tool == SHAULA_TOOL_PEN)
    shaula_properties_hud_set_panel(&state->properties_hud,
                                    SHAULA_PROPERTIES_PANEL_PEN);
  else if (tool == SHAULA_TOOL_HIGHLIGHT)
    shaula_properties_hud_set_panel(&state->properties_hud,
                                    SHAULA_PROPERTIES_PANEL_HIGHLIGHT);
  else if (tool == SHAULA_TOOL_TEXT)
    shaula_properties_hud_set_panel(&state->properties_hud,
                                    SHAULA_PROPERTIES_PANEL_TEXT);
  else if (tool == SHAULA_TOOL_ERASER)
    shaula_properties_hud_set_panel(&state->properties_hud,
                                    SHAULA_PROPERTIES_PANEL_ERASER);
  else if (state->properties_hud.active_panel == SHAULA_PROPERTIES_PANEL_ERASER)
    shaula_properties_hud_set_panel(&state->properties_hud,
                                    SHAULA_PROPERTIES_PANEL_NONE);
  shaula_preview_toolbar_update_tool_state(state);
  if (state->area != NULL) {
    const char *cursor = "crosshair";
    if (tool == SHAULA_TOOL_SELECT)
      cursor = "default";
    else if (tool == SHAULA_TOOL_HAND)
      cursor = "grab";
    else if (tool == SHAULA_TOOL_TEXT)
      cursor = "text";
    else if (tool == SHAULA_TOOL_ERASER)
      cursor = "none";
    gtk_widget_set_cursor_from_name(state->area, cursor);
  }
}

void shaula_preview_action_copy(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  shaula_preview_commit_eraser_pending(state);
  state->last_action = "copy";
  state->document.copied = FALSE;
  state->notified = FALSE;
  gboolean is_temp = FALSE;
  GError *error = NULL;
  char *source = render_or_original_png(state, &is_temp, &error);
  if (source == NULL) {
    report_error("copy render", error);
    if (error_notifications_enabled())
      state->notified = shaula_preview_notify("Could not copy screenshot",
                                              "Copy failed", NULL, FALSE, 5000);
    else
      state->notified = TRUE;
    return;
  }
  if (!shaula_clipboard_copy_png_file(source, &error)) {
    report_error("copy", error);
    if (error_notifications_enabled())
      state->notified = shaula_preview_notify("Could not copy screenshot",
                                              "Copy failed", NULL, FALSE, 5000);
    else
      state->notified = TRUE;
  } else {
    state->document.copied = TRUE;
    const char *thumbnail_path =
        shaula_preview_path_is_temporary_capture(source) ? NULL
                                                        : thumbnail_path_or_null(source);
    if (success_notifications_enabled())
      state->notified = shaula_preview_notify(
          "Screenshot captured", "You can paste the image from the clipboard.",
          thumbnail_path, TRUE, 2500);
    else
      state->notified = TRUE;
  }
  if (is_temp && !state->notified)
    g_unlink(source);
  g_free(source);
}

void shaula_preview_action_save(ShaulaPreviewState *state) {
  if (state == NULL || state->document.image == NULL)
    return;

  shaula_preview_commit_eraser_pending(state);
  state->last_action = "save";
  state->document.saved = FALSE;
  state->notified = FALSE;

  GError *error = NULL;
  char *target = timestamped_quick_save_path(&error);

  if (target == NULL) {
    notify_save_failure(state, "quick save target", error);
    return;
  }

  if (!save_rendered_png_to_path(state, target, &error)) {
    notify_save_failure(state, "quick save", error);
    g_free(target);
    return;
  }

  state->document.saved = TRUE;
  remember_real_save_path(state, target);
  if (success_notifications_enabled())
    state->notified = shaula_preview_notify_saved(
        target, thumbnail_path_or_null(target), TRUE, 6000);
  else
    state->notified = TRUE;
  g_free(target);
}

static void shaula_preview_action_accept(ShaulaPreviewState *state,
                                         gboolean copy_to_clipboard) {
  if (state == NULL || state->document.path == NULL)
    return;

  shaula_preview_commit_eraser_pending(state);
  state->last_action = copy_to_clipboard ? "copy" : "save";
  state->document.saved = FALSE;
  state->document.copied = FALSE;
  state->notified = FALSE;

  gboolean is_temp = FALSE;
  GError *error = NULL;
  char *source = render_or_original_png(state, &is_temp, &error);
  if (source == NULL) {
    report_error("accept render", error);
    if (error_notifications_enabled())
      state->notified = shaula_preview_notify("Could not save screenshot",
                                              "Save failed", NULL, FALSE, 6000);
    else
      state->notified = TRUE;
    if (state->app != NULL)
      g_application_quit(G_APPLICATION(state->app));
    return;
  }

  char *target = NULL;
  if (shaula_preview_path_is_temporary_capture(state->document.path))
    target = timestamped_quick_save_path(&error);
  else
    target = shaula_image_io_with_png_extension(state->document.path);
  if (target == NULL) {
    notify_save_failure(state, "accept save target", error);
    if (is_temp)
      g_unlink(source);
    g_free(source);
    if (state->app != NULL)
      g_application_quit(G_APPLICATION(state->app));
    return;
  } else if (g_strcmp0(source, target) == 0) {
    state->document.saved = TRUE;
    remember_real_save_path(state, target);
  } else if (!shaula_image_io_copy_file_bytes(source, target, &error)) {
    report_error("accept save", error);
    state->notified = shaula_preview_notify("Could not save screenshot",
                                            "Save failed", NULL, FALSE, 6000);
  } else {
    state->document.saved = TRUE;
    remember_real_save_path(state, target);
  }

  if (copy_to_clipboard && state->document.saved_path != NULL) {
    if (!shaula_clipboard_copy_png_file(state->document.saved_path, &error)) {
      report_error("accept copy", error);
      if (error_notifications_enabled())
        state->notified = shaula_preview_notify("Could not copy screenshot",
                                                "Copy failed", NULL, FALSE, 5000);
      else
        state->notified = TRUE;
    } else {
      state->document.copied = TRUE;
    }
  }

  if (state->document.saved && copy_to_clipboard && state->document.copied) {
    if (success_notifications_enabled())
      state->notified = shaula_preview_notify_saved(
          state->document.saved_path,
          thumbnail_path_or_null(state->document.saved_path), TRUE, 6000);
    else
      state->notified = TRUE;
  } else if (state->document.saved && !copy_to_clipboard) {
    if (success_notifications_enabled())
      state->notified = shaula_preview_notify_saved(
          state->document.saved_path,
          thumbnail_path_or_null(state->document.saved_path), TRUE, 6000);
    else
      state->notified = TRUE;
  } else if (state->document.saved && copy_to_clipboard && !state->document.copied &&
             !state->notified) {
    if (success_notifications_enabled())
      state->notified = shaula_preview_notify_saved(
          state->document.saved_path,
          thumbnail_path_or_null(state->document.saved_path), TRUE, 6000);
    else
      state->notified = TRUE;
  }

  if (is_temp)
    g_unlink(source);
  g_free(source);
  g_free(target);

  if (state->app != NULL)
    g_application_quit(G_APPLICATION(state->app));
}

void shaula_preview_action_done(ShaulaPreviewState *state) {
  shaula_preview_action_accept(state, state != NULL && state->copy_on_accept);
}

void shaula_preview_action_close(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  shaula_preview_commit_eraser_pending(state);
  state->last_action = "close";
  state->notified = FALSE;
  if (state->app != NULL)
    g_application_quit(G_APPLICATION(state->app));
}

static void on_save_response(GtkNativeDialog *dialog, int response,
                             gpointer data) {
  ShaulaPreviewState *state = data;
  if (response == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    GFile *file = gtk_file_chooser_get_file(chooser);
    if (file != NULL) {
      char *target = g_file_get_path(file);
      state->last_action = "save";
      state->document.saved = FALSE;
      state->notified = FALSE;
      if (target != NULL) {
        char *target_png = shaula_image_io_with_png_extension(target);
        if (target_png != NULL) {
          GError *error = NULL;
          if (!save_rendered_png_to_path(state, target_png, &error)) {
            notify_save_failure(state, "save", error);
          } else {
            state->document.saved = TRUE;
            remember_real_save_path(state, target_png);
            if (success_notifications_enabled())
              state->notified = shaula_preview_notify_saved(
                  target_png, thumbnail_path_or_null(target_png),
                  TRUE, 6000);
            else
              state->notified = TRUE;
          }
          g_free(target_png);
        } else {
          if (error_notifications_enabled())
            state->notified = shaula_preview_notify(
                "Could not save screenshot", "Save failed", NULL, FALSE, 6000);
          else
            state->notified = TRUE;
        }
      } else {
        if (error_notifications_enabled())
          state->notified = shaula_preview_notify(
              "Could not save screenshot", "Save failed", NULL, FALSE, 6000);
        else
          state->notified = TRUE;
      }
      g_free(target);
      g_object_unref(file);
    }
  }
  g_object_unref(dialog);
}

void shaula_preview_action_save_as(ShaulaPreviewState *state) {
  shaula_preview_commit_eraser_pending(state);
  GtkFileChooserNative *dialog = gtk_file_chooser_native_new(
      "Save Shaula Preview", GTK_WINDOW(state->window),
      GTK_FILE_CHOOSER_ACTION_SAVE, "Save", "Cancel");
  if (state->document.path != NULL) {
    char *basename = shaula_preview_path_is_temporary_capture(state->document.path)
                         ? timestamped_screenshot_basename()
                         : g_path_get_basename(state->document.path);
    char *png_name = shaula_image_io_with_png_extension(basename);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),
                                      png_name != NULL ? png_name : basename);
    g_free(png_name);
    g_free(basename);
  }
  g_signal_connect(dialog, "response", G_CALLBACK(on_save_response), state);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

void shaula_preview_action_discard(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  shaula_preview_commit_eraser_pending(state);
  state->last_action = "discard";
  state->notified = FALSE;
  if (state->app != NULL)
    g_application_quit(G_APPLICATION(state->app));
}

void shaula_preview_action_copy_path(ShaulaPreviewState *state) {
  if (state == NULL || state->document.path == NULL)
    return;
  GError *error = NULL;
  if (!shaula_clipboard_copy_text(state->document.path, &error))
    report_error("copy path", error);
}

void shaula_preview_action_copy_hover_color(ShaulaPreviewState *state) {
  if (state == NULL || !state->hover_color_valid)
    return;
  GError *error = NULL;
  if (!shaula_clipboard_copy_text(state->hover_hex, &error))
    report_error("copy hover color", error);
}

static void apply_color_to_selected_annotation(ShaulaPreviewState *state,
                                               ShaulaColor color) {
  ShaulaAnnotation *annotation =
      shaula_annotation_editor_single_selection(state);
  if (annotation == NULL)
    return;

  switch (annotation->type) {
  case SHAULA_ANNOTATION_ARROW:
    shaula_properties_hud_set_arrow_color(state, color);
    break;
  case SHAULA_ANNOTATION_RECTANGLE:
    shaula_properties_hud_set_rectangle_color(state, color);
    break;
  case SHAULA_ANNOTATION_HIGHLIGHT:
    shaula_properties_hud_set_highlight_color(state, color);
    break;
  case SHAULA_ANNOTATION_PEN:
    shaula_properties_hud_set_pen_color(state, color);
    break;
  case SHAULA_ANNOTATION_TEXT:
    shaula_properties_hud_set_text_color(state, color);
    break;
  case SHAULA_ANNOTATION_MEASURE:
    shaula_properties_hud_set_measure_color(state, color);
    break;
  }
}

void shaula_preview_action_use_hover_color(ShaulaPreviewState *state) {
  if (state == NULL || !state->hover_color_valid)
    return;

  ShaulaColor color = state->hover_color;
  state->current_color = color;

  if (shaula_annotation_editor_single_selection(state) != NULL) {
    apply_color_to_selected_annotation(state, color);
  } else {
    switch (state->active_tool) {
    case SHAULA_TOOL_ARROW:
    case SHAULA_TOOL_LINE:
      shaula_properties_hud_set_arrow_color(state, color);
      break;
    case SHAULA_TOOL_MEASURE:
      shaula_properties_hud_set_measure_color(state, color);
      break;
    case SHAULA_TOOL_RECTANGLE:
      shaula_properties_hud_set_rectangle_color(state, color);
      break;
    case SHAULA_TOOL_HIGHLIGHT:
      shaula_properties_hud_set_highlight_color(state, color);
      break;
    case SHAULA_TOOL_PEN:
      shaula_properties_hud_set_pen_color(state, color);
      break;
    case SHAULA_TOOL_TEXT:
      shaula_properties_hud_set_text_color(state, color);
      break;
    case SHAULA_TOOL_SPOTLIGHT:
      shaula_properties_hud_set_spotlight_border_color(state, color);
      break;
    case SHAULA_TOOL_SELECT:
    case SHAULA_TOOL_HAND:
    case SHAULA_TOOL_CROP:
    case SHAULA_TOOL_ERASER:
    case SHAULA_TOOL_COUNT:
      shaula_properties_hud_set_arrow_color(state, color);
      shaula_properties_hud_set_rectangle_color(state, color);
      shaula_properties_hud_set_highlight_color(state, color);
      shaula_properties_hud_set_pen_color(state, color);
      shaula_properties_hud_set_text_color(state, color);
      shaula_properties_hud_set_measure_color(state, color);
      shaula_properties_hud_set_spotlight_border_color(state, color);
      break;
    }
  }

  if (state->color_swatch != NULL)
    gtk_widget_queue_draw(state->color_swatch);
}

void shaula_preview_action_open_containing_folder(ShaulaPreviewState *state) {
  if (state == NULL || state->document.path == NULL)
    return;
  GError *error = NULL;
  if (!shaula_image_io_open_containing_folder(state->document.path, &error))
    report_error("open containing folder", error);
}
