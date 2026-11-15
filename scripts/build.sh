#!/usr/bin/env bash
# Build both plugins (EQ Artist Dots and EQ Artist Lines) and the test tools. For each plugin:
#   1. native build -> plugins/<variant>/build/params.json (parameter indices for the skin)
#   2. skins/TUI.json + Q-Links.json from params.json (the PNGs are committed; redraw them with
#      scripts/make_art.sh when the layout or palette changes)
#   3. full build (native, arm32, arm64), skins copied verbatim, SDK validation
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
PY="${PYTHON:-python3}"
cd "$ROOT"
[ -f "$SDK/tools/build.py" ] || git submodule update --init third_party/mpc-plugin-builder-sdk
for v in dots lines; do
    "$PY" "$SDK/tools/build.py" "plugins/$v" --target native >/dev/null
    EQA_VARIANT=$v "$PY" scripts/gen_skins.py
    "$PY" "$SDK/tools/build.py" "plugins/$v" "$@"
done
mkdir -p build
c++ -std=c++17 -O2 -Wall -Wextra -I core/include tests/core_tests.cpp -o build/core_tests
c++ -std=c++17 -O2 -Wall -I "$SDK/framework" -I core/include tests/scenario_host.cpp -o build/scenario_host
echo
echo "MPC folders: $ROOT/plugins/dots/build/Slow Ops - VST - EQ Artist Dots"
echo "             $ROOT/plugins/lines/build/Slow Ops - VST - EQ Artist Lines"
