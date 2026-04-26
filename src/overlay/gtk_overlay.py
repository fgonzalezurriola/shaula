import json
import os
import sys

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gtk4LayerShell", "1.0")

from gi.repository import Gdk, Gtk, Gtk4LayerShell


TOOLBAR_W = 312
TOOLBAR_H = 52
PADDING = 12
HANDLE_CLEARANCE = 18
BADGE_CLEARANCE = 30
JITTER = 6


class Overlay(Gtk.Application):
    def __init__(self):
        super().__init__(application_id="dev.shaula.overlay")
        self.window = None
        self.area = None
        self.start = None
        self.pointer = None
        self.selection = None
        self.toolbar = load_toolbar_position()
        self.dragging = False

    def do_activate(self):
        self.window = Gtk.ApplicationWindow(application=self)
        self.window.set_title("shaula-overlay")
        self.window.set_decorated(False)
        self.window.set_resizable(False)
        self.window.set_can_focus(True)

        Gtk4LayerShell.init_for_window(self.window)
        Gtk4LayerShell.set_namespace(self.window, "shaula-overlay")
        Gtk4LayerShell.set_layer(self.window, Gtk4LayerShell.Layer.OVERLAY)
        Gtk4LayerShell.set_keyboard_mode(self.window, Gtk4LayerShell.KeyboardMode.EXCLUSIVE)
        Gtk4LayerShell.set_anchor(self.window, Gtk4LayerShell.Edge.TOP, True)
        Gtk4LayerShell.set_anchor(self.window, Gtk4LayerShell.Edge.BOTTOM, True)
        Gtk4LayerShell.set_anchor(self.window, Gtk4LayerShell.Edge.LEFT, True)
        Gtk4LayerShell.set_anchor(self.window, Gtk4LayerShell.Edge.RIGHT, True)
        Gtk4LayerShell.set_exclusive_zone(self.window, -1)

        self.area = Gtk.DrawingArea()
        self.area.set_focusable(True)
        self.area.set_draw_func(self.draw)
        self.window.set_child(self.area)

        drag = Gtk.GestureDrag()
        drag.connect("drag-begin", self.on_drag_begin)
        drag.connect("drag-update", self.on_drag_update)
        drag.connect("drag-end", self.on_drag_end)
        self.area.add_controller(drag)

        click = Gtk.GestureClick()
        click.connect("pressed", self.on_click)
        self.area.add_controller(click)

        keys = Gtk.EventControllerKey()
        keys.connect("key-pressed", self.on_key)
        self.window.add_controller(keys)

        self.window.present()
        self.area.grab_focus()

    def output_size(self):
        return max(1, self.area.get_width()), max(1, self.area.get_height())

    def on_drag_begin(self, _gesture, x, y):
        self.start = (int(x), int(y))
        self.pointer = self.start
        self.dragging = True
        self.update_selection()

    def on_drag_update(self, _gesture, dx, dy):
        if self.start is None:
            return
        self.pointer = (int(self.start[0] + dx), int(self.start[1] + dy))
        self.update_selection()

    def on_drag_end(self, _gesture, dx, dy):
        if self.start is None:
            return
        self.pointer = (int(self.start[0] + dx), int(self.start[1] + dy))
        self.dragging = False
        self.update_selection()

    def on_click(self, _gesture, _presses, x, y):
        if self.selection and toolbar_capture_hit(self.toolbar, int(x), int(y)):
            self.confirm()
            return
        if toolbar_cancel_hit(self.toolbar, int(x), int(y)):
            self.cancel()

    def on_key(self, _controller, keyval, _keycode, _state):
        if keyval in (Gdk.KEY_Escape, Gdk.KEY_q):
            self.cancel()
            return True
        if keyval in (Gdk.KEY_Return, Gdk.KEY_KP_Enter):
            self.confirm()
            return True
        return False

    def update_selection(self):
        if self.start is None or self.pointer is None:
            return
        x1, y1 = self.start
        x2, y2 = self.pointer
        x = min(x1, x2)
        y = min(y1, y2)
        w = abs(x2 - x1)
        h = abs(y2 - y1)
        if w > 0 and h > 0:
            self.selection = (x, y, w, h)
            self.toolbar = compute_toolbar(self.output_size(), self.selection, self.toolbar)
        self.area.queue_draw()

    def confirm(self):
        if not self.selection:
            return
        x, y, w, h = self.selection
        print(json.dumps({
            "status": "ok",
            "action": "capture",
            "geometry": {"x": x, "y": y, "width": w, "height": h},
            "error": None,
        }), flush=True)
        self.quit()

    def cancel(self):
        print(json.dumps({
            "status": "cancel",
            "action": "cancel",
            "geometry": None,
            "error": None,
        }), flush=True)
        self.quit()

    def draw(self, _area, cr, width, height):
        cr.set_source_rgba(0, 0, 0, 0.48)
        cr.rectangle(0, 0, width, height)
        cr.fill()

        if self.selection:
            x, y, w, h = self.selection
            cr.set_operator(1)
            cr.set_source_rgba(0, 0, 0, 0)
            cr.rectangle(x, y, w, h)
            cr.fill()
            cr.set_operator(2)

            cr.set_source_rgba(1, 1, 1, 0.95)
            cr.set_line_width(2)
            cr.rectangle(x + 0.5, y + 0.5, w, h)
            cr.stroke()
            draw_handles(cr, x, y, w, h)
            draw_badge(cr, x, y, w, h)

        draw_toolbar(cr, self.toolbar, enabled=self.selection is not None)


