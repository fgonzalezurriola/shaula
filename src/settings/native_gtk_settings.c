#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>
#include "settings_config.h"

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
  gboolean niri_detected;
  char *niri_config_path;
  gboolean niri_shortcuts_installed;
  gboolean niri_shortcuts_conflicts;
  GtkWidget *niri_detected_label;
  GtkWidget *niri_config_path_label;
  GtkWidget *niri_shortcuts_status_label;
  GtkWidget *niri_shortcuts_detail_label;
  GtkWidget *niri_install_button;
  GtkWidget *niri_remove_button;
  GtkWidget *niri_open_config_button;
  GtkWidget *niri_recheck_button;
} AppState;

static AppState state;
static const int SETTINGS_CONTROL_W = 132;
static const int SETTINGS_SWITCH_W = 46;
static const int SETTINGS_SWITCH_H = 26;

static gboolean run_shaula(char **argv, gchar **stdout_text,
                           gchar **stderr_text, int *exit_code) {
  gint status = 1;
  GError *error = NULL;
  gboolean spawned =
      g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                   stdout_text, stderr_text, &status, &error);
  if (!spawned) {
    if (stderr_text != NULL)
      *stderr_text = g_strdup(error != NULL ? error->message : "spawn failed");
    if (error != NULL)
      g_error_free(error);
    if (exit_code != NULL)
      *exit_code = 127;
    return FALSE;
  }
  if (exit_code != NULL)
    *exit_code = g_spawn_check_wait_status(status, NULL) ? 0 : 1;
  return TRUE;
}

static gboolean json_niri_changed(const char *json) {
  const char *niri = strstr(json, "\"niri\":");
  if (niri == NULL)
    return FALSE;
  return strstr(niri, "\"changed\":true") != NULL;
}

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

  char *focused = g_strdup(next.focused ? "true" : "false");
  char *close_preview_on_save =
      g_strdup(next.close_preview_on_save ? "true" : "false");
  char *width = g_strdup_printf("%d", next.width);
  char *height = g_strdup_printf("%d", next.height);
  char *quick_skip = g_strdup(next.quick_skip_preview ? "true" : "false");
  char *quick_copy = g_strdup(next.quick_copy ? "true" : "false");
  char *quick_save = g_strdup(next.quick_save ? "true" : "false");
  char *area_skip = g_strdup(next.area_skip_preview ? "true" : "false");
  char *area_copy = g_strdup(next.area_copy ? "true" : "false");
  char *area_save = g_strdup(next.area_save ? "true" : "false");
  char *fullscreen_skip =
      g_strdup(next.fullscreen_skip_preview ? "true" : "false");
  char *fullscreen_copy = g_strdup(next.fullscreen_copy ? "true" : "false");
  char *fullscreen_save = g_strdup(next.fullscreen_save ? "true" : "false");
  char *all_screens_skip =
      g_strdup(next.all_screens_skip_preview ? "true" : "false");
  char *all_screens_copy = g_strdup(next.all_screens_copy ? "true" : "false");
  char *all_screens_save = g_strdup(next.all_screens_save ? "true" : "false");
  char *notifications_success =
      g_strdup(next.notifications_success ? "true" : "false");
  char *notifications_errors =
      g_strdup(next.notifications_errors ? "true" : "false");
  char *notifications_thumbnails =
      g_strdup(next.notifications_thumbnails ? "true" : "false");
  char *argv[] = {
      state.shaula_bin,
      "config",
      "save",
      "--json",
      "--region-mode",
      (char *)shaula_settings_region_mode_text(next.region_mode),
      "--preview-mode",
      (char *)shaula_settings_window_mode_text(next.window_mode),
      "--focused",
      focused,
      "--close-preview-on-save",
      close_preview_on_save,
      "--width",
      width,
      "--height",
      height,
      "--floating-position",
      (char *)shaula_settings_position_arg(&next),
      "--after-quick-skip-preview",
      quick_skip,
      "--after-quick-copy",
      quick_copy,
      "--after-quick-save",
      quick_save,
      "--after-area-skip-preview",
      area_skip,
      "--after-area-copy",
      area_copy,
      "--after-area-save",
      area_save,
      "--after-fullscreen-skip-preview",
      fullscreen_skip,
      "--after-fullscreen-copy",
      fullscreen_copy,
      "--after-fullscreen-save",
      fullscreen_save,
      "--after-all-screens-skip-preview",
      all_screens_skip,
      "--after-all-screens-copy",
      all_screens_copy,
      "--after-all-screens-save",
      all_screens_save,
      "--save-folder",
      next.save_folder != NULL ? next.save_folder : "",
      "--notifications-success",
      notifications_success,
      "--notifications-errors",
      notifications_errors,
      "--notifications-thumbnails",
      notifications_thumbnails,
      "--apply-niri",
      NULL,
  };
  gchar *out = NULL;
  gchar *err = NULL;
  int exit_code = 1;
  run_shaula(argv, &out, &err, &exit_code);
  g_free(focused);
  g_free(close_preview_on_save);
  g_free(width);
  g_free(height);
  g_free(quick_skip);
  g_free(quick_copy);
  g_free(quick_save);
  g_free(area_skip);
  g_free(area_copy);
  g_free(area_save);
  g_free(fullscreen_skip);
  g_free(fullscreen_copy);
  g_free(fullscreen_save);
  g_free(all_screens_skip);
  g_free(all_screens_copy);
  g_free(all_screens_save);
  g_free(notifications_success);
  g_free(notifications_errors);
  g_free(notifications_thumbnails);

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

  if (json_niri_changed(out))
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
  run_shaula(argv, NULL, &err, &exit_code);
  if (exit_code != 0) {
    char *message = g_strdup_printf("Could not open config file. %s",
                                    err != NULL ? err : "");
    set_status(message, TRUE);
    g_free(message);
  }
  g_free(err);
}

