#include <gtk/gtk.h>

#include "shortcuts/shortcuts.h"

typedef struct {
  const char *label;
  const char *icon;
  const char *const *arguments;
  guint shortcut_index;
} LauncherAction;

typedef struct {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *status;
  const char *shaula_bin;
  const LauncherAction *pending_action;
  ShaulaShortcutStatus shortcuts;
} LauncherState;

static LauncherState state;

static const char *const QUICK_ARGUMENTS[] = {"capture", "quick", "--json",
                                              NULL};
static const char *const AREA_ARGUMENTS[] = {"capture", "area", "--json",
                                             NULL};
static const char *const FULLSCREEN_ARGUMENTS[] = {
    "capture", "fullscreen", "--json", "--save", NULL};
static const char *const ALL_SCREENS_ARGUMENTS[] = {
    "capture", "all-screens", "--json", "--save", NULL};
static const char *const SETTINGS_ARGUMENTS[] = {"settings", NULL};
static const char *const DIRECTORY_ARGUMENTS[] = {
    "directory", "screenshots", "--open", NULL};
static const char *const REPORT_ARGUMENTS[] = {
    "https://github.com/fgonzalezurriola/shaula/issues", NULL};

static const LauncherAction CAPTURE_ACTIONS[] = {
    {"Quick Capture", "camera-photo-symbolic", QUICK_ARGUMENTS, 0},
    {"Capture Area", "selection-mode-symbolic", AREA_ARGUMENTS, 1},
    {"Capture Fullscreen", "video-display-symbolic", FULLSCREEN_ARGUMENTS, 2},
    {"Capture All Screens", "view-grid-symbolic", ALL_SCREENS_ARGUMENTS, 3},
};

static const LauncherAction UTILITY_ACTIONS[] = {
    {"Settings", "emblem-system-symbolic", SETTINGS_ARGUMENTS, G_MAXUINT},
    {"Open Screenshots Folder", "folder-open-symbolic", DIRECTORY_ARGUMENTS,
     G_MAXUINT},
    {"Report a Problem", "dialog-warning-symbolic", REPORT_ARGUMENTS,
     G_MAXUINT},
};

/* Launches a selected command without a shell so menu labels and external
 * values can never be interpreted as command text. */
static gboolean spawn_action(const LauncherAction *action, GError **error) {
  g_autoptr(GPtrArray) argv = g_ptr_array_new();
  gboolean report = action->arguments == REPORT_ARGUMENTS;
  g_ptr_array_add(argv, (gpointer)(report ? "xdg-open" : state.shaula_bin));
  for (guint i = 0; action->arguments[i] != NULL; i++)
    g_ptr_array_add(argv, (gpointer)action->arguments[i]);
  g_ptr_array_add(argv, NULL);
  return g_spawn_async(NULL, (char **)argv->pdata, NULL, G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, error);
}

/* Launches only after the menu surface has been unmapped so capture backends
 * cannot include the composited close frame in the resulting screenshot. */
static gboolean launch_pending_action(gpointer data) {
  (void)data;
  const LauncherAction *action = state.pending_action;
  state.pending_action = NULL;
  g_autoptr(GError) error = NULL;
  if (!spawn_action(action, &error)) {
    gtk_label_set_text(GTK_LABEL(state.status), error->message);
    gtk_widget_set_visible(state.status, TRUE);
    gtk_widget_set_visible(state.window, TRUE);
    gtk_window_present(GTK_WINDOW(state.window));
    return G_SOURCE_REMOVE;
  }
  g_application_quit(G_APPLICATION(state.app));
  return G_SOURCE_REMOVE;
}

static void on_action_clicked(GtkButton *button, gpointer data) {
  (void)button;
  if (state.pending_action != NULL)
    return;
  state.pending_action = data;
  gtk_widget_set_visible(state.window, FALSE);
  g_timeout_add(150U, launch_pending_action, NULL);
}

static GtkWidget *make_action_row(const LauncherAction *action) {
  GtkWidget *button = gtk_button_new();
  gtk_widget_add_css_class(button, "launcher-action");
  gtk_widget_set_hexpand(button, TRUE);

  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *icon = gtk_image_new_from_icon_name(action->icon);
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 18);
  gtk_widget_add_css_class(icon, "launcher-action-icon");
  gtk_box_append(GTK_BOX(row), icon);

  GtkWidget *label = gtk_label_new(action->label);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_box_append(GTK_BOX(row), label);

  if (state.shortcuts.state == SHAULA_SHORTCUT_STATE_ACTIVE &&
      action->shortcut_index < G_N_ELEMENTS(state.shortcuts.triggers)) {
    const char *trigger = state.shortcuts.triggers[action->shortcut_index];
    if (trigger != NULL && trigger[0] != '\0') {
      GtkWidget *shortcut = gtk_label_new(trigger);
      gtk_widget_add_css_class(shortcut, "launcher-shortcut");
      gtk_box_append(GTK_BOX(row), shortcut);
    }
  }

  gtk_button_set_child(GTK_BUTTON(button), row);
  g_signal_connect(button, "clicked", G_CALLBACK(on_action_clicked),
                   (gpointer)action);
  return button;
}

