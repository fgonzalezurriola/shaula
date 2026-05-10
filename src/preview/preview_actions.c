#include "preview_actions.h"

#include <glib/gstdio.h>
#include <sys/wait.h>
#include <stdio.h>

#include "preview_clipboard.h"
#include "preview_commands.h"
#include "preview_image_io.h"
#include "preview_render.h"
#include "preview_toolbar.h"

static void report_error(const char *context, GError *error) {
  if (error != NULL) {
    fprintf(stderr, "shaula-preview %s failed: %s\n", context, error->message);
    g_error_free(error);
  } else {
    fprintf(stderr, "shaula-preview %s failed\n", context);
  }
}

static char *render_or_original_png(ShaulaPreviewState *state, gboolean *is_temp,
                                    GError **error) {
  *is_temp = FALSE;
  if (shaula_preview_state_has_modifications(state) ||
      !shaula_image_io_path_has_png_extension(state->path)) {
    *is_temp = TRUE;
    return shaula_render_composited_png_temp(state, error);
  }
  return g_strdup(state->path);
}

/// Runtime boundary for immediate preview notifications.
///
/// Save/copy actions emit their own best-effort desktop notifications so the
/// helper does not depend on the GTK process exiting before the user gets
/// feedback. Failures are intentionally non-fatal and reported to the caller
/// only so the Zig service can keep a fallback path.
static gboolean shaula_preview_notify(const char *summary, const char *body,
                                      gboolean transient, int timeout_ms) {
  gchar *timeout = g_strdup_printf("%d", timeout_ms);
  gchar *argv[10];
  int argc = 0;
  argv[argc++] = "notify-send";
  argv[argc++] = "--app-name=Shaula";
  argv[argc++] = "--urgency";
  argv[argc++] = "normal";
  argv[argc++] = "--expire-time";
  argv[argc++] = timeout;
  if (transient)
    argv[argc++] = "--transient";
  argv[argc++] = (gchar *)summary;
  argv[argc++] = (gchar *)body;
  argv[argc] = NULL;

  gint status = 1;
  GError *error = NULL;
  gboolean spawned = g_spawn_sync(
      NULL, argv, NULL,
      G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
          G_SPAWN_STDERR_TO_DEV_NULL,
      NULL, NULL, NULL, NULL, &status, &error);
  if (!spawned) {
    if (error != NULL)
      g_error_free(error);
    g_free(timeout);
    return FALSE;
  }

  g_free(timeout);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void shaula_preview_action_set_tool(ShaulaPreviewState *state, ShaulaTool tool) {
  if (state == NULL)
    return;
  if (tool == SHAULA_TOOL_CROP && state->active_tool == SHAULA_TOOL_SELECT &&
      shaula_preview_apply_crop_to_selected_rect(state))
    return;
  shaula_preview_commit_history_gesture(state, TRUE);
  shaula_preview_cancel_operation(state);
  if (tool != SHAULA_TOOL_SELECT) {
    shaula_preview_clear_selection(state);
    shaula_preview_clear_region_selection(state);
  }
  state->active_tool = tool;
  if (tool == SHAULA_TOOL_PEN)
    state->active_properties_panel = SHAULA_PROPERTIES_PANEL_PEN;
  else if (tool == SHAULA_TOOL_HIGHLIGHT)
    state->active_properties_panel = SHAULA_PROPERTIES_PANEL_HIGHLIGHT;
  else if (tool == SHAULA_TOOL_TEXT)
    state->active_properties_panel = SHAULA_PROPERTIES_PANEL_TEXT;
  shaula_preview_toolbar_update_tool_state(state);
  if (state->area != NULL) {
    const char *cursor = "crosshair";
    if (tool == SHAULA_TOOL_SELECT)
      cursor = "default";
    else if (tool == SHAULA_TOOL_TEXT)
      cursor = "text";
    gtk_widget_set_cursor_from_name(state->area, cursor);
  }
}

void shaula_preview_action_copy(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  state->last_action = "copy";
  state->copied = FALSE;
  state->notified = FALSE;
  gboolean is_temp = FALSE;
  GError *error = NULL;
  char *source = render_or_original_png(state, &is_temp, &error);
  if (source == NULL) {
    report_error("copy render", error);
    state->notified = shaula_preview_notify("Could not copy screenshot",
                                            "Copy failed", FALSE, 5000);
    return;
  }
  if (!shaula_clipboard_copy_png_file(source, &error)) {
    report_error("copy", error);
    state->notified = shaula_preview_notify("Could not copy screenshot",
                                            "Copy failed", FALSE, 5000);
  } else {
    state->copied = TRUE;
    state->notified = shaula_preview_notify("Screenshot copied",
                                            "Copied to clipboard", TRUE, 1800);
  }
  if (is_temp)
    g_unlink(source);
  g_free(source);
}

void shaula_preview_action_accept(ShaulaPreviewState *state,
                                  gboolean copy_to_clipboard) {
  if (state == NULL || state->path == NULL)
    return;

  state->last_action = copy_to_clipboard ? "copy" : "save";
  state->saved = FALSE;
  state->copied = FALSE;
  state->notified = FALSE;

  gboolean is_temp = FALSE;
  GError *error = NULL;
  char *source = render_or_original_png(state, &is_temp, &error);
  if (source == NULL) {
    report_error("accept render", error);
    state->notified = shaula_preview_notify(
        "Could not save screenshot", "Save failed", FALSE, 6000);
    if (state->app != NULL)
      g_application_quit(G_APPLICATION(state->app));
    return;
  }

  char *target = shaula_image_io_with_png_extension(state->path);
  if (target == NULL) {
    state->notified = shaula_preview_notify(
        "Could not save screenshot", "Save failed", FALSE, 6000);
  } else if (g_strcmp0(source, target) == 0) {
    state->saved = TRUE;
    g_free(state->saved_path);
    state->saved_path = g_strdup(target);
  } else if (!shaula_image_io_copy_file_bytes(source, target, &error)) {
    report_error("accept save", error);
    state->notified = shaula_preview_notify(
        "Could not save screenshot", "Save failed", FALSE, 6000);
  } else {
    state->saved = TRUE;
    g_free(state->saved_path);
    state->saved_path = g_strdup(target);
  }

  if (copy_to_clipboard && state->saved_path != NULL) {
    if (!shaula_clipboard_copy_png_file(state->saved_path, &error)) {
      report_error("accept copy", error);
      state->notified = shaula_preview_notify(
          "Could not copy screenshot", "Copy failed", FALSE, 5000);
    } else {
      state->copied = TRUE;
    }
  }

  if (state->saved && copy_to_clipboard && state->copied) {
    state->notified = shaula_preview_notify(
        "Screenshot saved and copied", state->saved_path, TRUE, 2500);
  } else if (state->saved && !copy_to_clipboard) {
    state->notified = shaula_preview_notify(
        "Screenshot saved", state->saved_path, TRUE, 2500);
  } else if (state->saved && copy_to_clipboard && !state->copied &&
             !state->notified) {
    state->notified = shaula_preview_notify(
        "Screenshot saved", state->saved_path, TRUE, 2500);
  }

  if (is_temp)
    g_unlink(source);
  g_free(source);
  g_free(target);

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
      state->saved = FALSE;
      state->notified = FALSE;
      if (target != NULL) {
        char *target_png = shaula_image_io_with_png_extension(target);
        if (target_png != NULL) {
          gboolean is_temp = FALSE;
          GError *error = NULL;
          char *source = render_or_original_png(state, &is_temp, &error);
          if (source == NULL) {
            report_error("save render", error);
            state->notified = shaula_preview_notify(
                "Could not save screenshot", "Save failed", FALSE, 6000);
          } else if (!shaula_image_io_copy_file_bytes(source, target_png, &error)) {
            report_error("save", error);
            state->notified = shaula_preview_notify(
                "Could not save screenshot", "Save failed", FALSE, 6000);
          } else {
            state->saved = TRUE;
            g_free(state->saved_path);
            state->saved_path = g_strdup(target_png);
            state->notified = shaula_preview_notify("Screenshot saved",
                                                    target_png, TRUE, 2500);
          }
          if (is_temp)
            g_unlink(source);
          g_free(source);
          g_free(target_png);
        } else {
          state->notified = shaula_preview_notify("Could not save screenshot",
                                                  "Save failed", FALSE, 6000);
        }
      } else {
        state->notified = shaula_preview_notify("Could not save screenshot",
                                                "Save failed", FALSE, 6000);
      }
      g_free(target);
      g_object_unref(file);
    }
  }
  g_object_unref(dialog);
}

