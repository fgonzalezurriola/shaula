#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>
#include "config/noctalia_managed.h"
#include "settings_config.h"
#include "settings_niri.h"
#include "settings_process.h"
#include "settings_shortcuts.h"
#include "shortcuts/shortcuts.h"

typedef struct {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *content;
  GtkWidget *error_box;
  GtkWidget *form_box;
  GtkWidget *form_scroller;
  GtkWidget *status_label;
  GtkWidget *path_label;
  GtkDropDown *region_combo;
  GtkDropDown *window_combo;
  GtkDropDown *size_combo;
  GtkDropDown *position_combo;
  GtkSwitch *focused_switch;
  GtkSwitch *close_preview_on_save_switch;
  GtkSwitch *quick_open_switch;
  GtkSwitch *quick_copy_switch;
  GtkSwitch *quick_save_switch;
  GtkSwitch *area_open_switch;
  GtkSwitch *area_copy_switch;
  GtkSwitch *area_save_switch;
  GtkSwitch *fullscreen_open_switch;
  GtkSwitch *fullscreen_copy_switch;
  GtkSwitch *fullscreen_save_switch;
  GtkSwitch *all_screens_open_switch;
  GtkSwitch *all_screens_copy_switch;
  GtkSwitch *all_screens_save_switch;
  GtkEntry *save_folder_entry;
  GtkSwitch *notifications_success_switch;
  GtkSwitch *notifications_errors_switch;
  GtkSwitch *notifications_thumbnails_switch;
  GtkWidget *apply_button;
  GtkWidget *open_button;
  GtkWidget *reset_button;
  char *shaula_bin;
  char *config_path;
  gboolean config_exists;
  gboolean config_invalid;
  ShaulaSettingsConfig config;
  ShaulaShortcutStatus shortcuts;
  gboolean setup_mode;
  GtkWidget *shortcuts_state_label;
  GtkWidget *shortcuts_backend_label;
  GtkWidget *shortcuts_installation_label;
  GtkWidget *shortcuts_detail_label;
  GtkWidget *shortcuts_enable_button;
  GtkWidget *shortcuts_disable_button;
  GtkWidget *shortcuts_repair_button;
  GtkWidget *shortcuts_recheck_button;
  GtkWidget *shortcuts_menu_button;
  GtkWidget *noctalia_status_label;
  GtkWidget *noctalia_install_button;
  GtkWidget *noctalia_remove_button;
} AppState;

static AppState state;
static const int SETTINGS_CONTROL_W = 132;
static const int SETTINGS_SWITCH_W = 46;
static const int SETTINGS_SWITCH_H = 26;

static void set_status(const char *text, gboolean is_error) {
  gtk_label_set_text(GTK_LABEL(state.status_label), text);
  if (is_error)
    gtk_widget_add_css_class(state.status_label, "error");
  else
    gtk_widget_remove_css_class(state.status_label, "error");
}

static void read_controls(ShaulaSettingsConfig *config) {
  config->region_mode =
      (RegionMode)gtk_drop_down_get_selected(state.region_combo);
  config->window_mode =
      (WindowMode)gtk_drop_down_get_selected(state.window_combo);
  config->focused = gtk_switch_get_active(state.focused_switch);
  config->close_preview_on_save =
      gtk_switch_get_active(state.close_preview_on_save_switch);
  SizePreset size = (SizePreset)gtk_drop_down_get_selected(state.size_combo);
  shaula_settings_apply_size_preset(config, size);
  PositionPreset position =
      (PositionPreset)gtk_drop_down_get_selected(state.position_combo);
  shaula_settings_apply_position_preset(config, position);
  config->quick_skip_preview = !gtk_switch_get_active(state.quick_open_switch);
  config->quick_copy = gtk_switch_get_active(state.quick_copy_switch);
  config->quick_save = gtk_switch_get_active(state.quick_save_switch);
  config->area_skip_preview = !gtk_switch_get_active(state.area_open_switch);
  config->area_copy = gtk_switch_get_active(state.area_copy_switch);
  config->area_save = gtk_switch_get_active(state.area_save_switch);
  config->fullscreen_skip_preview = !gtk_switch_get_active(state.fullscreen_open_switch);
  config->fullscreen_copy = gtk_switch_get_active(state.fullscreen_copy_switch);
  config->fullscreen_save = gtk_switch_get_active(state.fullscreen_save_switch);
  config->all_screens_skip_preview = !gtk_switch_get_active(state.all_screens_open_switch);
  config->all_screens_copy =
      gtk_switch_get_active(state.all_screens_copy_switch);
  config->all_screens_save =
      gtk_switch_get_active(state.all_screens_save_switch);
  g_free(config->save_folder);
  config->save_folder =
      g_strdup(gtk_editable_get_text(GTK_EDITABLE(state.save_folder_entry)));
  config->notifications_success =
      gtk_switch_get_active(state.notifications_success_switch);
  config->notifications_errors =
      gtk_switch_get_active(state.notifications_errors_switch);
  config->notifications_thumbnails =
      gtk_switch_get_active(state.notifications_thumbnails_switch);
}

