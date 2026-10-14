# Modal

Modal is a fast, keyboard-driven MIDI sequencer for Linux and Windows, inspired by Voyetra Sequencer Plus Gold. It is a GUI application drawn on a fixed character grid, with Vim-like modal editing. It is MIDI-only: no audio tracks.

The full design is in `docs/design.md`. Read it before starting any task, and follow it. If a task seems to require departing from the design, stop and ask rather than deciding alone.

## How we work

- Work one milestone at a time, in the order given in `docs/design.md`. Don't build ahead into later milestones.
- Before writing code for a milestone or any substantial change, propose a short plan and wait for approval.
- Check the design's "To decide" section for the current milestone. Raise any open items before implementing the parts they affect.
- Keep changes small and reviewable. One logical change per commit, with a clear message.
- When a decision is made during implementation that the design doesn't cover, note it so `docs/design.md` can be updated.
- I (Neil) run anything involving real MIDI hardware, such as the loopback timing test. Don't claim timing results you haven't seen measured.

## Language and tooling

- C++20 throughout. No other implementation languages.
- CMake (3.25 or later), with presets for Linux (GCC and Clang) and Windows (MSVC).
- Dependencies via a package manager (vcpkg or Conan, to be confirmed in milestone 1). No vendored copies of libraries unless agreed.
- Main libraries: libremidi (MIDI I/O), SDL3 and SDL_ttf or FreeType (UI, from milestone 3). JUCE only if and when in-process plugin hosting is started.
- Unit tests with Catch2 or doctest (to be confirmed in milestone 1). Every engine feature gets tests.
- CI builds and tests on Linux and Windows from the first commit. Keep macOS compiling-possible: no platform APIs outside the platform layer.

## Code conventions

- Formatting via `.clang-format` in the repo root; run it before committing.
- Warnings as errors on all compilers (`-Wall -Wextra -Wpedantic`, MSVC `/W4`).
- Prefer standard library types, RAII, value semantics and `std::span` / `std::string_view` over raw pointers.
- No exceptions or heap allocation on the real-time engine thread. Communicate with it only through lock-free queues and atomically swapped immutable song snapshots.
- Platform-specific code (MIDI backends, thread priority, timers, file locations) lives only in the platform layer, behind small interfaces.
- Never hard-code tick values. PPQN is a single named constant; musical lengths are note fractions converted to ticks at the point of use. Tests run at more than one PPQN.
- Times are 64-bit integer ticks. Notes are stored as single events with a duration.

## Repository layout (initial)

```
CMakeLists.txt
CMakePresets.json
docs/design.md
src/core/       engine library: song model, scheduler, recorder, file I/O (no UI dependencies)
src/platform/   MIDI backend interface and implementations, threads, timers
src/ui/         grid renderer and editor (from milestone 3)
apps/           executables: command-line player, recorder, loopback timing tool, later the GUI
tests/          unit tests
```

## Building and testing

To be filled in during milestone 1 with the exact commands, e.g.:

```
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```
