#!/bin/bash
# Dismiss the "Installed theme Material Darker" infobar after Chromium starts.
set -euo pipefail

DISPLAY="${DISPLAY:-:99}"
CDP_PORT="${CDP_INTERNAL:-9223}"
CDP_URL="http://127.0.0.1:${CDP_PORT}"

for _ in $(seq 1 40); do
    if curl -sf "${CDP_URL}/json/version" >/dev/null 2>&1; then
        break
    fi
    sleep 0.25
done

# Let the infobar render, then dismiss it.
sleep 2

env DISPLAY="${DISPLAY}" xdotool mousemove 960 540 click 1 2>/dev/null || true
sleep 0.2
env DISPLAY="${DISPLAY}" xdotool key Escape 2>/dev/null || true