static void update_dynamic_controls(void) {
  gboolean floating =
      gtk_drop_down_get_selected(state.window_combo) == WINDOW_FLOATING;
  gtk_widget_set_sensitive(GTK_WIDGET(state.size_combo), floating);
  gtk_widget_set_sensitive(GTK_WIDGET(state.position_combo), floating);
  gtk_widget_set_sensitive(GTK_WIDGET(state.quick_copy_switch), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(state.quick_save_switch),
                           !gtk_switch_get_active(state.quick_open_switch));
  gtk_widget_set_sensitive(GTK_WIDGET(state.area_copy_switch), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(state.area_save_switch),
                           !gtk_switch_get_active(state.area_open_switch));
  gtk_widget_set_sensitive(GTK_WIDGET(state.fullscreen_copy_switch), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(state.fullscreen_save_switch),
                           !gtk_switch_get_active(state.fullscreen_open_switch));
  gtk_widget_set_sensitive(GTK_WIDGET(state.all_screens_copy_switch), TRUE);
  gtk_widget_set_sensitive(
      GTK_WIDGET(state.all_screens_save_switch),
      !gtk_switch_get_active(state.all_screens_open_switch));
}

static void on_control_changed(GtkWidget *widget, gpointer data) {
  (void)widget;
  (void)data;
  update_dynamic_controls();
}

static void on_save_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  ShaulaSettingsConfig next = state.config;
  next.column_display = g_strdup(state.config.column_display);
  next.floating_relative_to = g_strdup(state.config.floating_relative_to);
  next.save_folder = g_strdup(state.config.save_folder);
  read_controls(&next);

  g_auto(GStrv) argv =
      shaula_settings_build_save_argv(state.shaula_bin, &next);
  gchar *out = NULL;
  gchar *err = NULL;
  int exit_code = 1;
  shaula_settings_run_command(argv, &out, &err, &exit_code);

  if (exit_code != 0) {
    char *message =
        g_strdup_printf("%s%s", out != NULL && *out != '\0' ? out : "",
                        err != NULL && *err != '\0' ? err : "");
    set_status(message != NULL && *message != '\0'
                   ? message
                   : "ERR_CONFIG_UNREADABLE: save failed",
               TRUE);
    g_free(message);
    g_free(out);
    g_free(err);
    shaula_settings_config_clear(&next);
    return;
  }

  shaula_settings_config_clear(&state.config);
  state.config = next;
  state.config_exists = TRUE;

  gboolean niri_changed = FALSE;
  ShaulaSettingsNiriResult niri_result =
      shaula_settings_niri_rule_changed(out, &niri_changed);
  if (niri_result != SHAULA_SETTINGS_NIRI_OK)
    set_status("ERR_CONFIG_INVALID: saved, but the response was malformed.",
               TRUE);
  else if (niri_changed)
    set_status("Saved. Niri rule file changed. Reload Niri config with your "
               "normal workflow.",
               FALSE);
  else
    set_status("Saved. Niri rule was already up to date.", FALSE);
  g_free(out);
  g_free(err);
}

static void on_open_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  if (state.config_path == NULL) {
    set_status(
        "ERR_CONFIG_UNREADABLE: configuration path could not be resolved.",
        TRUE);
    return;
  }
  if (!g_file_test(state.config_path, G_FILE_TEST_EXISTS)) {
    set_status("Save first to create config.toml.", TRUE);
    return;
  }
  char *argv[] = {"xdg-open", state.config_path, NULL};
  gchar *err = NULL;
  int exit_code = 1;
  shaula_settings_run_command(argv, NULL, &err, &exit_code);
  if (exit_code != 0) {
    char *message = g_strdup_printf("Could not open config file. %s",
                                    err != NULL ? err : "");
    set_status(message, TRUE);
    g_free(message);
  }
  g_free(err);
}

static gboolean refresh_shortcuts_status(void) {
  g_autofree char *error = NULL;
  ShaulaShortcutResult result =
      shaula_shortcuts_query(&state.shortcuts, &error);
  if (state.shortcuts_state_label != NULL) {
    gtk_label_set_text(GTK_LABEL(state.shortcuts_state_label),
                       shaula_settings_shortcut_state_text(state.shortcuts.state));
    if (shaula_settings_shortcut_state_is_warning(state.shortcuts.state))
      gtk_widget_add_css_class(state.shortcuts_state_label, "error");
    else
      gtk_widget_remove_css_class(state.shortcuts_state_label, "error");
  }
  if (state.shortcuts_backend_label != NULL) {
    g_autofree char *backend = g_strdup_printf(
        "Status: %s%s%s", shaula_settings_shortcut_state_text(state.shortcuts.state),
        state.shortcuts.state == SHAULA_SHORTCUT_STATE_ACTIVE ? " via " : "",
        state.shortcuts.state == SHAULA_SHORTCUT_STATE_ACTIVE
            ? shaula_settings_shortcut_backend_text(state.shortcuts.backend)
            : "");
    gtk_label_set_text(GTK_LABEL(state.shortcuts_backend_label), backend);
  }
  if (state.shortcuts_installation_label != NULL) {
    gtk_label_set_text(
        GTK_LABEL(state.shortcuts_installation_label),
        shaula_settings_shortcut_registration_text(&state.shortcuts));
  }
  if (state.shortcuts_detail_label != NULL) {
    g_autofree char *detail = g_strdup_printf(
        "Quick capture: %s\nArea capture: %s\nFullscreen capture: %s\n"
        "All-screens capture: %s",
        state.shortcuts.triggers[0] != NULL ? state.shortcuts.triggers[0]
                                            : "Ctrl+Shift+1",
        state.shortcuts.triggers[1] != NULL ? state.shortcuts.triggers[1]
                                            : "Ctrl+Shift+2",
        state.shortcuts.triggers[2] != NULL ? state.shortcuts.triggers[2]
                                            : "Ctrl+Shift+3",
        state.shortcuts.triggers[3] != NULL ? state.shortcuts.triggers[3]
                                            : "Ctrl+Shift+4");
    gtk_label_set_text(GTK_LABEL(state.shortcuts_detail_label), detail);
  }
  if (state.shortcuts_enable_button != NULL) {
    gboolean can_enable = shaula_settings_shortcut_can_enable(&state.shortcuts);
    gtk_widget_set_visible(state.shortcuts_enable_button, can_enable);
    gtk_widget_set_sensitive(state.shortcuts_enable_button, can_enable);
  }
  if (state.shortcuts_disable_button != NULL) {
    gboolean can_disable = shaula_settings_shortcut_can_disable(&state.shortcuts);
    gtk_widget_set_visible(state.shortcuts_disable_button, can_disable);
    gtk_widget_set_sensitive(state.shortcuts_disable_button, can_disable);
  }
  if (state.shortcuts_repair_button != NULL) {
    gboolean can_repair = shaula_settings_shortcut_can_repair(&state.shortcuts);
    gtk_widget_set_visible(state.shortcuts_repair_button, can_repair);
    gtk_widget_set_sensitive(state.shortcuts_repair_button, can_repair);
  }
  if (result != SHAULA_SHORTCUT_RESULT_OK && error != NULL)
    set_status(error, TRUE);
  return result == SHAULA_SHORTCUT_RESULT_OK;
}

