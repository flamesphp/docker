#!/bin/bash
# Remove Chromium singleton locks left by docker build or previous runs.
set -euo pipefail

PROFILE_DIR="${1:?profile dir required}"

rm -f \
    "${PROFILE_DIR}/SingletonLock" \
    "${PROFILE_DIR}/SingletonCookie" \
    "${PROFILE_DIR}/lockfile" \
    "${PROFILE_DIR}/Default/Secure Preferences" \
    "${PROFILE_DIR}/Default/Preferences.backup"

find "${PROFILE_DIR}" -maxdepth 4 \
    \( -name SingletonLock -o -name SingletonCookie -o -name lockfile \) \
    -exec rm -f {} + 2>/dev/null || true

rm -rf /tmp/org.chromium.Chromium.* /tmp/.org.chromium.Chromium.* 2>/dev/null || true
