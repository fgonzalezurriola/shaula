#include "preview_composition.h"

#include <math.h>

#include "preview_annotation_editor.h"
#include "preview_edit_session.h"
#include "preview_toolbar.h"
#include "recent_captures.h"

#define SHAULA_COMPOSITION_MAX_ITEMS 4u
#define SHAULA_COMPOSITION_MAX_DIMENSION 16384
#define SHAULA_COMPOSITION_MAX_ASSET_PIXELS G_GINT64_CONSTANT(32000000)
#define SHAULA_COMPOSITION_MAX_TOTAL_PIXELS G_GINT64_CONSTANT(64000000)

typedef enum {
  COMPOSITION_VERTICAL,
  COMPOSITION_HORIZONTAL,
} CompositionLayout;

typedef enum {
  COMPOSITION_ALIGN_START,
  COMPOSITION_ALIGN_CENTER,
  COMPOSITION_ALIGN_END,
} CompositionAlignment;

typedef enum {
  COMPOSITION_SIZE_ORIGINAL,
  COMPOSITION_SIZE_MATCH_CROSS_AXIS,
} CompositionSizing;

typedef struct {
  int width;
  int height;
  ShaulaRect rects[SHAULA_COMPOSITION_MAX_ITEMS];
} CompositionPlan;

typedef struct {
  GdkPixbuf *pixbuf;
  char *label;
} CompositionItem;

typedef struct {
  gint ref_count;
  gboolean closing;
  gboolean clipboard_in_flight;
  ShaulaPreviewState *state;
  GtkWidget *dialog;
  GtkListBox *items_list;
  GtkDrawingArea *preview;
  GtkDropDown *layout;
  GtkDropDown *gap;
  GtkDropDown *alignment;
  GtkDropDown *sizing;
  GtkDropDown *recent;
  GtkWidget *recent_button;
  GtkWidget *clipboard_button;
  GtkWidget *apply_button;
  GtkWidget *feedback;
  GPtrArray *items;
  GPtrArray *recent_paths;
  GCancellable *clipboard_cancel;
  GdkClipboard *clipboard;
  gulong clipboard_changed_handler;
  gboolean clipboard_changed;
} CompositionDialog;

typedef struct {
  ShaulaPreviewState *state;
  GtkSpinButton *right;
  GtkSpinButton *bottom;
  GtkDropDown *background;
} CanvasDialog;

static void composition_item_free(gpointer data) {
  CompositionItem *item = data;
  if (item == NULL)
    return;
  g_clear_object(&item->pixbuf);
  g_free(item->label);
  g_free(item);
}

static gboolean pixbuf_within_limits(GdkPixbuf *pixbuf) {
  if (pixbuf == NULL)
    return FALSE;
  int width = gdk_pixbuf_get_width(pixbuf);
  int height = gdk_pixbuf_get_height(pixbuf);
  return width > 0 && height > 0 && width <= SHAULA_COMPOSITION_MAX_DIMENSION &&
         height <= SHAULA_COMPOSITION_MAX_DIMENSION &&
         (gint64)width * height <= SHAULA_COMPOSITION_MAX_ASSET_PIXELS;
}

