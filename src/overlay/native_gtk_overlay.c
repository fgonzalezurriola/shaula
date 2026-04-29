#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk4-layer-shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    TOOLBAR_W = 312,
    TOOLBAR_H = 52,
    PADDING = 12,
    HANDLE_CLEARANCE = 18,
    BADGE_CLEARANCE = 30,
    JITTER = 6,
    HANDLE_SIZE = 10,
};

typedef struct {
    int x;
    int y;
    int width;
    int height;
} ShaulaRect;

typedef struct {
    int x;
    int y;
} ShaulaPoint;

typedef struct {
    int width;
    int height;
} ShaulaAspect;

typedef enum {
    DRAG_NONE,
    DRAG_CREATE,
    DRAG_MOVE,
    DRAG_TOOLBAR,
} ShaulaDragMode;

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *area;
    GdkPixbuf *background;
    gboolean has_selection;
    ShaulaRect selection;
    gboolean has_toolbar;
    ShaulaPoint toolbar;
    gboolean has_aspect;
    ShaulaAspect aspect;
    ShaulaDragMode drag_mode;
    ShaulaPoint drag_start;
    ShaulaRect drag_origin;
} ShaulaOverlayState;

static ShaulaOverlayState state;

static int clamp_int(int value, int low, int high) {
    if (high < low) return low;
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static ShaulaPoint output_size(void) {
    if (state.area != NULL) {
        return (ShaulaPoint){
            .x = MAX(1, gtk_widget_get_width(state.area)),
            .y = MAX(1, gtk_widget_get_height(state.area)),
        };
    }
    return (ShaulaPoint){ .x = 1920, .y = 1080 };
}

static gboolean point_in_selection(ShaulaRect selection, ShaulaPoint point) {
    return point.x >= selection.x &&
        point.x <= selection.x + selection.width &&
        point.y >= selection.y &&
        point.y <= selection.y + selection.height;
}

static void queue_draw(void) {
    if (state.area != NULL) gtk_widget_queue_draw(state.area);
}

static void apply_aspect(int *width, int *height, ShaulaAspect ratio) {
    if (*width == 0 && *height > 0) {
        *width = MAX(1, (*height * ratio.width) / ratio.height);
    } else if (*height == 0 && *width > 0) {
        *height = MAX(1, (*width * ratio.height) / ratio.width);
    } else if (*width > 0 && *height > 0) {
        if ((*width * ratio.height) >= (*height * ratio.width)) {
            *width = MAX(1, (*height * ratio.width) / ratio.height);
        } else {
            *height = MAX(1, (*width * ratio.height) / ratio.width);
        }
    }
}

static gboolean clamp_selection(ShaulaRect input, ShaulaPoint bounds, ShaulaRect *out) {
    int left = clamp_int(input.x, 0, MAX(0, bounds.x - 1));
    int top = clamp_int(input.y, 0, MAX(0, bounds.y - 1));
    int right = clamp_int(input.x + input.width, 1, bounds.x);
    int bottom = clamp_int(input.y + input.height, 1, bounds.y);
    if (right <= left) right = MIN(bounds.x, left + 1);
    if (bottom <= top) bottom = MIN(bounds.y, top + 1);
    if (right <= left || bottom <= top) return FALSE;
    *out = (ShaulaRect){ .x = left, .y = top, .width = right - left, .height = bottom - top };
    return TRUE;
}

static gboolean geometry_from_points(ShaulaPoint anchor, ShaulaPoint point, ShaulaPoint bounds, ShaulaRect *out) {
    int width = abs(point.x - anchor.x);
    int height = abs(point.y - anchor.y);
    if (state.has_aspect) apply_aspect(&width, &height, state.aspect);
    if (width <= 0 || height <= 0) return FALSE;
    int x = point.x >= anchor.x ? anchor.x : anchor.x - width;
    int y = point.y >= anchor.y ? anchor.y : anchor.y - height;
    return clamp_selection((ShaulaRect){ .x = x, .y = y, .width = width, .height = height }, bounds, out);
}

static gboolean move_selection(ShaulaRect selection, int dx, int dy, ShaulaPoint bounds, ShaulaRect *out) {
    *out = (ShaulaRect){
        .x = clamp_int(selection.x + dx, 0, MAX(0, bounds.x - selection.width)),
        .y = clamp_int(selection.y + dy, 0, MAX(0, bounds.y - selection.height)),
        .width = selection.width,
        .height = selection.height,
    };
    return TRUE;
}

static void update_toolbar(void) {
    if (!state.has_selection) return;
    ShaulaRect selection = state.selection;
    ShaulaPoint bounds = output_size();
    int min_x = PADDING;
    int max_x = bounds.x - PADDING - TOOLBAR_W;
    int min_y = PADDING;
    int max_y = bounds.y - PADDING - TOOLBAR_H;
    int centered_x = selection.x + (selection.width - TOOLBAR_W) / 2;
    int below_y = selection.y + selection.height + HANDLE_CLEARANCE;
    int above_y = selection.y - TOOLBAR_H - HANDLE_CLEARANCE - BADGE_CLEARANCE;
    ShaulaPoint candidate;

    if (below_y <= max_y) {
        candidate = (ShaulaPoint){ .x = clamp_int(centered_x, min_x, max_x), .y = below_y };
    } else if (above_y >= min_y) {
        candidate = (ShaulaPoint){ .x = clamp_int(centered_x, min_x, max_x), .y = above_y };
    } else {
        candidate = (ShaulaPoint){
            .x = clamp_int((bounds.x - TOOLBAR_W) / 2, min_x, max_x),
            .y = bounds.y - (selection.y + selection.height) >= selection.y ? max_y : min_y,
        };
    }

    candidate.x = clamp_int(candidate.x, min_x, max_x);
    candidate.y = clamp_int(candidate.y, min_y, max_y);
    if (state.has_toolbar &&
        abs(state.toolbar.x - candidate.x) <= JITTER &&
        abs(state.toolbar.y - candidate.y) <= JITTER) {
        return;
    }
    state.toolbar = candidate;
    state.has_toolbar = TRUE;
}

static gboolean toolbar_capture_hit(ShaulaPoint point) {
    if (!state.has_toolbar) return FALSE;
    ShaulaPoint t = state.toolbar;
    return point.x >= t.x + 174 && point.x <= t.x + 248 &&
        point.y >= t.y + 11 && point.y <= t.y + 41;
}

static gboolean toolbar_cancel_hit(ShaulaPoint point) {
    if (!state.has_toolbar) return FALSE;
    ShaulaPoint t = state.toolbar;
    return point.x >= t.x + 258 && point.x <= t.x + 300 &&
        point.y >= t.y + 11 && point.y <= t.y + 41;
}

static void draw_background(cairo_t *cr, int width, int height) {
    if (state.background == NULL) return;
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(state.background, width, height, GDK_INTERP_BILINEAR);
    if (scaled == NULL) return;
    gdk_cairo_set_source_pixbuf(cr, scaled, 0, 0);
    cairo_paint(cr);
    g_object_unref(scaled);
}

static void draw_pill(cairo_t *cr, int x, int y, int width, int height, const char *label, double r, double g, double b, double a) {
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    cairo_move_to(cr, x + 10, y + 19);
    cairo_show_text(cr, label);
}

static void draw_handles(cairo_t *cr, ShaulaRect s) {
    ShaulaPoint points[8] = {
        { s.x, s.y },
        { s.x + s.width / 2, s.y },
        { s.x + s.width, s.y },
        { s.x + s.width, s.y + s.height / 2 },
        { s.x + s.width, s.y + s.height },
        { s.x + s.width / 2, s.y + s.height },
        { s.x, s.y + s.height },
        { s.x, s.y + s.height / 2 },
    };
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    for (int i = 0; i < 8; i += 1) {
        cairo_rectangle(cr, points[i].x - HANDLE_SIZE / 2, points[i].y - HANDLE_SIZE / 2, HANDLE_SIZE, HANDLE_SIZE);
        cairo_fill(cr);
    }
}

static void draw_badge(cairo_t *cr, ShaulaRect s) {
    int y = MAX(8, s.y - 30);
    char label[32];
    snprintf(label, sizeof(label), "%d x %d", s.width, s.height);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.72);
    cairo_rectangle(cr, s.x, y, 98, 24);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    cairo_move_to(cr, s.x + 8, y + 16);
    cairo_show_text(cr, label);
}

