#include "preview_system_clipboard.h"

#include <math.h>

#include "preview_annotation_editor.h"
#include "preview_canvas.h"
#include "preview_paste_placement.h"
#include "preview_state.h"
#include "preview_toolbar.h"

#define SHAULA_SYSTEM_PASTE_TEXT_MAX_BYTES (256u * 1024u)
#define SHAULA_SYSTEM_PASTE_IMAGE_MAX_DIMENSION 16384
#define SHAULA_SYSTEM_PASTE_IMAGE_MAX_PIXELS ((gint64)32000000)
#define SHAULA_SYSTEM_PASTE_MARGIN_SCREEN_PX 16.0
#define SHAULA_PREVIEW_STATE_DATA_KEY "shaula-preview-state"

struct ShaulaSystemClipboardPaste {
  gint ref_count;
  GWeakRef window_ref;
  GdkClipboard *clipboard;
  GCancellable *cancellable;
  gulong changed_handler;
  gboolean in_flight;
  gboolean clipboard_changed;
  gboolean closing;
};

static ShaulaSystemClipboardPaste *paste_ref(
    ShaulaSystemClipboardPaste *paste) {
  g_atomic_int_inc(&paste->ref_count);
  return paste;
}

static void paste_unref(ShaulaSystemClipboardPaste *paste) {
  if (paste == NULL || !g_atomic_int_dec_and_test(&paste->ref_count))
    return;
  g_weak_ref_clear(&paste->window_ref);
  g_clear_object(&paste->cancellable);
  g_clear_object(&paste->clipboard);
  g_free(paste);
}

static ShaulaPreviewState *live_state(ShaulaSystemClipboardPaste *paste,
                                      GtkWidget **window_out) {
  if (window_out != NULL)
    *window_out = NULL;
  if (paste == NULL || paste->closing)
    return NULL;
  GtkWidget *window = g_weak_ref_get(&paste->window_ref);
  if (window == NULL)
    return NULL;
  ShaulaPreviewState *state =
      g_object_get_data(G_OBJECT(window), SHAULA_PREVIEW_STATE_DATA_KEY);
  if (state == NULL) {
    g_object_unref(window);
    return NULL;
  }
  if (window_out != NULL)
    *window_out = window;
  else
    g_object_unref(window);
  return state;
}

static void disconnect_clipboard_changed(ShaulaSystemClipboardPaste *paste) {
  if (paste->clipboard != NULL && paste->changed_handler != 0)
    g_signal_handler_disconnect(paste->clipboard, paste->changed_handler);
  paste->changed_handler = 0;
}

static void finish_request(ShaulaSystemClipboardPaste *paste) {
  disconnect_clipboard_changed(paste);
  paste->in_flight = FALSE;
  g_clear_object(&paste->cancellable);

  GtkWidget *window = NULL;
  ShaulaPreviewState *state = live_state(paste, &window);
  if (state != NULL)
    shaula_preview_toolbar_update_system_paste_state(state);
  g_clear_object(&window);
}

static void report_feedback(ShaulaSystemClipboardPaste *paste,
                            const char *message) {
  GtkWidget *window = NULL;
  ShaulaPreviewState *state = live_state(paste, &window);
  if (state != NULL)
    shaula_preview_show_feedback(state, message, FALSE);
  g_clear_object(&window);
}

static void on_clipboard_changed(GdkClipboard *clipboard, gpointer data) {
  (void)clipboard;
  ShaulaSystemClipboardPaste *paste = data;
  if (!paste->in_flight || paste->closing)
    return;
  paste->clipboard_changed = TRUE;
  if (paste->cancellable != NULL)
    g_cancellable_cancel(paste->cancellable);
}

static gboolean formats_offer_image(GdkContentFormats *formats) {
  if (formats == NULL)
    return FALSE;
  if (gdk_content_formats_contain_gtype(formats, GDK_TYPE_TEXTURE))
    return TRUE;
  gsize count = 0;
  const char *const *mime_types =
      gdk_content_formats_get_mime_types(formats, &count);
  for (gsize i = 0; i < count; i++) {
    if (mime_types[i] != NULL && g_str_has_prefix(mime_types[i], "image/"))
      return TRUE;
  }
  return FALSE;
}