static const char *json_bool(const char *json, const char *key) {
  char *needle = g_strdup_printf("\"%s\":", key);
  const char *pos = strstr(json, needle);
  g_free(needle);
  if (pos == NULL)
    return NULL;
  pos += strlen(key) + 3;
  if (strncmp(pos, "true", 4) == 0)
    return "true";
  if (strncmp(pos, "false", 5) == 0)
    return "false";
  return NULL;
}

static char *json_string(const char *json, const char *key) {
  char *needle = g_strdup_printf("\"%s\":\"", key);
  const char *pos = strstr(json, needle);
  g_free(needle);
  if (pos == NULL)
    return NULL;
  pos += strlen(key) + 4;
  const char *end = strchr(pos, '"');
  if (end == NULL)
    return NULL;
  return g_strndup(pos, end - pos);
}

static void refresh_niri_status(void) {
  state.niri_detected = FALSE;
  state.niri_shortcuts_installed = FALSE;
  state.niri_shortcuts_conflicts = FALSE;
  g_free(state.niri_config_path);
  state.niri_config_path = NULL;

  char *argv[] = {state.shaula_bin, "config", "niri-keybinds-status", "--json",
                  NULL};
  gchar *out = NULL;
  gchar *err = NULL;
  int exit_code = 1;
  run_shaula(argv, &out, &err, &exit_code);

  if (exit_code == 0 && out != NULL) {
    const char *detected = json_bool(out, "niri_detected");
    state.niri_detected = detected != NULL && strcmp(detected, "true") == 0;

    char *path = json_string(out, "config_path");
    if (path != NULL)
      state.niri_config_path = path;

    const char *installed = json_bool(out, "installed");
    state.niri_shortcuts_installed =
        installed != NULL && strcmp(installed, "true") == 0;

    state.niri_shortcuts_conflicts = strstr(out, "\"conflicts\":[") != NULL &&
                                     strstr(out, "\"conflicts\":[]") == NULL;
  }

  g_free(out);
  g_free(err);

  if (state.niri_detected_label != NULL) {
    gtk_label_set_text(GTK_LABEL(state.niri_detected_label),
                       state.niri_detected ? "Detected" : "Not detected");
    if (state.niri_detected)
      gtk_widget_remove_css_class(state.niri_detected_label, "error");
    else
      gtk_widget_add_css_class(state.niri_detected_label, "error");
  }

  if (state.niri_config_path_label != NULL) {
    gtk_label_set_text(GTK_LABEL(state.niri_config_path_label),
                       state.niri_config_path != NULL ? state.niri_config_path
                                                      : "unknown");
  }

  if (state.niri_shortcuts_status_label != NULL) {
    if (!state.niri_detected) {
      gtk_label_set_text(GTK_LABEL(state.niri_shortcuts_status_label),
                         "Niri not detected");
      gtk_widget_remove_css_class(state.niri_shortcuts_status_label, "error");
    } else if (state.niri_shortcuts_conflicts) {
      gtk_label_set_text(GTK_LABEL(state.niri_shortcuts_status_label),
                         "Conflicts detected");
      gtk_widget_add_css_class(state.niri_shortcuts_status_label, "error");
    } else if (state.niri_shortcuts_installed) {
      gtk_label_set_text(GTK_LABEL(state.niri_shortcuts_status_label),
                         "Installed");
      gtk_widget_remove_css_class(state.niri_shortcuts_status_label, "error");
    } else {
      gtk_label_set_text(GTK_LABEL(state.niri_shortcuts_status_label),
                         "Not installed");
      gtk_widget_remove_css_class(state.niri_shortcuts_status_label, "error");
    }
  }

  if (state.niri_shortcuts_detail_label != NULL) {
    if (state.niri_shortcuts_installed) {
      gtk_label_set_text(GTK_LABEL(state.niri_shortcuts_detail_label),
                         "Quick capture: Ctrl+Shift+1\n"
                         "Area capture: Ctrl+Shift+2\n"
                         "Fullscreen: Ctrl+Shift+3\n"
                         "All screens: Ctrl+Shift+4");
    } else if (state.niri_shortcuts_conflicts) {
      gtk_label_set_text(GTK_LABEL(state.niri_shortcuts_detail_label),
                         "Existing CTRL+Shift+1/2/3/4 bindings found outside "
                         "Shaula block.");
    } else {
      gtk_label_set_text(GTK_LABEL(state.niri_shortcuts_detail_label), "");
    }
  }

  gboolean has_niri = state.niri_detected;
  if (state.niri_install_button != NULL)
    gtk_widget_set_sensitive(state.niri_install_button, has_niri);
  if (state.niri_remove_button != NULL)
    gtk_widget_set_sensitive(state.niri_remove_button,
                             has_niri && state.niri_shortcuts_installed);
  if (state.niri_open_config_button != NULL)
    gtk_widget_set_sensitive(state.niri_open_config_button,
                             state.niri_config_path != NULL);
}

