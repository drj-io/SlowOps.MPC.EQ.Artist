#!/usr/bin/env bash
# Zip both built plugins for a release: dist/eq-artist-dots-<version>-mpc.zip and dist/eq-artist-lines-<version>-mpc.zip.
# Run scripts/test.sh first (it builds both).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
cd "$ROOT"
mkdir -p dist
for v in dots lines; do
    version=$(python3 -c "import json; print(json.load(open('plugins/$v/plugin.json'))['version'])")
    tmp=$(mktemp -d)
    python3 "$SDK/tools/package.py" "plugins/$v" --out "$tmp" >/dev/null
    mv "$tmp"/*.zip "dist/eq-artist-$v-$version-mpc.zip"
    rmdir "$tmp"
    echo "dist/eq-artist-$v-$version-mpc.zip"
done