static gboolean formats_offer_text(GdkContentFormats *formats) {
  if (formats == NULL)
    return FALSE;
  if (gdk_content_formats_contain_gtype(formats, G_TYPE_STRING))
    return TRUE;
  gsize count = 0;
  const char *const *mime_types =
      gdk_content_formats_get_mime_types(formats, &count);
  for (gsize i = 0; i < count; i++) {
    if (mime_types[i] != NULL && g_str_has_prefix(mime_types[i], "text/"))
      return TRUE;
  }
  return FALSE;
}

static ShaulaRect visible_viewport_image(ShaulaPreviewState *state) {
  ShaulaRect image_bounds = {0, 0, shaula_preview_image_width(state),
                             shaula_preview_image_height(state)};
  if (state->area == NULL)
    return image_bounds;
  int width = gtk_widget_get_width(state->area);
  int height = gtk_widget_get_height(state->area);
  if (width <= 0 || height <= 0 || state->zoom <= 0.0)
    return image_bounds;
  ShaulaPoint top_left =
      shaula_preview_canvas_screen_to_image(state, 0.0, 0.0);
  ShaulaPoint bottom_right = shaula_preview_canvas_screen_to_image(
      state, (double)width, (double)height);
  return shaula_rect_normalized((ShaulaRect){
      top_left.x, top_left.y, bottom_right.x - top_left.x,
      bottom_right.y - top_left.y});
}

static gboolean insert_text(ShaulaPreviewState *state, const char *text) {
  ShaulaPasteTextValidation validation = shaula_paste_validate_text(
      text, SHAULA_SYSTEM_PASTE_TEXT_MAX_BYTES);
  switch (validation) {
  case SHAULA_PASTE_TEXT_EMPTY:
    shaula_preview_show_feedback(
        state, "Clipboard text is empty or contains only whitespace.", TRUE);
    return FALSE;
  case SHAULA_PASTE_TEXT_TOO_LARGE:
    shaula_preview_show_feedback(
        state,
        "Clipboard text is too large. Paste text smaller than 256 KiB.", TRUE);
    return FALSE;
  case SHAULA_PASTE_TEXT_INVALID_UTF8:
    shaula_preview_show_feedback(
        state, "Clipboard text is not valid Unicode text.", TRUE);
    return FALSE;
  case SHAULA_PASTE_TEXT_VALID:
    break;
  }

  ShaulaRect image_bounds = {0, 0, shaula_preview_image_width(state),
                             shaula_preview_image_height(state)};
  ShaulaRect viewport = visible_viewport_image(state);
  ShaulaPastePlacement placement;
  double margin = state->zoom > 0.0
                      ? SHAULA_SYSTEM_PASTE_MARGIN_SCREEN_PX / state->zoom
                      : SHAULA_SYSTEM_PASTE_MARGIN_SCREEN_PX;
  if (!shaula_paste_calculate_placement(1.0, 1.0, viewport, image_bounds,
                                        margin, &placement)) {
    shaula_preview_show_feedback(
        state, "Text could not be positioned inside the screenshot.", TRUE);
    return FALSE;
  }

  ShaulaAnnotation *annotation = shaula_annotation_new_text(
      (ShaulaPoint){placement.x + placement.width / 2.0,
                    placement.y + placement.height / 2.0},
      text, state->tool_defaults.text.color,
      state->tool_defaults.text.font_size, state->tool_defaults.text.align,
      state->tool_defaults.text.font_mode);
  if (annotation == NULL) {
    shaula_preview_show_feedback(
        state, "Not enough memory to create the text annotation.", TRUE);
    return FALSE;
  }

  double viewport_center_x = placement.x + placement.width / 2.0;
  double viewport_center_y = placement.y + placement.height / 2.0;
  shaula_annotation_move(
      annotation,
      viewport_center_x - (annotation->bounds.x + annotation->bounds.width / 2.0),
      viewport_center_y -
          (annotation->bounds.y + annotation->bounds.height / 2.0));
  ShaulaPoint clamp = shaula_paste_clamp_bounds_translation(
      annotation->bounds, image_bounds, margin);
  shaula_annotation_move(annotation, clamp.x, clamp.y);
  return shaula_annotation_editor_insert_external(state, annotation, TRUE);
}