static gboolean build_plan(ShaulaPreviewState *state, GPtrArray *items,
                           CompositionLayout layout, int gap,
                           CompositionAlignment alignment,
                           CompositionSizing sizing, CompositionPlan *plan,
                           GError **error) {
  if (state == NULL || state->document.image == NULL || items == NULL ||
      items->len == 0 || items->len > SHAULA_COMPOSITION_MAX_ITEMS) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                "Choose between one and four valid images.");
    return FALSE;
  }

  int base_width = shaula_preview_image_width(state);
  int base_height = shaula_preview_image_height(state);
  int cross_size = layout == COMPOSITION_VERTICAL ? base_width : base_height;
  gint64 asset_pixels = 0;
  for (guint i = 0; i < items->len; i++) {
    CompositionItem *item = g_ptr_array_index(items, i);
    if (item == NULL || !pixbuf_within_limits(item->pixbuf)) {
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                  "An image is invalid or exceeds 16,384 px or 32 MP.");
      return FALSE;
    }
    int width = gdk_pixbuf_get_width(item->pixbuf);
    int height = gdk_pixbuf_get_height(item->pixbuf);
    asset_pixels += (gint64)width * height;
    cross_size = MAX(cross_size,
                     layout == COMPOSITION_VERTICAL ? width : height);
  }
  if (asset_pixels > SHAULA_COMPOSITION_MAX_TOTAL_PIXELS) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
                "Selected images exceed the 64 MP composition budget.");
    return FALSE;
  }

  gint64 main_total = layout == COMPOSITION_VERTICAL ? base_height : base_width;
  int final_cross = layout == COMPOSITION_VERTICAL ? base_width : base_height;
  if (gap > 0)
    main_total += gap;
  for (guint i = 0; i < items->len; i++) {
    CompositionItem *item = g_ptr_array_index(items, i);
    int width = gdk_pixbuf_get_width(item->pixbuf);
    int height = gdk_pixbuf_get_height(item->pixbuf);
    if (sizing == COMPOSITION_SIZE_MATCH_CROSS_AXIS) {
      if (layout == COMPOSITION_VERTICAL) {
        height = MAX(1, (int)llround((double)height * cross_size / width));
        width = cross_size;
      } else {
        width = MAX(1, (int)llround((double)width * cross_size / height));
        height = cross_size;
      }
    }
    plan->rects[i].width = width;
    plan->rects[i].height = height;
    main_total += layout == COMPOSITION_VERTICAL ? height : width;
    if (i + 1 < items->len)
      main_total += gap;
    final_cross = MAX(final_cross,
                      layout == COMPOSITION_VERTICAL ? width : height);
  }

  gint64 final_width = layout == COMPOSITION_VERTICAL ? final_cross : main_total;
  gint64 final_height = layout == COMPOSITION_VERTICAL ? main_total : final_cross;
  if (final_width > SHAULA_COMPOSITION_MAX_DIMENSION ||
      final_height > SHAULA_COMPOSITION_MAX_DIMENSION ||
      final_width * final_height > SHAULA_COMPOSITION_MAX_TOTAL_PIXELS) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
                "The composed canvas exceeds 16,384 px or 64 MP.");
    return FALSE;
  }

  plan->width = (int)final_width;
  plan->height = (int)final_height;
  double cursor = layout == COMPOSITION_VERTICAL ? base_height : base_width;
  cursor += gap;
  for (guint i = 0; i < items->len; i++) {
    ShaulaRect *rect = &plan->rects[i];
    int item_cross = layout == COMPOSITION_VERTICAL ? (int)rect->width
                                                     : (int)rect->height;
    double cross = 0.0;
    if (alignment == COMPOSITION_ALIGN_CENTER)
      cross = (final_cross - item_cross) / 2.0;
    else if (alignment == COMPOSITION_ALIGN_END)
      cross = final_cross - item_cross;
    if (layout == COMPOSITION_VERTICAL) {
      rect->x = cross;
      rect->y = cursor;
      cursor += rect->height + gap;
    } else {
      rect->x = cursor;
      rect->y = cross;
      cursor += rect->width + gap;
    }
  }
  return TRUE;
}

static GdkPixbuf *expanded_image_new(GdkPixbuf *source, int width, int height,
                                     guint32 background, GError **error) {
  if (source == NULL || width < gdk_pixbuf_get_width(source) ||
      height < gdk_pixbuf_get_height(source) ||
      width > SHAULA_COMPOSITION_MAX_DIMENSION ||
      height > SHAULA_COMPOSITION_MAX_DIMENSION ||
      (gint64)width * height > SHAULA_COMPOSITION_MAX_TOTAL_PIXELS) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                "Canvas dimensions are outside the bounded limits.");
    return NULL;
  }
  GdkPixbuf *expanded =
      gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
  if (expanded == NULL) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
                "Not enough memory to expand the canvas.");
    return NULL;
  }
  gdk_pixbuf_fill(expanded, background);
  gdk_pixbuf_copy_area(source, 0, 0, gdk_pixbuf_get_width(source),
                       gdk_pixbuf_get_height(source), expanded, 0, 0);
  return expanded;
}