typedef enum {
  SHORTCUT_OPERATION_ENABLE,
  SHORTCUT_OPERATION_DISABLE,
  SHORTCUT_OPERATION_REPAIR,
} ShortcutOperation;

static void run_shortcut_operation(ShortcutOperation operation) {
  ShaulaShortcutOptions options = {.remember_choice = TRUE};
  g_autofree char *error = NULL;
  ShaulaShortcutResult result;
  if (operation == SHORTCUT_OPERATION_ENABLE)
    result = shaula_shortcuts_enable(&options, &state.shortcuts, &error);
  else if (operation == SHORTCUT_OPERATION_DISABLE)
    result = shaula_shortcuts_disable(&options, &state.shortcuts, &error);
  else
    result = shaula_shortcuts_repair(&options, &state.shortcuts, &error);
  refresh_shortcuts_status();
  if (result != SHAULA_SHORTCUT_RESULT_OK) {
    set_status(error != NULL ? error : "Shortcut operation failed.", TRUE);
    return;
  }
  if (state.shortcuts.state == SHAULA_SHORTCUT_STATE_UNSUPPORTED)
    set_status("Automatic global shortcuts are unavailable on this desktop. "
               "Desktop launcher actions remain available.", FALSE);
  else if (state.shortcuts.state == SHAULA_SHORTCUT_STATE_PERMISSION_DENIED)
    set_status("The desktop declined the shortcut request. You can repair or "
               "reconfigure it later.", FALSE);
  else if (state.shortcuts.state == SHAULA_SHORTCUT_STATE_PERMISSION_PENDING)
    set_status("Waiting for the desktop to approve the shortcut request.", FALSE);
  else if (operation == SHORTCUT_OPERATION_DISABLE)
    set_status("Capture shortcuts disabled. Your choice was remembered.", FALSE);
  else if (operation == SHORTCUT_OPERATION_REPAIR)
    set_status("Capture shortcuts repaired or reconfigured.", FALSE);
  else
    set_status("Capture shortcuts enabled.", FALSE);
}

static void on_shortcuts_enable_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  run_shortcut_operation(SHORTCUT_OPERATION_ENABLE);
}

static void on_shortcuts_disable_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  run_shortcut_operation(SHORTCUT_OPERATION_DISABLE);
}

static void on_shortcuts_repair_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  run_shortcut_operation(SHORTCUT_OPERATION_REPAIR);
}

static void on_shortcuts_recheck_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  refresh_shortcuts_status();
  set_status("Shortcut status rechecked.", FALSE);
}

/* Opens the universal capture menu asynchronously so Settings remains usable
 * while the independent launcher window is open. */
static void on_shortcuts_menu_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  char *argv[] = {state.shaula_bin, "launch", NULL};
  g_autoptr(GError) error = NULL;
  if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
                     &error)) {
    set_status(error->message, TRUE);
    return;
  }
  set_status("Shaula menu opened.", FALSE);
}

static char *noctalia_plugin_marker_path(void) {
  const char *xdg = g_getenv("XDG_CONFIG_HOME");
  g_autofree char *root = NULL;
  if (xdg != NULL && xdg[0] != '\0')
    root = g_strdup(xdg);
  else {
    const char *home = g_getenv("HOME");
    if (home != NULL && home[0] != '\0')
      root = g_build_filename(home, ".config", NULL);
  }
  return root != NULL
             ? g_build_filename(root, "noctalia", "plugins", "shaula",
                                ".shaula-managed", NULL)
             : NULL;
}

static void refresh_noctalia_status(void) {
  const gboolean detected = shaula_noctalia_detected();
  g_autofree char *marker = noctalia_plugin_marker_path();
  const gboolean installed =
      marker != NULL && g_file_test(marker, G_FILE_TEST_IS_REGULAR);
  if (state.noctalia_status_label != NULL)
    gtk_label_set_text(GTK_LABEL(state.noctalia_status_label),
                       installed ? "Installed"
                                 : (detected ? "Available" : "Not detected"));
  if (state.noctalia_install_button != NULL)
    gtk_widget_set_sensitive(state.noctalia_install_button,
                             detected && !installed);
  if (state.noctalia_remove_button != NULL)
    gtk_widget_set_sensitive(state.noctalia_remove_button, installed);
}

static void on_noctalia_install_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  g_autofree char *source = shaula_noctalia_plugin_source_resolve();
  ShaulaNoctaliaResult result = {0};
  ShaulaNoctaliaStatus status =
      shaula_noctalia_install(source, FALSE, &result);
  if (status != SHAULA_NOCTALIA_STATUS_OK)
    set_status(shaula_noctalia_status_token(status), TRUE);
  else
    set_status(result.changed ? "Noctalia integration installed."
                              : "Noctalia integration was already installed.",
               FALSE);
  shaula_noctalia_result_clear(&result);
  refresh_noctalia_status();
}

