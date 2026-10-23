#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds and tests every Linux preset, as CI does, and checks the formatting. Prints a line for
# each preset, "BUILD FAILED" with the first errors if it didn't build, so a failed build never
# reads as old test results.
#
#   tools/check-linux.sh            every preset
#   tools/check-linux.sh linux-gcc-debug linux-clang-asan
#
# With MODAL_TEST_ALSA_SEQ=1 set, the tests that need the ALSA sequencer run too (see
# docs/tools.md).
set -uo pipefail
cd "$(dirname "$0")/.."

presets=("$@")
if [ ${#presets[@]} -eq 0 ]; then
    presets=(linux-gcc-debug linux-gcc-release linux-clang-debug linux-clang-release
             linux-clang-asan linux-clang-tsan)
fi

if command -v clang-format > /dev/null; then
    if git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror; then
        echo "== format: ok"
    else
        echo "== format: NEEDS clang-format"
    fi
fi

for preset in "${presets[@]}"; do
    echo "== $preset"
    log="$(mktemp)"
    if ! cmake --build --preset "$preset" > "$log" 2>&1; then
        echo "BUILD FAILED"
        grep -E "error|warning:" "$log" | head -10
        rm -f "$log"
        continue
    fi
    rm -f "$log"
    # ThreadSanitizer's runtime clashes with the address randomisation recent kernels use:
    # clang 18 and later restart the tests with it off, older ones fail to start. setarch -R
    # turns it off for the tests alone, avoiding both, with no sudo and nothing system-wide.
    runner=()
    if [[ "$preset" == *tsan* ]] && command -v setarch > /dev/null; then
        runner=(setarch -R)
    fi
    results="$("${runner[@]}" ctest --preset "$preset" 2>&1)"
    grep -E "tests passed|tests failed|\*\*\*Failed" <<< "$results"
    if grep -q "unexpected memory mapping" <<< "$results"; then
        echo "ThreadSanitizer can't start: try 'sudo sysctl vm.mmap_rnd_bits=28' (until reboot)"
    fi
done
