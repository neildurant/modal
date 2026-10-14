# Modal — Design

Oct 14, 2026 · @Neil Durant

## Overview

A fast, keyboard-driven MIDI sequencer for Windows and Linux: what Voyetra Sequencer Plus Gold might have been if it were designed in 2026. It is not a clone. It keeps SPG's goals (speed, precision, every action a keystroke away) and borrows its editing model from Vim, Emacs and Mutt.

It is a GUI application drawn on a fixed character grid, like a terminal but without a terminal's limits. It is MIDI-only: it records, edits and plays MIDI to hardware and to software synths, and leaves audio to other tools. Working title: Modal, after both modal editing and musical modes.

## Design principles

1. **Keyboard first, keyboard complete.** Every action is reachable from the keyboard. The mouse is optional and never required.
2. **The cursor is always exact.** It sits on one cell, one event, one tick. Nothing is ever ambiguously selected.
3. **A grammar, not a list of shortcuts.** Counts, operators, motions and objects compose, so users can do things nobody designed a feature for.
4. **Timing is sacred.** The playback and recording engine never waits on the UI, the disk or the renderer.
5. **Fast to start, fast to respond.** Launch in under a second; every keystroke redraws within one frame.
6. **Plain text where possible.** Songs, configuration and keymaps are human-readable and diffable.
7. **Show the data honestly.** The event list shows exactly what will be sent. No hidden state that changes playback.
8. **Small and owned.** Few dependencies, a codebase one person can understand end to end.

## Scope

Version 1 is a complete, reliable sequencer for one person recording and editing MIDI. Everything else waits until that core feels right in daily use.

| Area | Version 1 | Later | Out of scope |
| --- | --- | --- | --- |
| Tracks & events | Unlimited tracks; notes, CC, program change, pitch bend, aftertouch, SysEx | Patterns/phrases (nice to have); MIDI 2.0 high-resolution events | Audio tracks |
| Editing | Full editing grammar, leader menu, undo/redo, registers, macros | Undo tree, Lua-scripted operators | — |
| Transforms | SPG-style transforms (see MIDI transforms) | Key changes over time, user-defined transforms | — |
| Views | Arrangement, piano roll, event list; resizable grid | Tempo/controller lanes, splits | Full score notation |
| Customisation | Themes, key bindings, status bar segments | Theme editor, shareable config packs | — |
| Recording | Real-time, step entry, loop and punch | Take lanes, MIDI learn | — |
| Sync | Internal clock, MIDI clock out | MIDI clock in, MTC | SMPTE hardware sync |
| Files | Native text format, SMF type 0/1 import/export | SPG song import (if reverse-engineered) | DAW project formats |
| Sound | Any MIDI port, hardware or virtual | In-process plugin hosting (VST3/CLAP) | Audio recording, mixing |
| Platforms | Linux, Windows | macOS (kept possible by design) | Mobile, web |

## Data model

A song is a list of tracks, each holding a time-ordered list of events, with all times in 64-bit integer ticks at a fixed 960 PPQN. Files at other resolutions are converted on import. Notes are stored as single events with a duration, not as separate note-on/note-off pairs, which makes editing simpler and avoids stuck notes.

**Keeping resolution changeable.** 960 is the starting value, but nothing should depend on it:

- PPQN is one named constant; no literal tick values (480, 240, …) appear anywhere else in the code.
- Musical lengths (grid steps, step-entry durations, quantise values, keymap and config settings) are stored as fractions of a whole note, e.g. 1/16 or 1/8 triplet, and converted to ticks only at the point of use.
- Song files record their PPQN; loading a file at a different resolution rescales it, exactly as SMF import does.
- Display of bar:beat:tick derives its tick-field width from PPQN.
- Unit tests run at more than one resolution (e.g. 960 and 6720) to catch hidden assumptions.

**Per-track meters (polymeter)**

As in SPG, each track has its own sequence of bars and time signatures, so tracks can run different meters at the same moment (4/4 against 3/4, 7/8 against 4/4) for polyrhythmic writing.

