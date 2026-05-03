#include "preview_toolbar.h"

#include "preview_actions.h"
#include "preview_commands.h"
#include "preview_icons.h"

#include <math.h>

typedef struct {
  const char *icon_name;
  const char *label;
  const char *tooltip;
  ShaulaTool tool;
} ToolActionSpec;

typedef struct {
  const char *icon_name;
  const char *label;
  const char *tooltip;
  GCallback callback;
} MenuActionSpec;

static const ToolActionSpec secondary_tools[] = {
    {"shaula-arrow-symbolic", "Arrow", "Arrow", SHAULA_TOOL_ARROW},
    {"shaula-text-symbolic", "Text", "Text", SHAULA_TOOL_TEXT},
    {"shaula-measure-symbolic", "Measure", "Measure", SHAULA_TOOL_MEASURE},
    {"shaula-rectangle-symbolic", "Rectangle", "Rectangle",
     SHAULA_TOOL_RECTANGLE},
    {"shaula-highlight-symbolic", "Highlight", "Highlight",
     SHAULA_TOOL_HIGHLIGHT},
    {"shaula-pen-symbolic", "Pen", "Pen", SHAULA_TOOL_PEN},
};

static GtkWidget *make_muted_label(const char *text);
static GtkWidget *make_fixed_width_muted_label(const char *text, int chars,
                                               float xalign);

static GtkWidget *make_toolbar_button(ShaulaPreviewState *state,
                                      const char *icon_name,
                                      const char *tooltip,
                                      GCallback callback) {
  GtkWidget *button = gtk_button_new();
  gtk_button_set_child(GTK_BUTTON(button),
                       shaula_preview_make_toolbar_icon(state, icon_name));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 32, 32);
  g_signal_connect(button, "clicked", callback, state);
  return button;
}

static GtkWidget *make_tool_toggle(ShaulaPreviewState *state,
                                   const char *icon_name, const char *tooltip,
                                   ShaulaTool tool) {
  GtkWidget *button = gtk_toggle_button_new();
  gtk_button_set_child(GTK_BUTTON(button),
                       shaula_preview_make_toolbar_icon(state, icon_name));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 32, 32);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
                               state->active_tool == tool);
  g_object_set_data(G_OBJECT(button), "tool", GINT_TO_POINTER(tool));
  g_signal_connect(button, "clicked", G_CALLBACK(shaula_preview_on_tool_clicked),
                   state);
  state->tool_buttons[tool] = button;
  return button;
}

