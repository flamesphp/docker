#!/bin/bash
set -euo pipefail

FLAMES_USER="${FLAMES_USER:-flames}"
CDP_INTERNAL="${CDP_INTERNAL:-9223}"
PROFILE_DIR="${PROFILE_DIR:-/var/lib/flames-chromium}"
MATERIAL_DARKER_EXT="/opt/flames-chromium/extensions/material-darker"

CHROME_VERSION="$(chromium --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)"
CHROME_VERSION="${CHROME_VERSION:-148.0.0.0}"
DEFAULT_USER_AGENT="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${CHROME_VERSION} Safari/537.36"
USER_AGENT="${STEALTH_USER_AGENT:-${DEFAULT_USER_AGENT}}"

DARK_MODE_ARGS=(
    --force-dark-mode
    --enable-features=WebUIDarkMode,WebContentsForceDark
)
EXTENSION_ARGS=()

if [[ -f "${MATERIAL_DARKER_EXT}/manifest.json" ]]; then
    EXTENSION_ARGS=(--load-extension="${MATERIAL_DARKER_EXT}")
    DARK_MODE_ARGS=()
fi

exec /run-as-flames.sh chromium \
    --no-sandbox \
    --no-first-run \
    --no-default-browser-check \
    --disable-sync \
    --disable-dev-shm-usage \
    --disable-infobars \
    --disable-blink-features=AutomationControlled \
    --exclude-switches=enable-automation \
    --user-agent="${USER_AGENT}" \
    --use-gl=angle \
    --use-angle=swiftshader \
    --enable-unsafe-swiftshader \
    "${DARK_MODE_ARGS[@]}" \
    "${EXTENSION_ARGS[@]}" \
    --disable-features=SearchEngineChoice,SearchEngineChoiceScreen,BookmarkBar,SignInPromo,SigninInterceptBubble,SigninInterception,ChromeWhatsNewUI,SyncPromo \
    --user-data-dir="${PROFILE_DIR}" \
    --window-position=0,0 \
    --window-size=1920,1080 \
    --force-device-scale-factor=1 \
    --remote-debugging-port="${CDP_INTERNAL}" \
    --remote-allow-origins=* \
    about:blank
