#!/usr/bin/env bash
# Package EQ Artist Dots for the open-mpc catalog (https://open-mpc.github.io/workflow.html):
#   dist/EQ-Artist-Dots-<version>-mpc-armv7.zip    Gen1 MPCs and Force (Plugin Manager, installers)
#   dist/EQ-Artist-Dots-<version>-mpc-aarch64.zip  Gen2 MPCs
# It runs the catalog's own tools from a checkout of open-mpc/mpc-vst-plugins (MPC_CATALOG, default ../mpc-vst-plugins),
# and stops if either zip fails the catalog check. Attach both zips to the GitHub release, next to the
# eq-artist-*-mpc.zip folders from scripts/package.sh (those are for copying into "VST2 Plugins" by hand).
#   scripts/release.sh [SDK folder, or a zip of it]   default: dist/eq-artist-dots-<version>-mpc.zip (run scripts/package.sh first)
# Only Dots is listed in the catalog; EQ Artist Lines (experimental) is a GitHub release download only.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CATALOG="${MPC_CATALOG:-$ROOT/../mpc-vst-plugins}"
PY="${PYTHON:-python3}"
ID=eq-artist-dots
REPO=drj-io/SlowOps.MPC.EQ.Artist
cd "$ROOT"
VERSION="$("$PY" -c 'import json; print(json.load(open("plugins/dots/plugin.json"))["version"])')"
SRC="${1:-dist/eq-artist-dots-$VERSION-mpc.zip}"
[ -f "$CATALOG/tools/sdk_release.py" ] || { echo "release: no catalog tools in $CATALOG (clone open-mpc/mpc-vst-plugins there, or set MPC_CATALOG)" >&2; exit 1; }
[ -e "$SRC" ] || { echo "release: $SRC not found (run scripts/test.sh and scripts/package.sh)" >&2; exit 1; }
STEM="dist/EQ-Artist-Dots-$VERSION-mpc"
rm -f "$STEM-armv7.zip" "$STEM-aarch64.zip"
"$PY" "$CATALOG/tools/sdk_release.py" "$SRC" --version "$VERSION" --id "$ID" --repo "$REPO" --license MIT -o dist
for arch in armv7 aarch64; do
    "$PY" "$CATALOG/tools/catalog_check.py" "$STEM-$arch.zip" --catalog --require-portable --expect-id "$ID" --expect-repo "$REPO"
done
ls -l "$STEM"-*.zip