def load_toolbar_position():
    path = os.environ.get("SHAULA_TOOLBAR_POSITION_FILE")
    candidates = []
    if path:
        candidates.append(path)
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if runtime:
        candidates.append(os.path.join(runtime, "shaula", "overlay", "toolbar-position.v1"))
    candidates.append("/tmp/shaula/overlay/toolbar-position.v1")

    for candidate in candidates:
        try:
            raw = open(candidate, "r", encoding="utf-8").read().strip()
            left, right = raw.split("|", 1)
            return (int(left), int(right))
        except Exception:
            pass
    return None


def compute_toolbar(output, selection, previous):
    out_w, out_h = output
    x, y, w, h = selection
    min_x = PADDING
    max_x = out_w - PADDING - TOOLBAR_W
    min_y = PADDING
    max_y = out_h - PADDING - TOOLBAR_H
    centered_x = x + (w - TOOLBAR_W) // 2
    below_y = y + h + HANDLE_CLEARANCE
    above_y = y - TOOLBAR_H - HANDLE_CLEARANCE - BADGE_CLEARANCE

    if below_y <= max_y:
        candidate = (clamp(centered_x, min_x, max_x), below_y)
    elif above_y >= min_y:
        candidate = (clamp(centered_x, min_x, max_x), above_y)
    else:
        room_below = out_h - (y + h)
        room_above = y
        edge_y = out_h - PADDING - TOOLBAR_H if room_below >= room_above else PADDING
        candidate = (clamp((out_w - TOOLBAR_W) // 2, min_x, max_x), edge_y)

    candidate = (clamp(candidate[0], min_x, max_x), clamp(candidate[1], min_y, max_y))
    if previous and abs(previous[0] - candidate[0]) <= JITTER and abs(previous[1] - candidate[1]) <= JITTER:
        return previous
    return candidate


def clamp(value, low, high):
    if high < low:
        return low
    return max(low, min(value, high))


def toolbar_capture_hit(toolbar, x, y):
    if toolbar is None:
        return False
    tx, ty = toolbar
    return tx + 174 <= x <= tx + 248 and ty + 11 <= y <= ty + 41


def toolbar_cancel_hit(toolbar, x, y):
    if toolbar is None:
        return False
    tx, ty = toolbar
    return tx + 258 <= x <= tx + 300 and ty + 11 <= y <= ty + 41


def draw_handles(cr, x, y, w, h):
    cr.set_source_rgba(1, 1, 1, 1)
    for hx, hy in ((x, y), (x + w, y), (x, y + h), (x + w, y + h)):
        cr.rectangle(hx - 5, hy - 5, 10, 10)
        cr.fill()


def draw_badge(cr, x, y, w, h):
    label = f"{w} x {h}"
    by = max(8, y - 30)
    rounded_rect(cr, x, by, 98, 24, 5)
    cr.set_source_rgba(0, 0, 0, 0.72)
    cr.fill()
    cr.set_source_rgba(1, 1, 1, 0.95)
    cr.select_font_face("Sans")
    cr.set_font_size(13)
    cr.move_to(x + 8, by + 16)
    cr.show_text(label)


def draw_toolbar(cr, toolbar, enabled):
    if toolbar is None:
        toolbar = (PADDING, PADDING)
    x, y = toolbar
    rounded_rect(cr, x, y, TOOLBAR_W, TOOLBAR_H, 8)
    cr.set_source_rgba(0.03, 0.035, 0.04, 0.90)
    cr.fill()
    cr.set_source_rgba(1, 1, 1, 0.18)
    cr.set_line_width(1)
    rounded_rect(cr, x + 0.5, y + 0.5, TOOLBAR_W - 1, TOOLBAR_H - 1, 8)
    cr.stroke()

    draw_pill(cr, x + 12, y + 11, 92, 30, "Aspect", (0.18, 0.19, 0.22, 1))
    draw_pill(cr, x + 112, y + 11, 52, 30, "Area", (0.24, 0.37, 0.58, 1))
    draw_pill(cr, x + 174, y + 11, 74, 30, "Capture", (0.15, 0.52, 0.35, 1) if enabled else (0.20, 0.24, 0.22, 0.9))
    draw_pill(cr, x + 258, y + 11, 42, 30, "Esc", (0.32, 0.19, 0.19, 1))


def draw_pill(cr, x, y, w, h, label, color):
    rounded_rect(cr, x, y, w, h, 5)
    cr.set_source_rgba(*color)
    cr.fill()
    cr.set_source_rgba(1, 1, 1, 0.95)
    cr.select_font_face("Sans")
    cr.set_font_size(13)
    cr.move_to(x + 10, y + 19)
    cr.show_text(label)


def rounded_rect(cr, x, y, w, h, r):
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -1.5708, 0)
    cr.arc(x + w - r, y + h - r, r, 0, 1.5708)
    cr.arc(x + r, y + h - r, r, 1.5708, 3.1416)
    cr.arc(x + r, y + r, r, 3.1416, 4.7124)
    cr.close_path()


if __name__ == "__main__":
    if not Gtk4LayerShell.is_supported():
        print(json.dumps({
            "status": "error",
            "action": "cancel",
            "geometry": None,
            "error": {"code": "ERR_OVERLAY_UNAVAILABLE", "message": "gtk4-layer-shell is not supported by this compositor"},
        }), flush=True)
        sys.exit(36)
    app = Overlay()
    sys.exit(app.run(sys.argv))
