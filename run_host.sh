#!/bin/sh
# Build CONDUCTOR for this PC and run a script: build/host/conductor.wav + build/host/conductor-<ms>.png
set -e
cd "$(dirname "$0")"
mkdir -p build/host
gcc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/conductor.exe firmware/src/conductor.c
rm -f build/host/conductor-*.png
./build/host/conductor.exe build/host/conductor "${1:-tests/scripts/conductor.txt}"
python - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("conductor-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png")); p.unlink()
PY