static gboolean apply_composition(ShaulaPreviewState *state, GPtrArray *items,
                                  CompositionLayout layout, int gap,
                                  CompositionAlignment alignment,
                                  CompositionSizing sizing, GError **error) {
  /* Orchestrates one preflighted document edit; dialogs only reach this point
   * after all source decoding and placement validation has succeeded. */
  CompositionPlan plan = {0};
  if (!build_plan(state, items, layout, gap, alignment, sizing, &plan, error))
    return FALSE;

  g_autoptr(GdkPixbuf) expanded = expanded_image_new(
      state->document.image, plan.width, plan.height, 0xf2f2f2ff, error);
  if (expanded == NULL)
    return FALSE;
  g_autoptr(GPtrArray) annotations =
      g_ptr_array_new_with_free_func(shaula_annotation_free);
  for (guint i = 0; i < items->len; i++) {
    CompositionItem *item = g_ptr_array_index(items, i);
    ShaulaAnnotation *annotation = shaula_annotation_new_image_take(
        GDK_PIXBUF(g_object_ref(item->pixbuf)), plan.rects[i]);
    if (annotation == NULL) {
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
                  "Not enough memory to create editable image objects.");
      return FALSE;
    }
    annotation->selected = TRUE;
    g_ptr_array_add(annotations, annotation);
  }

  int first_inserted_id = state->document.next_annotation_id;
  shaula_preview_cancel_operation(state);
  shaula_preview_edit_begin_action(state, "compose images");
  shaula_preview_edit_replace_image(state, GDK_PIXBUF(g_steal_pointer(&expanded)));
  for (guint i = 0; i < annotations->len; i++) {
    ShaulaAnnotation *annotation = g_ptr_array_index(annotations, i);
    shaula_preview_document_add_annotation(&state->document, annotation);
    g_ptr_array_index(annotations, i) = NULL;
  }
  for (guint i = 0; i < state->document.annotations->len; i++) {
    ShaulaAnnotation *annotation =
        g_ptr_array_index(state->document.annotations, i);
    if (annotation != NULL && annotation->id < first_inserted_id)
      annotation->selected = FALSE;
  }
  state->active_tool = SHAULA_TOOL_SELECT;
  shaula_annotation_editor_rebuild_selection(state);
  shaula_preview_edit_finish(
      state, (ShaulaPreviewEditFinish){.update_dimensions = TRUE,
                                       .fit_to_screen = TRUE,
                                       .reset_tool_to_select = TRUE,
                                       .queue_draw = TRUE});
  return TRUE;
}

static gboolean apply_canvas_expansion(ShaulaPreviewState *state, int right,
                                       int bottom, guint32 background,
                                       GError **error) {
  /* Commits one bounded right/down resize only after validating the complete
   * replacement pixbuf, so cancellation and allocation failures are atomic. */
  if (state == NULL || state->document.image == NULL || right < 0 || bottom < 0 ||
      (right == 0 && bottom == 0)) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                "Add space to the right, bottom, or both.");
    return FALSE;
  }
  int width = shaula_preview_image_width(state);
  int height = shaula_preview_image_height(state);
  if (right > SHAULA_COMPOSITION_MAX_DIMENSION - width ||
      bottom > SHAULA_COMPOSITION_MAX_DIMENSION - height) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                "Expanded canvas exceeds 16,384 px.");
    return FALSE;
  }
  g_autoptr(GdkPixbuf) expanded = expanded_image_new(
      state->document.image, width + right, height + bottom, background, error);
  if (expanded == NULL)
    return FALSE;
  shaula_preview_cancel_operation(state);
  shaula_preview_edit_begin_action(state, "expand canvas");
  shaula_preview_edit_replace_image(state, GDK_PIXBUF(g_steal_pointer(&expanded)));
  shaula_preview_edit_finish(
      state, (ShaulaPreviewEditFinish){.update_dimensions = TRUE,
                                       .fit_to_screen = TRUE,
                                       .queue_draw = TRUE});
  return TRUE;
}

static CompositionDialog *composition_ref(CompositionDialog *dialog) {
  g_atomic_int_inc(&dialog->ref_count);
  return dialog;
}

static void composition_unref(CompositionDialog *dialog) {
  if (dialog == NULL || !g_atomic_int_dec_and_test(&dialog->ref_count))
    return;
  if (dialog->clipboard != NULL && dialog->clipboard_changed_handler != 0)
    g_signal_handler_disconnect(dialog->clipboard,
                                dialog->clipboard_changed_handler);
  g_clear_object(&dialog->clipboard_cancel);
  g_clear_object(&dialog->clipboard);
  g_clear_pointer(&dialog->items, g_ptr_array_unref);
  g_clear_pointer(&dialog->recent_paths, g_ptr_array_unref);
  g_free(dialog);
}

static void composition_feedback(CompositionDialog *dialog,
                                 const char *message, gboolean error) {
  if (dialog == NULL || dialog->feedback == NULL)
    return;
  gtk_label_set_text(GTK_LABEL(dialog->feedback), message != NULL ? message : "");
  if (error)
    gtk_widget_add_css_class(dialog->feedback, "error");
  else
    gtk_widget_remove_css_class(dialog->feedback, "error");
}

static int composition_gap(CompositionDialog *dialog) {
  static const int gaps[] = {0, 8, 16, 32};
  guint selected = gtk_drop_down_get_selected(dialog->gap);
  return selected < G_N_ELEMENTS(gaps) ? gaps[selected] : 0;
}

