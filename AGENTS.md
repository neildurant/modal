# Modal

Modal is a fast, keyboard-driven MIDI sequencer for Linux and Windows, inspired by Voyetra Sequencer Plus Gold. It is a GUI application drawn on a fixed character grid, with Vim-like modal editing. It is MIDI-only: no audio tracks.

These are the instructions for any AI coding agent working on Modal (Claude Code reads them through `CLAUDE.md`, others through this file). Only one agent works at a time.

## Starting a session

1. Pull: another agent may have pushed since. `git log` from where you last were says what changed. Check the handoff for any build or test checks still outstanding (see Before pushing).
2. Read `docs/status.md` for the handoff and `docs/schedule.md` for the ongoing plan, current milestone and item states.
3. The full design is in `docs/design.md`, how each part of it was decided in `docs/decisions.md`, and the editing grammar as it is in `docs/grammar.md`. Read the parts the task touches before starting it, and follow them. If a task seems to require departing from the design, stop and ask rather than deciding alone.
4. Keep `docs/status.md` and `docs/schedule.md` up to date as work lands, so the next session, or the next agent, starts from them. Mark schedule items Doing when work starts and Done after validation and any required UI trial; keep pending checks explicit.

## How we work

- Work one milestone at a time, in the order given in `docs/schedule.md`. Don't build ahead into later milestones.
- Before writing code for a milestone or any substantial change, propose a short plan and wait for approval.
- Check the feature questions in the design's "To decide" section before implementing the current schedule item. Raise open items before implementing the parts they affect.
- Keep each kind of information in its home: schedule.md for work order, item states and unscheduled ideas; design.md for behaviour and unresolved design choices; grammar.md for implemented keys; configuration.md for configuration; decisions.md for dated reasoning; status.md for the current handoff and validation evidence. Link to the owning document instead of maintaining parallel progress lists. An agreed design does not mean its implementation is Done.
- Keep changes small and reviewable. One logical change per commit, with a clear message.
- Ask before committing a change that embeds a choice Neil might have an opinion on (behaviour, naming, keys, layout, style, dependencies), rather than only carrying out one already agreed.
- When a decision is made during implementation that the design doesn't cover, record it: the current behaviour in `docs/design.md` (and `docs/grammar.md` for keys and editing), and the decision, dated, with its reason, in `docs/decisions.md`. Say when a decision was made ("decided November 2026", "decided after use"), never who made it: no people's names in code, docs or commit messages.
- Neil tries changes to the UI before they're pushed: commit them locally, rebuild `modal`, and say what to try. Pushing a finished, passing step to `main` is otherwise fine.
- Neil runs anything involving real MIDI hardware, such as the loopback timing test. Don't claim timing results you haven't seen measured.
- Never drive the real desktop (sending keys to windows, taking screenshots of the screen) to test the GUI. Test through SDL's offscreen video driver, the `[.sample]` renders and unit tests, and ask Neil to try anything that needs the real window.

## Before pushing

- Run the whole-tree formatting check below, not just the files you remember touching.
- **Codex on native Linux:** build and test `linux-gcc-debug` and `linux-clang-debug`. Run Linux checks directly, without WSL, and skip local Windows builds. CI still checks Windows. Run the full Linux check (`tools/check-linux.sh`) when a change warrants it, and record any outstanding checks in `docs/status.md`.
- **Claude Code on Windows:** build and test `windows-msvc-debug`. MSVC misses some errors GCC and Clang refuse (an unused variable, a missing field initializer, a dangling temporary). When a change removes or moves code, adds struct fields or designated initializers, or changes interfaces, also build `linux-clang-debug` in WSL, with few jobs to spare memory: `wsl -d Ubuntu-24.04 --exec bash -c "cd /mnt/c/Users/neild/Src/modal && export VCPKG_ROOT=/opt/vcpkg && cmake --build --preset linux-clang-debug -j 4"`. Run the full Linux check when a change warrants it. On resuming after another agent, complete any outstanding checks from `docs/status.md` before starting new work; a recorded native Linux Clang pass satisfies the equivalent WSL check.
- After pushing, watch the CI run (`gh run list`, `gh run watch <id> --exit-status`) until it finishes, and report its result; confirm a pass with `gh run list --json conclusion` rather than trusting an exit code through a pipe.
- If `main` fails CI while untried local commits sit on top, fix it from a temporary worktree on `origin/main`, push that alone, and rebase the local work onto it.
- Before saying `modal` is ready to try, check that the executable (`modal` on Linux, `modal.exe` on Windows) is newer than the last change. On Windows it doesn't relink while Modal is running.

## Language and tooling

- C++20 throughout. No other implementation languages.
- CMake (3.25 or later), with presets for Linux (GCC and Clang) and Windows (MSVC).
- Dependencies via vcpkg in manifest mode (`vcpkg.json`, pinned `builtin-baseline`). No vendored copies of libraries unless agreed.
- Main libraries: libremidi (MIDI I/O), SDL3 and SDL_ttf or FreeType (UI, from milestone 3). JUCE only if and when in-process plugin hosting is started.
- Unit tests with Catch2 v3. Every engine feature gets tests.
- CI builds and tests on Linux and Windows from the first commit. Keep macOS compiling-possible: no platform APIs outside the platform layer.

## Code conventions

