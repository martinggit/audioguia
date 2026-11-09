#!/usr/bin/env bash
set -euo pipefail
app_root="$(realpath "$(dirname "$0")/..")"
sysroot="${AUDIOGUIA_LINUX_TOOLCHAIN:-$app_root/.tools/linux}"
compiler="$sysroot/usr/bin/x86_64-linux-gnu-g++-15"
output="$app_root/build/linux-tests"
mkdir -p "$output"
flags=(-std=c++17 -O1 -g -pthread -Wl,--sysroot="$sysroot" -idirafter "$sysroot/usr/include" -idirafter "$sysroot/usr/include/x86_64-linux-gnu" -B"$sysroot/usr/lib/x86_64-linux-gnu/" -I"$app_root/src" -I"$app_root/vendor")
core=("$app_root/src/domain/Models.cpp" "$app_root/src/domain/PlaybackPlanner.cpp" "$app_root/src/config/ConfigRepository.cpp" "$app_root/src/app/AppController.cpp")
for suite in core controller; do
    "$compiler" "${flags[@]}" "${core[@]}" "$app_root/tests/${suite}_tests.cpp" -o "$output/${suite}_tests"
    "$output/${suite}_tests"
done
"$compiler" "${flags[@]}" "$app_root/tests/keyboard_tests.cpp" -o "$output/keyboard_tests"
"$output/keyboard_tests"
"$compiler" "${flags[@]}" "$app_root/src/audio/MiniaudioPlayer.cpp" "$app_root/tests/audio_tests.cpp" -ldl -o "$output/audio_tests"
"$output/audio_tests"
