#!/bin/bash
set -euo pipefail

DISPLAY_NUM=99
VNC_PORT="${STEALTH_VNC_PORT:-5900}"
WEB_PORT="${STEALTH_VNC_WEBVIEW_PORT:-6080}"
CDP_INTERNAL=9223
CDP_EXTERNAL=9222
CDP_PORT="${STEALTH_REMOTE_PORT:-9222}"
AUDIO_PORT="${STEALTH_AUDIO_PORT:-8090}"
CURSOR_PORT="${STEALTH_CURSOR_PORT:-9230}"
SCREEN="1920x1080x24"

FLAMES_USER="flames"
FLAMES_HOME="/home/flames"
PULSE_SINK_NAME="flames_chromium_audio"
PULSE_RUNTIME="${FLAMES_HOME}/.runtime/pulse"
PULSE_SOCKET="${PULSE_RUNTIME}/pulse/native"

cleanup_stale_processes() {
    pkill -f "Xvfb :${DISPLAY_NUM}" 2>/dev/null || true
    pkill -f "x11vnc.*:${DISPLAY_NUM}" 2>/dev/null || true
    pkill -f "websockify.*6081" 2>/dev/null || true
    pkill -f "socat TCP4-LISTEN:${CDP_EXTERNAL}" 2>/dev/null || true
    pkill -u "${FLAMES_USER}" -f "ffmpeg.*${AUDIO_PORT}/stream.pcm" 2>/dev/null || true
    pkill -f "/tab-guard.sh" 2>/dev/null || true
    pkill -f "/cursor-server.py" 2>/dev/null || true
    pkill -u "${FLAMES_USER}" -f "ffmpeg.*${PULSE_SINK_NAME}.monitor" 2>/dev/null || true
    runuser -u "${FLAMES_USER}" -- pulseaudio --kill 2>/dev/null || true
    pkill -u "${FLAMES_USER}" -x chromium 2>/dev/null || true

    rm -f "/tmp/.X${DISPLAY_NUM}-lock"
    rm -f "/tmp/.X11-unix/X${DISPLAY_NUM}"
    rm -rf "${PULSE_RUNTIME}"
}

cleanup_stale_processes
export DISPLAY=":${DISPLAY_NUM}"
export FLAMES_USER="${FLAMES_USER}"
export FLAMES_HOME="${FLAMES_HOME}"
export PULSE_SINK_NAME="${PULSE_SINK_NAME}"
export PULSE_RUNTIME="${PULSE_RUNTIME}"
export PULSE_SOCKET="${PULSE_SOCKET}"

source /pulse-setup.sh

Xvfb ":${DISPLAY_NUM}" -screen 0 "${SCREEN}" -ac +extension GLX +render -noreset &
sleep 1

autocutsel -fork
autocutsel -selection CLIPBOARD -fork

x11vnc \
    -display ":${DISPLAY_NUM}" \
    -nopw \
    -forever \
    -shared \
    -clip clipboard \
    -cursor most \
    -rfbport "${VNC_PORT}" \
    -listen 0.0.0.0 \
    -bg \
    -o /tmp/x11vnc.log

websockify --daemon "127.0.0.1:6081" "localhost:${VNC_PORT}"

sed -e "s/__WEB_PORT__/${WEB_PORT}/g" \
    -e "s/__AUDIO_PORT__/${AUDIO_PORT}/g" \
    /etc/nginx/novnc.conf > /etc/nginx/conf.d/novnc.conf
sed "s/__CDP_PORT__/${CDP_PORT}/g" /opt/flames-novnc/index.html > /usr/share/novnc/index.html

socat "TCP4-LISTEN:${CDP_EXTERNAL},fork,reuseaddr" "TCP4:127.0.0.1:${CDP_INTERNAL}" &

/run-as-flames.sh /audio-stream.sh "${AUDIO_PORT}" &

PROFILE_DIR=/var/lib/flames-chromium
mkdir -p "${PROFILE_DIR}/Default"
cp /opt/flames-chromium/Default/Preferences "${PROFILE_DIR}/Default/Preferences"
cp /opt/flames-chromium/Default/Bookmarks "${PROFILE_DIR}/Default/Bookmarks"
cp /opt/flames-chromium/local-state.json "${PROFILE_DIR}/Local State"
chown -R "${FLAMES_USER}:${FLAMES_USER}" "${PROFILE_DIR}"

export CDP_INTERNAL="${CDP_INTERNAL}"
export PROFILE_DIR="${PROFILE_DIR}"

/chromium-launch.sh &

/tab-guard.sh "${CDP_INTERNAL}" &

export STEALTH_CURSOR_PORT="${CURSOR_PORT}"
python3 /cursor-server.py &

exec nginx -g 'daemon off;'