static void on_noctalia_remove_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  ShaulaNoctaliaResult result = {0};
  ShaulaNoctaliaStatus status = shaula_noctalia_remove(FALSE, &result);
  if (status != SHAULA_NOCTALIA_STATUS_OK &&
      status != SHAULA_NOCTALIA_STATUS_NOT_DETECTED)
    set_status(shaula_noctalia_status_token(status), TRUE);
  else
    set_status(result.changed ? "Noctalia integration removed."
                              : "Noctalia integration was not installed.",
               FALSE);
  shaula_noctalia_result_clear(&result);
  refresh_noctalia_status();
}

static void on_reset_response(GtkDialog *dialog, int response, gpointer data) {
  (void)data;
  gtk_window_destroy(GTK_WINDOW(dialog));
  if (response != GTK_RESPONSE_ACCEPT)
    return;
  ShaulaSettingsConfig defaults;
  shaula_settings_config_init_defaults(&defaults);
  char *argv[] = {
      state.shaula_bin,
      "config",
      "save",
      "--json",
      "--force",
      "--region-mode",
      "frozen",
      "--preview-mode",
      "floating",
      "--focused",
      "true",
      "--close-preview-on-save",
      "true",
      "--width",
      "1100",
      "--height",
      "720",
      "--default-column-display",
      "normal",
      "--floating-position",
      "centered",
      "--save-folder",
      "~/Pictures/shaula",
      NULL,
  };
  gchar *out = NULL;
  gchar *err = NULL;
  int exit_code = 1;
  shaula_settings_run_command(argv, &out, &err, &exit_code);
  if (exit_code != 0) {
    char *message =
        g_strdup_printf("%s%s", out != NULL && *out != '\0' ? out : "",
                        err != NULL && *err != '\0' ? err : "");
    set_status(message != NULL && *message != '\0'
                   ? message
                   : "ERR_CONFIG_UNREADABLE: reset failed",
               TRUE);
    g_free(message);
    g_free(out);
    g_free(err);
    shaula_settings_config_clear(&defaults);
    return;
  }
  g_free(out);
  g_free(err);
  shaula_settings_config_clear(&state.config);
  state.config = defaults;
  state.config_invalid = FALSE;
  gtk_widget_set_visible(state.error_box, FALSE);
  gtk_widget_set_visible(state.form_scroller, TRUE);
  set_status("Reset to defaults. Use Save to update the Niri rule.", FALSE);
}

static void on_reset_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(state.window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
      GTK_BUTTONS_NONE, "Reset Shaula settings to defaults?");
  gtk_message_dialog_format_secondary_text(
      GTK_MESSAGE_DIALOG(dialog),
      "The current config file will be backed up before defaults are written.");
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Reset", GTK_RESPONSE_ACCEPT);
  g_signal_connect(dialog, "response", G_CALLBACK(on_reset_response), NULL);
  gtk_window_present(GTK_WINDOW(dialog));
}

static GtkWidget *labeled_row(const char *label, GtkWidget *child) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_add_css_class(row, "settings-row");
  GtkWidget *text = gtk_label_new(label);
  gtk_widget_set_hexpand(text, TRUE);
  gtk_label_set_xalign(GTK_LABEL(text), 0.0);
  GtkWidget *control_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_size_request(control_slot, SETTINGS_CONTROL_W, -1);
  gtk_widget_set_halign(control_slot, GTK_ALIGN_END);
  gtk_widget_set_hexpand(control_slot, FALSE);
  const gboolean fixed_control = GTK_IS_SWITCH(child);
  if (fixed_control)
    gtk_widget_set_size_request(child, SETTINGS_SWITCH_W, SETTINGS_SWITCH_H);
  gtk_widget_set_halign(child, fixed_control ? GTK_ALIGN_END : GTK_ALIGN_FILL);
  gtk_widget_set_valign(child, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(child, !fixed_control);
  gtk_box_append(GTK_BOX(row), text);
  gtk_box_append(GTK_BOX(control_slot), child);
  gtk_box_append(GTK_BOX(row), control_slot);
  return row;
}

static GtkWidget *labeled_description_row(const char *label,
                                          const char *description,
                                          GtkWidget *child) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_add_css_class(row, "settings-row");
  GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand(text_box, TRUE);
  GtkWidget *title = gtk_label_new(label);
  gtk_label_set_xalign(GTK_LABEL(title), 0.0);
  GtkWidget *body = gtk_label_new(description);
  gtk_widget_add_css_class(body, "description");
  gtk_label_set_xalign(GTK_LABEL(body), 0.0);
  gtk_label_set_wrap(GTK_LABEL(body), TRUE);
  gtk_box_append(GTK_BOX(text_box), title);
  gtk_box_append(GTK_BOX(text_box), body);
  GtkWidget *control_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_size_request(control_slot, SETTINGS_CONTROL_W, -1);
  gtk_widget_set_halign(control_slot, GTK_ALIGN_END);
  if (GTK_IS_SWITCH(child))
    gtk_widget_set_size_request(child, SETTINGS_SWITCH_W, SETTINGS_SWITCH_H);
  gtk_widget_set_halign(child, GTK_ALIGN_END);
  gtk_widget_set_valign(child, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(row), text_box);
  gtk_box_append(GTK_BOX(control_slot), child);
  gtk_box_append(GTK_BOX(row), control_slot);
  return row;
}