static GtkWidget *make_menu_action_row(ShaulaPreviewState *state,
                                       const MenuActionSpec *spec) {
  GtkWidget *button = gtk_button_new();
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *icon = shaula_preview_make_toolbar_icon(state, spec->icon_name);
  GtkWidget *label = gtk_label_new(spec->label);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_box_append(GTK_BOX(row), icon);
  gtk_box_append(GTK_BOX(row), label);
  gtk_button_set_child(GTK_BUTTON(button), row);
  gtk_widget_set_tooltip_text(button, spec->tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
  g_signal_connect(button, "clicked", spec->callback, state);
  return button;
}

static GtkWidget *make_menu_tool_row(ShaulaPreviewState *state,
                                     const ToolActionSpec *spec) {
  GtkWidget *button = gtk_button_new();
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *icon = shaula_preview_make_toolbar_icon(state, spec->icon_name);
  GtkWidget *label = gtk_label_new(spec->label);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_box_append(GTK_BOX(row), icon);
  gtk_box_append(GTK_BOX(row), label);
  gtk_button_set_child(GTK_BUTTON(button), row);
  gtk_widget_set_tooltip_text(button, spec->tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
  g_object_set_data(G_OBJECT(button), "tool", GINT_TO_POINTER(spec->tool));
  g_signal_connect(button, "clicked", G_CALLBACK(shaula_preview_on_tool_clicked),
                   state);
  return button;
}

static int visible_secondary_count_for_width(ShaulaPreviewState *state,
                                             int width) {
  int extra = width - PREVIEW_TOOLBAR_BASE_VISIBLE_W;
  if (extra <= 0)
    return 0;
  int count = extra / PREVIEW_TOOLBAR_REVEAL_STEP_W;
  return MIN(count, state->toolbar_secondary_count);
}

static void append_separator(GtkWidget *box) {
  GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(separator, 4);
  gtk_widget_set_margin_bottom(separator, 4);
  gtk_box_append(GTK_BOX(box), separator);
}

static void rebuild_more_menu(ShaulaPreviewState *state, int visible_count) {
  if (state->more_menu_box == NULL)
    return;

  GtkWidget *child = gtk_widget_get_first_child(state->more_menu_box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(state->more_menu_box), child);
    child = next;
  }

  gboolean has_hidden_tools = FALSE;
  for (int i = visible_count; i < state->toolbar_secondary_count; i++) {
    ShaulaTool tool = state->toolbar_secondary_tools[i];
    for (int j = 0; j < (int)G_N_ELEMENTS(secondary_tools); j++) {
      if (secondary_tools[j].tool == tool) {
        gtk_box_append(GTK_BOX(state->more_menu_box),
                       make_menu_tool_row(state, &secondary_tools[j]));
        has_hidden_tools = TRUE;
        break;
      }
    }
  }
  if (has_hidden_tools)
    append_separator(state->more_menu_box);

  const MenuActionSpec actions[] = {
      {"shaula-select-symbolic", "Fit to screen", "Fit to screen (f)",
       G_CALLBACK(shaula_preview_on_fit_clicked)},
      {"shaula-select-symbolic", "Actual size", "Actual size (0)",
       G_CALLBACK(shaula_preview_on_actual_clicked)},
      {"shaula-discard-symbolic", "Reset annotations", "Reset annotations",
       G_CALLBACK(shaula_preview_on_reset_annotations_clicked)},
      {"shaula-copy-symbolic", "Copy path", "Copy original image path",
       G_CALLBACK(shaula_preview_on_copy_path_clicked)},
      {"shaula-more-symbolic", "Open containing folder",
       "Open containing folder",
       G_CALLBACK(shaula_preview_on_open_folder_clicked)},
  };
  for (int i = 0; i < (int)G_N_ELEMENTS(actions); i++)
    gtk_box_append(GTK_BOX(state->more_menu_box),
                   make_menu_action_row(state, &actions[i]));
}

/* Contract: the minimum preview width owns primary actions. Secondary tools
 * overflow deterministically so GTK never clips header controls on resize.
 */
static void update_toolbar_overflow(ShaulaPreviewState *state, int width) {
  int visible_count = visible_secondary_count_for_width(state, width);
  if (visible_count == state->toolbar_overflow_visible_count)
    return;

  for (int i = 0; i < state->toolbar_secondary_count; i++)
    gtk_widget_set_visible(state->toolbar_secondary[i], i < visible_count);

  rebuild_more_menu(state, visible_count);
  state->toolbar_overflow_visible_count = visible_count;
}

static gboolean on_topbar_tick(GtkWidget *widget, GdkFrameClock *clock,
                               gpointer data) {
  (void)clock;
  ShaulaPreviewState *state = data;
  update_toolbar_overflow(state, gtk_widget_get_width(widget));
  return G_SOURCE_CONTINUE;
}

static GtkWidget *make_more_button(ShaulaPreviewState *state) {
  GtkWidget *button = gtk_menu_button_new();
  GtkWidget *icon = shaula_preview_make_toolbar_icon(state, "shaula-more-symbolic");
  GtkWidget *popover = gtk_popover_new();
  GtkWidget *menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  gtk_menu_button_set_child(GTK_MENU_BUTTON(button), icon);
  gtk_widget_set_tooltip_text(button, "More");
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 32, 32);
  gtk_widget_set_margin_top(menu_box, 6);
  gtk_widget_set_margin_bottom(menu_box, 6);
  gtk_widget_set_margin_start(menu_box, 6);
  gtk_widget_set_margin_end(menu_box, 6);
  gtk_popover_set_child(GTK_POPOVER(popover), menu_box);
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);

  state->more_button = button;
  state->more_popover = popover;
  state->more_menu_box = menu_box;
  return button;
}

static void append_secondary_toolbar_button(ShaulaPreviewState *state,
                                            GtkWidget *actions, int index) {
  GtkWidget *button = make_tool_toggle(state, secondary_tools[index].icon_name,
                                       secondary_tools[index].tooltip,
                                       secondary_tools[index].tool);
  gtk_widget_set_visible(button, FALSE);
  gtk_box_append(GTK_BOX(actions), button);
  state->toolbar_secondary[state->toolbar_secondary_count] = button;
  state->toolbar_secondary_tools[state->toolbar_secondary_count] =
      secondary_tools[index].tool;
  state->toolbar_secondary_count++;
}

