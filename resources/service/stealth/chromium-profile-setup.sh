#!/bin/bash
# Pre-install Material Darker into the Chromium profile so Chrome does not
# show the "Installed theme" infobar (triggered by --load-extension).
set -euo pipefail

EXT_ID="dehdmadehnmbjcilgpjhdjkepcknkjad"
EXT_VERSION="2.4"
EXT_SRC="/opt/flames-chromium/extensions/material-darker"
PROFILE_DIR="${PROFILE_DIR:-/var/lib/flames-chromium}"
EXT_DEST="${PROFILE_DIR}/Default/Extensions/${EXT_ID}/${EXT_VERSION}_0"

if [[ ! -f "${EXT_SRC}/manifest.json" ]]; then
    exit 0
fi

mkdir -p "${EXT_DEST}"
cp -a "${EXT_SRC}/." "${EXT_DEST}/"