- Modal is licensed GPL-3.0-or-later. Every C++ file starts with `// SPDX-License-Identifier: GPL-3.0-or-later` and a blank line. A new library linked into the programs needs its licence added to `licenses/` and listed in `licenses/README.md`.
- Formatting via `.clang-format` in the repo root (clang-format 22.1.8, as pinned in CI); run it before committing.
- Naming: `PascalCase` types, `camelCase` functions and variables, `kPascalCase` constants, `member_` suffix for private data members, `snake_case` file names.
- Public headers live in `src/<lib>/include/modal/<lib>/*.hpp` and are included as `<modal/<lib>/x.hpp>`; tests are `tests/<lib>/*_test.cpp`.
- Warnings as errors on all compilers (`-Wall -Wextra -Wpedantic`, MSVC `/W4`).
- Prefer standard library types, RAII, value semantics and `std::span` / `std::string_view` over raw pointers.
- No exceptions or heap allocation on the real-time engine thread. Communicate with it only through lock-free queues and atomically swapped immutable song snapshots.
- Platform-specific code (MIDI backends, thread priority, timers, file locations) lives only in the platform layer, behind small interfaces.
- Never hard-code tick values. PPQN is a single named constant; musical lengths are note fractions converted to ticks at the point of use. Tests run at more than one PPQN (`GENERATE(core::kPpqn, core::Ppqn{96})`).
- Times are 64-bit integer ticks. Notes are stored as single events with a duration.
- Comments, docs and messages are written in plain British English, saying what something is for rather than restating the code.

## Tests

- A behaviour change comes with a test that fails without it. Where the UI's behaviour is concerned, tests drive a `Workspace` with keys (the `type` / `press` helpers in each test file's fixture) and check the song, the cursor and the status line.
- Catch2 test names mustn't start with `,` `~` `"` `/` or `!`: ctest or Catch reads those as separators, options or negation, and runs nothing.
- Files are LF. When editing from a script on Windows, write in binary mode so lines don't become CRLF.

## Repository layout

```
CMakeLists.txt
CMakePresets.json
README.md
AGENTS.md       these instructions (CLAUDE.md imports them)
docs/           status.md (the handoff), schedule.md (the ongoing milestone plan),
                design.md (the design as it is),
                decisions.md (how it was decided, dated), grammar.md, configuration.md
                and tools.md (for users)
assets/         fonts, the built-in modalrc and themes, built into the program
cmake/          the step that builds files from assets/ into the program
src/core/       engine library: song model, scheduler, recorder, file I/O (no UI dependencies)
src/platform/   MIDI backend interface and implementations, threads, timers, config directory
src/ui/         grid, layout, views, keys, keymaps and config, with no SDL (all unit-tested)
src/ui/sdl/     the SDL3 window, renderer, font and key translation
apps/           executables: modal (the GUI), command-line player, recorder, loopback timing tool
tests/          unit tests
```

## Building and testing

Requires `VCPKG_ROOT` pointing at a vcpkg clone (for example `C:\vcpkg` on Windows, or `/opt/vcpkg` on Linux). On Linux, also Ninja, autotools (`autoconf autoconf-archive automake libtool`, which vcpkg needs to build ALSA) the ALSA runtime (`libasound2t64`), which libremidi loads at run time, and the X11 and Wayland development packages vcpkg needs to build SDL3 (`pkg-config libx11-dev libxext-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev`). MIDI on Linux needs `/dev/snd/seq`, which WSL lacks.

Configure presets: `linux-gcc`, `linux-clang`, `linux-clang-asan` (ASan + UBSan, Debug only), `linux-clang-tsan` (ThreadSanitizer, Debug only), `windows-msvc`. Build and test presets append `-debug` or `-release` (except the sanitizer presets). Build trees go in `build/<preset>`. ThreadSanitizer's runtime clashes with the address randomisation of recent kernels: clang 18 and later notice and restart the tests with it off (printing a warning under `TSAN_OPTIONS=verbosity=1`), while older ones fail to start. `tools/check-linux.sh` runs it under `setarch -R`, which turns randomisation off for the tests alone and so avoids both; `sudo sysctl vm.mmap_rnd_bits=28` does it system-wide until reboot.

```
cmake --preset linux-gcc
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug

cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Check formatting: `git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror`.

On Linux (Debian or Ubuntu), `tools/setup-linux.sh` installs the packages, clang-format and vcpkg and configures the presets, and `tools/check-linux.sh` builds and tests every preset and checks the formatting, as CI does (`tools/check-linux.sh linux-gcc-debug` for one). Set `CMAKE_BUILD_PARALLEL_LEVEL=4` (or 8) in WSL, or the build can run it out of memory.

The command-line tools (`modal-ports`, `modal-play`, `modal-record`, `modal-loopback`), the hidden `[.timing]` tests and the `MODAL_TEST_ALSA_SEQ` flag for ALSA tests are described in `docs/tools.md`.

Configuration (`modalrc`, settings, key bindings, themes and `modal --write-config`) is described in `docs/configuration.md`.

The GUI is `modal` (in `build/<preset>/apps/Debug` or `Release`): `modal FILE` opens a `.modal` song or an imported `.mid` file, plain `modal` a new song, and `modal --demo` the demo song. Window tests run through SDL's offscreen video driver. The hidden `[.sample]` tests draw the font, the event list and the arrangement sketch to BMP files to look at (`MODAL_SAMPLE_BMP` names them), e.g. `modal_tests "[.sample]"`.

Neil's Linux laptop (Ubuntu, with a Roland UM-ONE MIDI interface) is for checks that need real hardware or a native Linux desktop (X11, Wayland, ALSA); Neil runs those, or asks for them.