static void draw_pixbuf_at(cairo_t *cr, GdkPixbuf *pixbuf, ShaulaRect rect,
                           double scale, double offset_x, double offset_y) {
  if (pixbuf == NULL || rect.width <= 0 || rect.height <= 0)
    return;
  cairo_save(cr);
  cairo_translate(cr, offset_x + rect.x * scale,
                  offset_y + rect.y * scale);
  cairo_scale(cr, rect.width * scale / gdk_pixbuf_get_width(pixbuf),
              rect.height * scale / gdk_pixbuf_get_height(pixbuf));
  gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
}

static void draw_composition_preview(GtkDrawingArea *area, cairo_t *cr,
                                     int width, int height, gpointer data) {
  (void)area;
  CompositionDialog *dialog = data;
  cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
  cairo_paint(cr);
  CompositionPlan plan = {0};
  if (!build_plan(dialog->state, dialog->items,
                  (CompositionLayout)gtk_drop_down_get_selected(dialog->layout),
                  composition_gap(dialog),
                  (CompositionAlignment)gtk_drop_down_get_selected(
                      dialog->alignment),
                  (CompositionSizing)gtk_drop_down_get_selected(dialog->sizing),
                  &plan, NULL))
    return;
  double scale = MIN((width - 24.0) / plan.width, (height - 24.0) / plan.height);
  scale = MAX(0.01, scale);
  double offset_x = (width - plan.width * scale) / 2.0;
  double offset_y = (height - plan.height * scale) / 2.0;
  cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
  cairo_rectangle(cr, offset_x, offset_y, plan.width * scale,
                  plan.height * scale);
  cairo_fill(cr);
  draw_pixbuf_at(cr, dialog->state->document.image,
                 (ShaulaRect){0, 0, shaula_preview_image_width(dialog->state),
                              shaula_preview_image_height(dialog->state)},
                 scale, offset_x, offset_y);
  for (guint i = 0; i < dialog->items->len; i++) {
    CompositionItem *item = g_ptr_array_index(dialog->items, i);
    draw_pixbuf_at(cr, item->pixbuf, plan.rects[i], scale, offset_x, offset_y);
  }
}

static void composition_update(CompositionDialog *dialog);

static void on_item_up(GtkButton *button, gpointer data) {
  (void)data;
  CompositionDialog *dialog = g_object_get_data(G_OBJECT(button), "dialog");
  guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "index"));
  if (dialog == NULL || index == 0 || index >= dialog->items->len)
    return;
  gpointer item = g_ptr_array_steal_index(dialog->items, index);
  g_ptr_array_insert(dialog->items, index - 1, item);
  composition_update(dialog);
}

static void on_item_down(GtkButton *button, gpointer data) {
  (void)data;
  CompositionDialog *dialog = g_object_get_data(G_OBJECT(button), "dialog");
  guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "index"));
  if (dialog == NULL || index + 1 >= dialog->items->len)
    return;
  gpointer item = g_ptr_array_steal_index(dialog->items, index);
  g_ptr_array_insert(dialog->items, index + 1, item);
  composition_update(dialog);
}

static void on_item_remove(GtkButton *button, gpointer data) {
  (void)data;
  CompositionDialog *dialog = g_object_get_data(G_OBJECT(button), "dialog");
  guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "index"));
  if (dialog == NULL || index >= dialog->items->len)
    return;
  g_ptr_array_remove_index(dialog->items, index);
  composition_update(dialog);
}

static GtkWidget *item_button(const char *label, CompositionDialog *dialog,
                              guint index, GCallback callback) {
  GtkWidget *button = gtk_button_new_with_label(label);
  g_object_set_data(G_OBJECT(button), "dialog", dialog);
  g_object_set_data(G_OBJECT(button), "index", GUINT_TO_POINTER(index));
  g_signal_connect(button, "clicked", callback, NULL);
  return button;
}

static void rebuild_item_rows(CompositionDialog *dialog) {
  GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(dialog->items_list));
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(dialog->items_list, child);
    child = next;
  }
  for (guint i = 0; i < dialog->items->len; i++) {
    CompositionItem *item = g_ptr_array_index(dialog->items, i);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    g_autofree char *dimensions = g_strdup_printf(
        "%s — %d×%d", item->label, gdk_pixbuf_get_width(item->pixbuf),
        gdk_pixbuf_get_height(item->pixbuf));
    g_autoptr(GdkTexture) thumbnail_texture =
        gdk_texture_new_for_pixbuf(item->pixbuf);
    GtkWidget *thumbnail = gtk_picture_new_for_paintable(
        GDK_PAINTABLE(thumbnail_texture));
    gtk_picture_set_content_fit(GTK_PICTURE(thumbnail),
                                GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(GTK_PICTURE(thumbnail), TRUE);
    gtk_widget_set_size_request(thumbnail, 56, 40);
    gtk_box_append(GTK_BOX(row), thumbnail);
    GtkWidget *label = gtk_label_new(dimensions);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    gtk_box_append(GTK_BOX(row),
                   item_button("↑", dialog, i, G_CALLBACK(on_item_up)));
    gtk_box_append(GTK_BOX(row),
                   item_button("↓", dialog, i, G_CALLBACK(on_item_down)));
    gtk_box_append(GTK_BOX(row),
                   item_button("Remove", dialog, i,
                               G_CALLBACK(on_item_remove)));
    gtk_list_box_append(dialog->items_list, row);
  }
}