static void do_niri_install(gboolean force);
static void on_niri_install_conflict_response(GtkDialog *dialog, int response,
                                              gpointer data);
static void on_niri_remove_response(GtkDialog *dialog, int response,
                                    gpointer data);

static void on_niri_install_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;

  if (state.niri_shortcuts_conflicts) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(state.window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE,
        "Existing CTRL+Shift+1/2/3/4 bindings detected outside the Shaula "
        "block.");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "Install anyway? This will add the Shaula managed keybinds block. "
        "You may need to manually remove conflicting bindings from your "
        "Niri config.");
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Install", GTK_RESPONSE_ACCEPT);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(on_niri_install_conflict_response), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
    return;
  }

  do_niri_install(FALSE);
}

static void do_niri_install(gboolean force) {
  char *argv[] = {state.shaula_bin,
                  "config",
                  "niri-keybinds-install",
                  "--json",
                  force ? "--force" : NULL,
                  NULL};
  gchar *out = NULL;
  gchar *err = NULL;
  int exit_code = 1;
  run_shaula(argv, &out, &err, &exit_code);

  if (exit_code != 0) {
    char *message =
        g_strdup_printf("%s%s", out != NULL && *out != '\0' ? out : "",
                        err != NULL && *err != '\0' ? err : "");
    set_status(message != NULL && *message != '\0'
                   ? message
                   : "ERR: keybind install failed",
               TRUE);
    g_free(message);
    g_free(out);
    g_free(err);
    return;
  }

  g_free(out);
  g_free(err);
  refresh_niri_status();
  set_status("Niri shortcuts installed. Niri should pick up config changes "
             "automatically; restart/reload Niri if they do not apply.",
             FALSE);
}