static GtkWidget *build_selection_actions_group(ShaulaPreviewState *state) {
  GtkWidget *group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_valign(group, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(group, "linked");

  state->duplicate_button =
      make_toolbar_button(state, "shaula-duplicate-symbolic",
                          "Duplicate selected annotation (Ctrl+D)",
                          G_CALLBACK(shaula_preview_on_duplicate_clicked));
  state->crop_selected_button =
      make_toolbar_button(state, "shaula-crop-symbolic",
                          "Crop to selected annotation",
                          G_CALLBACK(shaula_preview_on_crop_selected_clicked));
  state->blur_region_button =
      make_toolbar_button(state, "shaula-blur-symbolic", "Blur selected region",
                          G_CALLBACK(shaula_preview_on_blur_region_clicked));
  state->erase_region_button =
      make_toolbar_button(state, "shaula-erase-symbolic",
                          "Erase selected region",
                          G_CALLBACK(shaula_preview_on_erase_region_clicked));
  state->spotlight_region_button = make_toolbar_button(
      state, "shaula-spotlight-symbolic", "Spotlight selected region",
      G_CALLBACK(shaula_preview_on_spotlight_region_clicked));
  state->delete_button =
      make_toolbar_button(state, "shaula-trash-symbolic",
                          "Delete selected annotation (Delete)",
                          G_CALLBACK(shaula_preview_on_delete_clicked));

  gtk_box_append(GTK_BOX(group), state->duplicate_button);
  gtk_box_append(GTK_BOX(group), state->crop_selected_button);
  gtk_box_append(GTK_BOX(group), state->blur_region_button);
  gtk_box_append(GTK_BOX(group), state->erase_region_button);
  gtk_box_append(GTK_BOX(group), state->spotlight_region_button);
  gtk_box_append(GTK_BOX(group), state->delete_button);
  state->selection_actions_box = group;
  shaula_preview_toolbar_update_selection_state(state);
  return group;
}

static GtkWidget *make_muted_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_widget_add_css_class(label, "dim-label");
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  return label;
}

static GtkWidget *make_fixed_width_muted_label(const char *text, int chars,
                                               float xalign) {
  GtkWidget *label = make_muted_label(text);
  gtk_label_set_width_chars(GTK_LABEL(label), chars);
  gtk_label_set_xalign(GTK_LABEL(label), xalign);

  PangoAttrList *attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_font_features_new("tnum=1"));
  gtk_label_set_attributes(GTK_LABEL(label), attrs);
  pango_attr_list_unref(attrs);

  return label;
}

static GtkWidget *make_normal_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  return label;
}

static void draw_swatch(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                        gpointer data) {
  (void)area;
  ShaulaPreviewState *state = data;
  double rw = (double)w;
  double rh = (double)h;
  double r = 4.0;
  cairo_new_sub_path(cr);
  cairo_arc(cr, r, r, r, G_PI, 1.5 * G_PI);
  cairo_arc(cr, rw - r, r, r, 1.5 * G_PI, 2.0 * G_PI);
  cairo_arc(cr, rw - r, rh - r, r, 0, 0.5 * G_PI);
  cairo_arc(cr, r, rh - r, r, 0.5 * G_PI, G_PI);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, state->current_color.r, state->current_color.g,
                        state->current_color.b, state->current_color.a);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.15);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
}

