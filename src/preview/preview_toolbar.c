#include "preview_toolbar.h"

#include "preview_action_callbacks.h"
#include "preview_commands.h"
#include "preview_icons.h"

#include <math.h>

typedef struct {
  const char *icon_name;
  const char *label;
  ShaulaTool tool;
} ToolActionSpec;

typedef struct {
  const char *icon_name;
  const char *label;
  const char *shortcut;
  const char *tooltip;
  GCallback callback;
} MenuActionSpec;

static const ToolActionSpec secondary_tools[] = {
    {"shaula-select-symbolic", "Select", SHAULA_TOOL_SELECT},
    {"shaula-rectangle-symbolic", "Rectangle", SHAULA_TOOL_RECTANGLE},
    {"shaula-arrow-symbolic", "Arrow", SHAULA_TOOL_ARROW},
    {"shaula-line-symbolic", "Line", SHAULA_TOOL_LINE},
    {"shaula-text-symbolic", "Text", SHAULA_TOOL_TEXT},
    {"shaula-pen-symbolic", "Pen", SHAULA_TOOL_PEN},
    {"shaula-highlight-symbolic", "Highlight", SHAULA_TOOL_HIGHLIGHT},
    {"shaula-measure-symbolic", "Measure", SHAULA_TOOL_MEASURE},
    {"shaula-spotlight-symbolic", "Spotlight", SHAULA_TOOL_SPOTLIGHT},
};

static const MenuActionSpec utility_actions[] = {
    {"shaula-fit-to-screen-symbolic", "Fit to screen", NULL,
     "Fit to screen (F)", G_CALLBACK(shaula_preview_on_fit_clicked)},
    {"shaula-actual-size-symbolic", "Actual size", NULL, "Actual size",
     G_CALLBACK(shaula_preview_on_actual_clicked)},
    {"shaula-discard-symbolic", "Reset annotations", NULL,
     "Reset annotations",
     G_CALLBACK(shaula_preview_on_reset_annotations_clicked)},
};

static GtkWidget *make_muted_label(const char *text);

static char *tool_action_text(const char *label, ShaulaTool tool) {
  const char *hint = shaula_preview_tool_shortcut_hint(tool);
  if (hint == NULL || hint[0] == '\0')
    return g_strdup(label);
  return g_strdup_printf("%s (%s)", label, hint);
}

static void on_toolbar_move_drag_begin(GtkGestureDrag *gesture, double x,
                                       double y, gpointer data) {
  ShaulaPreviewState *state = data;
  if (state == NULL || state->window == NULL)
    return;

  GtkWidget *widget =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  GtkNative *native = gtk_widget_get_native(state->window);
  GdkSurface *surface = native != NULL ? gtk_native_get_surface(native) : NULL;
  GdkDevice *device = gtk_event_controller_get_current_event_device(
      GTK_EVENT_CONTROLLER(gesture));
  guint32 timestamp = gtk_event_controller_get_current_event_time(
      GTK_EVENT_CONTROLLER(gesture));
  guint button =
      gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  graphene_point_t local = GRAPHENE_POINT_INIT((float)x, (float)y);
  graphene_point_t window_point = GRAPHENE_POINT_INIT((float)x, (float)y);
  if (widget != NULL &&
      !gtk_widget_compute_point(widget, state->window, &local, &window_point))
    window_point = local;

  if (GDK_IS_TOPLEVEL(surface) && device != NULL && button > 0)
    gdk_toplevel_begin_move(GDK_TOPLEVEL(surface), device, (int)button,
                            window_point.x, window_point.y, timestamp);
}

static void add_toolbar_move_gesture(ShaulaPreviewState *state,
                                     GtkWidget *widget) {
  GtkGesture *drag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_toolbar_move_drag_begin),
                   state);
  gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drag));
}

