#include "preview_icons.h"

#include <linux/limits.h>
#include <stdio.h>
#include <unistd.h>

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

typedef struct {
  char *path;
  char *icon_name;
} ShaulaPreviewIconSource;

static void preview_icon_source_free(gpointer data) {
  ShaulaPreviewIconSource *source = data;
  if (source == NULL)
    return;
  g_free(source->path);
  g_free(source->icon_name);
  g_free(source);
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

/* Render only after the image joins its button hierarchy. At that point GTK
 * exposes the real computed foreground, including suggested-action colors.
 * Loading these stroke-based SVGs as traditional symbolic icons would ignore
 * their fill/stroke attributes and turn open shapes into solid silhouettes.
 */
static gboolean render_svg_with_widget_color(GtkImage *image,
                                             const char *path) {
  gchar *contents = NULL;
  gsize len = 0;
  if (!g_file_get_contents(path, &contents, &len, NULL))
    return FALSE;

  GdkRGBA foreground;
  gtk_style_context_get_color(
      gtk_widget_get_style_context(GTK_WIDGET(image)), &foreground);
  char *foreground_css = gdk_rgba_to_string(&foreground);

  gchar *recolored = contents;
  if (g_strrstr(contents, "currentColor") != NULL) {
    GString *svg = g_string_new(contents);
    g_string_replace(svg, "currentColor", foreground_css, 0);
    recolored = g_string_free(svg, FALSE);
    g_free(contents);
  }
  g_free(foreground_css);

  GInputStream *stream =
      g_memory_input_stream_new_from_data(recolored, -1, g_free);
  GdkPixbuf *pixbuf =
      gdk_pixbuf_new_from_stream_at_scale(stream, 16, 16, TRUE, NULL, NULL);
  g_object_unref(stream);
  if (pixbuf == NULL)
    return FALSE;

  gtk_image_set_from_pixbuf(image, pixbuf);
  g_object_unref(pixbuf);
  return TRUE;
}

static void on_toolbar_icon_map(GtkWidget *widget, gpointer data) {
  (void)data;
  ShaulaPreviewIconSource *source =
      g_object_get_data(G_OBJECT(widget), "shaula-preview-icon-source");
  if (source == NULL)
    return;
  if (!render_svg_with_widget_color(GTK_IMAGE(widget), source->path)) {
    g_warning("Failed to render Preview icon '%s'", source->icon_name);
    gtk_image_set_from_icon_name(GTK_IMAGE(widget), "image-missing-symbolic");
  }
}

GtkWidget *shaula_preview_make_toolbar_icon(ShaulaPreviewState *state,
                                            const char *icon_name) {
  char *path = find_toolbar_icon_file(state, icon_name);
  GtkWidget *icon = NULL;
  if (path != NULL) {
    ShaulaPreviewIconSource *source = g_new0(ShaulaPreviewIconSource, 1);
    source->path = path;
    source->icon_name = g_strdup(icon_name);
    icon = gtk_image_new();
    g_object_set_data_full(G_OBJECT(icon), "shaula-preview-icon-source",
                           source, preview_icon_source_free);
    g_signal_connect(icon, "map", G_CALLBACK(on_toolbar_icon_map), NULL);
  } else {
    g_warning("Failed to resolve Preview icon '%s'", icon_name);
    icon = gtk_image_new_from_icon_name("image-missing-symbolic");
  }
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
  gtk_widget_set_size_request(icon, 16, 16);
  gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
  return icon;
}