static GtkSwitch *make_switch(gboolean active) {
  GtkSwitch *sw = GTK_SWITCH(gtk_switch_new());
  gtk_switch_set_active(sw, active);
  g_signal_connect(sw, "notify::active", G_CALLBACK(on_control_changed), NULL);
  return sw;
}

static void add_matrix_row(GtkWidget *grid, int row, const char *label,
                           GtkSwitch **open_sw, GtkSwitch **copy_sw, GtkSwitch **save_sw,
                           gboolean skip_val, gboolean copy_val, gboolean save_val) {
  GtkWidget *row_label = gtk_label_new(label);
  gtk_label_set_xalign(GTK_LABEL(row_label), 0.0);
  gtk_widget_set_hexpand(row_label, TRUE);
  gtk_grid_attach(GTK_GRID(grid), row_label, 0, row, 1, 1);

  *open_sw = make_switch(!skip_val);
  *copy_sw = make_switch(copy_val);
  *save_sw = make_switch(save_val);

  gtk_widget_set_halign(GTK_WIDGET(*open_sw), GTK_ALIGN_CENTER);
  gtk_widget_set_halign(GTK_WIDGET(*copy_sw), GTK_ALIGN_CENTER);
  gtk_widget_set_halign(GTK_WIDGET(*save_sw), GTK_ALIGN_CENTER);

  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(*open_sw), 1, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(*copy_sw), 2, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(*save_sw), 3, row, 1, 1);
}

static void on_folder_response(GtkNativeDialog *dialog, int response,
                               gpointer data) {
  (void)data;
  if (response == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    GFile *file = gtk_file_chooser_get_file(chooser);
    if (file != NULL) {
      char *path = g_file_get_path(file);
      if (path != NULL)
        gtk_editable_set_text(GTK_EDITABLE(state.save_folder_entry), path);
      g_free(path);
      g_object_unref(file);
    }
  }
  g_object_unref(dialog);
}

static void on_select_folder_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  GtkFileChooserNative *dialog = gtk_file_chooser_native_new(
      "Select Shaula Directory", GTK_WINDOW(state.window),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Select", "Cancel");
  g_signal_connect(dialog, "response", G_CALLBACK(on_folder_response), NULL);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static GtkWidget *folder_row(void) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class(row, "settings-row");
  GtkWidget *label = gtk_label_new("Shaula Directory");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_widget_set_hexpand(label, TRUE);
  state.save_folder_entry = GTK_ENTRY(gtk_entry_new());
  gtk_editable_set_text(GTK_EDITABLE(state.save_folder_entry),
                        state.config.save_folder != NULL
                            ? state.config.save_folder
                            : "~/Pictures/shaula");
  gtk_widget_set_hexpand(GTK_WIDGET(state.save_folder_entry), TRUE);
  GtkWidget *button = gtk_button_new_with_label("Select Directory");
  g_signal_connect(button, "clicked", G_CALLBACK(on_select_folder_clicked),
                   NULL);
  gtk_box_append(GTK_BOX(row), label);
  gtk_box_append(GTK_BOX(row), GTK_WIDGET(state.save_folder_entry));
  gtk_box_append(GTK_BOX(row), button);
  return row;
}

