# SPDX-License-Identifier: GPL-3.0-or-later
#
# Short commands for building, testing and running Modal on Windows and Linux, through the CMake
# presets: `just build`, `just test`, `just run debug --demo`. `just` alone lists them. They're
# a convenience: the cmake commands in the README work without just.
#
# Each takes debug (the default) or release. On Linux, GCC by default: `just compiler=clang
# build` for Clang.

set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-Command"]

compiler := "gcc"
preset := if os() == "windows" { "windows-msvc" } else { "linux-" + compiler }
exe := if os() == "windows" { ".exe" } else { "" }
wsl_distro := "Ubuntu-24.04"

# List the commands
default:
    @{{just_executable()}} --list --unsorted

# Configure the preset, if it isn't already
configure:
    {{ if path_exists(justfile_directory() / "build" / preset / "CMakeCache.txt") == "true" { "" } else { "cmake --preset " + preset } }}

# Build Modal, its tools and tests (Windows can't relink modal while it's running)
build config="debug": configure
    cmake --build --preset {{preset}}-{{ if config =~ '^(debug|release)$' { config } else { error("the configuration is debug or release") } }}

# Run the unit tests, or those a Catch2 filter names: just test debug "*meter*"
test config="debug" filter="": (build config)
    ./build/{{preset}}/tests/{{capitalize(config)}}/modal_tests{{exe}} {{ if filter == "" { "" } else { quote(filter) } }}

# Build and start Modal, with its arguments: just run debug --demo
run config="debug" *args: (build config)
    ./build/{{preset}}/apps/{{capitalize(config)}}/modal{{exe}} {{args}}

# Format every C++ file
[linux]
fmt:
    git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format -i

# Format every C++ file
[windows]
fmt:
    clang-format -i (git ls-files '*.cpp' '*.hpp')

# Check the formatting, as CI does
[linux]
fmt-check:
    git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror

# Check the formatting, as CI does
[windows]
fmt-check:
    clang-format --dry-run --Werror (git ls-files '*.cpp' '*.hpp'); if ($LASTEXITCODE) { exit 1 }

# Every Linux preset built and tested, and the formatting, as CI does
[linux]
check:
    tools/check-linux.sh

# The Debug build and tests, and the formatting
[windows]
check: (test "debug") fmt-check

# The Clang Debug build and tests: in WSL on Windows, catching what MSVC lets through
[linux]
clang:
    {{ if path_exists(justfile_directory() / "build/linux-clang/CMakeCache.txt") == "true" { "" } else { "cmake --preset linux-clang" } }}
    cmake --build --preset linux-clang-debug
    ctest --preset linux-clang-debug

# The Clang Debug build and tests: in WSL on Windows, catching what MSVC lets through
[windows]
clang:
    wsl -d {{wsl_distro}} --cd "{{justfile_directory()}}" --exec bash -c "export VCPKG_ROOT=/opt/vcpkg; [ -f build/linux-clang/CMakeCache.txt ] || cmake --preset linux-clang; cmake --build --preset linux-clang-debug -j 4 && ctest --preset linux-clang-debug -j 4"

# Draw the hidden sample renders to build/samples, to look at
samples: (build "debug")
    cmake -E make_directory build/samples
    {{ if os() == "windows" { "$env:MODAL_SAMPLE_BMP='build/samples/sample.bmp'; " } else { "MODAL_SAMPLE_BMP=build/samples/sample.bmp " } }}./build/{{preset}}/tests/Debug/modal_tests{{exe}} '[.sample]'