static void install_toolbar_css(void) {
  static gboolean installed = FALSE;
  if (installed)
    return;

  GtkCssProvider *provider = gtk_css_provider_new();
  const char *css = "headerbar.shaula-preview-toolbar {"
                    "  background: alpha(@theme_bg_color, 0.98);"
                    "  color: @theme_fg_color;"
                    "  border-bottom: 1px solid @borders;"
                    "  box-shadow: 0 1px 0 alpha(@theme_fg_color, 0.06);"
                    "}"
                    "headerbar.shaula-preview-toolbar button.flat,"
                    "popover.shaula-preview-popover button.flat {"
                    "  color: @theme_fg_color;"
                    "  border: 1px solid transparent;"
                    "  border-radius: 7px;"
                    "  background: transparent;"
                    "}"
                    "headerbar.shaula-preview-toolbar button.flat:hover,"
                    "popover.shaula-preview-popover button.flat:hover,"
                    "headerbar.shaula-preview-toolbar button.shaula-stable-hover,"
                    "popover.shaula-preview-popover button.shaula-stable-hover {"
                    "  background: alpha(@theme_fg_color, 0.08);"
                    "  border-color: alpha(@theme_fg_color, 0.12);"
                    "}"
                    "headerbar.shaula-preview-toolbar button.flat:checked,"
                    "headerbar.shaula-preview-toolbar button.flat:active,"
                    "popover.shaula-preview-popover button.flat:active {"
                    "  background: alpha(@theme_fg_color, 0.14);"
                    "  border-color: alpha(@theme_fg_color, 0.20);"
                    "}"
                    "headerbar.shaula-preview-toolbar button.flat:disabled {"
                    "  opacity: 0.42;"
                    "}"
                    "headerbar.shaula-preview-toolbar button.suggested-action {"
                    "  border-radius: 7px;"
                    "}"
                    ".shaula-shortcut-badge {"
                    "  min-width: 0;"
                    "  min-height: 0;"
                    "  padding: 0;"
                    "  margin: 0;"
                    "  border: none;"
                    "  background: transparent;"
                    "  color: @theme_fg_color;"
                    "  font-size: 9px;"
                    "  font-weight: 400;"
                    "}"
                    ".shaula-menu-shortcut {"
                    "  min-width: 0;"
                    "  min-height: 0;"
                    "  padding: 2px 5px;"
                    "  border: 1px solid alpha(@theme_fg_color, 0.16);"
                    "  border-radius: 4px;"
                    "  background: alpha(@theme_fg_color, 0.06);"
                    "  color: alpha(@theme_fg_color, 0.72);"
                    "  font-family: monospace;"
                    "  font-size: 0.78em;"
                    "}"
                    "popover.shaula-preview-popover contents {"
                    "  background: alpha(@theme_bg_color, 0.98);"
                    "  color: @theme_fg_color;"
                    "  border: 1px solid @borders;"
                    "  border-radius: 8px;"
                    "}";
  gtk_css_provider_load_from_data(provider, css, -1);
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
  installed = TRUE;
}

static void on_stable_hover_enter(GtkEventControllerMotion *controller,
                                  double x, double y, gpointer data) {
  (void)x;
  (void)y;
  (void)data;
  GtkWidget *widget =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
  if (widget != NULL)
    gtk_widget_add_css_class(widget, "shaula-stable-hover");
}

static void on_stable_hover_leave(GtkEventControllerMotion *controller,
                                  gpointer data) {
  (void)data;
  GtkWidget *widget =
      gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
  if (widget != NULL)
    gtk_widget_remove_css_class(widget, "shaula-stable-hover");
}

static void install_stable_hover(GtkWidget *widget) {
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "enter", G_CALLBACK(on_stable_hover_enter), NULL);
  g_signal_connect(motion, "leave", G_CALLBACK(on_stable_hover_leave), NULL);
  gtk_widget_add_controller(widget, motion);
}

static GtkWidget *make_toolbar_button(ShaulaPreviewState *state,
                                      const char *icon_name,
                                      const char *tooltip, GCallback callback) {
  GtkWidget *button = gtk_button_new();
  gtk_button_set_child(GTK_BUTTON(button),
                       shaula_preview_make_toolbar_icon(state, icon_name));
  gtk_widget_set_tooltip_text(button, tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 32, 32);
  install_stable_hover(button);
  g_signal_connect(button, "clicked", callback, state);
  return button;
}

static GtkWidget *make_done_button(ShaulaPreviewState *state) {
  const char *tooltip =
      state->copy_on_accept ? "Save and copy (Enter)" : "Save (Enter)";
  GtkWidget *button =
      make_toolbar_button(state, "shaula-done-symbolic", tooltip,
                          G_CALLBACK(shaula_preview_on_done_clicked));
  gtk_widget_remove_css_class(button, "flat");
  gtk_widget_add_css_class(button, "suggested-action");
  return button;
}

static GtkWidget *make_close_button(ShaulaPreviewState *state) {
  return make_toolbar_button(state, "shaula-close-symbolic", "Close",
                             G_CALLBACK(shaula_preview_on_close_clicked));
}