static GtkWidget *build_form(void) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_add_css_class(box, "settings-form");

  GtkWidget *shortcuts_heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *shortcuts_title = gtk_label_new("Global Shortcuts");
  gtk_widget_add_css_class(shortcuts_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(shortcuts_title), 0.0);
  gtk_widget_set_hexpand(shortcuts_title, TRUE);
  GtkWidget *optional_label = gtk_label_new("Optional");
  gtk_widget_add_css_class(optional_label, "description");
  gtk_widget_set_valign(optional_label, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(shortcuts_heading), shortcuts_title);
  gtk_box_append(GTK_BOX(shortcuts_heading), optional_label);
  gtk_box_append(GTK_BOX(box), shortcuts_heading);

  GtkWidget *shortcuts_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(shortcuts_card, "settings-row");
  gtk_widget_set_margin_start(shortcuts_card, 16);
  gtk_widget_set_margin_end(shortcuts_card, 16);
  GtkWidget *shortcuts_description = gtk_label_new(
      "Capture from anywhere using Ctrl+Shift+1–4. The Shaula menu always "
      "works when global shortcuts are unavailable.");
  gtk_label_set_xalign(GTK_LABEL(shortcuts_description), 0.0);
  gtk_label_set_wrap(GTK_LABEL(shortcuts_description), TRUE);
  gtk_widget_add_css_class(shortcuts_description, "description");
  gtk_box_append(GTK_BOX(shortcuts_card), shortcuts_description);

  state.shortcuts_state_label = gtk_label_new("Checking shortcut status…");
  gtk_widget_set_visible(state.shortcuts_state_label, FALSE);
  state.shortcuts_backend_label = gtk_label_new("Status: checking…");
  gtk_label_set_xalign(GTK_LABEL(state.shortcuts_backend_label), 0.0);
  gtk_box_append(GTK_BOX(shortcuts_card), state.shortcuts_backend_label);
  state.shortcuts_installation_label = gtk_label_new("");
  gtk_widget_set_visible(state.shortcuts_installation_label, FALSE);

  state.shortcuts_detail_label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(state.shortcuts_detail_label), 0.0);
  gtk_label_set_wrap(GTK_LABEL(state.shortcuts_detail_label), TRUE);
  gtk_widget_add_css_class(state.shortcuts_detail_label, "description");
  gtk_box_append(GTK_BOX(shortcuts_card), state.shortcuts_detail_label);

  GtkWidget *shortcut_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  state.shortcuts_menu_button = gtk_button_new_with_label("Open Shaula Menu");
  state.shortcuts_enable_button = gtk_button_new_with_label("Enable");
  gtk_widget_add_css_class(state.shortcuts_enable_button, "suggested-action");
  state.shortcuts_disable_button = gtk_button_new_with_label("Disable");
  state.shortcuts_repair_button = gtk_button_new_with_label("Repair");
  state.shortcuts_recheck_button = gtk_button_new_with_label("Check Again");
  gtk_box_append(GTK_BOX(shortcut_buttons), state.shortcuts_menu_button);
  gtk_box_append(GTK_BOX(shortcut_buttons), state.shortcuts_enable_button);
  gtk_box_append(GTK_BOX(shortcut_buttons), state.shortcuts_disable_button);
  gtk_box_append(GTK_BOX(shortcut_buttons), state.shortcuts_repair_button);
  gtk_box_append(GTK_BOX(shortcut_buttons), state.shortcuts_recheck_button);
  gtk_box_append(GTK_BOX(shortcuts_card), shortcut_buttons);
  gtk_box_append(GTK_BOX(box), shortcuts_card);

  GtkWidget *capture_title = gtk_label_new("Capture behavior");
  gtk_widget_add_css_class(capture_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(capture_title), 0.0);
  gtk_box_append(GTK_BOX(box), capture_title);

  const char *regions[] = {"Live", "Frozen", NULL};
  state.region_combo = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(regions));
  gtk_drop_down_set_selected(state.region_combo, state.config.region_mode);
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Region mode", GTK_WIDGET(state.region_combo)));

  GtkWidget *after_title = gtk_label_new("After capture");
  gtk_widget_add_css_class(after_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(after_title), 0.0);
  gtk_box_append(GTK_BOX(box), after_title);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 32);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
  gtk_widget_set_margin_start(grid, 16);
  gtk_widget_set_margin_end(grid, 16);

  GtkWidget *h_open = gtk_label_new("Open preview");
  gtk_widget_add_css_class(h_open, "description");
  GtkWidget *h_copy = gtk_label_new("Copy to clipboard");
  gtk_widget_add_css_class(h_copy, "description");
  GtkWidget *h_save = gtk_label_new("Save automatically");
  gtk_widget_add_css_class(h_save, "description");

  gtk_grid_attach(GTK_GRID(grid), gtk_label_new(""), 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), h_open, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), h_copy, 2, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), h_save, 3, 0, 1, 1);

  add_matrix_row(grid, 1, "Quick capture", &state.quick_open_switch,
                 &state.quick_copy_switch, &state.quick_save_switch,
                 state.config.quick_skip_preview, state.config.quick_copy,
                 state.config.quick_save);
  add_matrix_row(grid, 2, "Area capture", &state.area_open_switch,
                 &state.area_copy_switch, &state.area_save_switch,
                 state.config.area_skip_preview, state.config.area_copy,
                 state.config.area_save);
  add_matrix_row(grid, 3, "All screens", &state.all_screens_open_switch,
                 &state.all_screens_copy_switch, &state.all_screens_save_switch,
                 state.config.all_screens_skip_preview,
                 state.config.all_screens_copy, state.config.all_screens_save);

  // Also add fullscreen to matrix
  add_matrix_row(grid, 4, "Fullscreen", &state.fullscreen_open_switch,
                 &state.fullscreen_copy_switch, &state.fullscreen_save_switch,
                 state.config.fullscreen_skip_preview, state.config.fullscreen_copy,
                 state.config.fullscreen_save);

  gtk_box_append(GTK_BOX(box), grid);

  GtkWidget *preview_title = gtk_label_new("Preview");
  gtk_widget_add_css_class(preview_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(preview_title), 0.0);
  gtk_box_append(GTK_BOX(box), preview_title);

  const char *windows[] = {
      "Auto",       "Tiling", "Floating", "Maximized", "Maximized to edges",
      "Fullscreen", NULL};
  state.window_combo = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(windows));
  gtk_drop_down_set_selected(state.window_combo, state.config.window_mode);
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Window mode", GTK_WIDGET(state.window_combo)));

  const char *sizes[] = {"Small", "Medium", "Large", "Custom", NULL};
  state.size_combo = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(sizes));
  gtk_drop_down_set_selected(
      state.size_combo, shaula_settings_size_preset_for_config(&state.config));
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Preview size", GTK_WIDGET(state.size_combo)));

  const char *positions[] = {"Centered", "Top Left", "Top Right", "Custom",
                             NULL};
  state.position_combo = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(positions));
  gtk_drop_down_set_selected(state.position_combo,
                             state.config.position_preset);
  gtk_box_append(GTK_BOX(box), labeled_row("Floating position",
                                           GTK_WIDGET(state.position_combo)));

  state.focused_switch = GTK_SWITCH(gtk_switch_new());
  gtk_switch_set_active(state.focused_switch, state.config.focused);
  gtk_box_append(GTK_BOX(box), labeled_row("Focus preview window",
                                           GTK_WIDGET(state.focused_switch)));

  state.close_preview_on_save_switch = GTK_SWITCH(gtk_switch_new());
  gtk_switch_set_active(state.close_preview_on_save_switch,
                        state.config.close_preview_on_save);
  gtk_box_append(GTK_BOX(box),
                 labeled_description_row(
                     "Close preview on save",
                     "Close the preview window after a successful Ctrl+S save.",
                     GTK_WIDGET(state.close_preview_on_save_switch)));

  GtkWidget *saving_title = gtk_label_new("Saving");
  gtk_widget_add_css_class(saving_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(saving_title), 0.0);
  gtk_box_append(GTK_BOX(box), saving_title);

  gtk_box_append(GTK_BOX(box), folder_row());

  GtkWidget *notifications_title = gtk_label_new("Notifications");
  gtk_widget_add_css_class(notifications_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(notifications_title), 0.0);
  gtk_box_append(GTK_BOX(box), notifications_title);
  state.notifications_success_switch =
      make_switch(state.config.notifications_success);
  state.notifications_errors_switch =
      make_switch(state.config.notifications_errors);
  state.notifications_thumbnails_switch =
      make_switch(state.config.notifications_thumbnails);
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Show success notifications",
                             GTK_WIDGET(state.notifications_success_switch)));
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Show error notifications",
                             GTK_WIDGET(state.notifications_errors_switch)));
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Include thumbnail",
                             GTK_WIDGET(state.notifications_thumbnails_switch)));

  GtkWidget *integrations_title = gtk_label_new("Integrations");
  gtk_widget_add_css_class(integrations_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(integrations_title), 0.0);
  gtk_box_append(GTK_BOX(box), integrations_title);

  GtkWidget *noctalia_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(noctalia_card, "settings-row");
  gtk_widget_set_margin_start(noctalia_card, 16);
  gtk_widget_set_margin_end(noctalia_card, 16);
  GtkWidget *noctalia_title = gtk_label_new("Noctalia Shell widget");
  gtk_label_set_xalign(GTK_LABEL(noctalia_title), 0.0);
  gtk_box_append(GTK_BOX(noctalia_card), noctalia_title);
  state.noctalia_status_label = gtk_label_new("Checking…");
  gtk_label_set_xalign(GTK_LABEL(state.noctalia_status_label), 0.0);
  gtk_widget_add_css_class(state.noctalia_status_label, "description");
  gtk_box_append(GTK_BOX(noctalia_card), state.noctalia_status_label);
  GtkWidget *noctalia_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  state.noctalia_install_button = gtk_button_new_with_label("Install widget");
  state.noctalia_remove_button = gtk_button_new_with_label("Remove widget");
  gtk_box_append(GTK_BOX(noctalia_buttons), state.noctalia_install_button);
  gtk_box_append(GTK_BOX(noctalia_buttons), state.noctalia_remove_button);
  gtk_box_append(GTK_BOX(noctalia_card), noctalia_buttons);
  gtk_box_append(GTK_BOX(box), noctalia_card);

  GtkWidget *advanced_title = gtk_label_new("Advanced");
  gtk_widget_add_css_class(advanced_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(advanced_title), 0.0);
  gtk_box_append(GTK_BOX(box), advanced_title);

  state.path_label =
      gtk_label_new(state.config_path != NULL ? state.config_path : "unknown");
  gtk_label_set_xalign(GTK_LABEL(state.path_label), 1.0);
  gtk_label_set_ellipsize(GTK_LABEL(state.path_label), PANGO_ELLIPSIZE_START);
  gtk_box_append(GTK_BOX(box), labeled_row("Config path", state.path_label));

  GtkWidget *advanced_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_start(advanced_buttons, 16);
  gtk_widget_set_margin_end(advanced_buttons, 16);
  state.open_button = gtk_button_new_with_label("Open Config File");
  state.reset_button = gtk_button_new_with_label("Reset to Defaults");
  gtk_widget_add_css_class(state.reset_button, "error");
  gtk_box_append(GTK_BOX(advanced_buttons), state.open_button);
  gtk_box_append(GTK_BOX(advanced_buttons), state.reset_button);
  gtk_box_append(GTK_BOX(box), advanced_buttons);

  g_signal_connect(state.open_button, "clicked", G_CALLBACK(on_open_clicked),
                   NULL);
  g_signal_connect(state.reset_button, "clicked", G_CALLBACK(on_reset_clicked),
                   NULL);
  g_signal_connect(state.shortcuts_enable_button, "clicked",
                   G_CALLBACK(on_shortcuts_enable_clicked), NULL);
  g_signal_connect(state.shortcuts_disable_button, "clicked",
                   G_CALLBACK(on_shortcuts_disable_clicked), NULL);
  g_signal_connect(state.shortcuts_repair_button, "clicked",
                   G_CALLBACK(on_shortcuts_repair_clicked), NULL);
  g_signal_connect(state.shortcuts_recheck_button, "clicked",
                   G_CALLBACK(on_shortcuts_recheck_clicked), NULL);
  g_signal_connect(state.shortcuts_menu_button, "clicked",
                   G_CALLBACK(on_shortcuts_menu_clicked), NULL);
  g_signal_connect(state.noctalia_install_button, "clicked",
                   G_CALLBACK(on_noctalia_install_clicked), NULL);
  g_signal_connect(state.noctalia_remove_button, "clicked",
                   G_CALLBACK(on_noctalia_remove_clicked), NULL);

  g_signal_connect(state.window_combo, "notify::selected",
                   G_CALLBACK(on_control_changed), NULL);
  g_signal_connect(state.size_combo, "notify::selected",
                   G_CALLBACK(on_control_changed), NULL);
  update_dynamic_controls();
  return box;
}

