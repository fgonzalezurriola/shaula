#include "preview_icons.h"

#include <linux/limits.h>
#include <string.h>

/* Register preview icon theme parents before toolbar widgets request icon
 * names. Installed builds resolve via the prefix; local ./dev runs can resolve
 * directly from the source tree.
 */
static void add_icon_search_path(GtkIconTheme *theme, const char *path) {
  if (path == NULL || path[0] == '\0')
    return;
  if (g_file_test(path, G_FILE_TEST_IS_DIR))
    gtk_icon_theme_add_search_path(theme, path);
}

static void remember_icon_root(ShaulaPreviewState *state, const char *path) {
  if (path == NULL || path[0] == '\0' || state->icon_root_count >= 2)
    return;
  if (!g_file_test(path, G_FILE_TEST_IS_DIR))
    return;
  state->icon_roots[state->icon_root_count++] = g_strdup(path);
}

static gboolean dark_theme_preferred(void) {
  GtkSettings *settings = gtk_settings_get_default();
  if (settings == NULL)
    return TRUE;

  gchar *theme = NULL;
  g_object_get(settings, "gtk-theme-name", &theme, NULL);
  if (theme != NULL) {
    gboolean dark =
        g_str_has_suffix(theme, "-dark") || g_str_has_suffix(theme, "-Dark") ||
        g_strrstr(theme, "Nord") != NULL || g_strrstr(theme, "nord") != NULL;
    g_free(theme);
    if (dark)
      return TRUE;
  }

  gint prefer_dark = 0;
  g_object_get(settings, "gtk-application-prefer-dark-theme", &prefer_dark,
               NULL);
  return prefer_dark != 0;
}

void shaula_preview_register_custom_icons(ShaulaPreviewState *state) {
  GdkDisplay *display = gdk_display_get_default();
  if (display == NULL)
    return;

  GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
  if (theme == NULL)
    return;

  char *exe = g_file_read_link("/proc/self/exe", NULL);
  if (exe != NULL) {
    char *exe_dir = g_path_get_dirname(exe);
    char icon_dir[PATH_MAX * 2];
    snprintf(icon_dir, sizeof(icon_dir), "%s/../share/icons", exe_dir);
    add_icon_search_path(theme, icon_dir);
    remember_icon_root(state, icon_dir);
    g_free(exe_dir);
    g_free(exe);
  }

  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    char source_icon_dir[PATH_MAX * 2];
    snprintf(source_icon_dir, sizeof(source_icon_dir),
             "%s/src/preview/icons", cwd);
    add_icon_search_path(theme, source_icon_dir);
    remember_icon_root(state, source_icon_dir);
  }
}

static char *find_toolbar_icon_file(ShaulaPreviewState *state,
                                    const char *icon_name) {
  for (int i = 0; i < state->icon_root_count; i++) {
    char *path = g_strdup_printf("%s/hicolor/scalable/actions/%s.svg",
                                 state->icon_roots[i], icon_name);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
      return path;
    g_free(path);
  }
  return NULL;
}

static GtkWidget *make_recolored_svg_icon(const char *path) {
  gchar *contents = NULL;
  gsize len = 0;
  if (!g_file_get_contents(path, &contents, &len, NULL))
    return NULL;

  const char *fg = dark_theme_preferred() ? "#ECEFF4" : "#2E3436";
  gchar *recolored = contents;
  if (g_strrstr(contents, "currentColor") != NULL) {
    GString *svg = g_string_new(contents);
    g_string_replace(svg, "currentColor", fg, 0);
    recolored = g_string_free(svg, FALSE);
    g_free(contents);
  }

  GInputStream *stream =
      g_memory_input_stream_new_from_data(recolored, -1, g_free);
  GdkPixbuf *pixbuf =
      gdk_pixbuf_new_from_stream_at_scale(stream, 16, 16, TRUE, NULL, NULL);
  g_object_unref(stream);
  if (pixbuf == NULL)
    return NULL;

  GtkWidget *icon = gtk_image_new_from_pixbuf(pixbuf);
  g_object_unref(pixbuf);
  return icon;
}

GtkWidget *shaula_preview_make_toolbar_icon(ShaulaPreviewState *state,
                                            const char *icon_name) {
  char *path = find_toolbar_icon_file(state, icon_name);
  GtkWidget *icon = path != NULL ? make_recolored_svg_icon(path) : NULL;
  g_free(path);
  if (icon == NULL)
    icon = gtk_image_new_from_icon_name(icon_name);
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
  gtk_widget_set_size_request(icon, 16, 16);
  gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
  return icon;
}
