#!/bin/bash
set -euo pipefail

# Material Dark — https://chromewebstore.google.com/detail/material-dark/keodjedmhcafpoglcokjlbiamaeaehel
EXT_ID="keodjedmhcafpoglcokjlbiamaeaehel"
DEST="/opt/flames-chromium/extensions/material-dark"
CRX="/tmp/material-dark.crx"

mkdir -p "${DEST}"

VERSION="$(chromium --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
VERSION="${VERSION:-131.0.6778.85}"

URL="https://clients2.google.com/service/update2/crx?response=redirect&prodversion=${VERSION}&acceptformat=crx2,crx3&x=id%3D${EXT_ID}%26uc"

echo "Downloading Material Dark (${EXT_ID}) for Chromium ${VERSION}..." >&2
curl -fsSL "${URL}" -o "${CRX}"

python3 - "${DEST}" <<'PY'
import io
import struct
import sys
import zipfile
from pathlib import Path

dest = Path(sys.argv[1])
data = Path("/tmp/material-dark.crx").read_bytes()

if data[:4] == b"Cr24":
    header_size = struct.unpack("<I", data[8:12])[0]
    payload = data[12 + header_size :]
elif data[:4] == b"Cr23":
    pub_len = struct.unpack("<I", data[8:12])[0]
    sig_len = struct.unpack("<I", data[12:16])[0]
    payload = data[16 + pub_len + sig_len :]
else:
    raise SystemExit("Unsupported CRX format")

with zipfile.ZipFile(io.BytesIO(payload)) as archive:
    archive.extractall(dest)

manifest = dest / "manifest.json"
if not manifest.is_file():
    raise SystemExit("manifest.json missing after CRX extraction")

print(f"Material Dark installed at {dest}", flush=True)
PY

rm -f "${CRX}"