static void draw_toolbar(cairo_t *cr, ShaulaPoint t, gboolean enabled) {
    cairo_set_source_rgba(cr, 0.03, 0.035, 0.04, 0.90);
    cairo_rectangle(cr, t.x, t.y, TOOLBAR_W, TOOLBAR_H);
    cairo_fill(cr);
    draw_pill(cr, t.x + 12, t.y + 11, 92, 30, state.has_aspect ? "Aspect" : "Free", 0.18, 0.19, 0.22, 1);
    draw_pill(cr, t.x + 112, t.y + 11, 52, 30, "Area", 0.24, 0.37, 0.58, 1);
    if (enabled) {
        draw_pill(cr, t.x + 174, t.y + 11, 74, 30, "Capture", 0.15, 0.52, 0.35, 1);
    } else {
        draw_pill(cr, t.x + 174, t.y + 11, 74, 30, "Capture", 0.20, 0.24, 0.22, 0.9);
    }
    draw_pill(cr, t.x + 258, t.y + 11, 42, 30, "Esc", 0.32, 0.19, 0.19, 1);
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    (void)area;
    (void)data;
    draw_background(cr, width, height);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.48);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    if (state.has_selection) {
        ShaulaRect s = state.selection;
        if (state.background != NULL) {
            cairo_save(cr);
            cairo_rectangle(cr, s.x, s.y, s.width, s.height);
            cairo_clip(cr);
            draw_background(cr, width, height);
            cairo_restore(cr);
        }
        cairo_set_source_rgba(cr, 1, 1, 1, 0.96);
        cairo_set_line_width(cr, 2);
        cairo_rectangle(cr, s.x + 0.5, s.y + 0.5, s.width, s.height);
        cairo_stroke(cr);
        draw_handles(cr, s);
        draw_badge(cr, s);
    }

    draw_toolbar(cr, state.has_toolbar ? state.toolbar : (ShaulaPoint){ .x = PADDING, .y = PADDING }, state.has_selection);
}