static GtkWidget *make_icon_with_badge(ShaulaPreviewState *state,
                                       const char *icon_name,
                                       const char *badge_text) {
  GtkWidget *overlay = gtk_overlay_new();
  GtkWidget *icon = shaula_preview_make_toolbar_icon(state, icon_name);
  gtk_widget_set_size_request(overlay, 22, 22);
  gtk_widget_set_halign(overlay, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(overlay, GTK_ALIGN_CENTER);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), icon);

  if (badge_text != NULL && badge_text[0] != '\0') {
    GtkWidget *badge = gtk_label_new(badge_text);
    gtk_widget_add_css_class(badge, "shaula-shortcut-badge");
    gtk_widget_set_halign(badge, GTK_ALIGN_END);
    gtk_widget_set_valign(badge, GTK_ALIGN_END);
    gtk_widget_set_margin_end(badge, -6);
    gtk_widget_set_margin_bottom(badge, -2);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), badge);
  }

  return overlay;
}

static GtkWidget *make_tool_toggle(ShaulaPreviewState *state,
                                   const char *icon_name, const char *label,
                                   ShaulaTool tool) {
  const char *badge = shaula_preview_tool_shortcut_badge(tool);
  char *tooltip = tool_action_text(label, tool);
  GtkWidget *button = gtk_toggle_button_new();
  gtk_button_set_child(GTK_BUTTON(button),
                       make_icon_with_badge(state, icon_name, badge));
  gtk_widget_set_tooltip_text(button, tooltip);
  g_free(tooltip);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 32, 32);
  install_stable_hover(button);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
                               state->active_tool == tool);
  g_object_set_data(G_OBJECT(button), "tool", GINT_TO_POINTER(tool));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(shaula_preview_on_tool_clicked), state);
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
  if (spec->shortcut != NULL && spec->shortcut[0] != '\0') {
    GtkWidget *shortcut = gtk_label_new(spec->shortcut);
    gtk_widget_add_css_class(shortcut, "shaula-menu-shortcut");
    gtk_widget_set_halign(shortcut, GTK_ALIGN_START);
    gtk_widget_set_valign(shortcut, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), shortcut);
  }
  gtk_box_append(GTK_BOX(row), icon);
  gtk_box_append(GTK_BOX(row), label);
  gtk_button_set_child(GTK_BUTTON(button), row);
  gtk_widget_set_tooltip_text(button, spec->tooltip);
  gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                 GTK_ACCESSIBLE_PROPERTY_LABEL, spec->tooltip,
                                 -1);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
  install_stable_hover(button);
  g_signal_connect(button, "clicked", spec->callback, state);
  return button;
}

static GtkWidget *make_menu_tool_row(ShaulaPreviewState *state,
                                     const ToolActionSpec *spec) {
  char *text = tool_action_text(spec->label, spec->tool);
  GtkWidget *button = gtk_button_new();
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *icon = shaula_preview_make_toolbar_icon(state, spec->icon_name);
  GtkWidget *label = gtk_label_new(text);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_box_append(GTK_BOX(row), icon);
  gtk_box_append(GTK_BOX(row), label);
  gtk_button_set_child(GTK_BUTTON(button), row);
  gtk_widget_set_tooltip_text(button, text);
  g_free(text);
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_halign(button, GTK_ALIGN_FILL);
  install_stable_hover(button);
  g_object_set_data(G_OBJECT(button), "tool", GINT_TO_POINTER(spec->tool));
  g_signal_connect(button, "clicked",
                   G_CALLBACK(shaula_preview_on_tool_clicked), state);
  return button;
}

static int visible_secondary_count_for_width(ShaulaPreviewState *state,
                                             int width) {
  int extra = width - PREVIEW_TOOLBAR_BASE_VISIBLE_W;
  if (extra <= 0)
    return 0;
  int count = extra / PREVIEW_TOOLBAR_REVEAL_STEP_W;
  return MIN(count, state->toolbar_secondary_count +
                        state->toolbar_utility_action_count);
}

static gboolean preview_init_debug_enabled(void) {
  const char *value = g_getenv("SHAULA_DEBUG_PREVIEW_INIT");
  return value != NULL && value[0] != '\0' && g_strcmp0(value, "0") != 0;
}