static GtkWidget *build_error_box(void) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *title = gtk_label_new("ERR_CONFIG_INVALID");
  gtk_widget_add_css_class(title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0);
  gtk_box_append(GTK_BOX(box), title);
  GtkWidget *body =
      gtk_label_new("Shaula could not parse the existing config file. Open it "
                    "to fix the issue or reset to defaults after a backup.");
  gtk_label_set_wrap(GTK_LABEL(body), TRUE);
  gtk_label_set_xalign(GTK_LABEL(body), 0.0);
  gtk_box_append(GTK_BOX(box), body);
  return box;
}

static void load_initial_state(AppState *app) {
  app->config_path = shaula_settings_resolve_config_path();
  shaula_settings_config_init_defaults(&app->config);
  if (app->config_path == NULL) {
    app->config_invalid = TRUE;
    return;
  }
  char *argv[] = {app->shaula_bin, "config", "show", "--json", NULL};
  gchar *out = NULL;
  gchar *err = NULL;
  int exit_code = 1;
  shaula_settings_run_command(argv, &out, &err, &exit_code);
  app->config_exists = g_file_test(app->config_path, G_FILE_TEST_EXISTS);
  if (exit_code != 0) {
    app->config_invalid = TRUE;
    g_free(out);
    g_free(err);
    return;
  }
  char *resolved_path = shaula_settings_config_path_from_show_json(out);
  if (resolved_path != NULL) {
    g_free(app->config_path);
    app->config_path = resolved_path;
  }
  shaula_settings_config_from_show_json(out, &app->config);
  g_free(out);
  g_free(err);
}