static void confirm(void) {
    if (!state.has_selection || state.selection.width <= 0 || state.selection.height <= 0) return;
    printf("{\"status\":\"ok\",\"action\":\"capture\",\"geometry\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},\"error\":null}\n",
        state.selection.x, state.selection.y, state.selection.width, state.selection.height);
    fflush(stdout);
    if (state.app != NULL) g_application_quit(G_APPLICATION(state.app));
}

static void cancel(void) {
    printf("{\"status\":\"cancel\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}\n");
    fflush(stdout);
    if (state.app != NULL) g_application_quit(G_APPLICATION(state.app));
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data) {
    (void)gesture;
    (void)data;
    ShaulaPoint p = { .x = (int)x, .y = (int)y };
    if (state.has_selection && toolbar_capture_hit(p)) {
        state.drag_mode = DRAG_TOOLBAR;
        return;
    }
    if (toolbar_cancel_hit(p)) {
        state.drag_mode = DRAG_TOOLBAR;
        return;
    }
    state.drag_start = p;
    state.drag_origin = state.selection;
    if (state.has_selection && point_in_selection(state.selection, p)) {
        state.drag_mode = DRAG_MOVE;
    } else {
        state.drag_mode = DRAG_CREATE;
        state.has_selection = FALSE;
    }
    queue_draw();
}

static void on_drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
    (void)gesture;
    (void)data;
    ShaulaPoint bounds = output_size();
    ShaulaPoint p = { .x = state.drag_start.x + (int)dx, .y = state.drag_start.y + (int)dy };
    ShaulaRect next;
    if (state.drag_mode == DRAG_CREATE) {
        state.has_selection = geometry_from_points(state.drag_start, p, bounds, &next);
        if (state.has_selection) state.selection = next;
    } else if (state.drag_mode == DRAG_MOVE) {
        if (move_selection(state.drag_origin, (int)dx, (int)dy, bounds, &next)) {
            state.selection = next;
            state.has_selection = TRUE;
        }
    }
    update_toolbar();
    queue_draw();
}

static void on_drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data) {
    on_drag_update(gesture, dx, dy, data);
    state.drag_mode = DRAG_NONE;
    queue_draw();
}

static void on_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)data;
    ShaulaPoint p = { .x = (int)x, .y = (int)y };
    if (state.has_selection && toolbar_capture_hit(p)) {
        confirm();
    } else if (toolbar_cancel_hit(p)) {
        cancel();
    }
}