static void composition_update(CompositionDialog *dialog) {
  if (dialog == NULL || dialog->closing)
    return;
  rebuild_item_rows(dialog);
  gboolean has_room = dialog->items->len < SHAULA_COMPOSITION_MAX_ITEMS;
  gtk_widget_set_sensitive(dialog->clipboard_button,
                           has_room && !dialog->clipboard_in_flight);
  gtk_widget_set_sensitive(dialog->recent_button,
                           has_room && dialog->recent_paths->len > 0);
  gtk_widget_set_sensitive(dialog->apply_button, dialog->items->len > 0);
  gtk_widget_queue_draw(GTK_WIDGET(dialog->preview));
}

static gboolean composition_add_pixbuf(CompositionDialog *dialog,
                                       GdkPixbuf *pixbuf, const char *label) {
  if (dialog == NULL || dialog->closing || pixbuf == NULL ||
      dialog->items->len >= SHAULA_COMPOSITION_MAX_ITEMS)
    return FALSE;
  if (!pixbuf_within_limits(pixbuf)) {
    composition_feedback(dialog,
                         "Image exceeds 16,384 px per side or 32 MP.", TRUE);
    return FALSE;
  }
  CompositionItem *item = g_new0(CompositionItem, 1);
  item->pixbuf = GDK_PIXBUF(g_object_ref(pixbuf));
  item->label = g_strdup(label != NULL ? label : "Image");
  g_ptr_array_add(dialog->items, item);
  composition_feedback(dialog, "", FALSE);
  composition_update(dialog);
  return TRUE;
}

static void on_files_response(GtkNativeDialog *chooser, int response,
                              gpointer data) {
  /* File chooser boundary: decode into dialog-owned items before any document
   * edit can be requested. */
  CompositionDialog *dialog = data;
  if (!dialog->closing && response == GTK_RESPONSE_ACCEPT) {
    g_autoptr(GListModel) files =
        gtk_file_chooser_get_files(GTK_FILE_CHOOSER(chooser));
    guint count = g_list_model_get_n_items(files);
    for (guint i = 0; i < count &&
                        dialog->items->len < SHAULA_COMPOSITION_MAX_ITEMS;
         i++) {
      g_autoptr(GFile) file = g_list_model_get_item(files, i);
      g_autofree char *path = g_file_get_path(file);
      g_autofree char *label = g_file_get_basename(file);
      g_autoptr(GError) error = NULL;
      g_autoptr(GdkPixbuf) pixbuf =
          path != NULL ? gdk_pixbuf_new_from_file(path, &error) : NULL;
      if (pixbuf == NULL ||
          !composition_add_pixbuf(dialog, pixbuf, label)) {
        composition_feedback(dialog,
                             error != NULL ? error->message
                                           : "A selected image is invalid.",
                             TRUE);
      }
    }
  }
  g_object_unref(chooser);
  composition_unref(dialog);
}

static void on_choose_files(GtkButton *button, gpointer data) {
  (void)button;
  CompositionDialog *dialog = data;
  GtkFileChooserNative *chooser = gtk_file_chooser_native_new(
      "Choose images", GTK_WINDOW(dialog->dialog),
      GTK_FILE_CHOOSER_ACTION_OPEN, "Add", "Cancel");
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);
  g_signal_connect(chooser, "response", G_CALLBACK(on_files_response),
                   composition_ref(dialog));
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}

static void on_clipboard_texture(GObject *source, GAsyncResult *result,
                                 gpointer data) {
  /* Clipboard boundary: finish the single cancellable read and transfer a
   * decoded texture into the dialog's owned source list. */
  CompositionDialog *dialog = data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GdkTexture) texture =
      gdk_clipboard_read_texture_finish(GDK_CLIPBOARD(source), result, &error);
  gboolean clipboard_changed = dialog->clipboard_changed;
  dialog->clipboard_in_flight = FALSE;
  if (dialog->clipboard != NULL && dialog->clipboard_changed_handler != 0)
    g_signal_handler_disconnect(dialog->clipboard,
                                dialog->clipboard_changed_handler);
  dialog->clipboard_changed_handler = 0;
  if (!dialog->closing && clipboard_changed) {
    composition_feedback(
        dialog,
        "Clipboard changed while reading. Copy the image again and retry.",
        TRUE);
  } else if (!dialog->closing && texture != NULL) {
    g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_get_from_texture(texture);
    if (pixbuf == NULL ||
        !composition_add_pixbuf(dialog, pixbuf, "Current clipboard"))
      composition_feedback(dialog, "Clipboard image could not be decoded.",
                           TRUE);
  } else if (!dialog->closing &&
             !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    composition_feedback(dialog, "Clipboard does not contain a readable image.",
                         TRUE);
  }
  composition_update(dialog);
  composition_unref(dialog);
}

