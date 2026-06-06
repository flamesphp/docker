#!/usr/bin/env python3
"""Per-tab cursor sync + Xlib overlay (arrow, orange flash on click) for Stealth VNC."""

from __future__ import annotations

import json
import os
import subprocess
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Dict, Optional, Tuple

from PIL import Image, ImageDraw
from Xlib import X, display
from Xlib.ext import shape as shape_ext
from Xlib.protocol import event
from Xlib.Xatom import ATOM

DISPLAY = os.environ.get("DISPLAY", ":99")
CDP_URL = "http://127.0.0.1:9223"
POLL_INTERVAL = 0.35

registry: Dict[str, Tuple[float, float]] = {}
visible_target: Optional[str] = None
focused_target: Optional[str] = None
lock = threading.Lock()
overlay: Optional["CursorOverlay"] = None
overlay_error: Optional[str] = None
shape_error: Optional[str] = None

X11_INPUT_OUTPUT = 1
X11_COPY_FROM_PARENT = 0
X11_EXPOSURE_MASK = 0x8000
X11_COORD_MODE_ORIGIN = 0
X11_CONVEX = 1

CLICK_FLASH_DURATION = 0.35  # seconds cursor stays orange after click


def xdotool_move(x: float, y: float) -> None:
    try:
        subprocess.Popen(
            ["env", f"DISPLAY={DISPLAY}", "xdotool", "mousemove",
             str(int(round(x))), str(int(round(y)))],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except Exception:
        pass


class CursorOverlay:
    """Xlib-drawn cursor with orange flash on click, transparent via SHAPE."""

    CANVAS = 96
    TIP_X  = 4
    TIP_Y  = 4
    ARROW: Tuple[Tuple[int, int], ...] = (
        (TIP_X + 0,  TIP_Y + 0),
        (TIP_X + 14, TIP_Y + 8),
        (TIP_X + 8,  TIP_Y + 9),
        (TIP_X + 5,  TIP_Y + 16),
    )

    def __init__(self, display_name: str) -> None:
        self._lock       = threading.Lock()
        self._visible    = False
        self._orange     = False          # True while click flash is active
        self._shape_ext: Optional[object] = None
        self._shape_mask_failed = False
        self._x = 960.0
        self._y = 540.0

        self.d      = display.Display(display_name)
        self.screen = self.d.screen()
        self.root   = self.screen.root
        self._init_shape()

        self._colors = {
            "white":       self._alloc(255, 255, 255),
            "black":       self._alloc(0,   0,   0),
            "orange":      self._alloc(255, 152, 0),
        }

        self.win = self.root.create_window(
            0, 0, self.CANVAS, self.CANVAS, 0,
            self.screen.root_depth,
            X11_INPUT_OUTPUT,
            X11_COPY_FROM_PARENT,
            background_pixel=self._colors["black"].pixel,
            colormap=self.screen.default_colormap,
            event_mask=X11_EXPOSURE_MASK,
            override_redirect=True,
        )
        self._gc = self.win.create_gc(
            foreground=self._colors["white"].pixel,
            background=self._colors["black"].pixel,
            line_width=1,
        )
        self._set_keep_above()
        self._paint()
        self._start_raise_loop()

    # ── helpers ──────────────────────────────────────────────────────────────

    def _alloc(self, r: int, g: int, b: int):
        return self.screen.default_colormap.alloc_color(r * 256, g * 256, b * 256)

    def _init_shape(self) -> None:
        global shape_error
        try:
            shape_ext.query_version(self.d)
            self._shape_ext = shape_ext
        except Exception as exc:
            shape_error = str(exc)
            print(f"cursor shape extension unavailable: {exc}", flush=True)

    def _gc_set(self, color_pixel: int, line_width: int = 1) -> None:
        self._gc.change(foreground=color_pixel, linewidth=line_width)

    def _polyline(self, points: Tuple[Tuple[int, int], ...]) -> None:
        outline = list(points) + [points[0]]
        try:
            self.win.polyline(self._gc, X11_COORD_MODE_ORIGIN, outline)
        except AttributeError:
            self.win.poly_line(self._gc, X11_COORD_MODE_ORIGIN, outline)

    # ── drawing ───────────────────────────────────────────────────────────────

    def _draw_arrow(self) -> None:
        points = self.ARROW
        self._gc_set(self._colors["black"].pixel, 2)
        self._polyline(points)
        color = self._colors["orange"].pixel if self._orange else self._colors["white"].pixel
        self._gc_set(color, 1)
        self.win.fill_poly(self._gc, X11_CONVEX, X11_COORD_MODE_ORIGIN, list(points))

    def _make_alpha(self) -> Image.Image:
        """Pillow alpha image — only used to build the SHAPE mask."""
        frame = Image.new("RGBA", (self.CANVAS, self.CANVAS), (0, 0, 0, 0))
        draw  = ImageDraw.Draw(frame, "RGBA")
        arrow = list(self.ARROW)
        fill  = (255, 152, 0, 255) if self._orange else (255, 255, 255, 255)
        draw.polygon(arrow, fill=fill)
        draw.line(arrow + [arrow[0]], fill=(0, 0, 0, 255), width=2)
        return frame.split()[3]

    def _apply_shape(self, alpha: Image.Image) -> None:
        if self._shape_ext is None:
            return

        SHAPE_SET      = 0
        SHAPE_BOUNDING = 0
        SHAPE_CLIP     = 1

        pixels = alpha.load()
        w, h   = alpha.size

        try:
            pixmap   = self.win.create_pixmap(w, h, 1)
            gc_clear = pixmap.create_gc(foreground=0, background=0)
            gc_draw  = pixmap.create_gc(foreground=1, background=0)
            pixmap.fill_rectangle(gc_clear, 0, 0, w, h)

            has_pixels = False
            for y in range(h):
                x = 0
                while x < w:
                    if pixels[x, y] < 32:
                        x += 1
                        continue
                    x0 = x
                    while x < w and pixels[x, y] >= 32:
                        x += 1
                    pixmap.fill_rectangle(gc_draw, x0, y, x - x0, 1)
                    has_pixels = True

            if not has_pixels:
                return

            for dest_kind in (SHAPE_BOUNDING, SHAPE_CLIP):
                self.win.shape_mask(SHAPE_SET, dest_kind, 0, 0, pixmap)
            self.d.sync()
        except Exception as exc:
            global shape_error
            shape_error = str(exc)
            if not self._shape_mask_failed:
                self._shape_mask_failed = True
                print(f"cursor shape mask failed: {exc}", flush=True)

    def _paint(self) -> None:
        self._gc_set(self._colors["black"].pixel, 1)
        self.win.fill_rectangle(self._gc, 0, 0, self.CANVAS, self.CANVAS)
        self._draw_arrow()
        self._apply_shape(self._make_alpha())

    # ── window management ─────────────────────────────────────────────────────

    def _set_keep_above(self) -> None:
        try:
            wm_state        = self.d.intern_atom("_NET_WM_STATE")
            wm_above        = self.d.intern_atom("_NET_WM_STATE_ABOVE")
            wm_type         = self.d.intern_atom("_NET_WM_WINDOW_TYPE")
            wm_notification = self.d.intern_atom("_NET_WM_WINDOW_TYPE_NOTIFICATION")
            self.win.change_property(wm_type, ATOM, 32, [wm_notification])
            msg = event.ClientMessage(
                window=self.win,
                client_type=wm_state,
                data=(32, [1, wm_above, 0, 0, 0]),
            )
            self.root.send_event(
                msg,
                event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask,
            )
            self.d.sync()
        except Exception:
            pass

    def _start_raise_loop(self) -> None:
        def loop() -> None:
            while True:
                time.sleep(0.05)
                with self._lock:
                    if not self._visible:
                        continue
                    try:
                        self.win.raise_window()
                        self.d.flush()
                    except Exception:
                        pass
        threading.Thread(target=loop, daemon=True, name="cursor-raise").start()

    def _raise(self) -> None:
        self.win.raise_window()
        self.d.flush()

    def _window_origin(self, x: float, y: float) -> Tuple[int, int]:
        return int(round(x)) - self.TIP_X, int(round(y)) - self.TIP_Y

    # ── public API ────────────────────────────────────────────────────────────

    def show(self) -> None:
        with self._lock:
            self._visible = True
            wx, wy = self._window_origin(self._x, self._y)
            self.win.configure(x=wx, y=wy)
            self.win.map()
            self._raise()
            self._paint()
            self.d.sync()

    def hide(self) -> None:
        with self._lock:
            self._visible = False
            self.win.unmap()
            self.d.sync()

    def move(self, x: float, y: float) -> None:
        with self._lock:
            self._x, self._y = x, y
            if not self._visible:
                self._visible = True
                self.win.map()
            wx, wy = self._window_origin(x, y)
            self.win.configure(x=wx, y=wy)
            self._raise()
            self._paint()
            self.d.sync()

    def click(self, x: float, y: float) -> None:
        with self._lock:
            self._x, self._y = x, y
            self._visible    = True
            self._orange     = True
            wx, wy = self._window_origin(x, y)
            self.win.configure(x=wx, y=wy)
            self.win.map()
            self._raise()
            self._paint()
            self.d.sync()

        threading.Thread(target=self._flash_restore, daemon=True).start()

    def _flash_restore(self) -> None:
        time.sleep(CLICK_FLASH_DURATION)
        with self._lock:
            self._orange = False
            if self._visible:
                self._raise()
                self._paint()
                self.d.sync()


# ── overlay lifecycle ─────────────────────────────────────────────────────────

def wait_for_chromium(timeout: float = 30.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(f"{CDP_URL}/json/version", timeout=2) as resp:
                if resp.status == 200:
                    return True
        except (urllib.error.URLError, TimeoutError, OSError):
            pass
        time.sleep(0.5)
    return False


def init_overlay() -> None:
    global overlay, overlay_error

    overlay = None
    overlay_error = None

    time.sleep(1.0)
    wait_for_chromium()

    for attempt in range(20):
        try:
            overlay = CursorOverlay(DISPLAY)
            overlay.show()
            overlay.move(960.0, 540.0)
            shape = "on" if overlay._shape_ext is not None and not overlay._shape_mask_failed else "off"
            detail = f", reason={shape_error}" if shape == "off" and shape_error else ""
            print(f"cursor overlay ready (xlib+pillow-shape, shape={shape}{detail})", flush=True)
            return
        except Exception as exc:
            overlay_error = str(exc)
            print(f"cursor overlay attempt {attempt + 1} failed: {exc}", flush=True)
            time.sleep(0.5)

    print(f"cursor overlay disabled: {overlay_error}", flush=True)


# ── cursor actions ────────────────────────────────────────────────────────────

def apply_cursor(target_id: str) -> None:
    pos = registry.get(target_id)
    if pos is None:
        return
    xdotool_move(pos[0], pos[1])
    if overlay is not None:
        overlay.show()
        overlay.move(pos[0], pos[1])


def apply_move(target_id: str, x: float, y: float) -> None:
    xdotool_move(x, y)
    if overlay is not None:
        overlay.show()
        overlay.move(x, y)


def apply_click(target_id: str, x: float, y: float) -> None:
    xdotool_move(x, y)
    if overlay is not None:
        overlay.show()
        overlay.click(x, y)


def hide_overlay() -> None:
    if overlay is not None:
        overlay.hide()


# ── CDP helpers ───────────────────────────────────────────────────────────────

def cdp_list() -> list:
    try:
        with urllib.request.urlopen(f"{CDP_URL}/json/list", timeout=2) as resp:
            data = json.loads(resp.read().decode())
            return data if isinstance(data, list) else []
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
        return []


def tab_visible(ws_url: str) -> bool:
    try:
        import websockets.sync.client as ws_client
    except ImportError:
        return False
    try:
        with ws_client.connect(ws_url, open_timeout=2, close_timeout=1) as ws:
            ws.send(json.dumps({
                "id": 1,
                "method": "Runtime.evaluate",
                "params": {"expression": "document.visibilityState", "returnByValue": True},
            }))
            deadline = time.time() + 2.0
            while time.time() < deadline:
                raw = ws.recv(timeout=1)
                msg = json.loads(raw)
                if msg.get("id") != 1:
                    continue
                value = msg.get("result", {}).get("result", {}).get("value")
                return value == "visible"
    except Exception:
        return False
    return False


def poll_visible_tab() -> None:
    global visible_target
    while True:
        try:
            detected: Optional[str] = None
            for tab in cdp_list():
                if not isinstance(tab, dict) or tab.get("type") != "page":
                    continue
                target_id = tab.get("id")
                ws_url    = tab.get("webSocketDebuggerUrl")
                if not isinstance(target_id, str) or not isinstance(ws_url, str):
                    continue
                if target_id not in registry:
                    continue
                if tab_visible(ws_url):
                    detected = target_id
                    break
            with lock:
                previous = visible_target
            if detected is not None and detected != previous:
                with lock:
                    visible_target = detected
                apply_cursor(detected)
        except Exception:
            pass
        time.sleep(POLL_INTERVAL)


# ── HTTP server ───────────────────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):
    def log_message(self, format: str, *args) -> None:
        return

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0:
            return {}
        body = self.rfile.read(length)
        data = json.loads(body.decode() or "{}")
        return data if isinstance(data, dict) else {}

    def _ok(self, payload: dict | None = None) -> None:
        body = json.dumps(payload or {"ok": True}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _bad(self, message: str) -> None:
        body = json.dumps({"error": message}).encode()
        self.send_response(400)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path == "/health":
            with lock:
                self._ok({
                    "ok": True,
                    "visible": visible_target,
                    "focused": focused_target,
                    "tabs": len(registry),
                    "overlay": overlay is not None,
                    "overlayEngine": "xlib+pillow-shape",
                    "overlayError": overlay_error,
                    "shapeError": shape_error,
                    "display": DISPLAY,
                })
            return
        self.send_response(404)
        self.end_headers()

    def do_POST(self) -> None:
        global visible_target, focused_target

        try:
            data = self._read_json()
        except json.JSONDecodeError:
            self._bad("invalid json")
            return

        if self.path == "/register":
            target_id = data.get("targetId")
            if not isinstance(target_id, str) or target_id == "":
                self._bad("targetId required")
                return
            x = float(data.get("x", 960))
            y = float(data.get("y", 540))
            with lock:
                registry[target_id] = (x, y)
            self._ok()
            return

        if self.path == "/unregister":
            target_id = data.get("targetId")
            if not isinstance(target_id, str) or target_id == "":
                self._bad("targetId required")
                return
            should_hide = False
            with lock:
                registry.pop(target_id, None)
                if visible_target == target_id:
                    visible_target = None
                if focused_target == target_id:
                    focused_target = None
                should_hide = focused_target is None and visible_target is None
            if should_hide:
                hide_overlay()
            self._ok()
            return

        if self.path == "/move":
            target_id = data.get("targetId")
            if not isinstance(target_id, str) or target_id == "":
                self._bad("targetId required")
                return
            x = float(data["x"])
            y = float(data["y"])
            with lock:
                registry[target_id] = (x, y)
            apply_move(target_id, x, y)
            self._ok()
            return

        if self.path == "/click":
            target_id = data.get("targetId")
            if not isinstance(target_id, str) or target_id == "":
                self._bad("targetId required")
                return
            x = float(data["x"])
            y = float(data["y"])
            with lock:
                registry[target_id] = (x, y)
            apply_click(target_id, x, y)
            self._ok()
            return

        if self.path == "/focus":
            target_id = data.get("targetId")
            if not isinstance(target_id, str) or target_id == "":
                self._bad("targetId required")
                return
            with lock:
                visible_target = target_id
                focused_target = target_id
                pos = registry.get(target_id, (960.0, 540.0))
                registry[target_id] = pos
            apply_cursor(target_id)
            self._ok()
            return

        self.send_response(404)
        self.end_headers()


def main() -> None:
    init_overlay()
    port = int(os.environ.get("STEALTH_CURSOR_PORT", "9230"))
    threading.Thread(target=poll_visible_tab, daemon=True).start()
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"cursor-server listening on 0.0.0.0:{port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
