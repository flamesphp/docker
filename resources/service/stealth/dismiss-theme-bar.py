#!/usr/bin/env python3
"""Dismiss the 'Installed theme' infobar by navigating the active tab."""

from __future__ import annotations

import json
import os
import subprocess
import time
import urllib.error
import urllib.request

CDP_PORT = int(os.environ.get("CDP_INTERNAL", "9223"))
CDP_URL = f"http://127.0.0.1:{CDP_PORT}"
DISPLAY = os.environ.get("DISPLAY", ":99")


def wait_cdp(timeout: float = 15.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(f"{CDP_URL}/json/version", timeout=1) as resp:
                if resp.status == 200:
                    return True
        except (urllib.error.URLError, TimeoutError, OSError):
            pass
        time.sleep(0.25)
    return False


def page_ws_url() -> str | None:
    with urllib.request.urlopen(f"{CDP_URL}/json/list", timeout=2) as resp:
        tabs = json.loads(resp.read().decode())

    for tab in tabs:
        if tab.get("type") == "page" and tab.get("webSocketDebuggerUrl"):
            return str(tab["webSocketDebuggerUrl"])

    return None


def navigate(ws_url: str, url: str) -> None:
    import websockets.sync.client as ws_client

    with ws_client.connect(ws_url, open_timeout=3, close_timeout=2) as ws:
        ws.send(json.dumps({
            "id": 1,
            "method": "Page.enable",
        }))
        ws.recv(timeout=2)

        ws.send(json.dumps({
            "id": 2,
            "method": "Page.navigate",
            "params": {"url": url},
        }))

        deadline = time.time() + 4.0
        while time.time() < deadline:
            msg = json.loads(ws.recv(timeout=1))
            if msg.get("id") == 2:
                break


def xdotool_click_and_escape() -> None:
    try:
        subprocess.run(
            ["env", f"DISPLAY={DISPLAY}", "xdotool", "mousemove", "960", "500", "click", "1"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=3,
            check=False,
        )
        time.sleep(0.15)
        subprocess.run(
            ["env", f"DISPLAY={DISPLAY}", "xdotool", "key", "Escape"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=3,
            check=False,
        )
    except Exception:
        pass


def main() -> None:
    if not wait_cdp():
        return

    time.sleep(2.5)

    ws_url = page_ws_url()
    if ws_url:
        try:
            navigate(ws_url, "data:text/html,<html><body></body></html>")
        except Exception:
            pass

    time.sleep(0.3)
    xdotool_click_and_escape()


if __name__ == "__main__":
    main()