static gboolean on_key(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType modifiers, gpointer data) {
    (void)controller;
    (void)keycode;
    (void)data;
    if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_q) {
        cancel();
        return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        confirm();
        return TRUE;
    }

    int dx = 0;
    int dy = 0;
    if (keyval == GDK_KEY_Left) dx = -1;
    if (keyval == GDK_KEY_Right) dx = 1;
    if (keyval == GDK_KEY_Up) dy = -1;
    if (keyval == GDK_KEY_Down) dy = 1;
    if ((dx != 0 || dy != 0) && state.has_selection) {
        int step = (modifiers & GDK_SHIFT_MASK) != 0 ? 10 : 1;
        ShaulaRect next;
        if (move_selection(state.selection, dx * step, dy * step, output_size(), &next)) {
            state.selection = next;
            update_toolbar();
            queue_draw();
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean load_aspect(void) {
    const char *raw = getenv("SHAULA_OVERLAY_ASPECT");
    if (raw == NULL || raw[0] == '\0') return FALSE;
    int w = 0;
    int h = 0;
    if (sscanf(raw, "%d:%d", &w, &h) != 2 || w <= 0 || h <= 0) return FALSE;
    state.aspect = (ShaulaAspect){ .width = w, .height = h };
    state.has_aspect = TRUE;
    return TRUE;
}

static void load_background(void) {
    const char *path = getenv("SHAULA_OVERLAY_BACKGROUND_PATH");
    if (path == NULL || path[0] == '\0') return;
    state.background = gdk_pixbuf_new_from_file(path, NULL);
}

static GdkMonitor *monitor_for_output(void) {
    const char *name = getenv("SHAULA_OVERLAY_OUTPUT_NAME");
    if (name == NULL || name[0] == '\0') return NULL;
    GdkDisplay *display = gdk_display_get_default();
    if (display == NULL) return NULL;
    GListModel *monitors = gdk_display_get_monitors(display);
    guint count = g_list_model_get_n_items(monitors);
    for (guint i = 0; i < count; i += 1) {
        GObject *object = g_list_model_get_item(monitors, i);
        if (object == NULL) continue;
        GdkMonitor *monitor = GDK_MONITOR(object);
        const char *connector = gdk_monitor_get_connector(monitor);
        if (connector != NULL && strcmp(connector, name) == 0) {
            return monitor;
        }
        g_object_unref(object);
    }
    return NULL;
}

static ShaulaPoint initial_surface_size(void) {
    GdkMonitor *monitor = monitor_for_output();
    if (monitor != NULL) {
        GdkRectangle rect;
        gdk_monitor_get_geometry(monitor, &rect);
        g_object_unref(monitor);
        return (ShaulaPoint){ .x = MAX(1, rect.width), .y = MAX(1, rect.height) };
    }
    return (ShaulaPoint){ .x = 1920, .y = 1080 };
}

static void on_activate(GtkApplication *app, gpointer data) {
    (void)data;
    GtkWidget *window = gtk_application_window_new(app);
    state.window = window;

    gtk_window_set_title(GTK_WINDOW(window), "shaula-overlay");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_namespace(GTK_WINDOW(window), "shaula-overlay");
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    GdkMonitor *monitor = monitor_for_output();
    if (monitor != NULL) {
        gtk_layer_set_monitor(GTK_WINDOW(window), monitor);
        g_object_unref(monitor);
    }
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(window), -1);

    ShaulaPoint size = initial_surface_size();
    gtk_window_set_default_size(GTK_WINDOW(window), size.x, size.y);

    GtkWidget *area = gtk_drawing_area_new();
    state.area = area;
    gtk_widget_set_focusable(area, TRUE);
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), size.x);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), size.y);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);
    gtk_window_set_child(GTK_WINDOW(window), area);

    GtkGesture *drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), NULL);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(window, keys);

    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(area);
}

int shaula_native_gtk_overlay_run(void) {
    const char *strategy = getenv("SHAULA_OVERLAY_HELPER_STRATEGY");
    if (strategy != NULL && strategy[0] != '\0' &&
        strcmp(strategy, "auto") != 0 &&
        strcmp(strategy, "gtk4-layer-shell") != 0) {
        printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"requested overlay strategy is not wired in the native helper\"}}\n");
        fflush(stdout);
        return 36;
    }
    if (getenv("SHAULA_OVERLAY_HELPER_FORCE_UNAVAILABLE") != NULL) {
        printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"forced unavailable\"}}\n");
        fflush(stdout);
        return 36;
    }
    if (getenv("SHAULA_OVERLAY_HELPER_FORCE_TIMEOUT") != NULL) {
        sleep(10);
        return 37;
    }
    if (getenv("SHAULA_OVERLAY_HELPER_PROBE") != NULL) {
        gtk_init();
        if (gtk_layer_is_supported()) {
            printf("{\"status\":\"ok\",\"action\":\"cancel\",\"geometry\":null,\"error\":null}\n");
            fflush(stdout);
            return 0;
        }
        printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"gtk4-layer-shell is not supported by this compositor\"}}\n");
        fflush(stdout);
        return 36;
    }

    gtk_init();
    memset(&state, 0, sizeof(state));
    state.toolbar = (ShaulaPoint){ .x = PADDING, .y = PADDING };
    state.has_toolbar = TRUE;
    load_aspect();
    load_background();

    if (!gtk_layer_is_supported()) {
        printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"gtk4-layer-shell is not supported by this compositor\"}}\n");
        fflush(stdout);
        if (state.background != NULL) g_object_unref(state.background);
        return 36;
    }

    GtkApplication *app = gtk_application_new("dev.shaula.overlay", G_APPLICATION_DEFAULT_FLAGS);
    if (app == NULL) {
        printf("{\"status\":\"error\",\"action\":\"cancel\",\"geometry\":null,\"error\":{\"code\":\"ERR_OVERLAY_UNAVAILABLE\",\"message\":\"gtk application could not be created\"}}\n");
        fflush(stdout);
        if (state.background != NULL) g_object_unref(state.background);
        return 36;
    }
    state.app = app;
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int rc = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    if (state.background != NULL) g_object_unref(state.background);
    return rc > 255 ? 255 : rc;
}

#ifdef SHAULA_OVERLAY_STANDALONE
int main(void) {
    return shaula_native_gtk_overlay_run();
}
#endif