static void on_composition_clipboard_changed(GdkClipboard *clipboard,
                                              gpointer data) {
  (void)clipboard;
  CompositionDialog *dialog = data;
  if (dialog == NULL || dialog->closing || !dialog->clipboard_in_flight)
    return;
  dialog->clipboard_changed = TRUE;
  if (dialog->clipboard_cancel != NULL)
    g_cancellable_cancel(dialog->clipboard_cancel);
}

static void on_add_clipboard(GtkButton *button, gpointer data) {
  (void)button;
  CompositionDialog *dialog = data;
  if (dialog->clipboard_in_flight)
    return;
  dialog->clipboard_in_flight = TRUE;
  dialog->clipboard_changed = FALSE;
  g_clear_object(&dialog->clipboard_cancel);
  dialog->clipboard_cancel = g_cancellable_new();
  g_clear_object(&dialog->clipboard);
  dialog->clipboard = g_object_ref(
      gtk_widget_get_clipboard(GTK_WIDGET(dialog->dialog)));
  dialog->clipboard_changed_handler = g_signal_connect(
      dialog->clipboard, "changed",
      G_CALLBACK(on_composition_clipboard_changed), dialog);
  composition_update(dialog);
  gdk_clipboard_read_texture_async(dialog->clipboard, dialog->clipboard_cancel,
                                   on_clipboard_texture,
                                   composition_ref(dialog));
}

static void on_add_recent(GtkButton *button, gpointer data) {
  (void)button;
  CompositionDialog *dialog = data;
  guint selected = gtk_drop_down_get_selected(dialog->recent);
  if (selected >= dialog->recent_paths->len)
    return;
  const char *path = g_ptr_array_index(dialog->recent_paths, selected);
  g_autoptr(GError) error = NULL;
  g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new_from_file(path, &error);
  if (pixbuf == NULL) {
    composition_feedback(dialog, "Recent capture is no longer available.",
                         TRUE);
    return;
  }
  g_autofree char *label = g_path_get_basename(path);
  (void)composition_add_pixbuf(dialog, pixbuf, label);
}

static void on_composition_option_changed(GObject *object, GParamSpec *pspec,
                                          gpointer data) {
  (void)object;
  (void)pspec;
  composition_update(data);
}

static void on_composition_destroy(GtkWidget *widget, gpointer data) {
  (void)widget;
  CompositionDialog *dialog = data;
  dialog->closing = TRUE;
  if (dialog->clipboard_cancel != NULL)
    g_cancellable_cancel(dialog->clipboard_cancel);
  composition_unref(dialog);
}

static void on_composition_response(GtkDialog *widget, int response,
                                    gpointer data) {
  CompositionDialog *dialog = data;
  if (response == GTK_RESPONSE_APPLY) {
    g_autoptr(GError) error = NULL;
    if (!apply_composition(
            dialog->state, dialog->items,
            (CompositionLayout)gtk_drop_down_get_selected(dialog->layout),
            composition_gap(dialog),
            (CompositionAlignment)gtk_drop_down_get_selected(
                dialog->alignment),
            (CompositionSizing)gtk_drop_down_get_selected(dialog->sizing),
            &error)) {
      composition_feedback(dialog,
                           error != NULL ? error->message
                                         : "Composition could not be applied.",
                           TRUE);
      return;
    }
  }
  gtk_window_destroy(GTK_WINDOW(widget));
}

static GtkWidget *labeled_control(const char *label, GtkWidget *control) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *text = gtk_label_new(label);
  gtk_label_set_xalign(GTK_LABEL(text), 0.0);
  gtk_widget_add_css_class(text, "dim-label");
  gtk_box_append(GTK_BOX(box), text);
  gtk_box_append(GTK_BOX(box), control);
  return box;
}