static gboolean insert_texture(ShaulaPreviewState *state,
                               GdkTexture *texture) {
  int width = gdk_texture_get_width(texture);
  int height = gdk_texture_get_height(texture);
  if (!shaula_paste_validate_image_dimensions(
          width, height, SHAULA_SYSTEM_PASTE_IMAGE_MAX_DIMENSION,
          SHAULA_SYSTEM_PASTE_IMAGE_MAX_PIXELS)) {
    shaula_preview_show_feedback(
        state,
        "Clipboard image is invalid or too large. Use an image under 16,384 px per side and 32 megapixels.",
        TRUE);
    return FALSE;
  }

  GdkPixbuf *pixbuf = gdk_pixbuf_get_from_texture(texture);
  if (pixbuf == NULL) {
    shaula_preview_show_feedback(
        state,
        "Clipboard image could not be decoded or there is not enough memory. Copy it again or use a smaller image.",
        TRUE);
    return FALSE;
  }

  ShaulaRect image_bounds = {0, 0, shaula_preview_image_width(state),
                             shaula_preview_image_height(state)};
  ShaulaRect viewport = visible_viewport_image(state);
  ShaulaPastePlacement placement;
  double margin = state->zoom > 0.0
                      ? SHAULA_SYSTEM_PASTE_MARGIN_SCREEN_PX / state->zoom
                      : SHAULA_SYSTEM_PASTE_MARGIN_SCREEN_PX;
  if (!shaula_paste_calculate_placement(width, height, viewport, image_bounds,
                                        margin, &placement)) {
    g_object_unref(pixbuf);
    shaula_preview_show_feedback(
        state, "Image could not be positioned inside the screenshot.", TRUE);
    return FALSE;
  }

  ShaulaAnnotation *annotation = shaula_annotation_new_image_take(
      pixbuf, (ShaulaRect){placement.x, placement.y, placement.width,
                           placement.height});
  if (annotation == NULL) {
    shaula_preview_show_feedback(
        state, "Not enough memory to create the image annotation.", TRUE);
    return FALSE;
  }
  return shaula_annotation_editor_insert_external(state, annotation, FALSE);
}

static gboolean finish_was_cancelled_for_change(
    ShaulaSystemClipboardPaste *paste, GError *error) {
  if (!paste->clipboard_changed)
    return FALSE;
  if (error != NULL)
    g_error_free(error);
  finish_request(paste);
  report_feedback(
      paste,
      "Clipboard changed while pasting. Copy the content again and retry.");
  paste_unref(paste);
  return TRUE;
}

static void on_texture_read(GObject *source, GAsyncResult *result,
                            gpointer data) {
  ShaulaSystemClipboardPaste *paste = data;
  GError *error = NULL;
  GdkTexture *texture = gdk_clipboard_read_texture_finish(
      GDK_CLIPBOARD(source), result, &error);
  if (finish_was_cancelled_for_change(paste, error)) {
    g_clear_object(&texture);
    return;
  }

  finish_request(paste);
  GtkWidget *window = NULL;
  ShaulaPreviewState *state = live_state(paste, &window);
  if (state != NULL) {
    if (texture == NULL) {
      shaula_preview_show_feedback(
          state,
          "Clipboard image could not be read. Copy the image again and retry.",
          TRUE);
    } else {
      insert_texture(state, texture);
    }
  }
  g_clear_error(&error);
  g_clear_object(&texture);
  g_clear_object(&window);
  paste_unref(paste);
}

