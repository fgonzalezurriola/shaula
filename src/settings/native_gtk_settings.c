#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "settings_config.h"

typedef struct {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *content;
  GtkWidget *error_box;
  GtkWidget *form_box;
  GtkWidget *status_label;
  GtkWidget *path_label;
  GtkDropDown *region_combo;
  GtkDropDown *window_combo;
  GtkDropDown *size_combo;
  GtkDropDown *position_combo;
  GtkSpinButton *width_spin;
  GtkSpinButton *height_spin;
  GtkSwitch *focused_switch;
  GtkSwitch *close_preview_on_save_switch;
  GtkWidget *apply_button;
  GtkWidget *open_button;
  GtkWidget *reset_button;
  char *shaula_bin;
  char *config_path;
  gboolean config_exists;
  gboolean config_invalid;
  ShaulaSettingsConfig config;
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
  if (size == SIZE_CUSTOM) {
    config->width = gtk_spin_button_get_value_as_int(state.width_spin);
    config->height = gtk_spin_button_get_value_as_int(state.height_spin);
  } else {
    shaula_settings_apply_size_preset(config, size);
  }
  PositionPreset position =
      (PositionPreset)gtk_drop_down_get_selected(state.position_combo);
  if (position != POSITION_CUSTOM)
    shaula_settings_apply_position_preset(config, position);
}

static void update_dynamic_controls(void) {
  gboolean floating =
      gtk_drop_down_get_selected(state.window_combo) == WINDOW_FLOATING;
  gboolean custom_size =
      gtk_drop_down_get_selected(state.size_combo) == SIZE_CUSTOM;
  gtk_widget_set_sensitive(GTK_WIDGET(state.size_combo), floating);
  gtk_widget_set_sensitive(GTK_WIDGET(state.position_combo), floating);
  gtk_widget_set_sensitive(GTK_WIDGET(state.width_spin),
                           floating && custom_size);
  gtk_widget_set_sensitive(GTK_WIDGET(state.height_spin),
                           floating && custom_size);
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
  read_controls(&next);

  char *focused = g_strdup(next.focused ? "true" : "false");
  char *close_preview_on_save =
      g_strdup(next.close_preview_on_save ? "true" : "false");
  char *width = g_strdup_printf("%d", next.width);
  char *height = g_strdup_printf("%d", next.height);
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
  gtk_widget_set_visible(state.form_box, TRUE);
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

static GtkWidget *build_form(void) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_add_css_class(box, "settings-form");

  GtkWidget *capture_title = gtk_label_new("Capture");
  gtk_widget_add_css_class(capture_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(capture_title), 0.0);
  gtk_box_append(GTK_BOX(box), capture_title);

  const char *regions[] = {"Live", "Frozen", NULL};
  state.region_combo = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(regions));
  gtk_drop_down_set_selected(state.region_combo, state.config.region_mode);
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Region mode", GTK_WIDGET(state.region_combo)));

  GtkWidget *preview_title = gtk_label_new("Preview Window");
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

  state.width_spin =
      GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(320, 7680, 10));
  gtk_spin_button_set_value(state.width_spin, state.config.width);
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Custom width", GTK_WIDGET(state.width_spin)));

  state.height_spin =
      GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(240, 4320, 10));
  gtk_spin_button_set_value(state.height_spin, state.config.height);
  gtk_box_append(GTK_BOX(box),
                 labeled_row("Custom height", GTK_WIDGET(state.height_spin)));

  const char *positions_all[] = {"Centered", "Top Left", "Top Right", "Custom",
                                 NULL};
  const char *positions_std[] = {"Centered", "Top Left", "Top Right", NULL};
  state.position_combo = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(
      state.config.position_preset == POSITION_CUSTOM ? positions_all
                                                      : positions_std));
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
  gtk_widget_set_tooltip_text(GTK_WIDGET(state.close_preview_on_save_switch),
                              "Close the preview window after a successful "
                              "Ctrl+S save.");
  gtk_box_append(GTK_BOX(box),
                 labeled_description_row(
                     "Close preview on save",
                     "Close the preview window after a successful Ctrl+S save.",
                     GTK_WIDGET(state.close_preview_on_save_switch)));

  GtkWidget *advanced_title = gtk_label_new("Advanced");
  gtk_widget_add_css_class(advanced_title, "section-title");
  gtk_label_set_xalign(GTK_LABEL(advanced_title), 0.0);
  gtk_box_append(GTK_BOX(box), advanced_title);

  state.path_label =
      gtk_label_new(state.config_path != NULL ? state.config_path : "unknown");
  gtk_label_set_xalign(GTK_LABEL(state.path_label), 1.0);
  gtk_label_set_ellipsize(GTK_LABEL(state.path_label), PANGO_ELLIPSIZE_START);
  gtk_box_append(GTK_BOX(box), labeled_row("Config path", state.path_label));

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
  gtk_window_set_default_size(GTK_WINDOW(state.window), 560, 620);
  gtk_window_set_resizable(GTK_WINDOW(state.window), TRUE);

  GtkWidget *titlebar = gtk_header_bar_new();
  gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(titlebar), TRUE);
  gtk_window_set_titlebar(GTK_WINDOW(state.window), titlebar);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(root, "settings-root");
  gtk_window_set_child(GTK_WINDOW(state.window), root);

  GtkWidget *center_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_halign(center_wrapper, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(center_wrapper, 560, -1);
  gtk_box_append(GTK_BOX(root), center_wrapper);

  state.content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_set_margin_top(state.content, 18);
  gtk_widget_set_margin_bottom(state.content, 18);
  gtk_widget_set_margin_start(state.content, 18);
  gtk_widget_set_margin_end(state.content, 18);
  gtk_box_append(GTK_BOX(center_wrapper), state.content);

  state.error_box = build_error_box();
  state.form_box = build_form();
  gtk_box_append(GTK_BOX(state.content), state.error_box);
  gtk_box_append(GTK_BOX(state.content), state.form_box);
  gtk_widget_set_visible(state.error_box, state.config_invalid);
  gtk_widget_set_visible(state.form_box, !state.config_invalid);

  state.status_label = gtk_label_new(
      state.config_invalid ? "Open the config file or reset to defaults."
                           : "Changes apply to future preview windows.");
  gtk_label_set_wrap(GTK_LABEL(state.status_label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(state.status_label), 0.0);
  gtk_box_append(GTK_BOX(state.content), state.status_label);

  GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(buttons, GTK_ALIGN_END);
  state.open_button = gtk_button_new_with_label("Open Config File");
  state.reset_button = gtk_button_new_with_label("Reset to Defaults");
  state.apply_button = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(state.apply_button, "suggested-action");
  gtk_box_append(GTK_BOX(buttons), state.open_button);
  gtk_box_append(GTK_BOX(buttons), state.reset_button);
  gtk_box_append(GTK_BOX(buttons), state.apply_button);
  gtk_box_append(GTK_BOX(state.content), buttons);

  gtk_widget_set_sensitive(state.apply_button, !state.config_invalid);
  g_signal_connect(state.open_button, "clicked", G_CALLBACK(on_open_clicked),
                   NULL);
  g_signal_connect(state.reset_button, "clicked", G_CALLBACK(on_reset_clicked),
                   NULL);
  g_signal_connect(state.apply_button, "clicked", G_CALLBACK(on_save_clicked),
                   NULL);

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
  shaula_settings_config_clear(&state.config);
  return rc > 255 ? 255 : rc;
}
