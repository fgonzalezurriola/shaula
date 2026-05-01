#include "preview_actions.h"

#include <glib/gstdio.h>
#include <stdio.h>

#include "preview_clipboard.h"
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

void shaula_preview_action_set_tool(ShaulaPreviewState *state, ShaulaTool tool) {
  if (state == NULL)
    return;
  shaula_preview_cancel_operation(state);
  state->active_tool = tool;
  shaula_preview_toolbar_update_tool_state(state);
  if (state->area != NULL) {
    const char *cursor = "crosshair";
    if (tool == SHAULA_TOOL_SELECT)
      cursor = "grab";
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
  gboolean is_temp = FALSE;
  GError *error = NULL;
  char *source = render_or_original_png(state, &is_temp, &error);
  if (source == NULL) {
    report_error("copy render", error);
    return;
  }
  if (!shaula_clipboard_copy_png_file(source, &error)) {
    report_error("copy", error);
  } else {
    state->copied = TRUE;
  }
  if (is_temp)
    g_unlink(source);
  g_free(source);
}

static void on_save_response(GtkNativeDialog *dialog, int response,
                             gpointer data) {
  ShaulaPreviewState *state = data;
  if (response == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    GFile *file = gtk_file_chooser_get_file(chooser);
    if (file != NULL) {
      char *target = g_file_get_path(file);
      char *target_png = shaula_image_io_with_png_extension(target);
      if (target_png != NULL) {
        state->last_action = "save";
        state->saved = FALSE;
        gboolean is_temp = FALSE;
        GError *error = NULL;
        char *source = render_or_original_png(state, &is_temp, &error);
        if (source == NULL) {
          report_error("save render", error);
        } else if (!shaula_image_io_copy_file_bytes(source, target_png, &error)) {
          report_error("save", error);
        } else {
          state->saved = TRUE;
          g_free(state->saved_path);
          state->saved_path = g_strdup(target_png);
        }
        if (is_temp)
          g_unlink(source);
        g_free(source);
        g_free(target_png);
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

void shaula_preview_action_reset_annotations(ShaulaPreviewState *state) {
  shaula_preview_reset_annotations(state);
}

void shaula_preview_action_copy_path(ShaulaPreviewState *state) {
  if (state == NULL || state->path == NULL)
    return;
  GError *error = NULL;
  if (!shaula_clipboard_copy_text(state->path, &error))
    report_error("copy path", error);
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
  shaula_preview_action_copy(data);
}

void shaula_preview_on_save_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_action_save_as(data);
}

void shaula_preview_on_discard_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_action_discard(data);
}

void shaula_preview_on_fit_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_action_fit(data);
}

void shaula_preview_on_actual_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_action_actual_size(data);
}

void shaula_preview_on_reset_annotations_clicked(GtkButton *button,
                                                gpointer data) {
  (void)button;
  shaula_preview_action_reset_annotations(data);
}

void shaula_preview_on_copy_path_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_action_copy_path(data);
}

void shaula_preview_on_open_folder_clicked(GtkButton *button, gpointer data) {
  (void)button;
  shaula_preview_action_open_containing_folder(data);
}

void shaula_preview_on_tool_clicked(GtkButton *button, gpointer data) {
  ShaulaTool tool =
      (ShaulaTool)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tool"));
  shaula_preview_action_set_tool(data, tool);
}
