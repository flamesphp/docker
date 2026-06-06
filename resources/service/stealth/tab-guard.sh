#!/bin/bash
set -uo pipefail

CDP_PORT="${1:-9223}"
CDP_URL="http://127.0.0.1:${CDP_PORT}"
FLAMES_USER="${FLAMES_USER:-flames}"
INTERVAL=0.2
CDP_DOWN_LIMIT=3

count_pages() {
    local list
    list=$(curl -sf "${CDP_URL}/json/list" 2>/dev/null) || {
        echo 0
        return
    }
    printf '%s' "${list}" | grep -c '"type": "page"' || echo 0
}

open_blank_tab() {
    curl -sf -X PUT "${CDP_URL}/json/new?about:blank" >/dev/null 2>&1 \
        || curl -sf "${CDP_URL}/json/new?about:blank" >/dev/null 2>&1 \
        || true
}

is_chromium_running() {
    pgrep -u "${FLAMES_USER}" -f "chromium.*--remote-debugging-port=${CDP_PORT}" >/dev/null 2>&1
}

wait_for_cdp() {
    local max_attempts="${1:-60}"
    local attempt=0

    while [ "${attempt}" -lt "${max_attempts}" ]; do
        if curl -sf "${CDP_URL}/json/version" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.25
        attempt=$((attempt + 1))
    done

    return 1
}

restart_chromium() {
    if is_chromium_running; then
        return 0
    fi

    echo "tab-guard: chromium is down, restarting" >&2
    /chromium-launch.sh &
    wait_for_cdp 40
}

if ! wait_for_cdp 60; then
    echo "tab-guard: CDP unavailable on port ${CDP_PORT}, starting chromium" >&2
    restart_chromium || {
        echo "tab-guard: unable to restore chromium" >&2
        exit 1
    }
fi

cdp_down=0

while true; do
    if ! curl -sf "${CDP_URL}/json/version" >/dev/null 2>&1; then
        cdp_down=$((cdp_down + 1))
        if [ "${cdp_down}" -ge "${CDP_DOWN_LIMIT}" ]; then
            restart_chromium
            cdp_down=0
        fi
        sleep "${INTERVAL}"
        continue
    fi

    cdp_down=0
    count=$(count_pages)

    if [ "${count}" -eq 0 ]; then
        open_blank_tab
        sleep "${INTERVAL}"
        count=$(count_pages)

        if [ "${count}" -eq 0 ]; then
            if is_chromium_running; then
                open_blank_tab
            else
                restart_chromium
            fi
        fi
    fi

    sleep "${INTERVAL}"
done
