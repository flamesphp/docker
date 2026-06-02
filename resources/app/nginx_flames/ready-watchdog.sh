#!/bin/bash
cd /var/www/html

INTERVAL="${FLAMES_READY_WATCHDOG_INTERVAL:-5}"

start_ready() {
    echo "[Flames Ready] watchdog: starting forge --ready" >> /proc/1/fd/1 2>> /proc/1/fd/2
    /usr/bin/php8.5 ./forge --ready >> /proc/1/fd/1 2>> /proc/1/fd/2 &
}

if ! pgrep -f '[f]orge --ready' > /dev/null 2>&1; then
    start_ready
fi

while true; do
    sleep "$INTERVAL"
    if ! pgrep -f '[f]orge --ready' > /dev/null 2>&1; then
        start_ready
    fi
done
