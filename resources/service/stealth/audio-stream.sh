#!/bin/bash
set -euo pipefail

AUDIO_PORT="${1:-8090}"
SINK_NAME="${PULSE_SINK_NAME:-flames_chromium_audio}"

while true; do
    ffmpeg -nostdin -hide_banner -loglevel warning \
        -fflags nobuffer+flush_packets \
        -flags low_delay \
        -avioflags direct \
        -f pulse -fragment_size 1024 -i "${SINK_NAME}.monitor" \
        -ac 2 -ar 48000 \
        -c:a pcm_s16le \
        -f s16le \
        -content_type application/octet-stream \
        -listen 1 -http_persistent 1 \
        "http://127.0.0.1:${AUDIO_PORT}/stream.pcm" || true
    sleep 0.2
done
