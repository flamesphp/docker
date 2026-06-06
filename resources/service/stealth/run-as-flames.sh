#!/bin/bash
set -euo pipefail

FLAMES_USER="${FLAMES_USER:-flames}"
FLAMES_HOME="${FLAMES_HOME:-/home/flames}"
SESSION_ENV="${FLAMES_SESSION_ENV:-${FLAMES_HOME}/.runtime/pulse/session.env}"

if [ ! -f "${SESSION_ENV}" ]; then
    echo "run-as-flames: missing ${SESSION_ENV}" >&2
    exit 1
fi

set -a
# shellcheck disable=SC1090
source "${SESSION_ENV}"
set +a

if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    exec runuser -u "${FLAMES_USER}" -- env \
        HOME="${HOME}" \
        XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
        PULSE_SERVER="${PULSE_SERVER}" \
        DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS}" \
        DISPLAY="${DISPLAY:-:99}" \
        "$@"
fi

exec runuser -u "${FLAMES_USER}" -- env \
    HOME="${HOME}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}" \
    PULSE_SERVER="${PULSE_SERVER}" \
    DISPLAY="${DISPLAY:-:99}" \
    "$@"