void shaula_preview_show_composition_dialog(ShaulaPreviewState *state) {
  /* Builds the transient composition sheet without touching document history
   * until the user confirms a complete placement plan. */
  if (state == NULL || state->window == NULL || state->document.image == NULL)
    return;
  CompositionDialog *dialog = g_new0(CompositionDialog, 1);
  dialog->ref_count = 1;
  dialog->state = state;
  dialog->items = g_ptr_array_new_with_free_func(composition_item_free);
  dialog->recent_paths = shaula_recent_captures_list(NULL);
  dialog->dialog = gtk_dialog_new_with_buttons(
      "Add images and expand canvas", GTK_WINDOW(state->window),
      GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Apply",
      GTK_RESPONSE_APPLY, NULL);
  gtk_window_set_default_size(GTK_WINDOW(dialog->dialog), 720, 620);
  dialog->apply_button =
      gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog->dialog),
                                         GTK_RESPONSE_APPLY);
  gtk_widget_add_css_class(dialog->apply_button, "suggested-action");

  GtkWidget *content =
      gtk_dialog_get_content_area(GTK_DIALOG(dialog->dialog));
  gtk_orientable_set_orientation(GTK_ORIENTABLE(content),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_box_set_spacing(GTK_BOX(content), 12);
  gtk_widget_set_margin_start(content, 16);
  gtk_widget_set_margin_end(content, 16);
  gtk_widget_set_margin_top(content, 12);
  gtk_widget_set_margin_bottom(content, 12);

  GtkWidget *sources = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *files = gtk_button_new_with_label("Choose files…");
  dialog->clipboard_button = gtk_button_new_with_label("Add clipboard");
  g_signal_connect(files, "clicked", G_CALLBACK(on_choose_files), dialog);
  g_signal_connect(dialog->clipboard_button, "clicked",
                   G_CALLBACK(on_add_clipboard), dialog);
  gtk_box_append(GTK_BOX(sources), files);
  gtk_box_append(GTK_BOX(sources), dialog->clipboard_button);

  g_auto(GStrv) recent_labels =
      g_new0(char *, dialog->recent_paths->len + 1);
  for (guint i = 0; i < dialog->recent_paths->len; i++)
    recent_labels[i] = g_path_get_basename(
        g_ptr_array_index(dialog->recent_paths, i));
  dialog->recent = GTK_DROP_DOWN(
      gtk_drop_down_new_from_strings((const char *const *)recent_labels));
  dialog->recent_button = gtk_button_new_with_label("Add recent");
  g_signal_connect(dialog->recent_button, "clicked", G_CALLBACK(on_add_recent),
                   dialog);
  gtk_widget_set_hexpand(GTK_WIDGET(dialog->recent), TRUE);
  gtk_box_append(GTK_BOX(sources), GTK_WIDGET(dialog->recent));
  gtk_box_append(GTK_BOX(sources), dialog->recent_button);
  gtk_box_append(GTK_BOX(content), sources);

  dialog->items_list = GTK_LIST_BOX(gtk_list_box_new());
  gtk_widget_add_css_class(GTK_WIDGET(dialog->items_list), "boxed-list");
  gtk_widget_set_vexpand(GTK_WIDGET(dialog->items_list), TRUE);
  gtk_box_append(GTK_BOX(content), GTK_WIDGET(dialog->items_list));

  GtkWidget *options = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  const char *layouts[] = {"Vertical", "Horizontal", NULL};
  const char *gaps[] = {"0 px", "8 px", "16 px", "32 px", NULL};
  const char *alignments[] = {"Start", "Center", "End", NULL};
  const char *sizes[] = {"Original size", "Match width / height", NULL};
  dialog->layout =
      GTK_DROP_DOWN(gtk_drop_down_new_from_strings(layouts));
  dialog->gap = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(gaps));
  dialog->alignment =
      GTK_DROP_DOWN(gtk_drop_down_new_from_strings(alignments));
  dialog->sizing = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(sizes));
  gtk_drop_down_set_selected(dialog->gap, 1);
  gtk_box_append(GTK_BOX(options),
                 labeled_control("Layout", GTK_WIDGET(dialog->layout)));
  gtk_box_append(GTK_BOX(options),
                 labeled_control("Gap", GTK_WIDGET(dialog->gap)));
  gtk_box_append(GTK_BOX(options),
                 labeled_control("Alignment", GTK_WIDGET(dialog->alignment)));
  gtk_box_append(GTK_BOX(options),
                 labeled_control("Sizing", GTK_WIDGET(dialog->sizing)));
  gtk_box_append(GTK_BOX(content), options);
  g_signal_connect(dialog->layout, "notify::selected",
                   G_CALLBACK(on_composition_option_changed), dialog);
  g_signal_connect(dialog->gap, "notify::selected",
                   G_CALLBACK(on_composition_option_changed), dialog);
  g_signal_connect(dialog->alignment, "notify::selected",
                   G_CALLBACK(on_composition_option_changed), dialog);
  g_signal_connect(dialog->sizing, "notify::selected",
                   G_CALLBACK(on_composition_option_changed), dialog);

  dialog->preview = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_widget_set_size_request(GTK_WIDGET(dialog->preview), -1, 190);
  gtk_drawing_area_set_draw_func(dialog->preview, draw_composition_preview,
                                 dialog, NULL);
  gtk_box_append(GTK_BOX(content), GTK_WIDGET(dialog->preview));
  dialog->feedback = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(dialog->feedback), 0.0);
  gtk_label_set_wrap(GTK_LABEL(dialog->feedback), TRUE);
  gtk_box_append(GTK_BOX(content), dialog->feedback);

  g_signal_connect(dialog->dialog, "response",
                   G_CALLBACK(on_composition_response), dialog);
  g_signal_connect(dialog->dialog, "destroy",
                   G_CALLBACK(on_composition_destroy), dialog);
  composition_update(dialog);
  gtk_window_present(GTK_WINDOW(dialog->dialog));
}