static void debug_preview_init(const char *stage, int value) {
  if (!preview_init_debug_enabled())
    return;
  g_printerr("[DEBUG-preview-init] %s=%d\n", stage, value);
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

  state->paste_system_button = NULL;
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

  gboolean has_hidden_utilities = FALSE;
  for (int i = 0; i < (int)G_N_ELEMENTS(utility_actions); i++) {
    gtk_box_append(GTK_BOX(state->more_menu_box),
                   make_menu_action_row(state, &utility_actions[i]));
    has_hidden_utilities = TRUE;
  }
  if (has_hidden_utilities)
    append_separator(state->more_menu_box);

  const MenuActionSpec paste_action = {
      "shaula-paste-symbolic", "Paste from clipboard", "Ctrl+Shift+V",
      "Paste clipboard text or image near canvas center; shortcut Ctrl+Shift+V",
      G_CALLBACK(shaula_preview_on_paste_system_clipboard_clicked)};
  state->paste_system_button = make_menu_action_row(state, &paste_action);
  gtk_box_append(GTK_BOX(state->more_menu_box), state->paste_system_button);
  shaula_preview_toolbar_update_system_paste_state(state);

  const MenuActionSpec actions[] = {
      {"shaula-save-symbolic", "Save As", NULL, "Save As (Ctrl+Shift+S)",
       G_CALLBACK(shaula_preview_on_save_as_clicked)},
      {"shaula-more-symbolic", "Open preview directory", NULL,
       "Open preview directory",
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
  if (width < PREVIEW_TOOLBAR_BASE_VISIBLE_W)
    width = PREVIEW_TOOLBAR_BASE_VISIBLE_W;
  int visible_count = visible_secondary_count_for_width(state, width);
  if (visible_count == state->toolbar_overflow_visible_count)
    return;

  for (int i = 0; i < state->toolbar_secondary_count; i++)
    gtk_widget_set_visible(state->toolbar_secondary[i], i < visible_count);

  rebuild_more_menu(state, visible_count);
  state->toolbar_overflow_visible_count = visible_count;
  debug_preview_init("overflow_visible_count", visible_count);
}

static void update_toolbar_overflow_from_layout(GtkWidget *widget,
                                                ShaulaPreviewState *state) {
  int available_width = PREVIEW_TOOLBAR_STABLE_ACTIONS_W;
  if (state->toolbar_actions != NULL && state->toolbar_metadata != NULL) {
    graphene_rect_t toolbar_bounds;
    graphene_rect_t metadata_bounds;
    if (gtk_widget_compute_bounds(state->toolbar_actions, widget,
                                  &toolbar_bounds) &&
        gtk_widget_compute_bounds(state->toolbar_metadata, widget,
                                  &metadata_bounds)) {
      int measured_width =
          (int)floorf(metadata_bounds.origin.x - toolbar_bounds.origin.x);
      if (measured_width >= PREVIEW_TOOLBAR_BASE_VISIBLE_W)
        available_width = measured_width;
    }
  }
  if (available_width >= PREVIEW_TOOLBAR_BASE_VISIBLE_W)
    update_toolbar_overflow(state, available_width);
}

static gboolean on_topbar_first_layout_tick(GtkWidget *widget,
                                            GdkFrameClock *clock,
                                            gpointer data) {
  (void)clock;
  update_toolbar_overflow_from_layout(widget, data);
  return G_SOURCE_REMOVE;
}

static void on_topbar_width_changed(GtkWidget *widget, GParamSpec *pspec,
                                    gpointer data) {
  (void)pspec;
  update_toolbar_overflow_from_layout(widget, data);
}

static GtkWidget *make_more_button(ShaulaPreviewState *state) {
  GtkWidget *button = gtk_menu_button_new();
  GtkWidget *icon =
      shaula_preview_make_toolbar_icon(state, "shaula-more-symbolic");
  GtkWidget *popover = gtk_popover_new();
  gtk_widget_add_css_class(popover, "shaula-preview-popover");
  GtkWidget *menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  gtk_menu_button_set_child(GTK_MENU_BUTTON(button), icon);
  gtk_widget_set_tooltip_text(button, "More");
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(button, 32, 32);
  install_stable_hover(button);
  gtk_widget_set_margin_top(menu_box, 6);
  gtk_widget_set_margin_bottom(menu_box, 6);
  gtk_widget_set_margin_start(menu_box, 6);
  gtk_widget_set_margin_end(menu_box, 6);
  gtk_widget_set_size_request(menu_box, 300, -1);
  gtk_popover_set_child(GTK_POPOVER(popover), menu_box);
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);

  state->more_button = button;
  state->more_popover = popover;
  state->more_menu_box = menu_box;
  return button;
}

static void append_secondary_toolbar_button(ShaulaPreviewState *state,
                                            GtkWidget *actions, int index) {
  GtkWidget *button = make_tool_toggle(
      state, secondary_tools[index].icon_name, secondary_tools[index].label,
      secondary_tools[index].tool);
  gtk_widget_set_visible(button, FALSE);
  gtk_box_append(GTK_BOX(actions), button);
  state->toolbar_secondary[state->toolbar_secondary_count] = button;
  state->toolbar_secondary_tools[state->toolbar_secondary_count] =
      secondary_tools[index].tool;
  state->toolbar_secondary_count++;
}

static GtkWidget *make_muted_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_widget_add_css_class(label, "dim-label");
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  return label;
}