void shaula_preview_action_save_as(ShaulaPreviewState *state) {
  GtkFileChooserNative *dialog = gtk_file_chooser_native_new(
      "Save Shaula Preview", GTK_WINDOW(state->window),
      GTK_FILE_CHOOSER_ACTION_SAVE, "Save", "Cancel");
  if (state->path != NULL) {
    char *basename = g_path_get_basename(state->path);
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
  state->last_action = "discard";
  state->notified = FALSE;
  if (state->app != NULL)
    g_application_quit(G_APPLICATION(state->app));
}

void shaula_preview_action_fit(ShaulaPreviewState *state) {
  shaula_preview_set_fit_mode(state, TRUE);
}

void shaula_preview_action_actual_size(ShaulaPreviewState *state) {
  shaula_preview_set_actual_size(state);
}

void shaula_preview_action_zoom_in(ShaulaPreviewState *state) {
  shaula_preview_zoom_by_factor(state, 1.12);
}

void shaula_preview_action_zoom_out(ShaulaPreviewState *state) {
  shaula_preview_zoom_by_factor(state, 1.0 / 1.12);
}

void shaula_preview_action_undo(ShaulaPreviewState *state) {
  shaula_preview_undo(state);
}

void shaula_preview_action_redo(ShaulaPreviewState *state) {
  shaula_preview_redo(state);
}

void shaula_preview_action_reset_annotations(ShaulaPreviewState *state) {
  shaula_preview_reset_annotations(state);
}

void shaula_preview_action_duplicate_selected(ShaulaPreviewState *state) {
  shaula_preview_duplicate_selected(state);
}

void shaula_preview_action_delete_selected(ShaulaPreviewState *state) {
  shaula_preview_delete_selected(state);
}

void shaula_preview_action_crop_selected(ShaulaPreviewState *state) {
  shaula_preview_apply_crop_to_selected_rect(state);
}

void shaula_preview_action_copy_path(ShaulaPreviewState *state) {
  if (state == NULL || state->path == NULL)
    return;
  GError *error = NULL;
  if (!shaula_clipboard_copy_text(state->path, &error))
    report_error("copy path", error);
}

void shaula_preview_action_copy_hover_color(ShaulaPreviewState *state) {
  if (state == NULL || !state->hover_color_valid)
    return;
  GError *error = NULL;
  if (!shaula_clipboard_copy_text(state->hover_hex, &error))
    report_error("copy hover color", error);
}

void shaula_preview_action_open_containing_folder(ShaulaPreviewState *state) {
  if (state == NULL || state->path == NULL)
    return;
  GError *error = NULL;
  if (!shaula_image_io_open_containing_folder(state->path, &error))
    report_error("open containing folder", error);
}

void shaula_preview_on_copy_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_COPY);
}