static void on_canvas_response(GtkDialog *dialog, int response, gpointer data) {
  CanvasDialog *canvas = data;
  if (response == GTK_RESPONSE_APPLY) {
    static const guint32 backgrounds[] = {
        0xf2f2f2ff, 0xd0d0d0ff, 0x505050ff, 0x151515ff};
    guint selected = gtk_drop_down_get_selected(canvas->background);
    g_autoptr(GError) error = NULL;
    if (!apply_canvas_expansion(
            canvas->state,
            gtk_spin_button_get_value_as_int(canvas->right),
            gtk_spin_button_get_value_as_int(canvas->bottom),
            selected < G_N_ELEMENTS(backgrounds) ? backgrounds[selected]
                                                  : backgrounds[0],
            &error)) {
      shaula_preview_show_feedback(
          canvas->state,
          error != NULL ? error->message : "Canvas could not be expanded.",
          TRUE);
      return;
    }
  }
  gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_canvas_destroy(GtkWidget *widget, gpointer data) {
  (void)widget;
  g_free(data);
}

void shaula_preview_show_canvas_dialog(ShaulaPreviewState *state) {
  /* Presents direct bounded canvas controls; only the Apply response reaches
   * the document edit session. */
  if (state == NULL || state->window == NULL || state->document.image == NULL)
    return;
  CanvasDialog *canvas = g_new0(CanvasDialog, 1);
  canvas->state = state;
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Expand canvas", GTK_WINDOW(state->window), GTK_DIALOG_MODAL, "Cancel",
      GTK_RESPONSE_CANCEL, "Expand", GTK_RESPONSE_APPLY, NULL);
  GtkWidget *apply = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog),
                                                        GTK_RESPONSE_APPLY);
  gtk_widget_add_css_class(apply, "suggested-action");
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_orientable_set_orientation(GTK_ORIENTABLE(content),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_box_set_spacing(GTK_BOX(content), 12);
  gtk_widget_set_margin_start(content, 16);
  gtk_widget_set_margin_end(content, 16);
  gtk_widget_set_margin_top(content, 12);
  gtk_widget_set_margin_bottom(content, 12);
  GtkWidget *description = gtk_label_new(
      "Add bounded space to the right or bottom. Existing content stays at "
      "the same coordinates and the operation is one undo step.");
  gtk_label_set_xalign(GTK_LABEL(description), 0.0);
  gtk_label_set_wrap(GTK_LABEL(description), TRUE);
  gtk_box_append(GTK_BOX(content), description);
  canvas->right = GTK_SPIN_BUTTON(
      gtk_spin_button_new_with_range(0, SHAULA_COMPOSITION_MAX_DIMENSION, 8));
  canvas->bottom = GTK_SPIN_BUTTON(
      gtk_spin_button_new_with_range(0, SHAULA_COMPOSITION_MAX_DIMENSION, 8));
  const char *backgrounds[] = {"Light", "Neutral", "Dark", "Black", NULL};
  canvas->background =
      GTK_DROP_DOWN(gtk_drop_down_new_from_strings(backgrounds));
  gtk_box_append(GTK_BOX(content),
                 labeled_control("Add right (px)", GTK_WIDGET(canvas->right)));
  gtk_box_append(GTK_BOX(content),
                 labeled_control("Add bottom (px)", GTK_WIDGET(canvas->bottom)));
  gtk_box_append(GTK_BOX(content),
                 labeled_control("New area background",
                                 GTK_WIDGET(canvas->background)));
  g_signal_connect(dialog, "response", G_CALLBACK(on_canvas_response), canvas);
  g_signal_connect(dialog, "destroy", G_CALLBACK(on_canvas_destroy), canvas);
  gtk_window_present(GTK_WINDOW(dialog));
}
