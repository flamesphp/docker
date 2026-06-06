#!/bin/bash
set -euo pipefail

FLAMES_HOME="${HOME:-/home/flames}"
PULSE_RUNTIME="${XDG_RUNTIME_DIR:-${FLAMES_HOME}/.runtime/pulse}"
SINK_NAME="${PULSE_SINK_NAME:-flames_chromium_audio}"
DBUS_ENV="${PULSE_RUNTIME}/dbus.env"

export HOME="${FLAMES_HOME}"
export XDG_RUNTIME_DIR="${PULSE_RUNTIME}"

mkdir -p "${XDG_RUNTIME_DIR}"
chmod 700 "${XDG_RUNTIME_DIR}"

if [ ! -f "${DBUS_ENV}" ] && command -v dbus-launch >/dev/null 2>&1; then
    # shellcheck disable=SC2046
    eval $(dbus-launch --sh-syntax)
    printf 'DBUS_SESSION_BUS_ADDRESS=%s\n' "${DBUS_SESSION_BUS_ADDRESS}" > "${DBUS_ENV}"
    chmod 600 "${DBUS_ENV}"
fi

if [ -f "${DBUS_ENV}" ]; then
    # shellcheck disable=SC1090
    source "${DBUS_ENV}"
fi

if ! pulseaudio --check 2>/dev/null; then
    pulseaudio -D \
        --exit-idle-time=-1 \
        --realtime=no \
        --disable-shm \
        --log-target=stderr \
        --log-level=notice \
        -n \
        --load="module-native-protocol-unix auth-anonymous=1" \
        --load="module-null-sink sink_name=${SINK_NAME} sink_properties=device.description=Flames_Chromium_Audio,device.latency_offset=1"
    sleep 1
fi

if ! pactl info >/dev/null 2>&1; then
    echo "pulse-start-flames: unable to connect to pulse server at unix:${PULSE_RUNTIME}/pulse/native" >&2
    exit 1
fi

if ! pactl list sinks short 2>/dev/null | grep -q "${SINK_NAME}"; then
    echo "pulse-start-flames: sink ${SINK_NAME} not available" >&2
    pactl list sinks short 2>&1 || true
    exit 1
fi

pactl set-default-sink "${SINK_NAME}"
pactl set-sink-volume "${SINK_NAME}" 100%
pactl set-sink-mute "${SINK_NAME}" 0
