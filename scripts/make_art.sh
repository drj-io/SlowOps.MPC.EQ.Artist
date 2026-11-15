#!/usr/bin/env bash
# Redraw every skin image of both plugins. Needs a Python with Pillow: PILLOW_PY=/path/to/python.
#   python3 -m venv /tmp/venv && /tmp/venv/bin/pip install pillow && PILLOW_PY=/tmp/venv/bin/python scripts/make_art.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PILLOW_PY="${PILLOW_PY:?set PILLOW_PY to a Python that has Pillow}"
cd "$ROOT"
for v in dots lines; do
    export EQA_VARIANT=$v
    python3 scripts/make_skin_art.py >/dev/null
    "$PILLOW_PY" scripts/make_text_art.py >/dev/null
    "$PILLOW_PY" scripts/make_para_art.py
done