void shaula_preview_on_save_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_SAVE_AS);
}

void shaula_preview_on_undo_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_UNDO);
}

void shaula_preview_on_redo_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_REDO);
}

void shaula_preview_on_discard_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_DISCARD);
}

void shaula_preview_on_fit_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_FIT_TO_SCREEN);
}

void shaula_preview_on_actual_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_ACTUAL_SIZE);
}

void shaula_preview_on_reset_annotations_clicked(GtkButton *button,
                                                gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_RESET_ANNOTATIONS);
}

void shaula_preview_on_duplicate_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED);
}

void shaula_preview_on_delete_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_DELETE_SELECTED);
}

void shaula_preview_on_crop_selected_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_CROP_SELECTED);
}

void shaula_preview_on_blur_region_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_BLUR_REGION);
}

void shaula_preview_on_erase_region_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_ERASE_REGION);
}

void shaula_preview_on_spotlight_toolbar_clicked(GtkButton *button,
                                                 gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT);
}

void shaula_preview_on_spotlight_region_clicked(GtkButton *button,
                                                gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION);
}

void shaula_preview_on_properties_back_clicked(GtkButton *button,
                                               gpointer data) {
  (void)button;
  shaula_preview_set_properties_panel(data, SHAULA_PROPERTIES_PANEL_NONE);
}

void shaula_preview_on_spotlight_color_set(GtkColorButton *button,
                                           gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_preview_set_spotlight_border_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_spotlight_width_changed(GtkRange *range,
                                               gpointer data) {
  shaula_preview_set_spotlight_border_width(data,
                                            gtk_range_get_value(range));
}

void shaula_preview_on_spotlight_shape_clicked(GtkButton *button,
                                               gpointer data) {
  ShaulaSpotlightShape shape = (ShaulaSpotlightShape)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(button), "spotlight-shape"));
  shaula_preview_set_spotlight_shape(data, shape);
}

void shaula_preview_on_copy_path_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data, SHAULA_PREVIEW_COMMAND_COPY_PATH);
}

void shaula_preview_on_open_folder_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_execute_command(data,
                                 SHAULA_PREVIEW_COMMAND_OPEN_CONTAINING_FOLDER);
}

