#!/usr/bin/env bash
# Full verification: build, core unit tests, SDK host simulator, scenario host (real binary).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
LIB="$ROOT/plugin/build/native/eq-artist.dylib"
[ "$(uname)" = "Darwin" ] || LIB="${LIB%.dylib}.so"
cd "$ROOT"
"$ROOT/scripts/build.sh"
echo; echo "== core tests"; ./build/core_tests
echo; echo "== SDK host simulator"
"$SDK/.sdk-tools/host_sim" "$LIB" test --seconds 4 --wav plugin/build/hostsim.wav
echo; echo "== scenario host (real plugin binary)"
./build/scenario_host "$LIB"
echo; echo "ALL CHECKS PASSED"
