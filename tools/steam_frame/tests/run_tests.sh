#!/usr/bin/env bash
# Host tests for the Steam Frame code that needs no headset and no Android toolchain. These are
# host-level evidence only (nothing here talks to a runtime or a GPU).
#   tools/steam_frame/tests/run_tests.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
lus="$root/libultraship"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

echo "== binding table"
python3 "$here/check_bindings.py"

cxx="${CXX:-clang++}"
cc="${CC:-clang}"
sfvr="$lus/third_party/sfvr"

echo "== system layer (View timing, chord, Back pulse)"
"$cc" -std=c11 -Wall -Wextra -c "$sfvr/src/sfvr_view.c" -I"$sfvr/include" -o "$out/sfvr_view.o"
"$cxx" -std=c++17 -Wall -Wextra -DSOH_STEAM_FRAME=1 -I"$lus/include" -I"$sfvr/include" \
    "$here/test_vr_system.cpp" "$lus/src/fast/vr_system.cpp" "$out/sfvr_view.o" -o "$out/test_vr_system"
"$out/test_vr_system"

echo "== diagnostics (vr-diag.ini, synthetic head, BMP writer)"
"$cxx" -std=c++17 -Wall -Wextra -DSOH_STEAM_FRAME=1 -DVR_FRAME_DIAG_HOST_TEST=1 -I"$lus/include" \
    "$here/test_vr_diag.cpp" "$lus/src/fast/vr_frame_diag.cpp" -o "$out/test_vr_diag"
"$out/test_vr_diag"

echo "== [vr-perf] line format (sfvr's formatter, the Frame stage names)"
"$cc" -std=c11 -Wall -Wextra -c "$sfvr/src/sfvr_perf.c" -I"$sfvr/include" -o "$out/sfvr_perf.o"
"$cxx" -std=c++17 -Wall -Wextra -I"$sfvr/include" "$here/test_vr_perf.cpp" "$out/sfvr_perf.o" -o "$out/test_vr_perf"
"$out/test_vr_perf"

echo "all host tests passed"
