#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sets up a Debian or Ubuntu machine to build and test Modal: the packages CI installs, the
# compilers, CMake and Ninja, and vcpkg (cloned to $VCPKG_ROOT, or ~/vcpkg if that isn't set).
# Then configures every Linux preset. Safe to run again.
#
#   tools/setup-linux.sh
#
# The first configure builds every dependency through vcpkg, which takes a while.
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v apt-get > /dev/null; then
    echo "This script installs packages with apt-get (Debian, Ubuntu); install the equivalents"
    echo "of the packages listed in it by hand on other distributions."
    exit 1
fi

# The compilers and build tools; what vcpkg needs to fetch and build (autotools for ALSA); the
# ALSA runtime, which libremidi loads; and the X11 and Wayland development files SDL3 builds
# against. The same as CI's list, with the compilers and tools CI's image already has.
sudo apt-get update
sudo apt-get install -y build-essential gcc g++ clang cmake ninja-build git curl zip unzip tar \
    pkg-config python3 autoconf autoconf-archive automake libtool libasound2t64 \
    libx11-dev libxext-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev

# clang-format, pinned to CI's version, through pip, since distributions' versions differ.
if ! clang-format --version 2> /dev/null | grep -q "22.1.8"; then
    sudo apt-get install -y pipx
    pipx install "clang-format==22.1.8" --force
    echo "clang-format 22.1.8 is in ~/.local/bin; make sure that's on your PATH"
fi

VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
if [ ! -d "$VCPKG_ROOT" ]; then
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi
if [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi
export VCPKG_ROOT
if ! grep -q "VCPKG_ROOT" "$HOME/.bashrc" 2> /dev/null; then
    echo "export VCPKG_ROOT=\"$VCPKG_ROOT\"" >> "$HOME/.bashrc"
    echo "Added VCPKG_ROOT to ~/.bashrc"
fi

for preset in linux-gcc linux-clang linux-clang-asan linux-clang-tsan; do
    echo "== configuring $preset"
    cmake --preset "$preset"
done

echo
echo "Set up. tools/check-linux.sh builds and tests every preset."
echo "ThreadSanitizer needs 'sudo sysctl vm.mmap_rnd_bits=28' on recent kernels (check-linux.sh"
echo "says so if it's needed)."