static void on_activate(GtkApplication *app, gpointer data) {
  (void)data;
  state.app = app;
  load_initial_state(&state);

  state.window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(state.window),
                       state.setup_mode ? "Shaula Setup" : "Shaula Settings");
  gtk_window_set_default_size(GTK_WINDOW(state.window), 680, 760);
  gtk_window_set_resizable(GTK_WINDOW(state.window), TRUE);

  GtkWidget *titlebar = gtk_header_bar_new();
  gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(titlebar), TRUE);
  gtk_window_set_titlebar(GTK_WINDOW(state.window), titlebar);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(root, "settings-root");
  gtk_window_set_child(GTK_WINDOW(state.window), root);

  state.error_box = build_error_box();
  gtk_box_append(GTK_BOX(root), state.error_box);

  state.form_scroller = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(state.form_scroller, TRUE);
  gtk_box_append(GTK_BOX(root), state.form_scroller);

  GtkWidget *center_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_halign(center_wrapper, GTK_ALIGN_CENTER);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(state.form_scroller), center_wrapper);

  state.form_box = build_form();
  gtk_widget_set_margin_top(state.form_box, 24);
  gtk_widget_set_margin_bottom(state.form_box, 64);
  gtk_widget_set_margin_start(state.form_box, 24);
  gtk_widget_set_margin_end(state.form_box, 24);
  gtk_widget_set_size_request(state.form_box, 640, -1);
  gtk_box_append(GTK_BOX(center_wrapper), state.form_box);

  gtk_widget_set_visible(state.error_box, state.config_invalid);
  gtk_widget_set_visible(state.form_scroller, !state.config_invalid);

  GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_add_css_class(footer, "settings-footer");
  gtk_widget_set_margin_start(footer, 24);
  gtk_widget_set_margin_end(footer, 24);
  gtk_widget_set_margin_top(footer, 16);
  gtk_widget_set_margin_bottom(footer, 16);

  state.status_label = gtk_label_new(
      state.config_invalid ? "Open the config file or reset to defaults."
                           : "Changes apply to future preview windows.");
  gtk_label_set_wrap(GTK_LABEL(state.status_label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(state.status_label), 0.0);
  gtk_widget_set_hexpand(state.status_label, TRUE);
  gtk_box_append(GTK_BOX(footer), state.status_label);

  GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(buttons, GTK_ALIGN_END);
  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  state.apply_button = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(state.apply_button, "suggested-action");
  gtk_box_append(GTK_BOX(buttons), cancel_button);
  gtk_box_append(GTK_BOX(buttons), state.apply_button);
  gtk_box_append(GTK_BOX(footer), buttons);

  gtk_box_append(GTK_BOX(root), footer);

  gtk_widget_set_sensitive(state.apply_button, !state.config_invalid);
  g_signal_connect_swapped(cancel_button, "clicked", G_CALLBACK(gtk_window_destroy), state.window);
  g_signal_connect(state.apply_button, "clicked", G_CALLBACK(on_save_clicked), NULL);

  refresh_shortcuts_status();
  refresh_noctalia_status();

  gtk_window_present(GTK_WINDOW(state.window));
}

static void install_css(GtkApplication *app, gpointer data) {
  (void)app;
  (void)data;
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
      provider,
      ".settings-root { background: @theme_bg_color; color: @theme_fg_color; }"
      ".settings-title { font-size: 24px; font-weight: 700; padding: 18px 18px "
      "0 18px; }"
      ".section-title { font-size: 12px; font-weight: 700; margin-top: 18px; "
      "margin-bottom: 2px; text-transform: uppercase; letter-spacing: 1px; "
      "opacity: 0.7; }"
      ".settings-row { min-height: 38px; }"
      ".description { opacity: 0.72; font-size: 12px; }"
      "button.suggested-action { font-weight: bold; }"
      ".error { color: @error_color; font-weight: bold; }");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

int main(int argc, char **argv) {
  shaula_shortcut_status_init(&state.shortcuts);
  state.setup_mode = argc >= 2 && g_str_equal(argv[1], "--setup");
  state.shaula_bin = g_strdup("shaula");
  GtkApplication *app =
      gtk_application_new("dev.shaula.settings", G_APPLICATION_DEFAULT_FLAGS);
  if (app == NULL)
    return 45;
  g_signal_connect(app, "startup", G_CALLBACK(install_css), NULL);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  int rc = g_application_run(G_APPLICATION(app), 0, NULL);
  g_object_unref(app);
  g_free(state.shaula_bin);
  g_free(state.config_path);
  shaula_shortcut_status_clear(&state.shortcuts);
  shaula_settings_config_clear(&state.config);
  return rc > 255 ? 255 : rc;
}