static void on_niri_install_conflict_response(GtkDialog *dialog, int response,
                                              gpointer data) {
  (void)data;
  gtk_window_destroy(GTK_WINDOW(dialog));
  if (response != GTK_RESPONSE_ACCEPT)
    return;
  do_niri_install(TRUE);
}

static void on_niri_remove_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(state.window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
      GTK_BUTTONS_NONE, "Remove Shaula Niri shortcuts?");
  gtk_message_dialog_format_secondary_text(
      GTK_MESSAGE_DIALOG(dialog),
      "The managed keybinds block will be removed from your Niri config. A "
      "backup will be created.");
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Remove", GTK_RESPONSE_ACCEPT);
  g_signal_connect(dialog, "response", G_CALLBACK(on_niri_remove_response),
                   NULL);
  gtk_window_present(GTK_WINDOW(dialog));
}

static void on_niri_remove_response(GtkDialog *dialog, int response,
                                    gpointer data) {
  (void)data;
  gtk_window_destroy(GTK_WINDOW(dialog));
  if (response != GTK_RESPONSE_ACCEPT)
    return;

  char *argv[] = {state.shaula_bin, "config", "niri-keybinds-remove", "--json",
                  NULL};
  gchar *out = NULL;
  gchar *err = NULL;
  int exit_code = 1;
  run_shaula(argv, &out, &err, &exit_code);

  if (exit_code != 0) {
    char *message =
        g_strdup_printf("%s%s", out != NULL && *out != '\0' ? out : "",
                        err != NULL && *err != '\0' ? err : "");
    set_status(message != NULL && *message != '\0'
                   ? message
                   : "ERR: keybind remove failed",
               TRUE);
    g_free(message);
    g_free(out);
    g_free(err);
    return;
  }

  g_free(out);
  g_free(err);
  refresh_niri_status();
  set_status("Shaula Niri shortcuts removed.", FALSE);
}

static void on_niri_open_config_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  if (state.niri_config_path == NULL) {
    set_status("Niri config path not detected.", TRUE);
    return;
  }
  char *argv[] = {"xdg-open", state.niri_config_path, NULL};
  gchar *err = NULL;
  int exit_code = 1;
  run_shaula(argv, NULL, &err, &exit_code);
  if (exit_code != 0) {
    char *message = g_strdup_printf("Could not open Niri config. %s",
                                    err != NULL ? err : "");
    set_status(message, TRUE);
    g_free(message);
  }
  g_free(err);
}

