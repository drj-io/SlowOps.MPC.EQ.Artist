#!/usr/bin/env bash
# Build Curve UI Test: native build (params.json) -> skin JSON -> full build (native, arm32, arm64,
# validation) -> SDK host simulator -> scenario host. The PNGs are committed; regenerate them with
# scripts/make_art.py (needs Pillow) when the layout changes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/../third_party/mpc-plugin-builder-sdk"
PY="${PYTHON:-python3}"
cd "$ROOT"
"$PY" "$SDK/tools/build.py" "$ROOT" --target native >/dev/null
"$PY" scripts/gen_skin.py
"$PY" "$SDK/tools/build.py" "$ROOT" "$@"
LIB="$ROOT/build/native/curve-ui-test.dylib"
[ "$(uname)" = "Darwin" ] || LIB="${LIB%.dylib}.so"
mkdir -p build
c++ -std=c++17 -O2 -Wall -I "$SDK/framework" tests/scenario_host.cpp -o build/scenario_host
echo; echo "== SDK host simulator"; "$SDK/.sdk-tools/host_sim" "$LIB" test --seconds 2
echo; echo "== scenario host"; ./build/scenario_host "$LIB"
echo; echo "MPC folder: $ROOT/build/Slow Ops - VST - Curve UI Test"