- **Time is shared; bars are not.** All events sit on one timeline in ticks, and tempo is global. Bars are a per-track view of that timeline, so "bar 10" on a 3/4 track is earlier than bar 10 on a 4/4 track.
- **Reference track.** One track (track 1 by default) is the reference meter. Markers, the loop range, the metronome and the main position display are measured against it. SMF export writes its meter as the file's time signatures, since standard MIDI files have only one.
- **Positions are always relative to a track.** The cursor's position and the inspector use the bars of the track the cursor is on. Typed positions like `:goto 10` or `10G` use the current track's bars; `:goto ref 10` uses the reference track's.
- **Motions follow each track's bars.** `}` moves to the next bar of the current track. Operators that span several tracks, such as a Visual block in the arrangement, select a time range, which may start or end mid-bar on some tracks.
- **Copy and paste by time.** Pasting from a 3/4 track into a 4/4 track keeps the material's timing, not its bar positions.
- **Apply meter to a range.** In the arrangement, select a range and set a time signature (`:meter 3/4`, or from the leader menu). Bars are rebuilt from the range start; if the range doesn't divide evenly, the last bar becomes a shorter remainder bar, e.g. 10 beats in 3/4 gives three 3/4 bars and one 1/4 bar.
- **Unify meters.** `:meter unify` copies the reference track's meter map to the selected tracks (or all tracks), removing polymeter where it isn't wanted.
- **Arrangement drawing.** Columns represent time, not bars (decided: SPG's bar-aligned view made polymeter confusing). Each track draws its own bar lines, so tracks in different meters show bar lines at different places, and the ruler shows the reference track's bars.

**Selections across tracks in different meters**

- **Bar motions use the cursor's track.** In a Visual selection, `}` and `{` step by the bars of the track the cursor is on. Start on the 4/4 track and the selection grows in 4/4 bars, even across a 3/4 track; move the cursor onto the 3/4 track with `j` and it grows in 3/4 bars. This is the same rule as everywhere else, so nothing special to learn.
- **A motion for any bar line.** `]|` and `[|` move to the next or previous bar line on any track in the selection, for selecting exactly up to where the meters next coincide or diverge.
- **Partial bars are shown.** Where a selection edge falls mid-bar on a track, that edge is drawn half-shaded, so it's clear the track is only partly selected.
- **Operators act on the time range.** Transforms, copy, delete and quantise work on whatever falls inside it, whole bars or not. Operations that need whole bars, such as applying a meter, use the bars of the cursor's track.

* **Song:** title, PPQN, tempo map, which track is the reference meter, markers, tracks, global settings such as count-in and metronome.
* **Track:** name, output port, MIDI channel, its own meter (time-signature) map, mute/solo/arm, optional transpose and velocity offset applied at playback, list of events.
* **Event:** tick, type, and type-specific fields. A note has pitch, velocity, duration and release velocity. Other types are CC, program change, pitch bend, channel and poly aftertouch, SysEx and meta (text, markers).
* **Markers:** named positions used as motion targets, e.g. verse, chorus, bridge.
* **Patterns (later):** reusable phrases referenced from the arrangement, if a pattern-based workflow proves useful.

The model is immutable at the edit level: each edit produces a new song state, sharing unchanged tracks. That makes undo cheap and lets the engine read a consistent snapshot while the UI edits the next one.

## Editing grammar

Editing follows Vim's model: `[count] operator [count] motion-or-object`, with `.` to repeat the last change. Key choices below are proposals; everything is rebindable in the keymap file.

**Modes**

- **Normal:** navigate and apply operators. The default mode.
- **Insert:** type notes and values at the cursor (step entry from the computer keyboard or MIDI keyboard).
- **Visual:** select a range by time, by events, or by rectangle (time × pitch or time × track).
- **Command:** `:` opens a command line for named commands, e.g. `:quantize 1/16 swing=55`, `:tempo 96`, `:w`.

**Motions**

| Key | Moves to |
| --- | --- |
| `j` / `k` | Next / previous event (event list) or pitch (piano roll) |
| `h` / `l` | Previous / next grid step |
| `w` / `b` | Next / previous beat |
| `}` / `{` | Next / previous bar |
| `]m` / `[m` | Next / previous marker |
| `n` / `N` | Next / previous match of the last search (e.g. same pitch, same CC) |
| `gg` / `G` | Start / end of song; `12G` = bar 12 |
| `f` + note | Next note of that pitch on this track |

**Text objects** (used after an operator, `i` = inner, `a` = around)

- `ib` / `ab`: this bar; `ip`: this phrase (between markers); `ic`: this chord (notes starting together); `it`: this whole track.

**Operators**

| Key | Operation |
| --- | --- |
| `d` / `y` / `p` | Delete / yank / put |
| `>` / `<` | Transpose up / down a semitone (count = semitones; `+` octave variant) |
| `q` | Quantise to the current grid |
| `v` | Set or scale velocity (prompts for value or %) |
| `t` | Shift in time by the grid step |
| `~` | Humanise (small random time and velocity changes) |
| `c` | Change channel or destination track |

Examples: `d2}` deletes two bars, `>ib` transposes this bar up a semitone, `5.` repeats the last change five times.

**Registers, macros and undo**

- Named registers (`"a` to `"z`) hold phrases; pasting fits them to the current grid position.
- Macros record and replay key sequences, as in Vim but on `Q` (`Qa` … `Q`, `@a`), since `q` is quantise.
- Linear undo/redo in version 1 (`u`, `Ctrl-R`); an undo tree later.
- Transport keys work in every mode: Space play/stop, Enter play from cursor, `R` record.

**Notes crossing a range's edges**

- **By default, a range owns the notes that start inside it.** Copy, delete and transforms act on those notes whole, including any part that sustains past the range end. Notes that started before the range and are still sounding are left alone. Nothing is cropped, so a held pad chord stays one chord.
- **A cut-at-edges mode splits notes at the boundaries.** In Visual mode, `|` toggles cut-at-edges (shown in the status line and by the edge markers changing). Operators then split notes crossing a range edge at the boundary first, and affect only the inside parts. Deleting bars 9–10 from a sustained chord leaves the chord sounding up to bar 9 and from bar 11. Without a Visual selection, `|` before an operator does the same for that one operation (`|d}`).
- **The edges are visible.** In Visual mode, notes that cross an edge are drawn with a marker, so it's clear which ones will be taken whole and which left behind.

**Leaving silence or closing the gap**

Both are needed, so both are operators. Uppercase means "and shift the timeline":

- `d` deletes and leaves silence; `D` deletes and closes the gap, moving everything after the range earlier on the affected tracks.
- `p` puts material over what's there (merging); `P` inserts it, moving everything after the insertion point later.
- Ripple operations on one track move that track only; in an arrangement selection covering several tracks, all of them move together, keeping them in sync.
- Vim's own `D` (delete to end of line) is given up; `dG` deletes from the cursor to the end of the song.

**Leader key and hint menu**

Less frequent commands live behind a leader key, as in Doom Emacs: press the leader (comma in Normal and Visual modes, leaving Space for play/stop; Ctrl-Space works as an alternative leader in every mode, including Insert), then a short mnemonic sequence. After a brief pause a hint menu appears at the bottom of the window listing the next available keys and what they do, so commands are discoverable without being memorised first.

| Sequence | Command |
| --- | --- |
| `, f s` / `, f o` | File: save / open |
| `, t n` / `, t d` | Track: new / delete |
| `, x i` / `, x k` | Transform: invert / transpose in key |
| `, v a` / `, v p` | View: arrangement / piano roll |
| `, p o` | Ports: choose output for this track |

The most frequent operations stay on single keys for speed and muscle memory; the leader tree holds everything else. Both are defined in the keymap file, and hint labels come from the same definitions.

## MIDI transforms

SPG-style transforms are first-class operators: each applies to any motion, object or selection, takes parameters on the command line, and is reachable from the `, x` leader menu. Each is a pure function on a list of events, so it is easy to test.

| Transform | What it does |
| --- | --- |
| Transpose (chromatic) | Shift pitches by semitones or octaves |
| Transpose in key | Move notes by scale degrees, staying in the song's key and scale |
| Invert | Mirror pitches around a pivot note, chromatically or within the scale |
| Retrograde | Reverse the time order of notes in the range |
| Velocity compress / expand | Scale velocities towards or away from a centre value |
| Velocity scale, offset, limit | Multiply by %, add or subtract, clamp to a min/max |
| Velocity ramp | Crescendo or diminuendo across the range |
| Time stretch | Double time, half time, or any ratio, about the range start |
| Quantise | Grid, strength %, swing, start only or start and length |
| Humanise | Small random changes to time and velocity, within set limits |
| Length | Legato (extend to next note), fixed length, scale lengths |
| Thin / filter | Delete events matching a condition, e.g. velocity below 20 |
| Split | Move notes above or below a pitch to another track |
| Remap | Change channel, CC number or note mapping |

The song has a key and scale setting (key changes over time later), used by the in-key transforms and by step entry.

## Views

The arrangement is the home view; Enter on a track opens it in the piano roll for detailed editing, and the event list shows the exact data. All three share one cursor position and selection, so switching views (or `, v`) never loses your place. A status line and transport bar are always visible.

- **Arrangement (home view).** One row per track, one column per bar, cells shaded by note density, with markers along the top. Used for navigating the song and for whole-section operations: copy verse to bar 33, mute tracks, set loop range.
- **Piano roll (track editing).** Pitch rows against time, drawn on the grid with pixel-precise note starts and lengths inside the cells. The cursor moves note to note (`j`/`k`, Tab) or by grid step. A velocity lane underneath shows bars per note.
- **Event list (exact data).** One row per event: bar:beat:tick, type, channel, data fields (pitch name, velocity, duration). Every field is a cell you can move to and edit directly, with `Ctrl-A` / `Ctrl-X` incrementing and decrementing as in Vim. Filters show only notes, only CCs, a pitch range, etc.
- **Status line and transport.** Contents set in configuration (see Customisation).

**Resizing.** Text size stays fixed when the window resizes; the grid gains or loses cells. Extra columns show more time at the same zoom, extra rows show more tracks or pitches, and panels such as the velocity lane keep fixed heights while the main area takes the rest. `Ctrl-+` / `Ctrl--` change the font size, and with it the cell size.

Splits (later): two views side by side, e.g. arrangement above event list, as with Vim windows.

**Inspector**

Inspired by SPG's note information area, a strip at the top of the window always shows every property of the event under the cursor, in every view. For a note: position, note name, MIDI note number, velocity, duration, release velocity, channel and track. For a CC: controller number and name, value, channel.

```
 17:2:480  │  E3  (52)  │  vel 96  │  dur 0:1:240  (1/8.)  │  rel 64  │  ch 1  │  Bass
```

- **Edit fields from Normal mode.** `e` moves focus into the inspector; Tab and Shift-Tab move between fields; Enter confirms and Escape cancels, returning to the note.
- **Jump straight to a field.** Inside the inspector, a single letter jumps to its field: `p` pitch, `v` velocity, `d` duration, `c` channel.
- **Flexible input.** Pitch accepts a name or a number (`F#3` or `54`); duration accepts a note fraction (`1/8.`, `1/16t`) or ticks; any number field accepts a relative change (`+10`, `-5`) and `Ctrl-A` / `Ctrl-X` to step it.
- **Works on selections.** With several events selected, each field shows the common value or a range (`vel 64–90`); typing a value sets all of them, and a relative change adjusts each one.
- **Configurable.** Which fields appear, their order, and whether the strip sits at the top or bottom are set in the config file, like the status bar.

## Customisation

Themes, key bindings and the status bar are all defined in a plain-text command file from the start, so the defaults are just shipped configuration. The file uses the same commands as the `:` command line: anything you can type there can go in the file, and vice versa. It can be re-run with `:source` without restarting.

```
" ~/.config/modal/modalrc
set grid=1/16
set theme=dusk
nmap q quantize
nmap Q macro-record
leader , x i  invert         "Invert"
leader , x k  transpose-key  "Transpose in key"
highlight cursor   fg=#1a1b26 bg=#e0af68
highlight playhead fg=#f7768e
statusline left=mode,position,tempo right=key,loop,midi-in
```

The command language stays flat: no variables, conditionals or loops. If programmability is ever needed, it comes as a separate scripting language (Lua), not by growing the command syntax.

- **Themes.** Colours are assigned to semantic roles (cursor, selection, playhead, note, muted track, velocity bar, marker, status segments), never hard-coded in views. Light and dark themes ship by default; users add their own.
- **Key bindings.** Per-mode bindings and leader sequences, rebindable like Vim's `map`. Each binding names a command and a hint label for the leader menu. The defaults aim for stable muscle memory and won't be reshuffled between versions.
- **Status bar.** Built from named segments (mode, position, tempo, time signature, key, loop/punch, record state, MIDI activity, pending keys, message), each with a format string, arranged left and right in configuration, like a Vim or Doom Emacs modeline.

## Input and recording

Recorded events are timestamped by the engine at arrival, never by the UI, so recording accuracy is independent of screen activity.

- **Real-time recording:** optional count-in, metronome as MIDI notes to a chosen port, input quantise off by default. Recording goes to the armed track(s); thru to the track's output so you hear the right sound while playing.
- **Loop recording:** each pass is kept as a separate take; version 1 merges or keeps the last, take lanes come later.
- **Punch in/out:** set by markers or the current selection.
- **Step entry from a MIDI keyboard:** play a note or chord; it is entered at the cursor with the current step length, and the cursor advances. Keys on the computer keyboard change step length, insert rests and tie.
- **Step entry from the computer keyboard:** in Insert mode, letters enter pitches (e.g. `c d e f g a b` plus accidentals and octave keys) and digits set durations.
- **Playable computer keyboard (later):** a tracker-style key-to-pitch layout using physical scancodes, with proper note-off on key release.
- **MIDI thru and filtering:** per-input filters for channel, message type and range.

## Playback and recording behaviour

- **Chasing.** When playback starts mid-song, each track first sends its latest program change, CC values, pitch bend and channel pressure from before the start point, then note-ons for notes already sounding, for their remaining duration. Note chasing can be turned off per track, for drums and other sounds where a late attack sounds wrong.
- **Tempo.** The tempo map is evaluated at the start position, so playback begins at the right tempo. Tempo is internal timing, not something sent as MIDI.
- **Stop.** Note-offs are sent for every note currently sounding. All Notes Off and a sustain reset on each channel are optional, off by default. A panic key sends All Notes Off and resets controllers on every port and channel.
- **Tracks and channels.** As in SPG, each track has exactly one output port and one MIDI channel. Notes recorded on any input channel are rechannelised to the armed track's channel. Multi-channel tracks in imported MIDI files are split into one track per channel.
- **Recording.** Recording replaces existing material in the recorded range by default; overdub (merge) is a setting.
- **MIDI clock.** A keyboard-driven transport and sync settings screen chooses which ports send MIDI clock, Start/Stop/Continue and Song Position Pointer. These settings are stored in the song, with port names resolved through each machine's port aliases.

* **Panic.** `Ctrl-.` (a full stop, literally) sends All Notes Off and resets controllers on every port and channel, in every mode.
* **Recording input.** Armed tracks listen to all MIDI inputs by default; each track can optionally be set to one input.
* **MIDI thru.** On by default, so you hear the armed track's sound while playing; can be switched off globally or per track.

## Metronome and count-in

The metronome is part of milestone 2, since recording depends on it.

- **Sound.** Sent as MIDI notes to a chosen port and channel (e.g. a drum module, or a plugin via Carla), with separate notes and velocities for the downbeat and other beats. Once in-process audio exists, a built-in click becomes an option.
- **When it plays.** Off, during recording only (default), or always.
- **Which meter it follows.** The reference track's meter by default; optionally the cursor's track, for practising a part in a different meter.
- **Count-in.** None, 1 bar (default), 2 bars, or a custom number of beats, in the meter at the record start point.
- **Pre-roll.** As an alternative to a count-in, playback starts a set number of bars before the record point, so you play along with the song before recording begins; useful with punch in.
- **Subdivision.** Optional clicks on eighths or sixteenths for slow tempos.
- **Quick toggles.** On the `, m` leader menu (metronome on/off, count-in length, pre-roll), and as status-line segments.

## File formats

Songs are saved in a plain-text format designed to be read, diffed and version-controlled with git. Standard MIDI Files are the interchange format with everything else.

**Native song format (sketch, details to be settled)**

```
song "Untitled"  ppqn=960
tempo 1:1:0 120.00
timesig 1:1:0 4/4
marker 17:1:0 "Chorus"

track "Bass"  port="Pro-800"  ch=1
  1:1:0    note E1  v=96  len=0:2:0
  1:3:0    note E1  v=88  len=0:1:480
  2:1:0    cc 74  64
```

- One event per line, positions as bar:beat:tick so diffs are readable. Absolute positions mean inserting an event changes only one line.
- Ports are stored by name, with a mapping file per machine, so songs move between computers.
- Saves are atomic (write a temporary file, then rename), with optional numbered backups.

**Other files**

- **SMF import/export:** type 0 and 1, preserving tempo map, markers and track names.
- **Configuration and keymaps:** plain text, in the style of `.vimrc` / `.muttrc`, reloadable without restarting.
- **Port aliases:** a small per-machine file mapping friendly names to OS port names.

## Architecture

The engine is a separate library running its own high-priority thread; the UI is a client of it. The two communicate only through lock-free queues and atomically swapped song snapshots, so a slow redraw or file save can never delay a MIDI event.

&#91;embedded content: architecture · UI thread, engine thread, MIDI layer\]

The editor publishes a new immutable song snapshot after each edit; the scheduler picks it up between events. Recorded notes and the play position flow back through a queue.

- **Engine library (`core`):** song model, scheduler, recorder, SMF and native file I/O. No UI dependencies; usable from a command-line player and from tests.
- **Platform layer:** a small `MidiBackend` interface over libremidi, plus thread-priority and timer helpers per OS.
- **Grid UI:** a cell-grid abstraction (characters, colours, attributes) plus optional pixel regions for the piano roll and velocity lanes. Rendered with SDL3 now; a terminal backend stays possible later.
- **Editor:** the grammar parser, operators, motions, views and undo history, operating on the song model.

**Planning for in-process plugin hosting**

Hosting VST3/CLAP synths later means an audio engine alongside the MIDI engine. To keep that open from day one:

- The scheduler sends events to *destinations*, not directly to MIDI ports. A destination can be a MIDI port now and a hosted plugin instrument later.
- Plugin hosting would use JUCE's hosting and audio-device modules (ASIO on Windows, ALSA/JACK on Linux, CoreAudio on a Mac), with events delivered sample-accurately in the audio callback.
- Plugin editor windows need native windows next to the SDL3 grid UI; this is workable but needs a spike before committing. The alternative is a JUCE-based window for the whole UI, with the same grid renderer drawn through JUCE.
- JUCE's licence (AGPL or commercial) bears on the undecided licence question.

## Platform specifics

The engine does its own timing on both platforms, so behaviour doesn't depend on OS-level MIDI scheduling. Platform differences stay behind one small MIDI interface.

|  | Linux | Windows |
| --- | --- | --- |
| MIDI API | ALSA sequencer (via libremidi) | WinMM now; Windows MIDI Services where available (via libremidi) |
| Virtual ports | Native: the app creates its own named ports | Not in WinMM: needs loopMIDI or Windows MIDI Services loopback |
| Port sharing | Multi-client | Often single-client under WinMM |
| Engine thread priority | `SCHED_FIFO` via rtkit or realtime limits | MMCSS "Pro Audio" task |
| Sleeping until next event | `clock_nanosleep` on `CLOCK_MONOTONIC` | High-resolution waitable timer |
| Compiler | GCC / Clang | MSVC |

**Build and tooling**

- C++20, CMake, dependencies via vcpkg (or Conan): SDL3, SDL\_ttf or FreeType, libremidi, a test framework such as Catch2 or doctest.
- CI builds and unit tests on both platforms from the first commit.
- A bundled monospace font so the grid looks identical everywhere.
- A loopback timing test tool: send a steady stream out, receive it back through a cable, report latency and jitter statistics.

**Keeping macOS possible**

macOS isn't planned, but nothing should rule it out. SDL3, libremidi (CoreMIDI) and JUCE all support it, so the rules are:

- No Windows- or Linux-only APIs outside the platform layer; thread priority and timers stay behind the same helpers (macOS would use a Mach time-constraint thread).
- Keymaps treat the main modifier abstractly, so `Ctrl` bindings can map to `Cmd` on a Mac.
- File paths, config locations and fonts go through one small module, not scattered `#ifdef`s.
- CI could add a macOS build early, at low cost, just to keep it compiling.

## Milestones

Each milestone ends with something runnable and testable on both platforms. Timing is proven before any UI is built on top of it.

1. **Engine and MIDI layer.** Song model, SMF import, playback through libremidi, command-line player. Loopback timing tool with jitter measurements on Linux and Windows.
2. **Recording.** Timestamped input, record to track, metronome, count-in and pre-roll, SMF export. Command-line recorder, verified against the loopback tool.
3. **Grid renderer.** SDL3 window, cell grid, font rendering, cursor, scrolling, high-DPI, keyboard input layer, keymap and theme files, resizable grid.
4. **Event list view and core grammar.** Navigation, motions, delete/yank/put, undo, transport keys, native song format save/load, inspector panel. First version usable for real work.
5. **Full grammar.** Remaining operators, text objects and MIDI transforms, leader key and hint menu, counts, `.` repeat, registers, macros, command line.
6. **Piano roll and arrangement views.** Shared cursor and selection, view switching.
7. **Step entry, loop and punch recording.** Take handling.
8. **Polish for daily use.** Configurable status bar, settings, port aliases, backups, performance pass, documentation.

Later: in-process plugin hosting, patterns, MIDI clock in, take lanes, splits, scripting, macOS.

## To decide

Settled one milestone at a time, at the start of that milestone's work. Add to this list as new questions come up.

**Milestones 1–2: engine and recording**

- [ ] Scheduler look-ahead. Proposal: the engine sends each event itself at its exact time, preparing only a small window (around 10 ms) ahead; any edit, mute or tempo change during playback discards and rebuilds that window. If milestone 1's loopback measurements show too much jitter on a platform, that backend switches to OS-timestamped delivery (around 50 ms ahead, recalled on stop or edit). The MIDI interface is designed to allow either.
- [x] Panic key: `Ctrl-.`.
- [x] Record input: all MIDI inputs by default, optional per-track input. MIDI thru on by default, changeable.
- [ ] Build tooling details (vcpkg or Conan; Catch2 or doctest): Claude Code proposes, Neil approves.

**Milestones 3–4: grid and first editing view**

- [ ] Song file grammar: exact syntax, quoting, comments, how SysEx is written.
- [ ] Default keymap: the full set of single-key bindings, designed for muscle memory.
- [ ] Undo granularity: is a recording pass or a macro run one undo step?
- [ ] Cursor mapping when switching between arrangement, piano roll and event list.

* [ ] Remainder bars: always the exact leftover (e.g. 1/4, 5/16), or let the user choose to extend the previous bar instead?
* [ ] How the arrangement ruler and per-track bar lines are drawn when meters differ, at different zoom levels.
* [ ] Whether the time-signature map in an imported SMF applies to all tracks (likely) or just the reference track.

- [x] Delete can leave silence (`d`) or close the gap (`D`); put can merge (`p`) or insert (`P`).
- [x] Cut-at-edges key is `|` (rarely used, so its reach doesn't matter; rebindable like everything else).

**Later milestones**

- [ ] Default parameters for each transform, and how they appear in the hint menu and command line.
- [ ] Behaviour when a song's saved port isn't present on this machine.

## Decisions and open questions

- [x] Most important from SPG: a fast, stable key-binding model that builds muscle memory.
- [x] Vim-like modal editing, plus a Doom Emacs-style leader key with a hint menu.
- [x] Leader key is `,`, with Ctrl-Space as an alternative leader in every mode; Space stays play/stop.
- [x] `q` is quantise; macros move to another key (`Q` proposed).
- [x] 960 PPQN, fixed internally, matching the common DAW editing resolution; kept as one constant so it can be raised later.
- [x] Arrangement is the home view; piano roll for editing a track.
- [x] Patterns are a nice-to-have, not version 1.
- [x] In-process plugin hosting is wanted eventually.
- [x] Configuration is a Vim-style command file, sharing the `:` command language.
- [ ] Vim uses `,` to repeat an `f` search backwards; pick another key for that (e.g. `\`).
- [ ] Licence: undecided; note JUCE's AGPL/commercial terms if hosting plugins.
- [ ] Working title Modal: check clashes before any public release (e.g. Modal Electronics makes synths).

* [x] Language: C++20 throughout. C# was considered, but GC pauses in the real-time engine, weak Linux MIDI support in .NET libraries, and C++-only plugin hosting all favour C++, as does day-to-day familiarity.
