# Modal

Modal is a fast, keyboard-driven MIDI sequencer for Linux and Windows, inspired by Voyetra Sequencer Plus Gold. It's a GUI program drawn on a fixed character grid, like a terminal, with Vim-like modal editing. It's MIDI only: no audio tracks.

## Status

Early, and not yet usable for making music. Built so far:

1. **Engine and MIDI layer:** playing Standard MIDI Files with timing measured on real hardware.
2. **Recording:** with metronome, count-in and pre-roll.
3. **Grid renderer:** the window, font and grid, keys and key bindings, settings and themes, and a `:` prompt, shown through a test view.

The first real view, the event list, comes next. The plan for the rest is in [docs/design.md](docs/design.md).

## Building

Modal is C++20, built with CMake 3.25 or later, with its libraries from [vcpkg](https://vcpkg.io) in manifest mode. Set `VCPKG_ROOT` to a vcpkg clone, then use a preset: `windows-msvc` on Windows; `linux-gcc` or `linux-clang` on Linux.

```
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

On Linux, vcpkg also needs Ninja, `pkg-config`, autotools (`autoconf autoconf-archive automake libtool`) and the X11 and Wayland development packages (`libx11-dev libxext-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev`), and MIDI needs the ALSA runtime (`libasound2t64`).

## Running

The programs are in `build/<preset>/apps/Release` (or `Debug`):

- `modal` is the GUI.
- `modal-ports`, `modal-play`, `modal-record` and `modal-loopback` list MIDI ports, play and record MIDI files, and measure timing. See [docs/tools.md](docs/tools.md).

### The test view

Until the event list arrives, `modal` shows a test view of three pages, which Tab goes through:

- **Grid:** a large grid to move around.
- **Arrange:** a sketch of the arrangement view, with a made-up song.
- **Samples:** every theme colour, and the characters the views use.

| Keys | What they do |
|------|--------------|
| `h` `j` `k` `l`, arrow keys | Move, with counts such as `12j` |
| `0` `$`, Home End | First and last column, or bar |
| `gg` `G` | First and last row, or bar; with a count, that one |
| Ctrl with `h` `j` `k` `l` or the arrows | Move, scrolling so the cursor stays in place on screen |
| `Ctrl-D` `Ctrl-U`, `Ctrl-F` `Ctrl-B` | Scroll half a page, or a page |
| `z`, `zz` | Centre the view on the cursor; reset the arrangement's zoom |
| `+` `-` | Zoom the arrangement |
| `Ctrl-+` `Ctrl--` `Ctrl-0` | Text bigger, smaller, or back to its configured size |
| `:` | The command prompt: `:set theme=light`, `:set fontsize?`, `:write-config modalrc`, `:q` |

## Configuring

Settings, key bindings and colours live in a Vim-style `modalrc` file. `:write-config modalrc` writes one with everything in it to start from. See [docs/configuration.md](docs/configuration.md).

## Licence

Not chosen yet. The bundled JetBrains Mono font is under the SIL Open Font License ([assets/fonts/OFL.txt](assets/fonts/OFL.txt)).
