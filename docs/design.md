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
- The constant (`kPpqn`) is the resolution of new songs and the target of import. Every conversion takes the PPQN from the song rather than the constant, so the same code runs at any resolution in tests (decided in milestone 1).

**Tempo.** Tempo is stored as integer microseconds per quarter note, which round-trips SMF exactly, and shown as BPM. The accepted range is 1 to 1000 BPM; SMF tempos outside it are clamped on import. Tempo changes are steps (no ramps). Tick-to-time conversion splits each calculation into whole quarter notes plus remaining ticks, so no intermediate value overflows for centuries of song time, without 128-bit arithmetic. A tick's time is the first whole nanosecond not before it, and a time's tick is the last tick at or before it, so converting a tick to time and back always gives the same tick.

**Per-track meters (polymeter)**

As in SPG, each track has its own sequence of bars and time signatures, so tracks can run different meters at the same moment (4/4 against 3/4, 7/8 against 4/4) for polyrhythmic writing.

- **Time is shared; bars are not.** All events sit on one timeline in ticks, and tempo is global. Bars are a per-track view of that timeline, so "bar 10" on a 3/4 track is earlier than bar 10 on a 4/4 track.
- **Reference track.** One track (track 1 by default) is the reference meter. Markers, the loop range, the metronome and the main position display are measured against it. SMF export writes its meter as the file's time signatures, since standard MIDI files have only one.
- **Positions are always relative to a track.** The cursor's position and the inspector use the bars of the track the cursor is on. Typed positions like `:goto 10` or `10G` use the current track's bars; `:goto ref 10` uses the reference track's.
- **Motions follow each track's bars.** `}` moves to the same position in the next bar of the current track. Operators that span several tracks, such as a Visual block in the arrangement, select a time range, which may start or end mid-bar on some tracks.
- **Copy and paste by time.** Pasting from a 3/4 track into a 4/4 track keeps the material's timing, not its bar positions.
- **Apply meter to a range.** In the arrangement, select a range and set a time signature (`:meter 3/4`, or from the leader menu). Bars are rebuilt from the range start; if the range doesn't divide evenly, the last bar becomes a shorter remainder bar, e.g. 10 beats in 3/4 gives three 3/4 bars and one 1/4 bar.
- **Beats and remainder bars (decided in milestone 1).** The beat is the denominator note, so 6/8 counts six eighth-note beats. A meter set mid-bar ends that bar with a remainder bar of the exact leftover, using a finer denominator when the leftover isn't whole beats (half a beat into a 4/4 bar gives a 1/8 bar); a leftover no time signature can express, such as a single tick, moves the change on to the next beat. A change inside the first bar after another change replaces that bar's meter rather than adding a bar. Removing a change keeps later changes at their positions in time, adding remainder bars where needed, and repeating the meter already in effect adds nothing.
- **Unify meters.** `:meter unify` copies the reference track's meter map to the selected tracks (or all tracks), removing polymeter where it isn't wanted.
- **Arrangement drawing.** Columns represent time, not bars (decided: SPG's bar-aligned view made polymeter confusing). Each track draws its own bar lines, so tracks in different meters show bar lines at different places, and the ruler shows the reference track's bars.

**Selections across tracks in different meters**

- **Bar motions use the cursor's track.** In a Visual selection, `}` and `{` step by the bars of the track the cursor is on. Start on the 4/4 track and the selection grows in 4/4 bars, even across a 3/4 track; move the cursor onto the 3/4 track with `j` and it grows in 3/4 bars. This is the same rule as everywhere else, so nothing special to learn.
- **A motion for any bar line.** `]|` and `[|` move to the next or previous bar line on any track in the selection, for selecting exactly up to where the meters next coincide or diverge.
- **Partial bars are shown.** Where a selection edge falls mid-bar on a track, that edge is drawn half-shaded, so it's clear the track is only partly selected.
- **Operators act on the time range.** Transforms, copy, delete and quantise work on whatever falls inside it, whole bars or not. Operations that need whole bars, such as applying a meter, use the bars of the cursor's track.

* **Song:** title, PPQN, tempo map, which track is the reference meter, markers, tracks, global settings such as count-in and metronome.
* **Track:** a stable ID, name, output port, MIDI channel, its own meter (time-signature) map, mute/solo/arm, note chasing on/off, optional transpose, velocity offset and time offset applied at playback, grid and default note length (see Editing grammar), list of events. The ID survives edits, so the engine can follow a track from one song snapshot to the next.
* **Event:** tick, type, and type-specific fields. A note has pitch, velocity, duration and release velocity. Other types are CC, program change, pitch bend, channel and poly aftertouch, SysEx and meta (text, markers). Events carry no channel; the track's channel applies.
* **Event order.** A track's events are in tick order, and events at the same tick are sent in the order stored. Import and editing put non-note events (program change, CC, etc.) before notes at the same tick, so a program change at 1:1:0 always takes effect before the first note.
* **Overlapping notes.** The model allows overlapping notes of the same pitch, since imported files can contain them; the editor never creates them (see Playback and recording behaviour).
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

**Motions** (decided October 2026)

Motions in time work the same in every view. In the piano roll the cursor is also on a pitch (see Views).

| Key | Moves to |
| --- | --- |
| `h` / `l` | Previous / next grid step |
| `b` / `w` | Previous / next beat |
| `{` / `}` | The same position one bar earlier / later (`4}` = four bars later) |
| `[[` / `]]` | Previous / next bar line |
| `0` / `$` | Start / last grid step of the current bar |
| `j` / `k` | Next / previous event (event list); down / up a pitch step (piano roll, below) |
| `Tab` / `Shift-Tab` | Next / previous note start at any pitch; in the piano roll the cursor's pitch jumps to that note |
| `(` / `)` | Previous / next chord (a group of notes starting together) |
| `f` + note / `F` + note | Next / previous note of that pitch on this track, e.g. `fE` |
| `[m` / `]m` | Previous / next marker |
| `n` / `N` | Next / previous match of the last search (e.g. same pitch, same CC) |
| `gg` / `G` | Start / end of the track; `12G` = bar 12 |
| `ma` / `'a` | Set mark `a` / jump back to it: a time, pitch and track, as in Vim |

- Counts work on every motion: `3w`, `2}`, `4l`. As in Vim, `0` is a motion only when no count has been started.
- `Tab` lands exactly on a note's real start, even if it is off the grid. Grid motions from an off-grid position first snap to the next grid line, then step normally.
- Notes starting together are visited low to high by `Tab`; `)` skips the whole chord.
- **Bar motions remember the intended position.** If `}` moves into a shorter bar (e.g. 3/4 after 4/4) and the position would fall beyond its end, the cursor stops at that bar's last grid step but remembers where it wanted to be. Moving on into a longer bar restores the original position, as Vim does with columns across lines of different lengths.
- Bar motions use the bars of the current track (see Per-track meters). `{` / `}` mean "the same position, one bar on" in the arrangement too, so they mean the same thing everywhere.

**Moving in pitch (piano roll)**

| Key | Moves |
| --- | --- |
| `k` / `j` | Up / down one step: a semitone, or a scale step when scale lock is on |
| `K` / `J` | Up / down an octave (always) |
| `Ctrl-k` / `Ctrl-j` | Up / down a semitone, even when scale lock is on |
| `gk` / `gj` | To the next note above / below that is sounding at the cursor's time (moving through a chord voicing) |
| `gp` + pitch | Straight to a pitch, e.g. `gp C4 Enter` |

**Scale lock** is a toggle, shown in the status line (key to decide; `, s` proposed). When it is on, `k` / `j` move by scale steps in the song's key and scale (later, following the chord track if one exists). From a pitch outside the key, `k` / `j` move to the nearest scale tone in that direction. Scale lock also applies to moving notes (`Alt-k` / `Alt-j`) and to step entry.

**Moving and editing notes**

The same motions move notes when combined with Alt:

| Key | Effect |
| --- | --- |
| `Alt-k` / `Alt-j` | Move the current note (or selection) up / down one step (a semitone or a scale step, per scale lock) |
| `Alt-K` / `Alt-J` | Move up / down an octave |
| `Alt-l` / `Alt-h` | Move later / earlier by a grid step |
| `Alt-}` / `Alt-{` | Move later / earlier by a bar |
| `=` / `-` | Lengthen / shorten the current note by a grid step |
| `o` | Create a note at the cursor, with the default note length and the last-used velocity (to decide), without entering Insert mode |
| `x` | Delete the note under the cursor |

Counts apply: `3Alt-l` moves three grid steps later. Operators (`>ib`, `t}`, etc.) remain for larger ranges.

**Grid**

- Values: straight 1/1 to 1/64; triplets (1/4T, 1/8T, 1/16T, …); dotted values; any tuplet from the command line, e.g. `:grid 1/16 5:4`.
- Stored **per track** and saved in the song. Shown in the status line, e.g. `grid 1/8T`.
- The grid drives `h` / `l` movement, and `q` quantise unless a value is given (`:quantize 1/16`).

| Key | Effect |
| --- | --- |
| `Ctrl-h` / `Ctrl-l` | Coarser / finer grid, through the straight values |
| `Ctrl-t` | Toggle triplet (1/8 ↔ 1/8T) |
| `:grid …` | Set any grid exactly |

**Default note length**

An independent setting for the length of new notes, after Sequencer Plus Gold's default note length.

- **Linked to the grid by default:** the length follows the grid, and the status line shows `len =grid`.
- Changing the length unlinks it; it then stays fixed whatever the grid does, and the status line shows both, e.g. `grid 1/16 │ len 1/8T`. `:len grid`, or the leader menu, links it again.
- Stored per track and saved in the song.
- Used by `o`, step entry (from a MIDI keyboard or the computer keyboard) and inserted rests. Not used by recording: played notes keep their real lengths.

| Key | Effect |
| --- | --- |
| `+` / `_` | Longer / shorter default length |
| `, l` + keys | Set it directly from the leader menu: `4` quarter, `8` eighth, `6` sixteenth, then `t` triplet or `.` dotted (e.g. `, l 8 t` = 1/8T) |

`+` / `_` step through all the common lengths in order of actual duration, including triplets and dotted values: … 1/16 → 1/8T → 1/16. → 1/8 → 1/4T → 1/8. → 1/4 → 1/2T → 1/4. → 1/2 … The list is configurable, e.g. to leave out dotted values.

**Text objects** (used after an operator, `i` = inner, `a` = around)

- `ib` / `ab`: this bar; `ip`: this phrase (between markers); `ic`: this chord (notes starting together); `it`: this whole track.

**Operators**

| Key | Operation |
| --- | --- |
| `d` / `y` / `p` | Delete / yank / put |
| `>` / `<` | Transpose up / down a semitone (count = semitones) |
| `q` | Quantise to the current grid |
| `v` | Set or scale velocity (prompts for value or %) |
| `t` | Shift in time by the grid step |
| `~` | Humanise (small random time and velocity changes) |
| `c` | Change channel or destination track |

Examples: `d2}` deletes from the cursor to the same position two bars later, `>ib` transposes this bar up a semitone, `5.` repeats the last change five times.

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
- Vim's own `D` (delete to end of line) is given up; `dG` deletes from the cursor to the end of the track.

**Leader key and hint menu**

Less frequent commands live behind a leader key, as in Doom Emacs: press the leader (comma in Normal and Visual modes, leaving Space for play/stop; Ctrl-Space works as an alternative leader in every mode, including Insert), then a short mnemonic sequence. After a brief pause a hint menu appears at the bottom of the window listing the next available keys and what they do, so commands are discoverable without being memorised first.

| Sequence | Command |
| --- | --- |
| `, f s` / `, f o` | File: save / open |
| `, t n` / `, t d` | Track: new / delete |
| `, x i` / `, x k` | Transform: invert / transpose in key |
| `, v a` / `, v p` | View: arrangement / piano roll |
| `, p o` | Ports: choose output for this track |
| `, l` … | Default note length (see Default note length) |
| `, s` | Scale lock on/off (proposed; see To decide) |

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

- **Arrangement (home view).** One row per track, cells shaded by note density, with markers along the top. Since tracks can have different meters (decided in milestone 3), a column is a fixed length of time at the current zoom (such as one beat of the reference meter), not a bar: the track headers are text cells, but each row's timeline is a pixel region where the track draws its own bar lines at their exact positions, and the cursor is an exact span of time (such as bar 5 of that track) drawn to the pixel. The ruler numbers the reference track's bars. Used for navigating the song and for whole-section operations: copy verse to bar 33, mute tracks, set loop range.
  - **As sketched in milestone 3's test view** (to be confirmed in milestone 6). Each bar is a block as wide as the bar is long, as SPG's squares but in proportion, so a 7/8 bar is 7/8 as wide as a 4/4 bar. Blocks are shaded in four levels by how many notes start in the bar (theme roles `Density1` to `Density4`); a bar with no notes is a faint line, and an empty track shows a dot on each bar line. A small gap separates blocks while a column holds no more than a whole note; zoomed out further, the gaps go, since the song's overall shape matters more than single bars there. Markers are faint lines at the same time on every track whatever their meters, beneath the blocks and text, through the ruler (broken only where a label sits across them) and across the tracks; and are named in the ruler, each name cut short with an ellipsis at the next marker, or left out if fewer than three characters fit. The ruler also shows the reference track's meter changes, each named if the name fits before the next change and otherwise shown by a tick, so a single bar in another meter never hides the change after it; and its bar numbers, counted in sections that start at bar 1 and at each marker's bar, so the numbers follow the song's structure: each section's first bar, then every n bars into it, where n (1, 2, 4, 8...) is the fewest bars that leave room for the widest number in the shortest bar. A number shows only if it fits before the next section's, so the sections' numbers come first. Track headers show no meter, since a track's meter can change along it; the status line shows the meter at the cursor. The cursor is one bar of its track, boxed on all four sides; `h`/`l` move by that track's bars and `j`/`k` keep the cursor's time, landing on the bar that contains it, so going back up returns to the same bar. SPG's highlighting of every eighth bar isn't used, since sections often run to 6 or 12 bars, or have an extra bar; it could be an option later.
- **Piano roll (track editing).** Pitch rows against time, drawn on the grid with pixel-precise note starts and lengths inside the cells. A velocity lane underneath shows bars per note. The cursor is a crosshair, a position in time plus a pitch, and doesn't have to sit on a note. When it is at the start of a note or inside one, that note is the **current note**: the inspector shows it and operators act on it. On an empty spot, the inspector shows the position and pitch only. Motions are in Editing grammar.
- **Event list (exact data).** One row per event: bar:beat:tick, type, channel, data fields (pitch name, velocity, duration). Every field is a cell you can move to and edit directly, with `Ctrl-A` / `Ctrl-X` incrementing and decrementing as in Vim. Filters show only notes, only CCs, a pitch range, etc.
- **Status line and transport.** Contents set in configuration (see Customisation).

**Piano roll pages (decided October 2026).** The piano roll shows a whole number of bars at a time, not a freely scrolling timeline.

- The bars on a page together are stretched to fill the window width, whatever their length: a one-bar page of 7/8 is as wide as one of 4/4. Within a page, time keeps one scale, so on a page mixing meters a 7/8 bar is a little narrower than a 4/4 bar.
- When the cursor moves past the last bar on screen, the view turns to the next page.
- `zi` / `zo` change the number of bars per page, stepping through a list: 1, 2, 4, 8, 16 by default, configurable (e.g. `set pagebars=1,2,3,4,6,8,12,16`). A count sets the page size directly: `3zp` is 3 bars per page. When zooming, the new page is the one containing the cursor.
- **Pages restart at each marker.** With a marker at bar 3, 4-bar pages show bars 3–6, 7–10 and so on, so pages follow the song's sections.
- Vertically, the view scrolls to keep the cursor's pitch visible; `zz` centres the pitch range on the cursor.
- Grid lines are drawn at the current grid, with beat and bar lines stronger. If the grid is too fine to show legibly across the page, lines are drawn at the finest level that fits; movement still uses the real grid. Notes are always drawn at their exact positions, so recorded material never looks falsely quantised.

**Resizing.** Text size stays fixed when the window resizes; the grid gains or loses cells. In the arrangement and event list, extra columns show more time at the same zoom (the piano roll instead stretches its page of bars to the width), extra rows show more tracks or pitches, and panels such as the velocity lane keep fixed heights while the main area takes the rest. `Ctrl-+` / `Ctrl--` change the font size, and with it the cell size. So that resizing is smooth rather than jumping a cell at a time (decided in milestone 3), regions are laid out in pixels: bars at the top are anchored to the top edge and bars at the bottom to the bottom edge, so they follow the window edge a pixel at a time, and right-aligned status segments follow the right edge. The main area takes everything between, including a partly visible last row and column, clipped, which are revealed gradually as the window grows. The cursor and scrolling count only fully visible rows and columns, so the cursor never sits on a half-hidden one.

**Grid regions and bars (decided in milestone 3).** Only the views you move around cell by cell (event list, arrangement, piano roll) are grid regions, laid out in cells. The bars (status line, inspector, command line, hint menu) aren't navigated as a grid, so they're laid out as segments of text at pixel positions instead:

- **A terminal look.** Bars use the same font and theme colours as everything else, with no bevels, gradients, borders or OS-style controls: a bar is a line of text on a theme-coloured background, like a Vim status line, so it blends in.
- **Groups anchored to edges.** Segments flow left to right in a left, centre or right group. The left group is anchored to the left edge, the right group to the right edge (so it follows the window edge a pixel at a time) and the centre group to the centre. Each segment keeps its own colours and attributes, and the bar's background fills its whole width.
- **Padding.** A margin between each group and its window edge (half a cell's width by default) and a minimum gap between groups (two cells by default), as settings.
- **Too little room.** The right group is kept whole, and the left group gives way: its last segment is cut short with `…`, and a segment that can't show at least a few characters is dropped. If the right group alone doesn't fit, it is cut from its left end the same way. Per-segment priorities can come with the configurable status line (milestone 8).
- **Fields without a grid.** Segments can be focusable fields, so `Tab` moves between the inspector's fields without a grid underneath.
- A bar is one line of text high, plus padding, and is anchored to its window edge (see Resizing). Bars that aren't text, such as the piano roll's ruler, are pixel regions that stretch with the width.

**Minimum window size (decided in milestone 3).** The window can't be dragged smaller than 80 × 24 cells, a setting (`set minsize=80x24`) that follows the font size: if a larger font puts the window below the minimum, the window grows to fit, as far as the screen allows. The default bars fit at the minimum, so the overflow rule above is a safety net for long content (a long track name, a message) rather than a routine sight.

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
set pagebars=1,2,4,8,16
set theme=dusk
nmap q quantize
nmap Q macro-record
leader , x i  invert         "Invert"
leader , x k  transpose-key  "Transpose in key"
highlight cursor      #e0af68
highlight cursor-text #1a1b26
statusline left=mode,position,tempo right=key,loop,midi-in
```

The command language stays flat: no variables, conditionals or loops. If programmability is ever needed, it comes as a separate scripting language (Lua), not by growing the command syntax.

- **Themes.** Colours are assigned to semantic roles (cursor, selection, playhead, note, muted track, velocity bar, marker, status segments), never hard-coded in views. Light and dark themes ship by default; users add their own.
- **Key bindings.** Per-mode bindings and leader sequences, rebindable like Vim's `map`. Each binding names a command and a hint label for the leader menu. Commands are named in lower case with hyphens, by what they act on: `cursor-left` moves the cursor, so a command moving notes left can't be mistaken for it (decided in milestone 3). The modes are Normal, Visual, Insert, operator-pending and command line. The defaults aim for stable muscle memory and won't be reshuffled between versions.
- **Config files (decided in milestone 3).** The user's command file is `~/.config/modal/modalrc` on Linux and `%APPDATA%\Modal\modalrc` on Windows, found through the platform layer. The shipped defaults (a default command file and the light and dark themes) are built into the program, so it runs with no files installed; the user's file runs after them. `set theme=NAME` starts again from the dark theme, then runs `themes/NAME` from the config directory, or else the built-in theme of that name, so a theme of your own need only set the colours it changes; `highlight` lines after it override single colours. The built-in files are ordinary command files in the repository (`assets/config/modalrc` with the default key bindings, `assets/themes/dark` and `light`, after Tokyo Night and Tokyo Night Day), built into the program. An error reports the file and line, and the rest of the file still runs; at startup errors go to stderr, and the first shows in the status line in the warning colour until the next key. Until the full command line exists (milestone 5), the file and the `:` prompt take only the commands that exist so far: `set`, `highlight`, the `map` family and `source`. User-facing details are in `docs/configuration.md`.
- **Command file syntax (decided in milestone 3).** A command a line; a line starting with `"` is a comment, as in Vim. Arguments are separated by spaces, and as in a shell, double quotes keep spaces in an argument (`set font="C:/My Fonts/Mono.ttf"`), with `\"` and `\\` inside; but a map command's keys are taken as they are, since `"` is a key. `highlight ROLE #rrggbb` sets one role's colour, so the cursor's text is `cursor-text` rather than a foreground half of a `cursor` group: roles are named in lower case with hyphens, one colour each. The map commands are `nmap`, `vmap`, `imap`, `omap` and `cmap`, each `KEYS COMMAND ["HINT"]`, with `nunmap` and so on; there is no plain `map` (Vim's covers three modes, which surprises), and no `noremap`, since bindings name commands rather than other keys. `source FILE` runs a file relative to the one running it.
- **Settings registry and generated config (decided in milestone 3).** Every setting is registered with its name, type, default, range and a one-line description, which later also drive `:set` completion, hint labels and error messages. Setting names are lower case without hyphens, as Vim's options are (`timeoutlen`, `fontsize`), while command names have hyphens (`cursor-left`). The first settings are `theme`, `font`, `fontsize`, `minsize` and `timeoutlen`. `modal --write-config FILE` (with `--force` to overwrite) and, with the `:` prompt, `:write-config FILE` (`:write-config!` to overwrite) write every setting, key binding and colour, in sections, each with a comment saying what it does, and list the commands keys can be bound to. What differs from the defaults is written as commands (settings with their current values, key bindings as `nmap` and `nunmap` lines against the built-in `modalrc`, colours as `highlight` lines against the chosen theme); the rest is commented out at its defaults, so a user changes only what they want and keeps later improvements to the rest, and what they're using now, including a size set with `Ctrl-+`, is kept (decided in milestone 3). Read back, the file gives the same config. A relative FILE is in the config directory for `:write-config` (the program's own current directory means little when started from a menu) and the current directory for `modal --write-config`, which reads `modalrc` first. It writes to a named file rather than to stdout, since its one use is to start a config file, and on Windows `modal` has no console. It is never written automatically.
- **The `:` prompt (decided in milestone 3).** `:` opens a prompt in the bottom bar, in command-line mode, whose editing keys are `cmap` bindings in the built-in `modalrc` (Enter, Escape, Backspace, Delete, the arrows, Home, End, `Ctrl-U`, Up and Down); any other character is typed, and Backspace on an empty line closes it, as in Vim. Digits are counts only in Normal, Visual and operator-pending modes, so they're typed at the prompt. Up and Down recall the session's lines; Tab completion waits for the full command line (milestone 5). The prompt runs the command-file commands, `set NAME?` to show settings, `source` with no file to run `modalrc` again (over the current state, as Vim's does), `write-config`, and `q` or `quit`. Settings, bindings and colours apply at once. Errors show in the warning colour and results in the `message` colour.
- **Status bar.** Built from named segments (mode, position, tempo, time signature, key, loop/punch, record state, MIDI activity, pending keys, message), each with a format string, arranged left and right in configuration, like a Vim or Doom Emacs modeline.

## Input and recording

Recorded events are timestamped by the engine at arrival, never by the UI, so recording accuracy is independent of screen activity.

- **Real-time recording:** optional count-in, metronome as MIDI notes to a chosen port, input quantise off by default. Recording goes to the armed track(s); thru to the track's output so you hear the right sound while playing.
- **Loop recording:** each pass is kept as a separate take; version 1 merges or keeps the last, take lanes come later.
- **Punch in/out:** set by markers or the current selection.
- **Step entry from a MIDI keyboard:** play a note or chord; it is entered at the cursor with the default note length (see Editing grammar), and the cursor advances. Keys on the computer keyboard change the length, insert rests and tie.
- **Step entry from the computer keyboard:** in Insert mode, letters enter pitches (e.g. `c d e f g a b` plus accidentals and octave keys) and digits set durations.
- **Playable computer keyboard (later):** a tracker-style key-to-pitch layout using physical scancodes, with proper note-off on key release.
- **MIDI thru and filtering:** per-input filters for channel, message type and range.
- **What is recorded (decided in milestone 2).** All channel messages are recorded and sent thru. SysEx is recorded but not sent thru, since WinMM blocks while sending it; it reaches the engine in fixed-size chunks, so nothing allocates, which limits one SysEx to 32 KB. MIDI clock, active sensing and the other system real-time and system common messages (such as MTC and song position) are ignored.
- **Replace (decided in milestone 2).** On the armed tracks, events starting between the record point and where recording stopped are removed, and an earlier note running into that range is cut short at the record point.
- **Count-in and recording (decided in milestone 2).** Anything played during the count-in is dropped, except what is still held at the record point: a held note starts there, and so does a held pedal. Other controllers moved during the count-in are dropped. Recording carries on past the song's end until stopped, with the metronome still playing.
- **Pedals (decided in milestone 2).** Sustain (CC 64), sostenuto (CC 66) and soft (CC 67) are stored as ordinary CC events, never folded into note lengths. So that a take never leaves a pedal down: a pedal still down when recording stops gets a pedal-up at the stop point, as held notes end there. When replace cuts through older pedal data, the old material is patched at both edges: a pedal-up at the record point if the old track had the pedal down there, and a pedal-down at the stop point if the old track had it down there, so what follows sounds as before.
- **Takes (decided in milestone 2).** A take is collected while recording and applied to the song when recording stops, as one edit, so a recording pass can later be one undo step. Every armed track gets the whole take (until tracks can be set to one input), on its own channel. The recorded range runs from the record point up to, not including, the stop point. A note played on a pitch already sounding in the take, from any input or channel, ends the earlier note where the new one starts, and at the same tick replaces it; overdubbing applies the same rule between the take and the old notes (whichever starts first ends where the other starts, and at the same tick the take's note wins), so recording never creates overlapping notes of one pitch. A note whose note-on and note-off arrive on the same tick lasts one tick. Overdubbing doesn't patch pedals at the record point, but if the take lets a pedal up at the stop point where the old track had it down, the old pedal goes down again there.

## Playback and recording behaviour

- **Chasing.** When playback starts mid-song, each track first sends its latest program change, CC values, pitch bend and channel pressure from before the start point, then note-ons for notes already sounding, for their remaining duration. Note chasing can be turned off per track, for drums and other sounds where a late attack sounds wrong. Chasing is per track (two tracks on one channel each send their own values), the values go out in the order they were originally sent (so bank select stays before program change, and RPN/NRPN sequences stay in order), and "before the start point" means stored position, not offset send time. Not chased: channel mode messages (CC 120–127), poly aftertouch and SysEx.
- **Mute and solo.** If any track is soloed, only soloed tracks play, even a muted one; otherwise unmuted tracks play.
- **Playback transpose and velocity offset.** A note transposed outside 0–127 isn't sent, and the player reports how many notes each track loses this way. Velocities are clamped to 1–127 after the offset, never 0 (which would be a note-off).
- **Tempo.** The tempo map is evaluated at the start position, so playback begins at the right tempo. Tempo is internal timing, not something sent as MIDI.
- **Track time offset.** As in SPG, each track has a time offset in milliseconds (−1000 to +1000), a track setting like transpose, applied at playback only. It shifts every event on the track, e.g. sending slow-attack string chords 40 ms ahead of the beat. Milliseconds rather than ticks, since it usually compensates for a real-world duration and should stay the same at any tempo. Stored events never move: the event list shows their positions as entered, and the offset is shown with the track's other settings (track header, inspector), never hidden. For a negative offset at the playback start point, the engine starts its clock early by the largest negative offset of any track, so early events still play in time. The metronome and MIDI clock are never offset. Recording onto a track with an offset subtracts it, so takes play back as they were heard.
- **Overlapping notes.** When notes of the same pitch on the same channel overlap, each new note retriggers (note-off, then note-on), and the final note-off is sent at the latest end of the overlapping notes, so a long note keeps sounding past a short one inside it. The editor never creates overlapping notes; overlaps in imported files are kept as they are, since the data is shown honestly.
- **Stop.** Note-offs are sent for every note currently sounding. The scheduler remembers which channels it has left with a pedal down (sustain, sostenuto or soft) and sends a pedal-up to just those, always (decided in milestone 2), up to 256 pedals; it does the same when playback restarts from another point while playing, before chasing there. All Notes Off and a sustain reset on every channel used are optional, off by default. A panic key stops playback and sends All Notes Off and resets controllers on every channel of every port playback has used.
- **Transport (GUI).** Easy ways both to stop and resume from where playback stopped, and to stop and replay from where it last started. The interim `modal-play` has only stop and resume (space).
- **Tracks and channels.** As in SPG, each track has exactly one output port and one MIDI channel. Notes recorded on any input channel are rechannelised to the armed track's channel. Multi-channel tracks in imported MIDI files are split into one track per channel.
- **Recording.** Recording replaces existing material in the recorded range by default; overdub (merge) is a setting.
- **MIDI clock.** A keyboard-driven transport and sync settings screen chooses which ports send MIDI clock, Start/Stop/Continue and Song Position Pointer. These settings are stored in the song, with port names resolved through each machine's port aliases.

* **Panic.** `Ctrl-.` (a full stop, literally) stops playback and sends All Notes Off and resets controllers on every channel of every port playback has used, in every mode. The interim command-line tools use `!` while playing and a `--panic` option instead, since terminals can't reliably detect `Ctrl-.`.
* **Recording input.** Armed tracks listen to all MIDI inputs by default; each track can optionally be set to one input.
* **MIDI thru.** On by default, so you hear the armed track's sound while playing; can be switched off globally or per track.
* **How thru behaves (decided in milestone 2).** Thru works whether or not the song is playing, so sounds can be tried before recording. It goes to every armed track with thru on, including muted and un-soloed ones, since thru is how a track being recorded is heard. Each gets the track's channel, transpose and velocity offset (so you hear what playback will sound like) but not its time offset. The engine remembers where each held thru note and pedal went (up to 1,024 notes and 256 pedals; a note-on beyond that isn't sent), so its note-off or pedal-up goes to the same place even if the track is disarmed or changed meanwhile. Stop leaves held thru notes alone, since their note-offs will follow; panic ends them, and reaches thru's ports too. Thru delay, from arrival to just after the send call, is measured separately from playback lateness.

## Metronome and count-in

The metronome is part of milestone 2, since recording depends on it.

- **Sound.** Sent as MIDI notes to a chosen port and channel (e.g. a drum module, or a plugin via Carla), with separate notes and velocities for the downbeat and other beats. Once in-process audio exists, a built-in click becomes an option.
- **When it plays.** Off, during recording only (default), or always.
- **Which meter it follows.** The reference track's meter by default; optionally the cursor's track, for practising a part in a different meter.
- **Count-in.** None, 1 bar (default), 2 bars, or a custom number of beats, in the meter at the record start point.
- **Pre-roll.** As an alternative to a count-in, playback starts a set number of bars before the record point, so you play along with the song before recording begins; useful with punch in.
- **Subdivision.** Optional clicks on eighths or sixteenths for slow tempos.
- **Defaults and generation (decided in milestone 2).** Channel 10; downbeat note 76 (Hi Wood Block) at velocity 110, other beats note 77 (Low Wood Block) at velocity 80, each a 1/32 note long. The port is chosen when recording until settings exist (milestone 8). The scheduler generates the clicks from the reference meter and tempo map rather than the plan's events, so they carry on past the song's end; they are never offset, muted or soloed. Count-in uses the tempo and reference meter at the record point.
- **Clicks (decided in milestone 2).** One click per beat, the beat being the time signature's denominator (so 6/8 clicks six eighths), with the first beat of each bar accented; other divisions, such as dotted quarters in 6/8 or the subdivisions above, come later. Playback starting mid-beat first clicks on the next beat. A click is cut short where the next beat comes sooner than its length. When recording, the metronome carries on past the song's end until stopped; in plain playback with the metronome always on, it stops when the song ends (with the last note-off), since there it is a timing reference for the song rather than a practice click. Whether this playback records (for "during recording only") is decided on the control side when the plan is compiled, so the engine needs no recording flag.
- **Count-in and pre-roll (decided in milestone 2).** One song setting chooses none, a count-in (1 bar by default, or a custom number of beats) or a pre-roll (1 bar by default). A count-in always clicks, whatever the metronome's "when" setting, since otherwise there's nothing to count with; without a metronome port there is no count-in. It comes before the record point with the song silent, at the tempo there, and runs the record point's meter backwards, so its accents fall where bars would start: from beat 3 of 4/4, a 1-bar count-in clicks beats 3, 4, 1 (accented) and 2. It is planned on the control side and carried with the play request, like chased events, so the engine allocates nothing; its clicks are at fixed times, so a tempo edit during a count-in doesn't move them. If a track's negative time offset reaches further back than the count-in, playback starts that much earlier. A pre-roll goes back the length of the bars before the record point's bar, keeping the position in the bar, and starts at bar 1 if there aren't enough bars.
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
- **SMF export (decided in milestone 2).** Type 1 by default, type 0 as an option, at the song's PPQN. The first track holds the title, tempo map, the reference track's meter and the markers; then one track per Modal track, with its name and channel. Events are written as stored: transpose, velocity offset, time offset and mute/solo aren't applied, so the file matches the event list. Port names aren't written until port aliases exist.
- **SMF export details (decided in milestone 2).** Each Modal track gets a channel prefix as well as its name; a type 0 file has only the title as its track name. At each tick come note-offs, then other events in stored order (so a program change precedes the notes it applies to), then note-ons, then the note-offs of zero-length notes, so a note ending and another starting on the same pitch and tick don't cut each other off. Running status is used, and reset after meta and SysEx events. Each track ends at its last event or note end, and the first track at the latest of all, since some programs take the song's length from it. Overlapping notes of one pitch are written as stored; reading them back pairs note-offs first on, first off, which sounds the same in Modal's playback. Tempos slower than an SMF holds (about 3.58 BPM) are clamped with a warning; a resolution above 32,767 or a gap between events over 2^28 ticks can't be written. Files are written to a temporary file and renamed into place. Empty tracks are written, but Modal's own import skips tracks with no events.
- **SMF import rules (milestone 1).** Files are rescaled to the song's PPQN, rounding each note's start and end separately so touching notes still touch. Note-ons and note-offs pair first on, first off; notes still sounding at the end of a track end there. Each file track becomes one track per channel it uses, named "Name (ch N)" when split; its SysEx and text go to the first of those, or to a track of their own if it has no channel events. A track with only tempo, meter, markers and a name (a conductor track) doesn't become a track; in a type 0 file, or a type 1 file whose first track is a conductor track, the first track's name is the song title. Tempo, time signatures (applied to every track) and markers are read from any track. Running status is accepted across meta and SysEx events, split SysEx packets are joined, and escapes other than complete SysEx messages are dropped. Not supported: type 2 files and SMPTE-timed files. Ignored: port and channel prefixes, SMPTE offset, key signature, sequencer-specific events. Text is kept as the file's raw bytes. Damaged files are read as far as possible, and every problem worked around is reported as a warning.
- **Configuration and keymaps:** plain text, in the style of `.vimrc` / `.muttrc`, reloadable without restarting.
- **Port aliases:** a small per-machine file mapping friendly names to OS port names.

## Architecture

The engine is a separate library running its own high-priority thread; the UI is a client of it. The two communicate only through lock-free queues and atomically swapped song snapshots, so a slow redraw or file save can never delay a MIDI event.

&#91;embedded content: architecture · UI thread, engine thread, MIDI layer\]

The editor publishes a new immutable song snapshot after each edit; the scheduler picks it up between events. Recorded notes and the play position flow back through a queue.

- **Snapshot handoff.** Songs share unchanged tracks through `shared_ptr<const Track>`. Snapshots reach the engine through a lock-free single-producer, single-consumer queue, not `std::atomic<shared_ptr>` (which takes a lock on common standard libraries). Retired snapshots are handed back and freed off the engine thread, so the engine never runs a destructor or frees memory.
- **Playback plan.** Each snapshot is compiled off the engine thread into a flat, time-sorted plan with destination, channel, transpose, velocity offset and mute/solo already applied. Notes keep their durations in the plan; the engine generates note-offs from its own table of sounding notes, which also serves stop, panic, overlapping notes and plan swaps during playback.
- **Engine wake-ups.** The engine sleeps until its next event, or at most 1 ms, so transport commands are seen within 1 ms (within the spin margin just before an event; see Clock).
- **Scheduler.** The scheduler plays a plan without doing any timing itself: the engine passes it the time on each wake-up, so tests drive it with made-up times. At equal times, note-offs go before other events, so touching notes of the same pitch retrigger cleanly. Its table of sounding notes holds 4,096 notes, allocated up front; if it is ever full, a new note-on isn't sent (and is counted), rather than risk a stuck note. Panic reaches up to 64 destinations: every one any plan has used.
- **Plan swaps during playback.** Everything due under the old plan is sent first; playback then carries on from the same tick in the new plan, so a tempo change keeps the bar and beat. Events not yet sent come from the new plan. Sounding notes end at their original tick, at the new tempo and time offset, even if the edit changed or removed the note; notes on tracks no longer in the plan (muted, removed or unrouted) end at once.
- **Stop options.** All Notes Off and sustain reset on stop are scheduler settings, set by `modal-play` flags, until settings exist (milestone 8). Each is sent once per destination and channel the plan's tracks use.
- **Engine commands.** Play, stop and panic go to the engine through one ordered command queue, so a quick play then stop can't be reordered. The chased events for a play are computed on the control side and travel with the plan snapshot, so the engine never allocates them. The engine keeps the snapshot it replaced alive until its next update, so it can finish with the old plan while switching. When the song ends, the engine reports it and stays in play until told to stop; what happens then is up to the client (`modal-play` exits). The engine reports the tick at which each play stopped (decided in milestone 2), so a take ends exactly where the stop took effect rather than where the control side thought it was. On each pass the engine takes in input before carrying out commands, since that input arrived before the commands take effect: a note that arrives just before a stop is carried out belongs to the take, even if the stop was given first.
- **Input and thru (decided in milestone 2).** Each input port feeds the engine through its own fixed-size lock-free queue of messages timestamped on arrival. The engine wakes as soon as input arrives (the clock has a wake call: an event waited on alongside the timer on Windows, an eventfd polled alongside an absolute timerfd on Linux), sends it thru to the armed tracks' ports and channels, and stamps it with its song tick and its time into the song (so the recorder can take a track's time offset off exactly), since only the engine knows the current time-to-tick mapping exactly, including tempo edits during a take. Stamped input goes back to the control side through a queue (4,096 chunks; what doesn't fit is counted), where the recorder turns it into events. All input goes back, playing or not, marked with the play it arrived during; the control side decides what a take keeps, so a note pressed just before playback and still held at the record point is known about. Input before the song's start (a count-in or lead-in from bar 1) is stamped with ticks before 0, at the starting tempo, so the recorder can tell it from input on the downbeat; the displayed position still stops at 0.
- **Send lateness.** Measured from each message's due time to just after it was handed to the MIDI layer: the engine's wake-up accuracy plus the driver call. This is the figure behind the ~1 ms rule for switching to OS-timestamped delivery; the time a message leaves the cable is what the loopback test measures. It is also reported in its two parts, wake lateness (from the due time to the start of the send call) and send time (in the call), so a slow timer and a slow driver can be told apart. Each is a count, the maximum and a histogram in 50 µs steps up to 20 ms.

- **Engine library (`core`):** song model, scheduler, recorder, SMF and native file I/O. No UI dependencies; usable from a command-line player and from tests.
- **Platform layer:** MIDI outputs behind the scheduler's small message-sink interface, timestamped MIDI input and port lookup by name, all over libremidi; plus thread-priority, clock and console-key helpers per OS.
- **Grid UI:** a cell-grid abstraction (characters, colours, attributes) plus optional pixel regions for the piano roll, velocity lanes and arrangement timelines. Rendered with SDL3 now; a terminal backend stays possible later.
- **Rendering (decided in milestone 3).** SDL3's 2D renderer, with text through SDL_ttf (FreeType, without HarfBuzz, since a fixed grid needs no shaping). Each glyph is rendered once into a texture atlas and each cell is drawn from it. Box drawing lines (light and heavy) and block elements are drawn as exact rectangles rather than from the font, as terminals do, since the font's own glyphs leave seams between cells and its partial blocks aren't exact eighths; the shades are flat tints rather than dither patterns, for density shading. Pixel regions are rectangles of whole cells, so they fit the layout, inside which a view draws filled rectangles and lines at exact pixel positions, clipped to the region, in the same pass as the text. The window redraws only when something changes, synced to the display: every frame while the playhead moves, otherwise only on input or a resize, so an idle window takes no CPU.
- **Cell size and high DPI (decided in milestone 3).** The font size is in points, scaled by the display's pixel density. Cells are a whole number of pixels, from the font's advance and line height, so the grid never blurs or drifts. Moving to a display with another scale, or `Ctrl-+` / `Ctrl--`, renders the atlas again. On Windows, SDL3 works in real pixels and reports the display's scaling (such as 150%) as the display scale, so the font is scaled by that and the text is crisp. The window opens at a size in cells (120 × 36 by default), so it's the same size relative to its text on any display.
- **Text size keys (decided in milestone 3).** `Ctrl-+` and `Ctrl--` (`font-bigger`, `font-smaller`) change the text a point at a time, with counts, within `fontsize`'s range of 4 to 72, and `Ctrl-0` (`font-reset`) goes back to the size the config sets; plus is bound as `<C-=>`, `<C-S-=>` and `<C-+>` for the layouts that type it differently, and the keypad's as `<C-kPlus>`, `<C-kMinus>` and `<C-k0>`. The window keeps its size and the grid gains or loses cells, as in a terminal; it grows only to stay at the minimum size. The `fontsize` setting follows the change for the session, but nothing is written to the user's files. The new size and cell size show in the status line in the `message` colour until the next key. These are the window's commands, run by the program around the view, which hands it commands it doesn't have. Thin lines and the space beside them are a hairline, a seventh of a cell's width and at least a pixel, so drawing keeps its proportions at any size.
- **Keys with Ctrl or Alt (decided in milestone 3).** SDL's key code has Shift applied, so the key's own character comes from its scancode in the keyboard layout, unshifted: Ctrl-Shift-d is `<C-S-d>`. The keypad types its characters, so a binding of `+` works from either plus key, except with Ctrl or Alt, when nothing is typed and its keys arrive as `<kPlus>`, `<kMinus>` and `<k0>` to `<k9>`, Vim's names for them.
- **Keys (decided in milestone 3).** Printable keys are bound by the character they type, so `|`, `"` or `D` work whichever key types them on any layout. Ctrl and Alt combinations and special keys are bound by key code, written in Vim notation (`<C-a>`, `<Esc>`, `<S-Tab>`, `<F5>`). Physical scancodes are kept for the later playable computer keyboard. The main modifier is abstract, so it can be Cmd on a Mac. As in Vim, a key that only starts longer bindings waits for the next key, and one that is both a binding and the start of a longer one waits up to `timeoutlen` (half a second by default, a setting) before running the shorter; pending keys show in the status line, with the count. A key that continues no binding runs the keys before it if they're a binding, and is then taken afresh; otherwise those keys and the count are given up, and Escape gives up whatever is waiting. With Ctrl or Alt a letter is the same in either case, so `<C-d>` and `<C-D>` are both Ctrl-d as in Vim, and Ctrl-Shift-d is written `<C-S-d>`. A typed character comes from the system's text input, which follows the keyboard layout and dead keys; with Ctrl or Alt, which type no text, it is the key's own unshifted character, with Shift as a modifier. Ctrl and Alt held together are taken as AltGr, which types text on many layouts, so they can't be bound together with a character.
- **UI code layout (decided in milestone 3).** `src/ui` holds the cell grid, layout, key handling, keymaps and the config parser, with no SDL, so all of it is unit-tested directly; `src/ui/sdl` holds the renderer and key translation, smoke-tested through SDL's offscreen video driver in CI; `apps/modal` is the program. `src/ui` links the core library, for ticks, meters and the song model.
- **Test view (decided in milestone 3).** Three pages, gone through with Tab. The grid page has a 1000 × 1000 grid of cells labelled with their row and column, ten characters wide, under fixed column numbers and beside fixed row numbers, as the event list will have fixed headers; above it, a pixel region shows four bars of 4/4 over bars of 7/8 on the same time scale, with bar numbers placed to the pixel and notes. The arrange page sketches the arrangement (see Views) with a made-up song: sections in several meters changing on every track at once, a single extra 2/4 bar, two tracks in meters of their own, markers and four densities of notes, with `+` (or `=`) and `-` to zoom through seven zoom levels, from a sixteenth note to four whole notes in a column, a quarter note by default. The samples page shows each theme role, the text attributes and the characters the views use. Keys go through the keymap to named commands, with counts as in Vim: `hjkl` and the arrow keys (`cursor-left` and so on), `0` (or Home) and `$` (or End, `{count}$` ending on a later row), `gg` and `G` (`go-first`, `go-last`, or with a count the row or bar counted), `Ctrl-D`/`Ctrl-U` by half a screen and `Ctrl-F`/`Ctrl-B` (or Page Down and Page Up) by a screen less two rows, as Vim does, `z` to centre the view on the cursor (`scroll-centre`) and `zz` to reset the zoom (`zoom-reset`), which also try out `timeoutlen`, since `z` waits to see if another `z` follows. Escape, or a key with no binding, gives up a count. The cursor stays in the rows and columns fully shown, scrolling only as far as it must, including when the window shrinks.
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
| Engine thread priority | `SCHED_FIFO` via realtime limits (`audio` group or `limits.conf`); no rtkit for now; warns if unavailable | MMCSS "Pro Audio" task |
| Sleeping until next event | Absolute `timerfd` on `CLOCK_MONOTONIC`, polled with an `eventfd` for wakes | High-resolution waitable timer |
| Compiler | GCC / Clang | MSVC |

- **Clock.** The engine's clock is the steady clock (`CLOCK_MONOTONIC` on Linux, the performance counter on Windows). MIDI input is timestamped on the same clock when it arrives, so recorded and loopback times compare directly with send times. The sleep never wakes early. Before an event, the engine sleeps until a margin before it (1.5 ms by default) and busy-waits the rest, since the first Windows loopback run (October 2026, Babyface, WinMM) showed sleeping alone woke 99% of the time within 0.95 ms, at worst 1.7 ms late. The engine always tells the clock when the next event is due, so the spin starts a margin before it even when that falls inside a routine 1 ms wake-up; a command arriving during a spin waits for it, so commands are seen within the margin rather than 1 ms. With the spin, the engine's own waking on that machine (measured without MIDI) went from 99% within 0.85 ms to 99% within 0.05 ms. The margin is a `--spin` option on the command-line tools, for measuring.
- **Measured timing, Windows.** Loopback runs by Neil, October 2026, on one PC with a Babyface interface through WinMM and a MIDI cable, using `modal-loopback`'s default stream (2,000 notes, one every 10 ms):

  |  | No spin | Spin (1.5 ms) |
  | --- | --- | --- |
  | Latency, due to back: median / p99 / max | 1.46 / 1.95 / 2.38 ms | 1.10 / 1.36 / 1.79 ms |
  | Jitter: p99 − median / standard deviation | 0.49 / 0.21 ms | 0.26 / 0.07 ms |
  | Send lateness: p99 / max | 0.90 / 1.29 ms | 0.35 / 0.54 ms |
  | of which waking: p99 / max | 0.80 / 1.19 ms | within 0.05 / 0.04 ms |

  All messages came back, in order. With the spin, what remains is the driver call (up to about 0.5 ms) and the interface and cable (about 1 ms, mostly the 0.96 ms a 3-byte message takes on the wire). Both are well under the 1 ms threshold for OS-timestamped delivery, so Windows stays engine-timed. Linux is still to be measured, on a native install (see To decide).
- **Measured recording, Windows.** A `modal-loopback --record` run by Neil, October 2026, on the same setup with the spin, using the default stream at 120 BPM and 960 PPQN (a tick is about 0.52 ms): all 2,000 notes were recorded, none missing or extra, and every one at the tick its arrival time gives. Recorded positions were 1 to 3 ticks late (median 2), which is the loopback latency (median 1.12 ms, max 1.84 ms) rounded down to a tick, and recorded lengths were within one tick of those sent, from rounding each end. A take from a MIDI keyboard through `modal-record` was also checked in the saved file.
- **Opening ports by name.** A port name given by the user picks out the port with exactly that name, or else the one port with that name ignoring case, or else the one port whose name contains it ignoring case. If several match equally well, or none, the error lists the candidates or all the ports. Ports are opened before playback starts; devices appearing and disappearing during a session come later.
- **SysEx on Windows blocks.** libremidi's WinMM backend waits until a SysEx message has been fully sent, about 0.3 ms per byte at MIDI's speed, and may allocate to copy it. Accepted for milestone 1, since SysEx usually comes at the start of a song. The fix (a SysEx thread per port, keeping later messages to that port behind it, or Windows MIDI Services) stays inside the MIDI backend, behind the scheduler's message sink.
- **Engine thread priority.** `SCHED_FIFO` at priority 70 on Linux; the MMCSS "Pro Audio" task at high priority on Windows. If it can't be raised, the engine runs anyway and says why.

**Build and tooling**

- C++20, CMake, dependencies via vcpkg in manifest mode with a pinned baseline: SDL3, SDL\_ttf or FreeType, libremidi, Catch2 v3 for tests.
- CI builds and unit tests on both platforms from the first commit.
- **Testing on Linux, in three tiers.** Unit tests run in WSL (Ubuntu 24.04) and in CI with GCC and Clang. ALSA integration tests (ports through `snd-seq-dummy`, notes between virtual ports through libremidi) run on a native Linux install, since neither WSL nor the GitHub-hosted runners have an ALSA sequencer: the runners' Azure kernel ships no sound modules (checked October 2026). A self-hosted runner on that machine could bring them into CI later. Timing is measured only on a native Linux install with real MIDI hardware and a loopback cable.
- A bundled monospace font so the grid looks identical everywhere: JetBrains Mono NL (the version without ligatures), under the SIL Open Font License, shipped with its licence (decided in milestone 3). It has box drawing, shades and eighth blocks, arrows, `♯` and `♭`; it lacks `♮` and the note symbols. Its bold weight is bundled too, for bold text, since synthesised bold looks noticeably cruder. A `font` setting can choose any other.
- A loopback timing test tool: send a steady stream out, receive it back through a cable, report latency and jitter statistics.
- A command-line recorder, `modal-record` (decided in milestone 2): records from an input onto a new track, or merges into an existing one with `--overdub`, optionally playing along with a file, and writes the result as a MIDI file. Its bars come from the play-along file's tempo map and meter, or else from `--tempo` (default 120 BPM) and `--meter` (default 4/4), which can't be combined with a file. Recording is checked through the cable by a `--record` mode of the loopback tool, which reports how far each recorded position is from what was sent. In that mode (decided in milestone 2) the stream is a song at 120 BPM with no metronome or count-in, MIDI thru is off since it would send the stream round the cable again, and the take can be saved with `--save` to look at. It also checks that every note is recorded at the tick its arrival time gives.

**Keeping macOS possible**

macOS isn't planned, but nothing should rule it out. SDL3, libremidi (CoreMIDI) and JUCE all support it, so the rules are:

- No Windows- or Linux-only APIs outside the platform layer; thread priority and timers stay behind the same helpers (macOS would use a Mach time-constraint thread).
- Keymaps treat the main modifier abstractly, so `Ctrl` bindings can map to `Cmd` on a Mac.
- File paths, config locations and fonts go through one small module, not scattered `#ifdef`s.
- CI could add a macOS build early, at low cost, just to keep it compiling.

## Milestones

Each milestone ends with something runnable and testable on both platforms. Timing is proven before any UI is built on top of it.

1. **Engine and MIDI layer.** Song model, SMF import, playback through libremidi, command-line player. Loopback timing tool with jitter measurements on Linux and Windows. *Done, October 2026, except the Linux measurements.*
2. **Recording.** Timestamped input, record to track, metronome, count-in and pre-roll, SMF export. Command-line recorder, verified against the loopback tool. *Done, October 2026, except the Linux measurements.*
3. **Grid renderer.** SDL3 window, cell grid, font rendering, cursor, scrolling, high-DPI, keyboard input layer, keymap and theme files, resizable grid. Until the event list arrives in milestone 4, the program shows a test view (decided in milestone 3): a large scrollable grid of labelled cells, sample text in each theme role and a pixel-region demo of bar lines in two meters, with counts on cursor motions and a minimal `:` prompt.
4. **Event list view and core grammar.** Navigation, motions, delete/yank/put, undo, transport keys, native song format save/load, inspector panel. First version usable for real work.
5. **Full grammar.** Remaining operators, text objects and MIDI transforms, leader key and hint menu, counts, `.` repeat, registers, macros, command line.
6. **Piano roll and arrangement views.** Shared cursor and selection, view switching.
7. **Step entry, loop and punch recording.** Take handling.
8. **Polish for daily use.** Configurable status bar, settings, port aliases, backups, performance pass, documentation.

Later: in-process plugin hosting, patterns, MIDI clock in, take lanes, splits, scripting, macOS.

## Ideas (not scheduled)

Distinctive features to consider after version 1. Not committed to any milestone.

**Finding and developing ideas**
- **Musical search and replace:** `/` searches by interval pattern or rhythm, in any key; `n` jumps between matches; `:s` substitutes across the song.
- **Linked variations:** copies that stay linked to the original, optionally with a transform (inverted, transposed, half speed); editing the original updates every variation.
- **Retrospective capture:** MIDI input is always buffered; one key turns the last few minutes of playing into a take.

**Recording**
- **Half-speed recording:** record at half the tempo (or another fraction) and play back at full speed, for passages too fast to play in; similar uses to step entry.
- **Loop takes to choose from:** loop over a section, keeping each pass as a take, then audition and pick the best afterwards (building on milestone 7's loop recording and the take lanes listed under Later).

**Rhythm and time**
- **Polytempo:** a track can run at a ratio of the song tempo (e.g. 3:2) for phasing and tempo canons.
- **Free-time recording:** record without a click, then tap or mark downbeats to fit a tempo map and bar lines.
- **Tuplets and generators:** arbitrary and nested tuplets (7:4, 5:3) as grid settings; Euclidean rhythm transform.
- **Stretch and compress:** a transform that scales a range of bars in time, by a percentage or to a target length in beats or bars.

**Song structure and editing**
- **Sections as blocks:** working with blocks of music larger than a bar, such as a chorus, as a unit: either operations on the range between start and end markers, or a structural primitive holding a group of bars.
- **Track loops:** as in SPG, a track can loop (shown by an `L` at its end); a starting point to build on.
- **Split screen:** several tracks side by side, scrolled together, for editing notes across them in sync.

**Harmony and arranging**
- **Chord track:** a chord-progression lane used by step entry, transpose-in-key and reharmonisation.
- **Arranging checks:** optional warnings for out-of-range notes per instrument, out-of-key notes, overlapping same-pitch notes, parallel fifths.
- **MusicXML export:** to Dorico, keeping keys and per-staff meters.

**Versions and annotation**
- **Alternatives:** named alternative versions of a section, switchable and auditionable in place.
- **Text lanes:** lyrics, rehearsal notes and cues at song positions.
- **To-do markers:** markers flagged as to-dos, gathered in a to-do list to jump between.

**Later**
- **Live rig mode:** send program changes and setup messages per song section, stepped through with a footswitch.

## To decide

Settled one milestone at a time, at the start of that milestone's work. Add to this list as new questions come up.

**Milestones 1–2: engine and recording**

- [x] Scheduler look-ahead. The engine sends each event itself at its exact time. Since the playback plan already holds every event's send time, there is no window to prepare: the engine sends whatever is due when it wakes, and any edit, mute or tempo change during playback applies to every event not yet sent. If milestone 1's loopback measurements show too much jitter on a platform (p99 send lateness or loopback jitter above about 1 ms), that backend switches to OS-timestamped delivery (around 50 ms ahead, recalled on stop or edit). The MIDI interface is designed to allow either. Windows measured well under the threshold (see Measured timing, Windows); Linux is still to be measured (below).
- [x] Panic key: `Ctrl-.`.
- [x] Record input: all MIDI inputs by default, optional per-track input. MIDI thru on by default, changeable.
- [x] Milestone 2's recording, pedal, metronome, export and tool decisions (October 2026): see Input and recording, Playback and recording behaviour, Metronome and count-in, Other files, Architecture, and Build and tooling.
- [x] Build tooling: vcpkg (simplest CMake and MSVC integration, all planned libraries in its registry) and Catch2 v3 (its generators make tests at several PPQNs simple).
- [ ] Linux timing and recording, on a native Linux install with real MIDI hardware and a loopback cable: `modal-loopback`, plain and with `--record`, with and without the spin, as was done on Windows. Milestones 1 and 2 are otherwise done; if Linux shows too much jitter, its backend switches to OS-timestamped delivery (see Scheduler look-ahead).
- [ ] Windows MIDI Services. vcpkg's libremidi port is built without its Windows MIDI Services backend, so Windows uses WinMM for now. Using Windows MIDI Services (loopback ports, multi-client access) would need a local overlay port that enables it. Shelved for now, since WinMM measures well (October 2026).
- [x] An imported SMF's time signatures apply to all tracks; a change that falls mid-bar starts a remainder bar. (Moved up from milestones 3–4, since import needs it.)

**Milestones 3–4: grid and first editing view**

- [x] Milestone 3's rendering, font, DPI, key, config and test-view decisions (October 2026): see Views, Customisation, Architecture and Build and tooling.

- [ ] Song file grammar: exact syntax, quoting, comments, how SysEx is written.
- [ ] Default keymap: the full set of single-key bindings, designed for muscle memory.
- [ ] Undo granularity: is a recording pass or a macro run one undo step?
- [ ] Cursor mapping when switching between arrangement, piano roll and event list.

* [x] Remainder bars are always the exact leftover (e.g. 1/4, 5/16). To get something else, the user selects the bars and applies a meter again, e.g. a 3/4 bar plus a 1/4 remainder re-barred as one 4/4 bar.
* [ ] How the arrangement ruler and per-track bar lines are drawn when meters differ, at different zoom levels. The approach is settled (time-based columns, each track's bar lines in a pixel region; see Views), and milestone 3's test view sketches the blocks, gaps, markers and ruler; the zoom levels and ruler details are confirmed in milestone 6.

- [x] Delete can leave silence (`d`) or close the gap (`D`); put can merge (`p`) or insert (`P`).
- [x] Cut-at-edges key is `|` (rarely used, so its reach doesn't matter; rebindable like everything else).

**Milestone 6: piano roll and arrangement views**

- [ ] Scale-lock toggle key. Proposed `, s`. `\` was considered, but it is the proposed key for repeating an `f` search backwards (since `,` is the leader). Decide which key gets `\`.
- [ ] Confirm `Ctrl-h` / `Ctrl-l` for grid size on both platforms (no conflicts expected in an SDL3 GUI app).
- [ ] Velocity used by `o`: the last-used velocity, or a per-track default velocity setting?

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
- [ ] Vim uses `,` to repeat an `f` search backwards; pick another key for that (e.g. `\`), together with the scale-lock key (see To decide, milestone 6).
- [x] Piano roll navigation, grid and default note length (October 2026): a crosshair cursor; `{` / `}` keep the position in the bar, with bar lines on `[[` / `]]`; `gg` / `G` go to the start and end of the track; pitch motions and scale lock; Alt plus a motion moves notes; a per-track grid and default note length; the piano roll shows pages of whole bars. See Editing grammar and Views.
- [ ] Licence: undecided; note JUCE's AGPL/commercial terms if hosting plugins.
- [ ] Working title Modal: check clashes before any public release (e.g. Modal Electronics makes synths).

* [x] Language: C++20 throughout. C# was considered, but GC pauses in the real-time engine, weak Linux MIDI support in .NET libraries, and C++-only plugin hosting all favour C++, as does day-to-day familiarity.