static void on_niri_recheck_clicked(GtkButton *button, gpointer data) {
  (void)button;
  (void)data;
  refresh_niri_status();
  set_status("Niri shortcuts status rechecked.", FALSE);
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
      "live",
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
      "--floating-position",
      "centered",
      NULL,
  };
  gchar *out = NULL;
  gchar *err = NULL;
  int exit_code = 1;
  run_shaula(argv, &out, &err, &exit_code);
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

  const char *sizes[] = {"Small", "Medium", "Large", NULL};
  state.size_combo = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(sizes));
  gtk_drop_down_set_selected(
      state.size_combo, shaula_settings_size_preset_for_config(&state.config));
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Preview size", GTK_WIDGET(state.size_combo)));

  const char *positions[] = {"Centered", "Top Left", "Top Right", NULL};
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

  GtkWidget *niri_title = gtk_label_new("Niri integration");
  gtk_widget_add_css_class(niri_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(niri_title), 0.0);
  gtk_box_append(GTK_BOX(box), niri_title);

  GtkWidget *niri_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(niri_card, "settings-row");
  gtk_widget_set_margin_start(niri_card, 16);
  gtk_widget_set_margin_end(niri_card, 16);

  state.niri_detected_label = gtk_label_new("checking...");
  gtk_label_set_xalign(GTK_LABEL(state.niri_detected_label), 0.0);
  gtk_box_append(GTK_BOX(niri_card), GTK_WIDGET(state.niri_detected_label));

  state.niri_shortcuts_status_label = gtk_label_new("checking...");
  gtk_label_set_xalign(GTK_LABEL(state.niri_shortcuts_status_label), 0.0);
  gtk_box_append(GTK_BOX(niri_card), GTK_WIDGET(state.niri_shortcuts_status_label));

  state.niri_shortcuts_detail_label = gtk_label_new("");
  gtk_widget_add_css_class(state.niri_shortcuts_detail_label, "description");
  gtk_label_set_xalign(GTK_LABEL(state.niri_shortcuts_detail_label), 0.0);
  gtk_label_set_wrap(GTK_LABEL(state.niri_shortcuts_detail_label), TRUE);
  gtk_box_append(GTK_BOX(niri_card), state.niri_shortcuts_detail_label);

  GtkWidget *niri_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  state.niri_install_button =
      gtk_button_new_with_label("Install / Update Shortcuts");
  state.niri_remove_button = gtk_button_new_with_label("Remove Shortcuts");
  state.niri_open_config_button =
      gtk_button_new_with_label("Open Niri Config");
  state.niri_recheck_button = gtk_button_new_with_label("Recheck");
  gtk_box_append(GTK_BOX(niri_buttons), state.niri_install_button);
  gtk_box_append(GTK_BOX(niri_buttons), state.niri_remove_button);
  gtk_box_append(GTK_BOX(niri_buttons), state.niri_open_config_button);
  gtk_box_append(GTK_BOX(niri_buttons), state.niri_recheck_button);
  gtk_box_append(GTK_BOX(niri_card), niri_buttons);

  gtk_box_append(GTK_BOX(box), niri_card);

  GtkWidget *advanced_title = gtk_label_new("Advanced");
  gtk_widget_add_css_class(advanced_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(advanced_title), 0.0);
  gtk_box_append(GTK_BOX(box), advanced_title);

  state.path_label =
      gtk_label_new(state.config_path != NULL ? state.config_path : "unknown");
  gtk_label_set_xalign(GTK_LABEL(state.path_label), 1.0);
  gtk_label_set_ellipsize(GTK_LABEL(state.path_label), PANGO_ELLIPSIZE_START);
  gtk_box_append(GTK_BOX(box), labeled_row("Config path", state.path_label));

  state.niri_config_path_label = gtk_label_new("unknown");
  gtk_label_set_xalign(GTK_LABEL(state.niri_config_path_label), 1.0);
  gtk_label_set_ellipsize(GTK_LABEL(state.niri_config_path_label),
                          PANGO_ELLIPSIZE_START);
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Niri config path",
                             GTK_WIDGET(state.niri_config_path_label)));

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
  g_signal_connect(state.niri_install_button, "clicked",
                   G_CALLBACK(on_niri_install_clicked), NULL);
  g_signal_connect(state.niri_remove_button, "clicked",
                   G_CALLBACK(on_niri_remove_clicked), NULL);
  g_signal_connect(state.niri_open_config_button, "clicked",
                   G_CALLBACK(on_niri_open_config_clicked), NULL);
  g_signal_connect(state.niri_recheck_button, "clicked",
                   G_CALLBACK(on_niri_recheck_clicked), NULL);

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
  run_shaula(argv, &out, &err, &exit_code);
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
  gtk_window_set_title(GTK_WINDOW(state.window), "Shaula Settings");
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

  refresh_niri_status();

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
  state.shaula_bin =
      g_strdup(argc >= 2 && argv[1][0] != '\0' ? argv[1] : "shaula");
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
  g_free(state.niri_config_path);
  shaula_settings_config_clear(&state.config);
  return rc > 255 ? 255 : rc;
}