static void on_text_read(GObject *source, GAsyncResult *result, gpointer data) {
  ShaulaSystemClipboardPaste *paste = data;
  GError *error = NULL;
  char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result,
                                              &error);
  if (finish_was_cancelled_for_change(paste, error)) {
    g_free(text);
    return;
  }

  finish_request(paste);
  GtkWidget *window = NULL;
  ShaulaPreviewState *state = live_state(paste, &window);
  if (state != NULL) {
    if (text == NULL) {
      shaula_preview_show_feedback(
          state,
          "Clipboard text could not be read. Copy the text again and retry.",
          TRUE);
    } else {
      insert_text(state, text);
    }
  }
  g_clear_error(&error);
  g_free(text);
  g_clear_object(&window);
  paste_unref(paste);
}

ShaulaSystemClipboardPaste *shaula_system_clipboard_paste_new(
    GtkWidget *window, ShaulaPreviewState *state) {
  if (window == NULL || state == NULL)
    return NULL;
  GdkDisplay *display = gtk_widget_get_display(window);
  if (display == NULL)
    return NULL;

  ShaulaSystemClipboardPaste *paste =
      g_new0(ShaulaSystemClipboardPaste, 1);
  paste->ref_count = 1;
  g_weak_ref_init(&paste->window_ref, G_OBJECT(window));
  paste->clipboard = g_object_ref(gdk_display_get_clipboard(display));
  g_object_set_data(G_OBJECT(window), SHAULA_PREVIEW_STATE_DATA_KEY, state);
  return paste;
}

void shaula_system_clipboard_paste_free(ShaulaSystemClipboardPaste *paste) {
  if (paste == NULL)
    return;
  paste->closing = TRUE;
  disconnect_clipboard_changed(paste);
  if (paste->cancellable != NULL)
    g_cancellable_cancel(paste->cancellable);
  GtkWidget *window = g_weak_ref_get(&paste->window_ref);
  if (window != NULL) {
    g_object_set_data(G_OBJECT(window), SHAULA_PREVIEW_STATE_DATA_KEY, NULL);
    g_object_unref(window);
  }
  paste_unref(paste);
}

gboolean shaula_system_clipboard_paste_is_busy(
    const ShaulaSystemClipboardPaste *paste) {
  return paste != NULL && (paste->in_flight || paste->closing);
}

gboolean shaula_system_clipboard_paste_request(ShaulaPreviewState *state) {
  if (state == NULL || state->system_clipboard_paste == NULL ||
      state->document.image == NULL)
    return FALSE;
  ShaulaSystemClipboardPaste *paste = state->system_clipboard_paste;
  if (paste->closing)
    return FALSE;
  if (paste->in_flight)
    return TRUE;

  GdkContentFormats *formats = gdk_clipboard_get_formats(paste->clipboard);
  gboolean has_image = formats_offer_image(formats);
  gboolean has_text = formats_offer_text(formats);
  if (!has_image && !has_text) {
    shaula_preview_show_feedback(
        state, "Clipboard has no supported text or image.", FALSE);
    return TRUE;
  }

  paste->clipboard_changed = FALSE;
  paste->in_flight = TRUE;
  paste->cancellable = g_cancellable_new();
  paste->changed_handler = g_signal_connect(
      paste->clipboard, "changed", G_CALLBACK(on_clipboard_changed), paste);
  shaula_preview_toolbar_update_system_paste_state(state);

  if (has_image) {
    gdk_clipboard_read_texture_async(paste->clipboard, paste->cancellable,
                                     on_texture_read, paste_ref(paste));
  } else {
    gdk_clipboard_read_text_async(paste->clipboard, paste->cancellable,
                                  on_text_read, paste_ref(paste));
  }
  return TRUE;
}

void shaula_system_clipboard_paste_cancel(ShaulaPreviewState *state) {
  if (state == NULL || state->system_clipboard_paste == NULL)
    return;
  ShaulaSystemClipboardPaste *paste = state->system_clipboard_paste;
  paste->closing = TRUE;
  disconnect_clipboard_changed(paste);
  if (paste->cancellable != NULL)
    g_cancellable_cancel(paste->cancellable);
}