static void apply_fixed_code_width(GtkWidget *label, int chars, int px_width,
                                   float xalign) {
  gtk_label_set_width_chars(GTK_LABEL(label), chars);
  gtk_label_set_max_width_chars(GTK_LABEL(label), chars);
  gtk_label_set_xalign(GTK_LABEL(label), xalign);
  gtk_widget_set_size_request(label, px_width, -1);

  PangoAttrList *attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_family_new("monospace"));
  gtk_label_set_attributes(GTK_LABEL(label), attrs);
  pango_attr_list_unref(attrs);
}

static GtkWidget *make_normal_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  return label;
}

static void draw_swatch(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                        gpointer data) {
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
  ShaulaColor color =
      state->hover_color_valid ? state->hover_color : state->current_color;
  cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
  cairo_fill_preserve(cr);

  /* Border matches the SVG icon foreground resolved from the GTK theme. */
  GdkRGBA fg;
  gtk_style_context_get_color(gtk_widget_get_style_context(GTK_WIDGET(area)),
                              &fg);
  cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.55);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
}

static GtkWidget *build_tool_group(ShaulaPreviewState *state) {
  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(actions, PREVIEW_TOOLBAR_FULL_VISIBLE_W, -1);
  state->toolbar_secondary_count = 0;
  state->toolbar_utility_action_count = 0;
  state->toolbar_overflow_visible_count = -1;

  gtk_box_append(
      GTK_BOX(actions),
      make_toolbar_button(state, "shaula-copy-symbolic",
                          "Copy image (Ctrl+C when no annotations selected)",
                          G_CALLBACK(shaula_preview_on_copy_clicked)));
  gtk_box_append(
      GTK_BOX(actions),
      make_toolbar_button(state, "shaula-save-symbolic", "Save (Ctrl+S)",
                          G_CALLBACK(shaula_preview_on_save_clicked)));
  state->undo_button =
      make_toolbar_button(state, "shaula-undo-symbolic", "Undo (Ctrl+Z)",
                          G_CALLBACK(shaula_preview_on_undo_clicked));
  state->redo_button =
      make_toolbar_button(state, "shaula-redo-symbolic", "Redo (Ctrl+Shift+Z)",
                          G_CALLBACK(shaula_preview_on_redo_clicked));
  gtk_widget_set_sensitive(state->undo_button, FALSE);
  gtk_widget_set_sensitive(state->redo_button, FALSE);
  gtk_box_append(GTK_BOX(actions), state->undo_button);
  gtk_box_append(GTK_BOX(actions), state->redo_button);

  gtk_box_append(GTK_BOX(actions),
                 make_tool_toggle(state, "shaula-hand-symbolic", "Pan",
                                  SHAULA_TOOL_HAND));
  gtk_box_append(GTK_BOX(actions),
                 make_tool_toggle(state, "shaula-crop-symbolic", "Crop",
                                  SHAULA_TOOL_CROP));

  for (int i = 0; i < (int)G_N_ELEMENTS(secondary_tools); i++)
    append_secondary_toolbar_button(state, actions, i);

  gtk_box_append(GTK_BOX(actions),
                 make_tool_toggle(state, "shaula-eraser-symbolic", "Eraser",
                                  SHAULA_TOOL_ERASER));

  gtk_box_append(GTK_BOX(actions), make_more_button(state));

  return actions;
}