static GtkWidget *make_separator(void) {
  GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(separator, 6);
  gtk_widget_set_margin_bottom(separator, 6);
  return separator;
}

static void on_activate(GtkApplication *app, gpointer data) {
  (void)data;
  if (state.window != NULL) {
    gtk_window_present(GTK_WINDOW(state.window));
    return;
  }

  (void)shaula_shortcuts_query(&state.shortcuts, NULL);
  state.window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(state.window), "Shaula");
  gtk_window_set_default_size(GTK_WINDOW(state.window), 390, -1);
  gtk_window_set_resizable(GTK_WINDOW(state.window), FALSE);
  gtk_window_set_icon_name(GTK_WINDOW(state.window), "shaula");

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(root, "launcher-root");
  gtk_widget_set_margin_start(root, 10);
  gtk_widget_set_margin_end(root, 10);
  gtk_widget_set_margin_top(root, 10);
  gtk_widget_set_margin_bottom(root, 10);

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_add_css_class(header, "launcher-header");
  GtkWidget *app_icon = gtk_image_new_from_icon_name("shaula");
  gtk_image_set_pixel_size(GTK_IMAGE(app_icon), 32);
  gtk_box_append(GTK_BOX(header), app_icon);
  GtkWidget *title = gtk_label_new("Shaula");
  gtk_widget_add_css_class(title, "launcher-title");
  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(root), header);
  gtk_box_append(GTK_BOX(root), make_separator());

  for (guint i = 0; i < G_N_ELEMENTS(CAPTURE_ACTIONS); i++)
    gtk_box_append(GTK_BOX(root), make_action_row(&CAPTURE_ACTIONS[i]));
  gtk_box_append(GTK_BOX(root), make_separator());
  for (guint i = 0; i < G_N_ELEMENTS(UTILITY_ACTIONS); i++)
    gtk_box_append(GTK_BOX(root), make_action_row(&UTILITY_ACTIONS[i]));

  state.status = gtk_label_new(NULL);
  gtk_label_set_wrap(GTK_LABEL(state.status), TRUE);
  gtk_label_set_xalign(GTK_LABEL(state.status), 0.0f);
  gtk_widget_add_css_class(state.status, "launcher-error");
  gtk_widget_set_margin_top(state.status, 8);
  gtk_widget_set_visible(state.status, FALSE);
  gtk_box_append(GTK_BOX(root), state.status);

  gtk_window_set_child(GTK_WINDOW(state.window), root);
  gtk_window_present(GTK_WINDOW(state.window));
}

static void install_css(GtkApplication *app, gpointer data) {
  (void)app;
  (void)data;
  GtkSettings *settings = gtk_settings_get_default();
  if (settings != NULL)
    g_object_set(settings, "gtk-enable-animations", FALSE, NULL);
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
      provider,
      ".launcher-root { background: #171717; color: #f4f1ed; }"
      ".launcher-header { min-height: 42px; padding: 2px 8px; }"
      ".launcher-title { color: #ff7a1a; font-size: 22px; font-weight: 800; }"
      ".launcher-action { min-height: 42px; padding: 0 10px; border: 0; "
      "border-radius: 0; background: transparent; color: #f4f1ed; "
      "box-shadow: none; }"
      ".launcher-action:hover { background: #2b2b2b; "
      "box-shadow: inset 3px 0 #ff7a1a; }"
      ".launcher-action:active { background: #383838; }"
      ".launcher-action-icon { color: #d7d7d7; }"
      ".launcher-action:hover .launcher-action-icon { color: #ff7a1a; }"
      ".launcher-shortcut { color: #9c9c9c; font-size: 12px; }"
      ".launcher-error { color: #ff7676; font-size: 12px; padding: 0 8px; }"
      "separator { background: #3a3a3a; min-height: 1px; }");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

int main(int argc, char **argv) {
  shaula_shortcut_status_init(&state.shortcuts);
  state.shaula_bin = argc >= 2 && argv[1][0] != '\0' ? argv[1] : "shaula";
  state.app = gtk_application_new("dev.shaula.launcher",
                                  G_APPLICATION_DEFAULT_FLAGS);
  if (state.app == NULL)
    return 45;
  g_signal_connect(state.app, "startup", G_CALLBACK(install_css), NULL);
  g_signal_connect(state.app, "activate", G_CALLBACK(on_activate), NULL);
  int rc = g_application_run(G_APPLICATION(state.app), 0, NULL);
  g_object_unref(state.app);
  shaula_shortcut_status_clear(&state.shortcuts);
  return rc > 255 ? 255 : rc;
}
