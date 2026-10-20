# Modal

Modal is a fast, keyboard-driven MIDI sequencer for Linux and Windows, inspired by Voyetra Sequencer Plus Gold. It's a GUI program drawn on a fixed character grid, like a terminal, with Vim-like modal editing. It's MIDI only: no audio tracks.

## Status

Early, and not yet usable for making music. Built so far:

1. **Engine and MIDI layer:** playing Standard MIDI Files with timing measured on real hardware.
2. **Recording:** with metronome, count-in and pre-roll.
3. **Grid renderer:** the window, font and grid, keys and key bindings, settings and themes, and a `:` prompt.
4. **Under way:** songs made of sections, saved in a plain-text format, with undo, and the event list, the first real view.

Editing comes next. The plan for the rest is in [docs/design.md](docs/design.md).

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

### The event list

`modal FILE` opens a song (`.modal`) or imports a MIDI file (`.mid`) and shows its event list, one track at a time; plain `modal` shows a made-up demo song. Each event is a row, with its fields in columns. The conductor's section starts, meters, tempos and markers are shown dimmed among a track's events, to find the way by; they're edited in the conductor's own list, track 0.

| Keys | What they do |
|------|--------------|
| `j` `k`, `h` `l`, arrow keys | Move between events and fields, with counts such as `12j` |
| `0` `$`, Home End | First and last field |
| `gg` `G` | First event and the end; with a count, that bar |
| `w` `b`, `}` `{`, `]]` `[[` | The event at or after the next or previous beat, the same place a bar on or back, or a bar line |
| `)` `(` | The next or previous chord: events within 30 ms of each other (`set chordwindow`) |
| `]s` `[s`, `]m` `[m` | The next or previous section's start, or marker |
| `d` `y` with a motion, `dd` `yy` | Delete or yank events: `dj` this event and the next, `d}` up to where `}` goes |
| `x`, `p` | Delete this event; put what was yanked or deleted at the cursor's time |
| `u` `Ctrl-R` | Undo and redo, going back to where the change was made |
| `gt` `gT` | Next and previous track, the conductor as track 0; `3gt` goes to track 3 |
| Ctrl with `j` `k` or the arrows | Move, scrolling so the cursor stays in place on screen |
| `Ctrl-D` `Ctrl-U`, `Ctrl-F` `Ctrl-B` | Scroll half a page, or a page |
| `zz` | Centre the cursor |
| `Ctrl-+` `Ctrl--` `Ctrl-0` | Text bigger, smaller, or back to its configured size |
| `:` | The command prompt: `:set theme=light`, `:set fontsize?`, `:write-config modalrc`, `:q` |

## Configuring

Settings, key bindings and colours live in a Vim-style `modalrc` file. `:write-config modalrc` writes one with everything in it to start from. See [docs/configuration.md](docs/configuration.md).

## Licence

Not chosen yet. The bundled JetBrains Mono font is under the SIL Open Font License ([assets/fonts/OFL.txt](assets/fonts/OFL.txt)).