static GtkWidget *build_tool_group(ShaulaPreviewState *state) {
  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);
  state->toolbar_secondary_count = 0;
  state->toolbar_overflow_visible_count = -1;

  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button(state, "shaula-copy-symbolic",
                                     "Copy (Ctrl+C)",
                                     G_CALLBACK(shaula_preview_on_copy_clicked)));
  gtk_box_append(GTK_BOX(actions),
                 make_toolbar_button(state, "shaula-save-symbolic",
                                     "Save As (Ctrl+S)",
                                     G_CALLBACK(shaula_preview_on_save_clicked)));
  state->undo_button =
      make_toolbar_button(state, "shaula-undo-symbolic", "Undo",
                          G_CALLBACK(shaula_preview_on_undo_clicked));
  state->redo_button =
      make_toolbar_button(state, "shaula-redo-symbolic", "Redo",
                          G_CALLBACK(shaula_preview_on_redo_clicked));
  gtk_widget_set_sensitive(state->undo_button, FALSE);
  gtk_widget_set_sensitive(state->redo_button, FALSE);
  gtk_box_append(GTK_BOX(actions), state->undo_button);
  gtk_box_append(GTK_BOX(actions), state->redo_button);

  gtk_box_append(GTK_BOX(actions),
                 make_tool_toggle(state, "shaula-crop-symbolic", "Crop",
                                  SHAULA_TOOL_CROP));
  gtk_box_append(GTK_BOX(actions),
                 make_tool_toggle(state, "shaula-select-symbolic", "Select",
                                  SHAULA_TOOL_SELECT));
  gtk_box_append(GTK_BOX(actions),
                 make_tool_toggle(state, "shaula-spotlight-symbolic",
                                  "Spotlight", SHAULA_TOOL_SPOTLIGHT));

  gtk_box_append(GTK_BOX(actions), build_selection_actions_group(state));

  for (int i = 0; i < (int)G_N_ELEMENTS(secondary_tools); i++)
    append_secondary_toolbar_button(state, actions, i);

  gtk_box_append(GTK_BOX(actions), make_more_button(state));

  return actions;
}

static GtkWidget *build_metadata_group(ShaulaPreviewState *state) {
  GtkWidget *metadata = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_valign(metadata, GTK_ALIGN_CENTER);

  GtkWidget *swatch = gtk_drawing_area_new();
  state->color_swatch = swatch;
  gtk_widget_set_size_request(swatch, 16, 16);
  gtk_widget_set_valign(swatch, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_end(swatch, 4);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch), draw_swatch, state,
                                 NULL);
  gtk_box_append(GTK_BOX(metadata), swatch);

  char hex[8];
  shaula_color_to_hex(state->current_color, hex);
  state->color_hex_label = make_normal_label(hex);
  gtk_box_append(GTK_BOX(metadata), state->color_hex_label);

  if (state->image != NULL) {
    char size_buf[32];
    snprintf(size_buf, sizeof(size_buf), "%d\xc3\x97%d px",
             shaula_preview_image_width(state),
             shaula_preview_image_height(state));
    state->dimensions_label = make_muted_label(size_buf);
    gtk_box_append(GTK_BOX(metadata), state->dimensions_label);
  }

  state->zoom_label = make_fixed_width_muted_label("100% Zoom", 9, 1.0f);
  gtk_box_append(GTK_BOX(metadata), state->zoom_label);
  gtk_widget_set_margin_end(metadata, 8);

  GtkWidget *discard_btn =
      make_toolbar_button(state, "shaula-discard-symbolic", "Discard (Esc)",
                          G_CALLBACK(shaula_preview_on_discard_clicked));
  gtk_box_append(GTK_BOX(metadata), discard_btn);

  return metadata;
}

GtkWidget *shaula_preview_toolbar_build(ShaulaPreviewState *state) {
  GtkWidget *bar = gtk_header_bar_new();

  GtkWidget *toolbar = build_tool_group(state);
  gtk_widget_set_halign(toolbar, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(toolbar, GTK_ALIGN_CENTER);

  GtkWidget *right_group = build_metadata_group(state);
  gtk_widget_set_halign(right_group, GTK_ALIGN_END);
  gtk_widget_set_valign(right_group, GTK_ALIGN_CENTER);

  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(bar), toolbar);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), right_group);
  update_toolbar_overflow(state, PREVIEW_TOOLBAR_BASE_VISIBLE_W);
  gtk_widget_add_tick_callback(bar, on_topbar_tick, state, NULL);

  return bar;
}

void shaula_preview_toolbar_update_tool_state(ShaulaPreviewState *state) {
  for (int i = 0; i < SHAULA_TOOL_COUNT; i++) {
    if (state->tool_buttons[i] == NULL)
      continue;
    GtkToggleButton *button = GTK_TOGGLE_BUTTON(state->tool_buttons[i]);
    gboolean active = state->active_tool == (ShaulaTool)i;
    if (gtk_toggle_button_get_active(button) != active)
      gtk_toggle_button_set_active(button, active);
  }
  shaula_preview_toolbar_update_selection_state(state);
}

void shaula_preview_toolbar_update_history_state(ShaulaPreviewState *state) {
  if (state == NULL)
    return;
  if (state->undo_button != NULL)
    gtk_widget_set_sensitive(
        state->undo_button,
        shaula_preview_command_available(state, SHAULA_PREVIEW_COMMAND_UNDO));
  if (state->redo_button != NULL)
    gtk_widget_set_sensitive(
        state->redo_button,
        shaula_preview_command_available(state, SHAULA_PREVIEW_COMMAND_REDO));
}

