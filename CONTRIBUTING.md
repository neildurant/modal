# Contributing to Modal

Thanks for your interest in Modal. It's early days: one maintainer, and a design that's still
being built milestone by milestone. Contributions are welcome, and a little coordination first
saves wasted work.

## Before you start

- **Read the design.** [`docs/design.md`](docs/design.md) describes how Modal is meant to work,
  what's decided and what's still open, and the order things are being built in (see Milestones
  and To decide). Changes should follow it.
- **Open an issue first for anything substantial,** a new feature or a change to how something
  behaves, so we can agree the approach before you write the code. Small fixes, typos and clear
  bugs can go straight to a pull request.
- **Bug reports** are most useful with your operating system, how you built Modal (preset and
  compiler), the steps that show the problem, and for MIDI problems your interface and ports
  (`modal-ports` lists them). A song or MIDI file that shows it helps a lot.

## Building and testing

The [README](README.md) explains how to build Modal on Linux and Windows. Every change needs to
build without warnings (warnings are errors) and pass the tests on both:

```
cmake --preset linux-gcc
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

or the `windows-msvc` presets on Windows. CI runs GCC and Clang (Debug and Release), the
AddressSanitizer and ThreadSanitizer builds, and MSVC on every pull request.

## How the code is written

- **C++20, CMake and vcpkg.** No other languages, and dependencies come through vcpkg rather
  than copied into the repository.
- **Formatting:** run `clang-format` (22.1.8, as CI uses) on what you change. CI checks it:
  `git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror`.
- **Naming:** `PascalCase` types, `camelCase` functions and variables, `kPascalCase`
  constants, `member_` for private data members, `snake_case` file names.
- **Layout:** the engine in `src/core` (no UI), MIDI and other platform code only in
  `src/platform`, the views in `src/ui` (no SDL, so they can be tested), and SDL in
  `src/ui/sdl`. Public headers go in `src/<lib>/include/modal/<lib>/`.
- **Tests:** every engine feature, and every view behaviour that can be, gets Catch2 tests in
  `tests/<lib>/`. Anything that touches ticks runs at more than one resolution
  (`MODAL_ANY_PPQN()`).
- **Timing:** nothing on the engine thread allocates memory or throws; it talks to the rest
  only through lock-free queues and immutable song snapshots. Tick values are never written
  as numbers: lengths come from the PPQN constant.
- **Licence line:** every C++ file starts with `// SPDX-License-Identifier: GPL-3.0-or-later`.
  A new library linked into the programs needs its licence added to [`licenses/`](licenses/).

## Commits and pull requests

- One logical change per commit, with a message that says what it does and why.
- Keep pull requests small enough to review; several small ones beat one large one.
- If your change makes a decision the design doesn't cover, say so in the pull request, so
  `docs/design.md` can be updated with it.
- Timing claims need measurements on real hardware (`modal-loopback`, see
  [`docs/tools.md`](docs/tools.md)), with the interface and settings used.

## Licence

Modal is licensed under the GNU General Public License, version 3 or later. By contributing,
you agree that your contribution is licensed under the same terms.
