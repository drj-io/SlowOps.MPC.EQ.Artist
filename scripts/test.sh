#!/usr/bin/env bash
# Full verification: build both plugins, core unit tests, then for each plugin the SDK host simulator and
# the scenario host (which drives the real binary).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
EXT=dylib; [ "$(uname)" = "Darwin" ] || EXT=so
cd "$ROOT"
"$ROOT/scripts/build.sh"
echo; echo "== core tests"; ./build/core_tests
for v in dots lines; do
    LIB="$ROOT/plugins/$v/build/native/eq-artist-$v.$EXT"
    echo; echo "== $v: SDK host simulator"
    "$SDK/.sdk-tools/host_sim" "$LIB" test --seconds 4 --wav "plugins/$v/build/hostsim.wav"
    echo; echo "== $v: scenario host (real plugin binary)"
    ./build/scenario_host "$LIB" "plugins/$v/build"
done
echo; echo "ALL CHECKS PASSED"
