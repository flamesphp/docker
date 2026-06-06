#!/bin/bash
# Pre-install Material Darker into a baked profile (files + Preferences only).
# Do NOT launch Chromium here — Secure Preferences MAC is tied to the build
# hostname and breaks on runtime. Chrome regenerates it on first launch.
set -euo pipefail

BAKED="/opt/flames-chromium-baked"
EXT="/opt/flames-chromium/extensions/material-darker"
EXT_ID="dehdmadehnmbjcilgpjhdjkepcknkjad"
EXT_VERSION="2.4"
EXT_DEST="${BAKED}/Default/Extensions/${EXT_ID}/${EXT_VERSION}_0"

mkdir -p "${BAKED}/Default"
cp /opt/flames-chromium/Default/Preferences "${BAKED}/Default/Preferences"
cp /opt/flames-chromium/Default/Bookmarks "${BAKED}/Default/Bookmarks"
cp /opt/flames-chromium/local-state.json "${BAKED}/Local State"

if [[ ! -f "${EXT}/manifest.json" ]]; then
    echo "chromium-bake: no extension found, skipping"
    exit 0
fi

mkdir -p "${EXT_DEST}"
cp -a "${EXT}/." "${EXT_DEST}/"

python3 - <<'PY'
import json
from pathlib import Path

prefs_path = Path("/opt/flames-chromium-baked/Default/Preferences")
prefs = json.loads(prefs_path.read_text())

prefs["extensions"] = {
    "settings": {
        "dehdmadehnmbjcilgpjhdjkepcknkjad": {
            "active_permissions": {
                "api": [],
                "explicit_host": [],
                "manifest_permissions": [],
                "scriptable_host": [],
            },
            "creation_flags": 9,
            "from_webstore": True,
            "granted_permissions": {
                "api": [],
                "explicit_host": [],
                "manifest_permissions": [],
                "scriptable_host": [],
            },
            "install_time": "13300000000000000",
            "location": 1,
            "manifest": {
                "manifest_version": 3,
                "name": "Material Darker",
                "theme": {},
                "version": "2.4",
            },
            "path": "dehdmadehnmbjcilgpjhdjkepcknkjad/2.4_0",
            "state": 1,
        }
    },
    "theme": {
        "id": "dehdmadehnmbjcilgpjhdjkepcknkjad",
        "use_system": False,
    },
}

prefs_path.write_text(json.dumps(prefs, indent=2) + "\n")
PY

/chromium-profile-cleanup.sh "${BAKED}"

if [[ ! -f "${EXT_DEST}/manifest.json" ]]; then
    echo "chromium-bake: extension files missing" >&2
    exit 1
fi

chown -R flames:flames "${BAKED}"
echo "chromium-bake: profile ready"