void shaula_preview_toolbar_update_selection_state(ShaulaPreviewState *state) {
  if (state == NULL)
    return;

  gboolean can_duplicate = shaula_preview_command_available(
      state, SHAULA_PREVIEW_COMMAND_DUPLICATE_SELECTED);
  gboolean can_crop = shaula_preview_command_available(
      state, SHAULA_PREVIEW_COMMAND_CROP_SELECTED);
  gboolean can_blur = shaula_preview_command_available(
      state, SHAULA_PREVIEW_COMMAND_BLUR_REGION);
  gboolean can_erase = shaula_preview_command_available(
      state, SHAULA_PREVIEW_COMMAND_ERASE_REGION);
  gboolean can_spotlight = shaula_preview_command_available(
      state, SHAULA_PREVIEW_COMMAND_SPOTLIGHT_REGION);
  gboolean can_delete = shaula_preview_command_available(
      state, SHAULA_PREVIEW_COMMAND_DELETE_SELECTED);
  gboolean has_object_selection = state->selected_annotation != NULL;
  gboolean has_region_selection = state->has_region_selection;
  gboolean show_group =
      state->active_tool == SHAULA_TOOL_SELECT &&
      state->active_properties_panel == SHAULA_PROPERTIES_PANEL_NONE &&
      (can_duplicate || can_crop || can_blur || can_erase || can_spotlight ||
       can_delete);
  gboolean show_spotlight_properties =
      state->active_properties_panel == SHAULA_PROPERTIES_PANEL_SPOTLIGHT;

  if (state->selection_actions_box != NULL)
    gtk_widget_set_visible(state->selection_actions_box, show_group);
  if (state->properties_box != NULL)
    gtk_widget_set_visible(state->properties_box, show_spotlight_properties);
  if (state->spotlight_color_button != NULL) {
    GdkRGBA rgba = {state->spotlight_border_color.r,
                    state->spotlight_border_color.g,
                    state->spotlight_border_color.b,
                    state->spotlight_border_color.a};
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(state->spotlight_color_button),
                               &rgba);
  }
  if (state->spotlight_width_scale != NULL &&
      fabs(gtk_range_get_value(GTK_RANGE(state->spotlight_width_scale)) -
           state->spotlight_border_width) > 0.01)
    gtk_range_set_value(GTK_RANGE(state->spotlight_width_scale),
                        state->spotlight_border_width);
  if (state->spotlight_sharp_button != NULL)
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(state->spotlight_sharp_button),
        state->spotlight_shape == SHAULA_SPOTLIGHT_SHAPE_SHARP_RECTANGLE);
  if (state->spotlight_rounded_button != NULL)
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(state->spotlight_rounded_button),
        state->spotlight_shape == SHAULA_SPOTLIGHT_SHAPE_ROUNDED_RECTANGLE);
  if (state->duplicate_button != NULL) {
    gtk_widget_set_visible(state->duplicate_button, has_object_selection);
    gtk_widget_set_sensitive(state->duplicate_button, can_duplicate);
  }
  if (state->crop_selected_button != NULL) {
    gtk_widget_set_visible(state->crop_selected_button, can_crop);
    gtk_widget_set_sensitive(state->crop_selected_button, can_crop);
    gtk_widget_set_tooltip_text(
        state->crop_selected_button,
        state->has_region_selection ? "Crop to selected region"
                                    : "Crop to selected annotation");
  }
  if (state->blur_region_button != NULL) {
    gtk_widget_set_visible(state->blur_region_button, has_region_selection);
    gtk_widget_set_sensitive(state->blur_region_button, can_blur);
  }
  if (state->erase_region_button != NULL) {
    gtk_widget_set_visible(state->erase_region_button, has_region_selection);
    gtk_widget_set_sensitive(state->erase_region_button, can_erase);
  }
  if (state->spotlight_region_button != NULL) {
    gtk_widget_set_visible(state->spotlight_region_button,
                           has_region_selection);
    gtk_widget_set_sensitive(state->spotlight_region_button, can_spotlight);
  }
  if (state->delete_button != NULL) {
    gtk_widget_set_visible(state->delete_button, has_object_selection);
    gtk_widget_set_sensitive(state->delete_button, can_delete);
  }
}
