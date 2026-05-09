#include <gtk/gtk.h>

static void on_activate(GtkApplication *app, gpointer data) {
  GtkWidget *window = gtk_application_window_new(app);
  const char *items[] = {"One", "Two", NULL};
  GtkWidget *drop = gtk_drop_down_new_from_strings(items);
  gtk_window_set_child(GTK_WINDOW(window), drop);
  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
  GtkApplication *app = gtk_application_new("org.test", 0);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  return g_application_run(G_APPLICATION(app), 0, NULL);
}