static GtkWidget *build_metadata_group(ShaulaPreviewState *state) {
  GtkWidget *metadata = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_valign(metadata, GTK_ALIGN_CENTER);

  /* Color readout: swatch + hex on top, hint below. */
  GtkWidget *color_stack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_valign(color_stack, GTK_ALIGN_CENTER);

  GtkWidget *color_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_halign(color_row, GTK_ALIGN_CENTER);

  GtkWidget *swatch = gtk_drawing_area_new();
  state->color_swatch = swatch;
  gtk_widget_set_size_request(swatch, 16, 16);
  gtk_widget_set_valign(swatch, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(swatch, "Click to use sampled color");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch), draw_swatch, state,
                                 NULL);
  GtkGesture *pick_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(pick_click),
                                GDK_BUTTON_PRIMARY);
  g_signal_connect(pick_click, "pressed",
                   G_CALLBACK(shaula_preview_on_use_hover_color_clicked),
                   state);
  gtk_widget_add_controller(swatch, GTK_EVENT_CONTROLLER(pick_click));
  gtk_box_append(GTK_BOX(color_row), swatch);

  char hex[8];
  shaula_color_to_hex(state->current_color, hex);
  state->color_hex_label =
      make_normal_label(state->hover_color_valid ? state->hover_hex : hex);
  apply_fixed_code_width(state->color_hex_label, 7, 72, 0.0f);
  gtk_widget_set_tooltip_text(state->color_hex_label,
                              "Tab to copy color; click swatch to use");
  gtk_box_append(GTK_BOX(color_row), state->color_hex_label);

  gtk_box_append(GTK_BOX(color_stack), color_row);

  GtkWidget *hint = make_muted_label("[Tab copy]");
  PangoAttrList *hint_attrs = pango_attr_list_new();
  pango_attr_list_insert(hint_attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
  gtk_label_set_attributes(GTK_LABEL(hint), hint_attrs);
  pango_attr_list_unref(hint_attrs);
  gtk_box_append(GTK_BOX(color_stack), hint);

  gtk_box_append(GTK_BOX(metadata), color_stack);

  /* Dimensions + zoom stacked in a single compact column. */
  GtkWidget *info_stack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_valign(info_stack, GTK_ALIGN_CENTER);

  if (state->document.image != NULL) {
    char size_buf[32];
    snprintf(size_buf, sizeof(size_buf), "%dw\xc3\x97%dh",
             shaula_preview_image_width(state),
             shaula_preview_image_height(state));
    state->dimensions_label = make_muted_label(size_buf);
    gtk_label_set_xalign(GTK_LABEL(state->dimensions_label), 1.0f);
    gtk_label_set_width_chars(GTK_LABEL(state->dimensions_label), 9);
    gtk_label_set_max_width_chars(GTK_LABEL(state->dimensions_label), 12);
    gtk_widget_set_size_request(state->dimensions_label, 72, -1);
    PangoAttrList *dim_attrs = pango_attr_list_new();
    pango_attr_list_insert(dim_attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
    pango_attr_list_insert(dim_attrs, pango_attr_family_new("monospace"));
    gtk_label_set_attributes(GTK_LABEL(state->dimensions_label), dim_attrs);
    pango_attr_list_unref(dim_attrs);
    gtk_box_append(GTK_BOX(info_stack), state->dimensions_label);
  }

  state->zoom_label = make_muted_label("Zoom 100%");
  gtk_label_set_xalign(GTK_LABEL(state->zoom_label), 1.0f);
  gtk_label_set_width_chars(GTK_LABEL(state->zoom_label), 9);
  gtk_label_set_max_width_chars(GTK_LABEL(state->zoom_label), 9);
  gtk_widget_set_size_request(state->zoom_label, 72, -1);
  {
    PangoAttrList *zoom_attrs = pango_attr_list_new();
    pango_attr_list_insert(zoom_attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
    pango_attr_list_insert(zoom_attrs, pango_attr_family_new("monospace"));
    gtk_label_set_attributes(GTK_LABEL(state->zoom_label), zoom_attrs);
    pango_attr_list_unref(zoom_attrs);
  }
  gtk_box_append(GTK_BOX(info_stack), state->zoom_label);

  gtk_box_append(GTK_BOX(metadata), info_stack);
  gtk_widget_set_margin_end(metadata, 8);

  return metadata;
}

static GtkWidget *build_right_group(ShaulaPreviewState *state) {
  GtkWidget *group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_valign(group, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(group), build_metadata_group(state));
  gtk_box_append(GTK_BOX(group), make_done_button(state));
  gtk_box_append(GTK_BOX(group), make_close_button(state));
  return group;
}

GtkWidget *shaula_preview_toolbar_build(ShaulaPreviewState *state) {
  install_toolbar_css();
  GtkWidget *bar = gtk_header_bar_new();
  gtk_widget_add_css_class(bar, "shaula-preview-toolbar");
  gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(bar), FALSE);
  gtk_widget_set_size_request(bar, -1, PREVIEW_HEADER_ESTIMATED_H);

  GtkWidget *toolbar = build_tool_group(state);
  state->toolbar_actions = toolbar;
  gtk_widget_set_halign(toolbar, GTK_ALIGN_START);
  gtk_widget_set_valign(toolbar, GTK_ALIGN_CENTER);

  GtkWidget *right_group = build_right_group(state);
  state->toolbar_metadata = right_group;
  gtk_widget_set_halign(right_group, GTK_ALIGN_END);
  gtk_widget_set_valign(right_group, GTK_ALIGN_CENTER);

  GtkWidget *title_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(title_spacer, TRUE);
  add_toolbar_move_gesture(state, title_spacer);
  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(bar), title_spacer);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), toolbar);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), right_group);
  update_toolbar_overflow(state, PREVIEW_TOOLBAR_FULL_VISIBLE_W);
  debug_preview_init("toolbar_built", 1);
  g_signal_connect(bar, "notify::width", G_CALLBACK(on_topbar_width_changed),
                   state);
  gtk_widget_add_tick_callback(bar, on_topbar_first_layout_tick, state, NULL);

  return bar;
}

