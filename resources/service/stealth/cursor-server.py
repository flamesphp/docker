#!/usr/bin/env python3
"""Per-tab X11 cursor sync for Stealth VNC. Polls visible tab (manual VNC switch)."""

from __future__ import annotations

import json
import subprocess
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Dict, Optional, Tuple

DISPLAY = ":99"
FLAMES_USER = "flames"
CDP_URL = "http://127.0.0.1:9223"
POLL_INTERVAL = 0.35

registry: Dict[str, Tuple[float, float]] = {}
visible_target: Optional[str] = None
lock = threading.Lock()


def xdotool_move(x: float, y: float) -> None:
    subprocess.run(
        [
            "runuser",
            "-u",
            FLAMES_USER,
            "--",
            "env",
            f"DISPLAY={DISPLAY}",
            "xdotool",
            "mousemove",
            "--sync",
            str(int(round(x))),
            str(int(round(y))),
        ],
        check=False,
        timeout=3,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


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
            ws.send(
                json.dumps(
                    {
                        "id": 1,
                        "method": "Runtime.evaluate",
                        "params": {
                            "expression": "document.visibilityState",
                            "returnByValue": True,
                        },
                    }
                )
            )
            deadline = time.time() + 2.0
            while time.time() < deadline:
                raw = ws.recv(timeout=1)
                msg = json.loads(raw)
                if msg.get("id") != 1:
                    continue
                value = (
                    msg.get("result", {})
                    .get("result", {})
                    .get("value")
                )
                return value == "visible"
    except Exception:
        return False

    return False


def apply_cursor(target_id: str) -> None:
    pos = registry.get(target_id)
    if pos is None:
        return
    xdotool_move(pos[0], pos[1])


def poll_visible_tab() -> None:
    global visible_target

    while True:
        try:
            detected: Optional[str] = None

            for tab in cdp_list():
                if not isinstance(tab, dict) or tab.get("type") != "page":
                    continue

                target_id = tab.get("id")
                ws_url = tab.get("webSocketDebuggerUrl")

                if not isinstance(target_id, str) or not isinstance(ws_url, str):
                    continue

                if target_id not in registry:
                    continue

                if tab_visible(ws_url):
                    detected = target_id
                    break

            if detected is not None and detected != visible_target:
                with lock:
                    visible_target = detected
                apply_cursor(detected)
        except Exception:
            pass

        time.sleep(POLL_INTERVAL)


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
                self._ok(
                    {
                        "ok": True,
                        "visible": visible_target,
                        "tabs": len(registry),
                    }
                )
            return
        self.send_response(404)
        self.end_headers()

    def do_POST(self) -> None:
        global visible_target

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
            with lock:
                registry.pop(target_id, None)
                if visible_target == target_id:
                    visible_target = None
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
                active = visible_target
            if active == target_id:
                xdotool_move(x, y)
            self._ok()
            return

        if self.path == "/focus":
            target_id = data.get("targetId")
            if not isinstance(target_id, str) or target_id == "":
                self._bad("targetId required")
                return
            with lock:
                visible_target = target_id
                pos = registry.get(target_id, (960.0, 540.0))
                registry[target_id] = pos
            apply_cursor(target_id)
            self._ok()
            return

        self.send_response(404)
        self.end_headers()


def main() -> None:
    port = int(__import__("os").environ.get("STEALTH_CURSOR_PORT", "9230"))
    threading.Thread(target=poll_visible_tab, daemon=True).start()
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"cursor-server listening on 0.0.0.0:{port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