void shaula_preview_on_tool_clicked(GtkButton *button, gpointer data) {
  ShaulaTool tool =
      (ShaulaTool)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tool"));
  ShaulaPreviewCommand command = SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT;
  switch (tool) {
  case SHAULA_TOOL_SELECT:
    command = SHAULA_PREVIEW_COMMAND_SET_TOOL_SELECT;
    break;
  case SHAULA_TOOL_CROP:
    command = SHAULA_PREVIEW_COMMAND_SET_TOOL_CROP;
    break;
  case SHAULA_TOOL_ARROW:
    command = SHAULA_PREVIEW_COMMAND_SET_TOOL_ARROW;
    break;
  case SHAULA_TOOL_TEXT:
    command = SHAULA_PREVIEW_COMMAND_SET_TOOL_TEXT;
    break;
  case SHAULA_TOOL_MEASURE:
    command = SHAULA_PREVIEW_COMMAND_SET_TOOL_MEASURE;
    break;
  case SHAULA_TOOL_RECTANGLE:
    command = SHAULA_PREVIEW_COMMAND_SET_TOOL_RECTANGLE;
    break;
  case SHAULA_TOOL_HIGHLIGHT:
    command = SHAULA_PREVIEW_COMMAND_SET_TOOL_HIGHLIGHT;
    break;
  case SHAULA_TOOL_PEN:
    command = SHAULA_PREVIEW_COMMAND_SET_TOOL_PEN;
    break;
  case SHAULA_TOOL_SPOTLIGHT:
    command = SHAULA_PREVIEW_COMMAND_SET_TOOL_SPOTLIGHT;
    break;
  case SHAULA_TOOL_COUNT:
    return;
  }
  shaula_preview_execute_command(data, command);
}

void shaula_preview_on_arrow_color_set(GtkColorButton *button, gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_preview_set_arrow_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_arrow_width_changed(GtkRange *range, gpointer data) {
  shaula_preview_set_arrow_stroke_width(data, gtk_range_get_value(range));
}

void shaula_preview_on_arrow_stroke_style_clicked(GtkButton *button,
                                                  gpointer data) {
  PreviewArrowStrokeStyle style =
      (PreviewArrowStrokeStyle)GPOINTER_TO_INT(
          g_object_get_data(G_OBJECT(button), "arrow-stroke-style"));
  shaula_preview_set_arrow_stroke_style(data, style);
  shaula_preview_toolbar_update_selection_state(data);
}

void shaula_preview_on_rectangle_color_set(GtkColorButton *button,
                                           gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_preview_set_rectangle_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_rectangle_width_changed(GtkRange *range,
                                               gpointer data) {
  shaula_preview_set_rectangle_stroke_width(data,
                                            gtk_range_get_value(range));
}

void shaula_preview_on_rectangle_stroke_style_clicked(GtkButton *button,
                                                      gpointer data) {
  PreviewArrowStrokeStyle style =
      (PreviewArrowStrokeStyle)GPOINTER_TO_INT(
          g_object_get_data(G_OBJECT(button), "rectangle-stroke-style"));
  shaula_preview_set_rectangle_stroke_style(data, style);
  shaula_preview_toolbar_update_selection_state(data);
}

void shaula_preview_on_rectangle_fill_toggled(GtkButton *button,
                                              gpointer data) {
  shaula_preview_set_rectangle_filled(
      data, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}

void shaula_preview_on_rectangle_corners_clicked(GtkButton *button,
                                                 gpointer data) {
  PreviewRectangleCorners corners =
      (PreviewRectangleCorners)GPOINTER_TO_INT(
          g_object_get_data(G_OBJECT(button), "rectangle-corners"));
  shaula_preview_set_rectangle_corners(data, corners);
}

void shaula_preview_on_pen_color_set(GtkColorButton *button, gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_preview_set_pen_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_pen_width_changed(GtkRange *range, gpointer data) {
  shaula_preview_set_pen_stroke_width(data, gtk_range_get_value(range));
}

void shaula_preview_on_pen_opacity_changed(GtkRange *range, gpointer data) {
  shaula_preview_set_pen_opacity(data, gtk_range_get_value(range));
}

void shaula_preview_on_highlight_color_set(GtkColorButton *button,
                                           gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_preview_set_highlight_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, rgba.alpha});
}

void shaula_preview_on_highlight_width_changed(GtkRange *range,
                                               gpointer data) {
  shaula_preview_set_highlight_stroke_width(data, gtk_range_get_value(range));
}

void shaula_preview_on_highlight_opacity_changed(GtkRange *range,
                                                 gpointer data) {
  shaula_preview_set_highlight_opacity(data, gtk_range_get_value(range));
}

void shaula_preview_on_text_color_set(GtkColorButton *button, gpointer data) {
  GdkRGBA rgba;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
  shaula_preview_set_text_color(
      data, (ShaulaColor){rgba.red, rgba.green, rgba.blue, 1.0});
}

void shaula_preview_on_text_size_changed(GtkRange *range, gpointer data) {
  shaula_preview_set_text_font_size(data, gtk_range_get_value(range));
}

void shaula_preview_on_text_align_clicked(GtkButton *button, gpointer data) {
  ShaulaTextAlign align = (ShaulaTextAlign)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(button), "text-align"));
  shaula_preview_set_text_align(data, align);
}