void shaula_preview_toolbar_prepare_initial_layout(ShaulaPreviewState *state,
                                                   int window_width) {
  if (state == NULL || state->toolbar_actions == NULL)
    return;

  const int metadata_and_chrome_reserve = 230;
  int available_width = window_width - metadata_and_chrome_reserve;
  available_width = MAX(PREVIEW_TOOLBAR_BASE_VISIBLE_W, available_width);
  available_width = MIN(PREVIEW_TOOLBAR_FULL_VISIBLE_W, available_width);
  gtk_widget_set_size_request(state->toolbar_actions, available_width, -1);
  update_toolbar_overflow(state, available_width);
  debug_preview_init("initial_overflow_width", available_width);
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

void shaula_preview_toolbar_update_system_paste_state(
    ShaulaPreviewState *state) {
  if (state == NULL || state->paste_system_button == NULL)
    return;
  gtk_widget_set_sensitive(
      state->paste_system_button,
      shaula_preview_command_available(
          state, SHAULA_PREVIEW_COMMAND_PASTE_SYSTEM_CLIPBOARD));
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
  guint selected_count = shaula_annotation_editor_selected_count(state);
  gboolean has_object_selection = selected_count > 0;
  gboolean has_region_selection = state->has_region_selection;
  gboolean show_group =
      state->active_tool == SHAULA_TOOL_SELECT &&
      state->properties_hud.active_panel == SHAULA_PROPERTIES_PANEL_NONE &&
      (can_duplicate || can_crop || can_blur || can_erase || can_spotlight ||
       can_delete);

  if (state->selection_actions_box != NULL)
    gtk_widget_set_visible(state->selection_actions_box, show_group);
  shaula_properties_hud_sync_widgets(state);

  if (state->duplicate_button != NULL) {
    const char *duplicate_label =
        selected_count > 1 ? "Duplicate selected annotations (Ctrl+D)"
                           : "Duplicate selected annotation (Ctrl+D)";
    gtk_widget_set_visible(state->duplicate_button, has_object_selection);
    gtk_widget_set_sensitive(state->duplicate_button, can_duplicate);
    gtk_widget_set_tooltip_text(state->duplicate_button, duplicate_label);
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(state->duplicate_button),
        GTK_ACCESSIBLE_PROPERTY_LABEL, duplicate_label, -1);
  }
  if (state->crop_selected_button != NULL) {
    gtk_widget_set_visible(state->crop_selected_button, can_crop);
    gtk_widget_set_sensitive(state->crop_selected_button, can_crop);
    gtk_widget_set_tooltip_text(state->crop_selected_button,
                                state->has_region_selection
                                    ? "Crop to selected region"
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
    gtk_widget_set_tooltip_text(
        state->delete_button,
        selected_count > 1 ? "Delete selected annotations (Delete)"
                           : "Delete selected annotation (Delete)");
  }
}
