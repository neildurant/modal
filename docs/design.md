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

**Sections and markers (decided October 2026)**

Two distinct ideas:

- **Sections are the song's structure.** A song is an ordered sequence of named sections (Intro, Verse 1, Chorus, Bridge…) that together cover the whole timeline, with no gaps or overlaps, each spanning every track. A new song starts as one section covering everything ("Section 1"), split as the structure emerges.
- **Markers are bookmarks:** named points for navigation and reminders ("fix this fill"), with no structural meaning. Vim's marks (`ma`, `'a`) are in the same family.

Section properties: a name, a length, an optional colour (from the theme's section palette), its takes (see Input and recording), its cue slot (see Playback and recording behaviour), and later a link to another section and alternatives.

- **Content is stored per section.** Each section holds the conductor's tempo and meter changes, its markers, and each track's meter and events, all positioned relative to the section's start. Moving or duplicating a section then changes nothing inside it, and the song file is grouped by section.
- **A flattened timeline.** From the sections the core builds each song snapshot's flattened form: absolute events, meters and tempo per track, as before sections. Playback planning, the scheduler, recording, SMF import and export and the views read the flattened form, so they work on one timeline as they always have; edits go to the section concerned.
- **Events belong to the section they start in.** A note sustaining past the section's end still belongs to the section it starts in, and moves with it whole.
- **Bar lines realign at every section boundary.** Each section starts with a new bar on every track, whatever the meters, so a bar on a track in another meter can never straddle two sections. Bar numbers still run on through the song in the usual way (Verse 1 is bars 5–20 after a 4-bar Intro). A boundary that would fall mid-bar on some track splits that bar: its first part becomes a remainder bar, a real bar with a meter change of its own (a 7/8 bar cut after one eighth becomes a 1/8 bar), and the original meter carries on in the next section. Making such a boundary warns, naming the tracks, e.g. "splits bar 5 of Lead (7/8)", since a boundary that doesn't line up is usually not a real section boundary. So every section is a whole number of bars on every track. The remainder always fits a time signature, since a boundary is always at some track's bar line.
- **Tempo and meter changes belong to their section** and move with it.
- **A section's length** is stored in ticks, and is a whole number of the conductor's bars, counted through its meter changes.
- **Each section has its own tempo and meter at its start (decided in milestone 4),** even where they repeat the section before, so a section moved elsewhere keeps its tempo: a 140 BPM chorus moved after a 90 BPM verse stays at 140. The flattened tempo map leaves out repeats.
- **The end of the song (decided in milestone 4).** A new song is one section, "Section 1", of eight bars. It was "Song" at first, which read as if it were the song itself rather than a section waiting to be named; "Section 1" says it's a section, one of more to come, as "Track 1" does for tracks (decided in milestone 4). Anything put past the end (a recording, an edit, a marker, a tempo or meter change) grows the last section to the next bar line; it never shrinks by itself. Changing the conductor's meter keeps every section's length in ticks, ending it in a remainder bar if need be, except the last, which grows to the next bar line instead. Splitting a section mid-bar does the same for the new section, which starts a new bar: it ends in a remainder bar, or grows if it's the last.
- **How the remainder bars are stored (decided in milestone 4).** The conductor's meter in a section is kept a whole number of bars explicitly, so a remainder bar is written into it as a meter change, and the file can give the length as `bars=N`. A track's own meter needn't end on the section's end: the end cuts its last bar short, and flattening makes that a remainder bar, so a section growing restores the whole bar. Splitting a section writes the remainder into both.
- **In the core (decided in milestone 4).** `Track` holds only settings; sections hold each track's content (`TrackContent`: an optional meter of its own and its events). The song keeps its flattened timeline (the tempo, the conductor's meter, each track's meter and events), rebuilt after each edit and shared between snapshots, which playback, export, the views and the tools read. Edits in ticks from the song's start (`updateEvents`, `updateMeter`, `updateTempo`, `setMarker`) are shared out to the sections by where things start, so the recorder and SMF import work as before; operators on sections come later.
- **Markers belong to the section they're in,** and move and are copied with it. A marker's name is unique within its section, and it is named with its section where that matters, e.g. "Chorus 2 › fix this fill", so duplicating a section never needs its markers renamed. Section names are unique across the song: a duplicated section is numbered, so a copy of "Chorus" is "Chorus 2", then "Chorus 3".

**Per-track meters (polymeter)**

As in SPG, each track has its own sequence of bars and time signatures, so tracks can run different meters at the same moment (4/4 against 3/4, 7/8 against 4/4) for polyrhythmic writing.

- **Time is shared; bars are not.** All events sit on one timeline in ticks, and tempo is global. Bars are a per-track view of that timeline, so "bar 10" on a 3/4 track is earlier than bar 10 on a 4/4 track.
- **The conductor track (decided October 2026).** The song's tempo and reference meter are a special track, the conductor, which holds only tempo and meter changes (per section), never notes, like the tempo track of other sequencers. Markers, the loop range, the metronome and the main position display are measured against its bars, so deleting, reordering or muting tracks never changes the song's bars. SMF export writes its meter as the file's time signatures, since standard MIDI files have only one. An empty song has bars and a tempo before it has any tracks.
- **Tracks follow the conductor's meter unless they have their own.** In each section, a track either follows the conductor's meter or has its own, and then its whole meter in that section is its own; it never follows for part of a section. A track following the conductor changes with it; a track with its own meter keeps it, even if it happens to match. `:meter follow` sets a track following again. Meter commands change the conductor and the tracks following it by default, and in a selection only the tracks selected.
- **Positions are always relative to a track.** The cursor's position and the inspector use the bars of the track the cursor is on. Typed positions like `:goto 10` or `10G` use the current track's bars; `:goto ref 10` uses the conductor's.
- **Motions follow each track's bars.** `}` moves to the same position in the next bar of the current track. Operators that span several tracks, such as a Visual block in the arrangement, select a time range, which may start or end mid-bar on some tracks.
- **Copy and paste by time.** Pasting from a 3/4 track into a 4/4 track keeps the material's timing, not its bar positions.
- **Apply meter to a range.** In the arrangement, select a range and set a time signature (`:meter 3/4`, or from the leader menu). Bars are rebuilt from the range start; if the range doesn't divide evenly, the last bar becomes a shorter remainder bar, e.g. 10 beats in 3/4 gives three 3/4 bars and one 1/4 bar.
- **Beats and remainder bars (decided in milestone 1).** The beat is the denominator note, so 6/8 counts six eighth-note beats. A meter set mid-bar ends that bar with a remainder bar of the exact leftover, using a finer denominator when the leftover isn't whole beats (half a beat into a 4/4 bar gives a 1/8 bar); a leftover no time signature can express, such as a single tick, moves the change on to the next beat. A change inside the first bar after another change replaces that bar's meter rather than adding a bar. Removing a change keeps later changes at their positions in time, adding remainder bars where needed, and repeating the meter already in effect adds nothing.
- **Unify meters.** `:meter unify` sets the selected tracks (or all tracks) following the conductor, removing polymeter where it isn't wanted; from a track with its own meter, `:meter unify` can also copy that meter to the conductor, for a song whose real meter is that track's.
- **Arrangement drawing.** Columns represent time, not bars (decided: SPG's bar-aligned view made polymeter confusing). Each track draws its own bar lines, so tracks in different meters show bar lines at different places, and the ruler shows the conductor's bars.

**Selections across tracks in different meters**

- **Bar motions use the cursor's track.** In a Visual selection, `}` and `{` step by the bars of the track the cursor is on. Start on the 4/4 track and the selection grows in 4/4 bars, even across a 3/4 track; move the cursor onto the 3/4 track with `j` and it grows in 3/4 bars. This is the same rule as everywhere else, so nothing special to learn.
- **A motion for any bar line.** `]|` and `[|` move to the next or previous bar line on any track in the selection, for selecting exactly up to where the meters next coincide or diverge.
- **Partial bars are shown.** Where a selection edge falls mid-bar on a track, that edge is drawn half-shaded, so it's clear the track is only partly selected.
- **Operators act on the time range.** Transforms, copy, delete and quantise work on whatever falls inside it, whole bars or not. Operations that need whole bars, such as applying a meter, use the bars of the cursor's track.

* **Song:** title, PPQN, tracks, sections, global settings such as count-in and metronome.
* **Section:** name, length, colour, the conductor's tempo and meter changes, markers, and each track's content: its own meter (or none, following the conductor) and its events, all relative to the section's start. Later its takes and cue slot.
* **Track:** a stable ID, name, output port, MIDI channel, mute/solo/arm, note chasing on/off, optional transpose, velocity offset and time offset applied at playback, grid and default note length (see Editing grammar). Its meter and events are held by the sections. The ID survives edits, so the engine can follow a track from one song snapshot to the next.
* **Event:** tick, type, and type-specific fields. A note has pitch, velocity, duration and release velocity. Other types are CC, program change, pitch bend, channel and poly aftertouch, SysEx and meta (text, markers). Events carry no channel; the track's channel applies.
* **Event order.** A track's events are in tick order, and events at the same tick are sent in the order stored. Import and editing put non-note events (program change, CC, etc.) before notes at the same tick, so a program change at 1:1:0 always takes effect before the first note.
* **Overlapping notes.** The model allows overlapping notes of the same pitch, since imported files can contain them; the editor never creates them (see Playback and recording behaviour).
* **Markers:** bookmarks, named positions used as motion targets (see Sections and markers).
* **Patterns (later):** reusable phrases referenced from the arrangement, if a pattern-based workflow proves useful.

The model is immutable at the edit level: each edit produces a new song state, sharing unchanged sections and tracks. That makes undo cheap and lets the engine read a consistent snapshot while the UI edits the next one.

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
| `[s` / `]s` | Previous / next section start |
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
| (to decide) | Up / down a semitone, even when scale lock is on. `Ctrl-k` / `Ctrl-j` were proposed, but Ctrl with `hjkl` moves and scrolls in every view (decided in milestone 3) |
| `gk` / `gj` | To the next note above / below that is sounding at the cursor's time (moving through a chord voicing) |
| `gp` + pitch | Straight to a pitch, e.g. `gp C4 Enter` |

**Scale lock** is a toggle, shown in the status line (key to decide; `, t s` proposed, with the other toggles under `, t`). When it is on, `k` / `j` move by scale steps in the song's key and scale (later, following the chord track if one exists). From a pitch outside the key, `k` / `j` move to the nearest scale tone in that direction. Scale lock also applies to moving notes (`Alt-k` / `Alt-j`) and to step entry.

**Moving and editing notes**

The same motions move notes when combined with Alt:

| Key | Effect |
| --- | --- |
| `Alt-k` / `Alt-j` | Move the current note (or selection) up / down one step (a semitone or a scale step, per scale lock) |
| `Alt-K` / `Alt-J` | Move up / down an octave |
| `Alt-l` / `Alt-h` | Move later / earlier to the next grid step |
| `Alt-L` / `Alt-H` | Nudge later / earlier by the `nudge` setting (5 ms) |
| `Alt-}` / `Alt-{` | Move later / earlier by a bar |
| `=` / `-` | Lengthen / shorten the current note to the next grid step |
| `+` / `_` | Lengthen / shorten it by a nudge |
| `o` | Create a note at the cursor, with the track's default note (its length and velocity), without entering Insert mode |
| `x` | Delete the note under the cursor |

Counts apply: `3Alt-l` moves three grid steps later. Operators (`>ib`, `t}`, etc.) remain for larger ranges.

**Editing notes, decided for milestone 5 (step 4c).**
- **To the grid.** `Alt-h` / `Alt-l` move a note's start to the grid steps counted from each bar line, as `h` / `l` move the cursor, so a note off the grid lands on it at the first press: moving by a grid's distance while staying off it would make no sense. `=` / `-` move its end the same way, to the next or previous grid step, and `-` doesn't shorten a note below one grid step (a note already shorter isn't made shorter still).
- **Nudging.** For tweaking a played, unquantised part or a groove, `Alt-H` / `Alt-L` nudge a note earlier or later, and `+` / `_` lengthen or shorten it, by the `nudge` setting: milliseconds rather than ticks or a note fraction, since timing feel is heard in milliseconds at any tempo, as `chordwindow` is (5 ms by default, about the least a shift is felt; a tick is far too fine, half a millisecond at 960 PPQN and 120 BPM). Counts multiply it. The Shift keys are the fine forms of the grid keys beside them, so in the piano roll `+` is no longer another `=`. A nudge never shortens a note below one nudge.
- **Collisions.** Moving, lengthening, adding with `o` or putting with `p` onto a note of the same pitch is refused, naming that note, as in the event list; `o` on a note says there's one there. Moving before the song's start fails, as motions do. Counts on `o` wait, since a run of notes needs its spacing thought about.
- **Operators.** `x` and `dd` delete the current note, `yy` and `Y` yank it. `d` and `y` with a motion in time take the notes starting in that time at every pitch, as the arrangement's take its area, but only notes, since they're all the piano roll shows (as built, leaving the track's other events alone); with a pitch motion (`dj`) they wait for Visual blocks (milestone 6). `p` puts the register at the cursor's time, at the notes' own pitches, and a register of several tracks from the arrangement onto this track and those after it, as there. Each edit is one undo step, and undo brings the cursor back to the note.

**Grid**

- Values: straight 1/1 to 1/64; triplets (1/4T, 1/8T, 1/16T, …); dotted values; any tuplet from the command line, e.g. `:grid 1/16 5:4`.
- Stored **per track** and saved in the song. Shown in the piano roll's bar, e.g. `Grid: 1/8t`. A new track's is 1/16.
- The grid drives `h` / `l` movement, and `q` quantise unless a value is given (`:quantize 1/16`).

| Key | Effect |
| --- | --- |
| `zh` / `zl` (or `z` with Left / Right) | Finer / coarser grid, halving or doubling it and keeping triplets (1/8T to 1/4T): the steps narrower to the left, wider to the right (decided October 2026, after trying them the other way) |
| `Ctrl-t` | Toggle triplet (1/8 ↔ 1/8T) |
| `:grid …` | Set any grid exactly |

**Default note length**

An independent setting for the length of new notes, after Sequencer Plus Gold's default note length.

- **Linked to the grid by default:** the length follows the grid, and the piano roll's bar shows `Note: grid v100`.
- Changing the length unlinks it; it then stays fixed whatever the grid does, and the bar shows it, e.g. `Grid: 1/16  Note: 1/8t v100`. `:len grid`, or the leader menu, links it again.
- With a velocity, the two making the track's **default note** (decided for milestone 5), which `o` adds.
- Stored per track and saved in the song.
- Used by `o`, step entry (from a MIDI keyboard or the computer keyboard) and inserted rests. Not used by recording: played notes keep their real lengths.

| Key | Effect |
| --- | --- |
| `, n` | Take the current note's length and velocity as the default note |
| `:len 1/8` / `:len grid` | Set the default length, or link it to the grid again |
| `, l` + keys | Set it directly from the leader menu (milestone 6): `4` quarter, `8` eighth, `6` sixteenth, then `t` triplet or `.` dotted (e.g. `, l 8 t` = 1/8T) |

`+` / `_` were to step the default length through the common lengths. They were dropped (decided for milestone 5): a length is better judged from a note you can see, and `+` steps the field under the cursor in the event list; in the piano roll it lengthens the current note by a nudge (see Moving and editing notes).

**Text objects** (used after an operator, `i` = inner, `a` = around)

- `ib` / `ab`: this bar; `is`: this section's contents; `as`: this section as a structural unit (its contents, length, tempo and meters); `ic`: this chord (notes starting together); `it`: this whole track; `gn`: the next search match (see Search). `is` and `as` replace the earlier "phrase between markers" (`ip`), since markers are now bookmarks.

**Operators**

| Key | Operation |
| --- | --- |
| `d` / `y` / `p` | Delete / yank / put |
| `>` / `<` | Transpose up / down a semitone (count = semitones) |
| `q` | Quantise to the current grid |
| `^` | Set or scale velocity (prompts for a value, a change or a %: `^ib 80`, `^ic +10`, `^ib 80%`) |
| `!` | Transform: opens the transform hint menu for the range (see MIDI transforms) |
| `t` | Shift in time by the grid step |
| `~` | Humanise (small random time and velocity changes) |
| `c` | Change channel or destination track |

Examples: `d2}` deletes from the cursor to the same position two bars later, `>ib` transposes this bar up a semitone, `5.` repeats the last change five times.

- **The velocity operator is `^` (decided October 2026),** an upside-down `v` and the marcato accent sign, since `v` is Visual mode. Vim's `^` (first non-blank) has no use in Modal. `%` is kept for something else.
- **`!` is the general transform operator (decided October 2026),** with an imperative feel: `!` and a motion open the transform menu for that range. Velocity, the most common transform, keeps its own key.

**In milestone 4 (decided for milestone 4).** The operators `d`, `y` and `p` work over the motions, with counts, on rows of the event list as Vim's do on lines (`d}` deletes from this event up to the one `}` lands on); `dd` deletes the event under the cursor (`3dd` it and the next two) and `yy` yanks it; `x` deletes the event and `o` adds a note at the cursor. There is one unnamed register, and linear undo and redo. Visual mode, named registers, `.`, marks, `f` / `F` and searches come in milestone 6.

**Sections in milestone 4 (decided October 2026).** The section model and the song format, the flattened timeline, SMF import and export with sections, the section shown in the event list (with markers, dimmed) and in the status line, `]s` / `[s`, `:section NAME` to split at the cursor's bar line (with the warning when a bar is split). As built: `:section` works in every view, the bar line being that of the cursor's bar in the view's own bars (the conductor's in its list, a track's own where it has its own meter); with no name, the new section is called `Section` and the next number, so a new song's sections go Section 1, Section 2; a name another section has is numbered; at a section's start, or past the song's end, it's refused; and when the bar line falls inside a bar of the conductor (and the tracks following it) or of a track with its own meter, the new section still starts there, the bar it cuts ending short as a remainder bar, and the message says which ("Bridge starts at 2:1:000; this cuts a bar short in Lead"). Each split is one undo step. `D` / `P`, rippling to the end of the section with the question when an insert would overflow it, come in milestone 5 with the time-based views (decided in milestone 4): the event list is for exact tweaking of events, not editing in time, and its cursor is always on an event, so time spans there would do odd things or need workarounds. The section strip comes with the views in milestone 5, section operations with the full grammar in milestone 6, the outline view in milestone 7, and looping, takes and cueing in milestone 8.

**Registers, macros and undo**

- Named registers (`"a` to `"z`) hold phrases; pasting fits them to the current grid position.
- Macros record and replay key sequences, as in Vim but on `Q` (`Qa` … `Q`, `@a`), since `q` is quantise.
- Linear undo/redo in version 1 (`u`, `Ctrl-R`); an undo tree later.
- **Undo steps (decided for milestone 4).** Each operator, field edit or command is one undo step; so are a whole recording pass (milestone 8) and a whole macro run (milestone 6), and `.` repeats a change as one step.
- **Undo history (decided in milestone 4).** Every change to the song is an undo step, the track settings, mute, solo and renames included, since they're saved in the file; an edit that leaves the song as it was is none. Moving and scrolling aren't steps, so `u` never has to wade through navigation: instead each step records where it was made, the cursor's track and position and the view's state (scroll and zoom, or page), and undo and redo go back there, so stepping through the history shows each change where it was made. A view scrolls further only if the change reaches beyond that. Undoing back to the version last saved or loaded makes the song unmodified again. `undolevels` (1,000 by default) is how many steps can be undone; what can be redone doesn't count. Past versions are kept without their flattened timeline, which is rebuilt on undo, so a step costs only what it changed (typically one track's part of one section), and a thousand steps take a few tens of MB at most. Each step has a title that reads on its own, as in an undo list one day ("Delete 3 events in Bass"), shown as "Undid: …", and its time. The core's `Document` holds the song, its file and the history, and calls a listener after every change, which hands the engine each new snapshot.
- Transport keys: Space plays from the play-from marker or stops, `Ctrl-Space` plays from the cursor, `R` records. Space and the play keys work in Normal mode and the views' other non-typing modes; where text is typed (a field edit, the `:` prompt) Space types a space. `Ctrl-.` panics in every mode.
- **Playing in milestone 4 (decided for milestone 4).** Space plays and stops, `Ctrl-Space` plays from the cursor, `Ctrl-.` panics, and the list follows the playhead; recording in the GUI comes in milestone 8. `:port NAME` sets the current track's output port, matching names as the command-line tools do, and a `port` setting names the default for new tracks. The test view goes when the event list arrives; its arrange page's code stays in the UI library for milestone 5 to build on.
- **Enter edits, `Ctrl-Space` plays from the cursor (decided in milestone 4).** Enter was first to play from the cursor, but in the event list it edits the field under the cursor, and Enter keeps to editing everywhere: in the arrangement, Enter on a bar opens it in the piano roll. `Ctrl-Enter` was tried for playing from the cursor, then `Ctrl-Space`, beside Space, was chosen. macOS takes `Ctrl-Space` for switching input sources by default, and Chinese, Japanese and Korean input methods often do on other systems, so it may need rebinding there. `Ctrl-Space` as the Insert mode leader (milestone 8) is in another mode, so they don't clash.
- **The play-from marker (decided in milestone 4).** Playback always starts from one marker, so what Space will do is shown rather than remembered: a filled ▶ in the event list's margin (an outline ▷ was tried first, and was too easy to miss) on the row at or before it (in the cursor's colours when the cursor is on that row's position), and `From 9:1:000` in the status line. `m Space` puts it at the cursor (the marks to come in milestone 6 are `m` and a letter, which Space isn't), and `Ctrl-Space` puts it there and plays. It starts at the song's start. Space first played from where playback last started or stopped, remembered out of sight, with a `play-again` command and `Ctrl-Enter` for the cursor; the marker replaced them.
- **Rewind (decided in milestone 4).** A setting, `rewind` (on by default), with `, t Space` to flip it while playing, decides whether the marker follows playback; the status line shows it. On, the marker stays put, so Space plays the same passage again, and the cursor stays where it was left, the view going back to it on stopping: for listening to a passage over and over while tweaking it. Off, the marker moves to where playback stops, so Space carries on from there, and the cursor follows the playhead and stays where playback stopped, ready to edit there; once playback stops by itself at the song's end, where carrying on would play nothing, the marker goes back to the start.
- **Playing, as built in milestone 4.** The commands are `play-toggle` (Space), `play-cursor` (`Ctrl-Space`), `marker-set` (`m Space`), `panic` (`Ctrl-.`, in Normal mode and at the prompt and field edits) and `rewind-toggle` (`, t Space`, which sets the `rewind` setting, so it stays as other settings change). The row playing, the last at or before the playhead, has the `playhead` colour across the list. The view follows the playhead the same way with rewind on or off (decided in milestone 4): the playhead moves down to a third of the way down the view, and then the list scrolls under it, so what's coming next shows, until the end of the list comes into view and the playhead goes on down. Turning pages was tried first with rewind on, and scrolling with the playhead on the bottom row with it off, but rewind should only decide where the marker and cursor end up, and the bottom row hid what was coming; scrolling also took away the playhead's judder. While playing, the view keeps to the playhead, so moving the cursor out of view doesn't scroll to it until playback stops. (Changed in milestone 5, October 2026, after the arrangement's view flickered between the two: moving the cursor or scrolling while playing lets go of the playhead, as in most sequencers, in every view. The view then stays with the cursor and the playhead runs on, off screen if need be, until playback starts again or `zf` (`playhead-follow`) catches up with it; the cursor stays where it was moved when playback stops, even with rewind off.) While playing, the status line shows ▶ and the playhead's position, meter and section in place of the cursor's. The window draws a frame every 8 ms while playing (fewer if the display's refresh holds it back), and none once stopped and the engine has said where. Playback stops by itself at the song's end. The program's playback runs the engine on its own thread at realtime priority (saying so in the status line if it can't), hands it the song after every change, and looks up the tracks' ports when it starts and when the `port` setting changes, naming any it can't find in the status line. `:port NAME` matches NAME as the command-line tools do and stores the port's full name, as one undo step ("Set port of Bass to …"); a name matching several ports or none is refused, naming the candidates or listing the ports, and `:port` alone says which port the track plays to and lists them. The conductor has none. The new port is opened as the player is handed the changed song, during playback too.
- **Tracks without a port (decided in milestone 4).** A track with no port, as every track of an imported MIDI file, plays to the `port` setting's port, or, if that is empty (the default), to the first output port found, so a fresh setup makes sound (on Windows through the GS Wavetable Synth). The track's bar shows it as `Port: default`. New tracks leave their port empty rather than copying the setting.
- **Toggles on `, t` (decided in milestone 4).** Toggles go under the leader and `t`, since they're used now and then rather than all the time: `, t Space` for rewind first, and later scale lock (`, t s`), the metronome, follow and thru, which the hint menu lists together. Until the leader menu (milestone 6), `, t Space` is an ordinary key sequence. `t` itself stays the time-shift operator.

**Notes crossing a range's edges**

- **By default, a range owns the notes that start inside it.** Copy, delete and transforms act on those notes whole, including any part that sustains past the range end. Notes that started before the range and are still sounding are left alone. Nothing is cropped, so a held pad chord stays one chord.
- **A cut-at-edges mode splits notes at the boundaries.** In Visual mode, `|` toggles cut-at-edges (shown in the status line and by the edge markers changing). Operators then split notes crossing a range edge at the boundary first, and affect only the inside parts. Deleting bars 9–10 from a sustained chord leaves the chord sounding up to bar 9 and from bar 11. Without a Visual selection, `|` before an operator does the same for that one operation (`|d}`).
- **The edges are visible.** In Visual mode, notes that cross an edge are drawn with a marker, so it's clear which ones will be taken whole and which left behind.

**Leaving silence or closing the gap**

Both are needed, so both are operators. Uppercase means "and shift the timeline":

- `d` deletes and leaves silence; `D` deletes and closes the gap, moving everything after the range earlier on the affected tracks.
- `p` puts material over what's there (merging); `P` inserts it, moving everything after the insertion point later.
- Vim's own `D` (delete to end of line) is given up; `dG` deletes from the cursor to the end of the track.

**How far ripple reaches (decided October 2026)**

- **Ripple stays inside the section.** On one track, `D` moves the material after the range earlier only up to the end of the current section, and `P` moves it later only up to there. The section keeps its length, so `D` leaves silence at the end of the section on that track; other sections and other tracks are unchanged. Not the view, since an edit's result must never depend on zoom or page size; and not the whole track, which would shift every later section of that track against the other tracks and move notes across section boundaries. The section is the musical unit: tightening or extending a phrase within it keeps the rest of the song aligned.
- **Wider scopes are explicit.** A whole section on every track (from the section strip, or an arrangement selection covering every track) removes or inserts time across all tracks, so the section shortens or lengthens and the rest of the song moves with it, a structural edit like deleting or inserting bars. `:ripple track` ripples to the end of the track, for rare cases, never by default.
- **Ripple works on spans of time,** not single notes: `D}`, `Dib`, or a Visual time range. On a single note, especially one in a chord, closing a gap has no clear meaning, so `x` deletes the note and leaves its time.
- **An insert that would overflow the section** never pushes notes into the next section silently. The status line asks (yes / no) whether to grow the section by whole bars, on every track, to make room; the new bars on other tracks are empty. Declining cancels the insert.
- Each ripple, including any growth of the section it causes, is one undo step.

**Sections**

| Command / key | Effect |
| --- | --- |
| `:section NAME` (or `, S s`) | Start a new section at the cursor's bar line, splitting the current section |
| `, S r` / `, S c` | Rename / recolour the current section |
| `, S j` | Join with the next section |
| `, S +` / `, S -` | Add / remove a bar at the end of the section; later material ripples |
| `:section length N` | Set the section's length in bars |
| `yas` | Yank the section (its structure and contents) |
| `das` | Delete the section entirely; later material closes up |
| `dis` | Clear the section's contents, leaving an empty section of the same length |
| `>is`, `qis`, … | Any operator on the section's contents, as with any text object |

- Putting a whole section always inserts it (sections can't be merged), so on the section strip `p` / `P` put after / before, as with lines in Vim.
- Every track's events and the section's tempo changes, meter changes, markers and takes move with it.
- Each section operation is one undo step. The section strip and outline view (see Views) operate on whole sections; splitting is done from the tracks, where the cursor has an exact bar position.

**Visual modes (decided October 2026)**

The three Visual modes, with the piano roll's two dimensions of time and pitch:

| Key | Selects | Example |
| --- | --- | --- |
| `v` | A time range, every pitch within it | Everything in bars 3–4 |
| `V` | Whole rows | Every kick drum on the page |
| `Ctrl-v` | A box of time × pitch (or time × track) | The top notes of the chords in bars 3–4 |

- `V` means whole rows of the current view everywhere: whole events in the event list, whole tracks in the arrangement, whole pitch rows in the piano roll, and a run of sections on the section strip.
- In the piano roll, `V` selects every note at the cursor's pitch on the current page; `j` / `k` add further pitch rows (kick and snare together). A text object after `V` sets the time range: `V` then `as` takes the rows across the whole section, `V` then `it` across the whole track. `:select pitch C1 in section` (or `in page`, `in track`) does the same from the command line or palette, and takes drum-map names (`:select pitch kick in track`).
- Typical uses: boost the kick drum with `V` then `+10` or `:vel lift 20%`; move every kick to another sound with `V`, then `Alt-j` / `Alt-k` with scale lock off, or by typing the new note in the inspector.

**Search (decided October 2026)**

As in Vim, search matches are a layer separate from the selection.

- **Matches** are the result of the last search, drawn in their own style (an outline, say), distinct from the selection's fill, so both can be seen at once. They stay highlighted until `:noh` (or a key, to decide), and the status line shows the position among them, e.g. `match 3/24`.
- **Searching:** `*` / `#` search forwards / backwards for the current note's pitch; `/` and a query searches; `n` / `N` go to the next / previous match. Search covers the current track in the piano roll and event list; across tracks in the arrangement can come later.
- **Queries (to begin with):** a pitch (`/C1`, `/F#3`) or a drum-map name (`/kick`); conditions (`/C1 vel<60` for quiet kicks, `/vel>110`, `/len<1/32`); event types (`/cc64` for sustain, `/cc1`, `/bend`). Interval and rhythm patterns can come later (see Ideas: musical search and replace).
- **All at once:** `g/` makes the matches on the current page the selection, and `:select matches in section` (or `in track`) does so over a wider range; then any operator or transform applies.
- **One at a time:** `gn` is a text object for the next match. An operator with `gn` changes the next match and moves to it; `.` repeats the change on the following match, and `n` skips one. For example, `^gn +15` accents the next kick, then `.` on each kick to accent and `n` past the ones to leave.

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
| `, t s` | Scale lock on/off (proposed; see To decide) |

The most frequent operations stay on single keys for speed and muscle memory; the leader tree holds everything else. Both are defined in the keymap file, and hint labels come from the same definitions.

**Command palette (decided October 2026)**

Three ways to reach any action: keys, for speed and muscle memory; the hint menu, for browsing by category; and the command palette, a fuzzy search for when you roughly know what you want but not where it lives or how it's bound.

| Key | Effect |
| --- | --- |
| `Ctrl-p` | Open the palette, in any mode (Insert mode to decide) |
| `, :` | The same from the leader, like Doom Emacs's `SPC :` |
| `Ctrl-n` / `Ctrl-p`, or `Ctrl-j` / `Ctrl-k` | Move down / up the list, inside the palette |
| `Enter` | Run the highlighted item |
| `Tab` | Copy the command to the command line to edit before running it |
| `Esc` | Close |

- A box opens at the top of the window, with the filtered list beneath it, updating as you type.
- **Matching:** fuzzy ("vcomp" matches "Velocity compress"), searching descriptions and aliases too, so "crescendo" finds "Velocity ramp" and "pedal" finds the sustain tools. Frequently and recently used commands rank higher, and the ranking is kept between sessions. Commands that don't apply in the current view rank lower or are dimmed, and the palette shows what a command will act on, e.g. "selection: 4 bars, Bass".
- **Teaching the bindings:** each row shows the command's title, description and key or leader sequence; after a command runs from the palette, the status line briefly shows its binding ("Also: , x c").
- **A front end to the command line:** every action is a `:` command, so `Enter` runs it with its defaults or prompts for its parameters with the defaults filled in, and `Tab` puts the whole command on the command line (`:vel compress 50% window=1bar`). Commands run from the palette repeat with `.` and record in macros like any other.
- **Searching more than commands:** a first character changes what's searched. `@` sections (`Enter` jumps there, or during playback cues it, with `Shift-Enter` to jump at once); `#` tracks (switch to the track in the current view); `'` markers and marks (jump to it); `?` settings, with their current values (edit the setting). For example `Ctrl-p @br Enter` jumps to the Bridge, and `Ctrl-p ?metro` shows the metronome's settings. Results show useful context: a section's bar range, meter and takes, a track's port and channel, a marker's position.
- All of it comes from the command registry (see Architecture).

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

**Velocity without clipping (decided October 2026)**

Adding a fixed amount clips loud notes at 127 and soft ones at the floor, flattening the dynamics, so velocity changes avoid hard limits by default:

- **Lift and lower by headroom.** `:vel lift 25%` moves each note a quarter of the way towards 127, and `:vel lower 25%` a quarter of the way towards the floor, keeping the notes' order and proportional spacing; nothing reaches the limit.
- **A soft knee on relative changes.** `+10` (in the inspector, the command line or the `^` operator) and the step keys add the amount in the middle of the range but ease off smoothly near the limits. `:set velclip=hard` gives plain clipping.
- **A floor,** 1 by default and configurable. Velocity is never set to 0, which means note-off.

**Velocity compress and expand over time**

Two timescales for two aims, evenness from note to note and the shape of a phrase, working relative to a moving average:

| Command | Effect |
| --- | --- |
| `:vel compress 50%` | Pull each note halfway towards the local average: evens out note-to-note unevenness, keeps crescendos |
| `:vel expand 150%` | Push notes away from the local average: sharpens accents |
| `:vel shape 130%` | Exaggerate the phrase's contour (the moving average itself): bigger swells, the same note-to-note detail |
| `:vel shape 70%` | Flatten the contour: a more level part |

Options: `window=` the averaging window (`1/2`, `1bar` by default, `2bars`; a larger window treats more as phrase shape); `centre=` the local average (default), the selection's average or a fixed value; `threshold=` to act only on notes above (or below) a value, like an audio compressor taming peaks. All use the soft limits above, and compress and expand also work within chords.

**Chords and voicing balance**

Notes in a chord share a start, so their velocity bars would overlap. In the velocity lane a chord's bars fan out side by side within its column, low to high, and `Tab` steps through the chord's notes there. In the note area, `gk` / `gj` move through the chord's notes and the field step keys change the current note's velocity, with counts. On any chord note the inspector shows the chord's range (`vel 64–90`); `^ic 80` sets every note, and a relative change (`^ic +10`) moves them all, keeping their balance.

| Transform | Effect |
| --- | --- |
| Accent top note | Top note up by N and the others down by N, to bring out a melody |
| Balance by pitch | A straight velocity slope from bottom to top (e.g. 70 → 95), or reversed to bring out the bass |
| Compress / expand within a chord | Pull the chord's velocities together or push them apart about their average, keeping the overall level |

These apply to one chord, or to every chord in a range (accent top note over `ib` voices a whole bar of chords).

**Pedalling (decided October 2026)**

Switch controllers (sustain CC 64, sostenuto CC 66, soft CC 67, legato and portamento; the set is configurable) are edited as regions rather than value curves (see Views: piano roll). Their tools:

| Tool | Effect |
| --- | --- |
| Re-pedal at chord changes | Pedal down just after each chord change and up just before the next, with a configurable gap (legato pedalling) |
| Pedal per bar / beat | A fresh pedal on each bar or beat across a range |
| Clean up | Merge overlapping or flickering pedal events |
| Clear | Remove the pedalling from a range |
| Offset | Move every pedal change in a range slightly later |
| Bake | Turn pedalling into longer notes, for instruments that ignore sustain |

**Adjusting by ear**

Any velocity transform, and the pedal offset, can run interactively: run the command on a selection and the selection loops; `k` / `j` change the amount (compression 50% → 60%), applied live as it plays, with the amount and step shown in the status line; `Enter` accepts and `Esc` cancels, restoring the original. The whole adjustment is one undo step.

## Views

The arrangement is the home view; Enter on a track opens it in the piano roll for detailed editing, and the event list shows the exact data. All three share one cursor position and selection, so switching views (or `, v`) never loses your place. A status line and transport bar are always visible.

- **One cursor, shown by each view (decided for milestone 5).** The cursor is one position in the song, shared by the views: a track, a time (a tick) and a pitch, and the note it's on, if any. The arrangement shows it as the bar of that track holding the time; the piano roll as a crosshair at the time and pitch; the event list as the row at the time, or the note's own row when the cursor is on a note, so going from the event list to the piano roll and back lands on the same row. Opening the piano roll from the arrangement keeps the time (not snapping to the bar's start) and takes the pitch last used on that track, or the middle of its notes the first time; going back, the cursor is on the bar holding it. Each view keeps its own scroll and zoom, so a view comes back as it was left, as Vim's buffers do.
- **Moving between views (decided for milestone 5).** Enter on a bar in the arrangement opens that bar in the piano roll: the page holding it, at the page size last used on that track, the cursor at the bar's start; Enter keeps to editing, as in the event list. With a selection of bars (milestone 6), Enter opens exactly those bars as the page, and turning the page then moves on by as many bars, until the zoom changes and pages fall back into line with the section. Backspace goes back to the view the cursor came from, like a browser's back button (rather than Escape, which Vim users press often just to be sure of Normal mode, and which cancels a count or operator). Until the leader menu, `ga`, `gr` and `ge` go to the arrangement, the piano roll and the event list; single keys may be better in the end, to settle in milestone 6's review of the key map.
- **The selection is an area (decided for milestone 5).** What's selected is an area: a time range on a set of tracks, optionally narrowed to a range of pitches ("bars 5–8 of Bass and Drums"; "bars 5–8, C3 to G3"). Whatever is inside it is selected, the same in every view, and it survives switching views. Operators act on the events within it. Picking notes that an area can't hold, scattered ones and not their neighbours, comes later: from searches (milestone 6), and by adding or removing the current note from the selection by hand, where a search can't easily say what's wanted; so the model leaves room for a selection that is an area plus notes added or taken out.
- **The workspace and the views, as built in milestone 5.** A workspace holds what every view shares: keys, counts and pending operators, the `:` prompt and messages, the status line, playing and following the playhead, undo, `:port` and the unnamed register. Each view (the event list, the arrangement and the piano roll) does the rest its own way, through one interface: its commands, an operator over a motion, drawing its area, its bar and the inspector, the shared cursor, where an edit was made for undo, and showing the playhead. Commands are registered once, by name; one a view shares with another (`cursor-down`) goes to the view shown, and one it doesn't have says so ("Zoom in isn't in the event list"). Modal opens in the arrangement, the home view. The shared cursor's pitch is none until a view has one to keep (a note's in the event list, the piano roll's, or the one last used on a track in the arrangement), so the piano roll can tell a first visit, where it starts at the middle of the track's notes.
- **The arrangement, as built in milestone 5 (step 1).** Milestone 3's sketch, now a view of the song as it is: the track's bar and the inspector above it (the cursor's bar: its position, time, number of the song's bars, meter, the notes starting in it and its section), the ruler with the play-from marker (▶ in the markers' row, whose names stop short of it or start after it, moved there in October 2026 from among the bar numbers, where it covered them) and the playhead (a line through the ruler and tracks, kept a third of the way across while playing, the view gliding smoothly under it rather than stepping a column at a time, which made the playhead jump back at each step), and the tracks. `h` / `l` (and `{` / `}`, `[[` / `]]`) move by the track's bars, `j` / `k` (and `gt` / `gT`) between tracks keeping the time, `0` / `$` and `gg` / `G` to the first and last bar (`5gg` to bar 5), Ctrl with the motions and `Ctrl-D` / `Ctrl-U` / `Ctrl-F` / `Ctrl-B` scroll, `zz` centres, `]s` / `[s` and `]m` / `[m` go to sections and markers, and `zi` / `zo` / `z0` zoom (decided for milestone 5, as Vim's `z` family; `z0` because `zz` centres).
- **Operators in the arrangement (decided for milestone 5).** `d`, `y` and `p` work on areas of bars: what an area owns is the events starting in it, notes whole even where they sustain past its end (see Notes crossing a range's edges). `dd` is the cursor's bar, `3dd` it and the next two, and `dG` from it to the end, rather than the whole track, which a line in Vim would suggest but which is too big to hit by accident. Motions across bars take bars of the cursor's track, ending before the bar they land on as Vim's character motions do (`d2l` is this bar and the next; `dh` the bar before), except `G`, `gg` and `$`, which take the bar they land on too, as Vim's line motions do. Motions across tracks take the cursor's bar on each track they cover (`dj` is this bar here and on the track below, `y2j` on three tracks), so the unnamed register holds an area: a list of events for each track, from the top track down, their ticks from the area's start. `p` puts it at the start of the cursor's bar, onto the cursor's track and those below, merging with what's there; the event list puts it at its cursor's time the same way. A Visual selection (milestone 6) is the more deliberate way to the same areas.
- **The piano roll, as built in milestone 5 (step 4a, viewing and moving).** `gr`, or Enter on a bar in the arrangement (at the bar's start), opens the cursor's track in the piano roll. The page header is the view's own bar, rather than a row of its own: the track, its channel and port on the left, and on the right the section, the bars in view in the track's bars, and the grid. The inspector shows the current note as the event list's does, or on an empty spot `No note` with the cursor's position, time and pitch. Above the notes, a ruler of two rows (decided October 2026, after use, since a view can run on into the next section): the sections as blocks in the arrangement strip's two shades, each named in the colour of its line from where it shows, the names drawn straight on the blocks (as on the arrangement's strip too, whose names take the line colour as well) rather than on a background of their own, which stood taller than the block, so a section begun before the view keeps its name at the left edge; the markers by name and the play-from marker ▶, names stopping short of the next or of ▶, or starting after ▶ when they're at the same place; and below, the bar numbers in the track's bars, every so many bars from each section's first if they're crowded. Each section's start is a line down through the notes and the lane, stronger than a bar line, and each marker a faint one. Below them, the keyboard column (note names, C's bold, black keys' rows shaded across the page, the cursor's pitch highlighted), the notes, and a velocity lane three rows high with its bar lines. Notes are drawn at their exact places: a solid start half a cell wide, then a lighter body, both brighter the louder the note, a pixel short of the next; the current note in the accent colour. The cursor is a faint band along its row and down the time it covers, through the lane, with that span boxed: the current note's start to end, or on an empty spot the length of the default note from the cursor, the note `o` would add, so it shows what you'd get (decided October 2026, rather than the grid step, which looked odd on short notes under a wide grid). The playhead is a line through the notes and lane, and the pages turn with it; while the cursor doesn't follow it (with rewind on), the view comes back to the cursor when playback stops, as the event list's does (decided October 2026, after use: the pages had stayed put while the playhead went off them). Motions: `h` / `l` by grid steps counted from each bar line (from off the grid, to the steps either side first), `w` / `b`, `{` / `}` (keeping the place in the bar), `[[` / `]]`, `0` / `$` (the bar's start and last grid step), `gg` / `G` (the song's start and its last grid step, or bar {count}), `]s` / `[s`, `]m` / `[m`, `Tab` / `Shift-Tab` to each note's start in time and then low to high, landing on its pitch, and `(` / `)` to each chord's start, on its lowest note; `k` / `j` a semitone, `K` / `J` an octave; `Ctrl-j` / `Ctrl-k` and `Ctrl-D` / `Ctrl-U` / `Ctrl-F` / `Ctrl-B` scroll the pitches with the cursor, and `zz` centres on its pitch (then later in time too, and `Ctrl-h` / `Ctrl-l` and `Ctrl-{` / `Ctrl-}` scrolling in time: see Piano roll pages); `zi` / `zo` / `z0` step the bars to a page; `gt` / `gT` go between tracks (not the conductor, which has no notes), each track keeping its pitch and page size. Motions past the song's end fail, as in Vim. Decided while building it: `J` stays bound to `join`, which the piano roll takes as an octave down, since one key can't be bound to two commands; and operators came with step 4c (see Editing notes, under Moving and editing notes).
- **The grid and default note, as built in milestone 5 (step 4b).** Each track's grid and default note are track settings, saved in the song. `zh` / `zl` (and `z` with Left / Right) halve and double the grid from 1/64 to a whole note, keeping triplets (first built the other way round, which felt reversed in use); `Ctrl-t` turns triplets on and off; `:grid 1/8t` sets it, and `:grid` alone shows it. `, n` takes the current note's length and velocity as the default note (on an empty spot it says there's no note); `:len 1/4` sets the length, `:len grid` links it to the grid again, and `:len` shows the default note. The piano roll's bar shows `Grid: 1/16  Note: grid v100`. Changes are undo steps (decided October 2026), with grid steps one after another on a track merged into one (so `u` after several `zh` goes back to the grid before them), unless the song was saved between; steps that bring the grid back to where it was leave no step.

- **Arrangement (home view).** One row per track, cells shaded by note density, with markers along the top. Since tracks can have different meters (decided in milestone 3), a column is a fixed length of time at the current zoom (such as one beat of the reference meter), not a bar: the track headers are text cells, but each row's timeline is a pixel region where the track draws its own bar lines at their exact positions, and the cursor is an exact span of time (such as bar 5 of that track) drawn to the pixel. The ruler numbers the conductor's bars. Used for navigating the song and for whole-section operations: copy verse to bar 33, mute tracks, set loop range. The section strip (below) sits above the tracks.
  - **As sketched in milestone 3's test view** (to be confirmed in milestone 5). Each bar is a block as wide as the bar is long, as SPG's squares but in proportion, so a 7/8 bar is 7/8 as wide as a 4/4 bar. Blocks are shaded in four levels by how many notes start in the bar (theme roles `density-1` to `density-4`); a bar with no notes is a faint line, and an empty track shows a dot on each bar line. A small gap separates blocks while a column holds no more than a whole note; zoomed out further, the gaps go, since the song's overall shape matters more than single bars there. Markers are faint lines at the same time on every track whatever their meters, beneath the blocks and text, through the ruler (broken only where a label sits across them) and across the tracks; and are named in the ruler, each name cut short with an ellipsis at the next marker, or left out if fewer than three characters fit. The ruler also shows the reference meter's changes (the conductor's), each named if the name fits before the next change and otherwise shown by a tick, so a single bar in another meter never hides the change after it; and its bar numbers, counted in sections that start at bar 1 and at each marker's bar, so the numbers follow the song's structure: each section's first bar, then every n bars into it, where n (1, 2, 4, 8...) is the fewest bars that leave room for the widest number in the shortest bar. A number shows only if it fits before the next section's, so the sections' numbers come first. Track headers show no meter, since a track's meter can change along it; the status line shows the meter at the cursor. The cursor is one bar of its track, boxed on all four sides; `h`/`l` move by that track's bars and `j`/`k` keep the cursor's time, landing on the bar that contains it, so going back up returns to the same bar. SPG's highlighting of every eighth bar isn't used, since sections often run to 6 or 12 bars, or have an extra bar; it could be an option later. The sketch predates sections: in the real view, the ruler's numbering restarts its counting at each section rather than each marker, and section boundaries are the faint lines down through the tracks, with markers drawn more lightly.
- **Piano roll (track editing).** Pitch rows against time, drawn on the grid with pixel-precise note starts and lengths inside the cells. A velocity lane underneath shows bars per note. The cursor is a crosshair, a position in time plus a pitch, and doesn't have to sit on a note. When it is at the start of a note or inside one, that note is the **current note**: the inspector shows it and operators act on it. On an empty spot, the inspector shows the position and pitch only. Motions are in Editing grammar.
- **Event list (exact data).** One row per event: bar:beat:tick, type, channel, data fields (pitch name, velocity, duration). Every field is a cell you can move to and edit directly, with `Ctrl-A` / `Ctrl-X` incrementing and decrementing as in Vim. Filters show only notes, only CCs, a pitch range, etc.
  - **Decided for milestone 4.** The list shows one track at a time, as in SPG, with keys to change track (`gt` / `gT` to begin with, open to better ideas). Tempo changes, meter changes, section starts and markers are listed dimmed in every track's list, at their positions. The conductor track (decided October 2026) has a list of its own too, reached with `gt` like any track, where its tempo and meter changes are edited. `j` / `k` move between events and `h` / `l` between an event's fields, like a spreadsheet; the time motions (`w` / `b`, `{` / `}`, `[[` / `]]`) move the cursor by time, to the event at or after where they land, and never move notes, which the Alt keys do. Enter edits the field under the cursor in place, as a small prompt in the cell, where any character is typed (so `-40` and `+10` can be entered); in Normal mode, `+` and `-` step the field under the cursor, with counts, and `Ctrl-A` / `Ctrl-X` do the same for Vim habits. The inspector shows the event under the cursor; editing its fields with `e` comes with the piano roll (milestone 5), where a note's fields can't otherwise be reached.
  - **Motions in the event list (decided in milestone 4).** They're for getting about, not marking out time, since the event list is for exact tweaking of events rather than editing in time, and its cursor is always on a row; there are no placeholder rows for empty time, which some tracks would have thousands of. `w` / `b` (a beat), `}` / `{` (the same place a bar on or back, keeping to that place while repeated, through shorter bars) and `]]` / `[[` (a bar line) work in the track's own bars from the cursor's row, with counts, and go to the first row at or after where they land. Going back, if that row isn't earlier in time than the cursor's, the motion is taken again from there until it is, or reaches the start, so it never sticks: `{` means back a bar at a time until there's an event. `)` / `(` go to the next or previous chord: events starting within `chordwindow` of the first of them (30 ms by default, in milliseconds since it's about how a person plays, not a musical length, so it's the same at any tempo; measured from the first event, not chained, so a fast run isn't one long chord); from inside a chord, `(` goes to its start first. `]s` / `[s` and `]m` / `[m` go to a section's start or a marker, on its own row; going back from a section's start (or an event at it) goes to the section before. `0` / `$` stay the first and last field in the event list, since `[[` goes to the bar's start; the piano roll keeps the design's meaning, the start and last grid step of the bar.
  - **Operators in the event list (decided in milestone 4).** `d` and `y` take a motion, with counts that multiply (`2d3j` is `d6j`), and work on rows as Vim's operators do on lines: `j`, `k`, `gg` and `G` take whole rows, the one landed on too (`dj` is this event and the next); the time motions stop before the row they land on (`d}` deletes from this event up to the one `}` goes to; `d{` from where it goes up to this one). Pressing the operator's key again is the event under the cursor (`dd`, `3dd`), and `x` is the same as `dd`, since a list has no characters to delete. Only the track's own events are touched: the conductor's rows and the end row are passed over. In operator-pending mode the Normal mode's motions work through their own bindings, unless `omap` binds those keys, and any other key gives the operator up, as in Vim. Commands say they're motions in the command registry. One unnamed register holds the events yanked or deleted, their times from the first's, so putting keeps their timing in any meter; yanking says how many. `p` puts them with the first at the cursor's time, merged in without overlapping notes of one pitch, by the recorder's rule (whichever starts first ends where the other starts, and at the same tick the new one wins). Each change is an undo step titled for an undo list ("Delete 3 events in Bass"); `u` and `Ctrl-R` (and `:undo`, `:redo`, with counts) go back to where it was made, on the same row among those at its time, saying "Undid: …". The conductor's list is edited with field editing (see Field editing in the event list), which also brought `o`, adding a note and going straight into editing its pitch. The view's messages in the status line start with a capital, as Vim's do ("Nothing to undo", "2 events yanked"); config errors keep their `modalrc:3: …` form.
  - **Field editing in the event list (decided in milestone 4).** Enter edits the field under the cursor in its cell, holding its value as shown, without a note value or note number in brackets; typing replaces it, as in a spreadsheet, and the editing keys (the arrows, Home, End, Backspace) keep it; Enter sets it and Escape leaves it. A value out of range, or an edit that would make a note overlap another of its pitch, keeps the prompt open with a message saying why (naming the other note: "Would overlap the F1 at 2:1:000"), to put right; an overlap is refused rather than resolved, since resolving would silently shorten or replace a note out of sight (putting, a deliberate paste, keeps the recorder's rule). A field takes a value or a change with `+` or `-`: a position as bar:beat:tick (or bar:beat, or bar) or a change of bars:beats:ticks or a note value (`+1/16`, `-0:0:010`); a pitch by name or number, or semitones; a velocity 1–127 (never 0, which is a note-off), a value 0–127, a bend −8192 to 8191; a length as bars:beats:ticks in the meter where the note starts or a note value; text as typed; SysEx as hex bytes. `+` and `-`, and `Ctrl-A` and `Ctrl-X`, step a field with counts, kept within its range: a value, release or bend by 1 (bend's step to be judged in use), a pitch by a semitone, a position by a beat of the meter where the event is, a length by a 1/16 note until the per-track grid (milestone 5). An edited event takes the cursor with it when it moves. Each edit is an undo step ("Set velocity in Bass"). `o` shows a new note row after the cursor's, at its time, with the velocity and length of the note before (or 100 and 1/8), and edits its pitch; Enter adds it as one step, Escape leaves nothing. In the conductor's list, Enter and the steps change a section's or marker's name, a tempo (stepped in whole BPM) or a meter (its top stepped); a meter that a change soon after, such as a remainder bar, leaves no room for is refused with a message naming that change; `dd`, `x` and `d` with a motion delete tempo and meter changes and markers, but not a section's own tempo and meter at its start, nor a section, which are joined later. Moving the conductor's changes, and adding them, come later; yanking and putting are for track events only. Several events at once wait for Visual mode (milestone 6).
  - **As built in milestone 4.** The columns are the position, the event's type, then its fields: Data (a note's pitch as name and number, `E1 (28)`, a controller, a program, or a name, text or SysEx bytes, which run on to the edge), Value (a velocity or value), Length and Release, under headings that stay put as the list scrolls; each field is a cell the cursor can be on, and `h` / `l` keep to a field as the rows change, as Vim keeps to a column. Positions are in the track's own bars, the ticks padded to a quarter note's width (three digits at 960 PPQN), so the columns line up. A note's length shows as bars, beats and ticks in the meter where it starts, with in brackets the common note value it is or is within 5% of, if there's one: `0:0:480 (1/8)`, `0:0:492 (≈1/8)`, or `0:1:252` alone (decided in milestone 4). The common values are those with denominators up to 16: straight from a whole note to 1/16, dotted and triplet to 1/8. and 1/8t, and whole notes beyond. Showing only the nearest value with ticks over or under (`1/8 + 12`) was tried first, but recorded files gave values like 13/48 and a straight note named as a triplet less 95 ticks, which no musician would use. Editing takes bars:beats:ticks or a note value, and relative changes of either, `+1/16` or `-0:0:010` (see Field editing in the event list). The 5% is to be checked against real files. At one tick the rows are a section start, then meter, tempo and markers, then events in the order they're sent. The conductor's rows in a track's list are only there to find the way by, and are changed only in the conductor's own list (decided in milestone 4), so that changing them never changes other tracks unseen and editing notes never deletes a tempo; the cursor still lands on them. A track's own meter shows undimmed. The last row is the end of the song, where the cursor can sit in an empty track. The conductor's list is track 0, before track 1, so `gT` from track 1 goes to it and `gt` from the last wraps round to it; with a count, `gt` goes to that track. The list opens on track 1. The top bar names the track, its channel and port, and the section the cursor is in with its bars; the status line shows the mode, the cursor's position, the meter there and the section. `modal FILE` opens a `.modal` song or imports a `.mid` file (not to be saved over); without one it starts a new song, and `modal --demo` opens the demo song from the arrangement sketch (see A new song).
- **Status line and transport.** Contents set in configuration (see Customisation).

**Piano roll pages (decided October 2026, revised after use).** The piano roll shows a width of a whole number of bars at a time, usually starting at a bar line, rather than a freely zoomed timeline; but it can be scrolled to start anywhere.

- The bars in view together are stretched to fill the window width, whatever their length: a one-bar page of 7/8 is as wide as one of 4/4. Within the view, time keeps one scale, so on a page mixing meters a 7/8 bar is a little narrower than a 4/4 bar. Off a bar line, the view runs from where it starts to the same place that many bars on, except while it's scrolled (below).
- **Opening a track** (Enter on a bar in the arrangement, coming to the piano roll at a time outside its view, or `gt` / `gT`) shows whole bars, in pages that restart at each section (decided October 2026, rather than at each marker): with a section starting at bar 3, 4-bar pages show bars 3–6, 7–10 and so on. The view is always the full number of bars, so a page near a section's end runs on into the next section rather than stopping short.
- **Turning:** when the cursor moves past either edge, the view turns by its own width, keeping where it starts in the bar (a view from bar 3 beat 3 turns to bar 7 beat 3), so turning never jumps. Following the playhead turns it the same way.
- **Scrolling (decided October 2026):** `zz` centres the view on the cursor in time as well as in pitch (as Vim's centres the line), keeping the number of bars, so partial bars may show at either edge. `Ctrl-h` / `Ctrl-l` move the cursor by grid steps and scroll the view with it, so the cursor stays put on screen; `Ctrl-{` / `Ctrl-}` do the same by a bar, keeping the cursor's place in the bar, the view moving as far as the cursor does (bound as Ctrl-Shift-`[` / `]` too, the way most keyboards send them; in the arrangement they scroll by bars, as `Ctrl-h` / `Ctrl-l` do there). While scrolled, the view keeps its length in time, so its scale stays put as bars of another length come and go (decided October 2026, after use: fitting the bars at each step made the view stretch and shrink, and the cursor jump); turning, zooming and opening a track fit whole bars again. The view never starts before bar 1, but may show time past the song's end, with the end marked, so `zz` can centre there.
- **Zooming:** `zi` / `zo` change the number of bars in view, stepping through a list: 1, 2, 4, 8, 16 by default, configurable later (e.g. `set pagebars=1,2,3,4,6,8,12,16`); a count setting the size directly (`3zp`) is to come. Zooming keeps the cursor at the same place across the view (decided October 2026, after use, rather than anchoring on the bar line nearest the cursor and returning to whole bars, which left zooming out and back in at a different place): with the cursor halfway across 4 bars, zooming out to 8 keeps it halfway, so zooming back returns to the same view, except where the view would start before bar 1 and stops there instead. `z0` goes back to the default number of bars as whole bars, the section's page holding the cursor.
- The view's bar shows the section the cursor is in and the bars in view, partial ones included (`Bars: 8–12`), rather than a page number, since the view needn't be one of the section's pages.
- Vertically, the view scrolls (unlike horizontal paging) to keep the cursor's pitch visible; when a track opens, the view centres on the track's pitch range.
- Grid lines are drawn at the current grid, with beat and bar lines stronger. A grid coarser than a beat is drawn as itself, its steps as beat lines and the beats between left out (decided October 2026, after use: drawing the beats as well made a grid of 1/2 or 1/1 look like one of 1/4). If the grid is too fine to show legibly across the page, lines are drawn at the finest level that fits; movement still uses the real grid. Notes are always drawn at their exact positions, so recorded material never looks falsely quantised.

**The piano roll screen (decided October 2026)**

```
 NOTE  21:2:480 │ E2 (40) │ vel 96 │ len 1/8 │ ch 1 │ Bass          ← inspector
 Chorus · bars 21–24 · page 2/4                     grid 1/16 │ len =grid
──────┬─────────────────────────────────────────────────────────────
  G2  │ ·           │           ·           │                        ← pitch rows
  F#2 │▒            │                       │                        (black keys shaded)
  E2  │   ████      │  ██[██]       ████    │
  D2  │             │        ███            │
  ...
──────┼─────────────────────────────────────────────────────────────
  vel │ █  █  █     │  █ [█]  █     █       │                        ← controller lane
──────┴─────────────────────────────────────────────────────────────
 NORMAL │ 21:2:480 │ ♩120 │ E minor │ scale lock │ ...                ← status line
```

From the top: the inspector (the current note's details, or the cursor's position and pitch on an empty spot; `e` enters it); the page header (section, bar range, page number, grid and default note length); the keyboard column of note names, with black-key rows shaded across the full width; the note area, the page's bars stretched to the width; the controller lane; and the status line.

- **Notes** have a distinct start (a bold first cell) and lighter sustain, so repeated notes of one pitch read as separate notes. Brightness follows velocity, so dynamics show in the note area as well as the lane. Off-grid notes are drawn at their exact positions, inside cells. The current note is in the accent colour, selected notes in the selection colour, ghost notes dimmed.
- **The cursor** is a crosshair: its cell highlighted, with a faint highlight along its row and column, so both its pitch and position read at a glance.
- **Scale lock** shows as in-key rows slightly brighter and out-of-key rows dimmer, so out-of-key notes stand out.
- **Rows:** the number of pitch rows follows the window's height (see Piano roll pages for scrolling and `zz`).
- **Hearing it:** optional audition plays each note as the cursor lands on it (useful when stepping with `Tab`); Space plays from the cursor, `, L` loops the page or section, and the playhead is a vertical line, with pages turning by themselves when follow is on.

**Ghost notes.** Other tracks' notes shown dimmed behind the current track's, for writing a part against others. They can't be edited and the cursor ignores them. Choosing them is quick: `]g` / `[g` show the next / previous track as the ghost, one at a time, with no setup; `, g` opens a hint menu of tracks with number keys (`1 Drums ✓  2 Bass ✓  3 Pad`), where numbers toggle tracks and `Esc` closes it; `, v g` shows or hides the ghosts without forgetting them. Each track remembers its own ghosts, saved in the song (the keys track always ghosting the bass, say).

**Split view.** Other tracks in panes of their own, locked together in time, with Vim's window keys: `Ctrl-w s` opens another track in a pane above or below, `Ctrl-w j` / `Ctrl-w k` move to the pane below / above, `gt` / `gT` change which track the focused pane shows, `Ctrl-w c` closes it and `Ctrl-w =` evens the heights. Every pane shows the same bars and turns pages together; pitch scrolls in each on its own. Every pane can be edited; the focused one has the cursor and drives the inspector. More than two panes are allowed (drums, bass and keys stacked). Ghosts are for how one line sits against another, splits for writing interlocking parts.

**Controller lanes.** The lane shows velocity by default; `, v c` switches it to a chosen CC (CC 1, say), pitch bend, aftertouch or sustain. Velocity, sustain, CC 1 and pitch bend get good default support first; several lanes at once can come later. `gl` moves the cursor into the lane, where `h` / `l` go to the previous / next note or event, `k` / `j` raise and lower the value, digits and `Enter` set it, and `Esc` goes back to the notes. Operators and transforms work on lane values over motions and selections (a velocity ramp over a selection).

**Switch controllers.** Controllers that are really on or off (sustain, sostenuto, soft, legato and portamento switches; the set is configurable) are regions, not curves: the lane draws pedal-down spans as solid bars, like notes, just under the note area, and they're edited like notes, with `Tab` / `Shift-Tab` to the next / previous region, `o` to put the pedal down at the cursor (for the default length, or up to the next region), `=` / `-` to lengthen and shorten by a grid step, `Alt-h` / `Alt-l` to move earlier and later, and `x` to delete. Down and up are the only states, so there's no dragging values. Recorded and imported values of 64 and above are down, below 64 up; half-pedal values are kept, and shown in a lighter shade, but edits treat a region as one unit. The pedalling tools are in MIDI transforms.

**Drum maps.** A track can have a drum map, a name for each note number: General MIDI by default, or a custom map, chosen per track and saved in the song or as a reusable map file. With one, the keyboard column shows names (Kick, Snare, Closed HH) rather than note names, the inspector shows the name beside the note number, and `gp`, search, `:select pitch` and the palette take the names (`gp kick`, `/snare`). Drum tracks start with scale lock off.

**Folding.** `, v f` toggles folding, showing only the pitch rows that hold notes (in the track, or optionally the section), so a drum track shows a handful of rows rather than 128. With folding on, `j` / `k` step between the rows shown, and so does extending a `V` selection. For example: fold a drum track, put the cursor on Kick, `V`, and adjust.

**The section strip (decided October 2026)**

```
           │1   5   9   13  17  21  25  29
 SECTIONS  │Intro  │Verse 1        │Chorus         │Verse 2 ...
───────────┼───────────────────────────────────────────
 1 Drums   │░░░░░░▒▒▒▒▒▒▒▒▒▒▒▒▒▒▓▓▓▓▓▓▓▓▓▓▓▓
 2 Bass    │      ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▓▓▓▓▓▓▓▓▓▓▓▓
```

A row above the tracks in the arrangement, showing each section's name across its span in its colour; section boundaries are faint lines down through the tracks. `k` from the first track moves up onto the strip, `gs` jumps to it from anywhere in the arrangement, and `j` returns to the track the cursor came from, at the same position. On the strip the cursor is a whole section, and the inspector shows it: name, bar range, length, meter, takes and cue slot. `h` / `l` go to the previous / next section and `0` / `$` the first / last, with counts.

Sections behave like lines in Vim:

| Key | Effect |
| --- | --- |
| `x` | Delete the section under the cursor; later material closes up |
| `Y` | Yank the section under the cursor |
| `d` + motion / `dd` | Delete sections over a motion / this section (`d$` deletes to the last) |
| `y` + motion / `yy` | Yank sections over a motion / this section (`y2l` yanks this and the next) |
| `p` / `P` | Put the yanked sections after / before this one |
| `Alt-h` / `Alt-l` | Move the section earlier / later, swapping with its neighbour |
| `J` | Join with the next section |
| `=` / `-` | Lengthen / shorten the section by a bar (later material ripples) |
| `i` / `a` | Insert a new empty section before / after this one, then ask for its name |
| `c` | Rename it (focus goes to the name in the inspector) |
| `e` | Edit any of the section's fields in the inspector: name, colour, length, cue slot |
| `.` | Repeat the last operation |

- `d` and `y` stay operators taking motions, as everywhere; `x` and `Y` are the single keys for the section under the cursor. Counts apply (`3dd`, `2yy`). A section that moves takes the cursor with it, and each operation is one undo step. Duplicate a chorus to the end with `Y`, `$`, `p`; move the bridge two places earlier with `Alt-h`, `Alt-h`.
- `V` selects a run of sections, for `d`, `y` and `Alt-h` / `Alt-l` to act on the block, and `, L` to loop it.
- `Enter` plays from the start of the section, `Shift-Enter` cues it during playback, `, L` loops it and `:cue F3` gives it a cue slot.
- Splitting is done from the tracks (`:section NAME`), where the cursor has an exact bar position; the strip works on whole sections.
- **Decided for milestone 5's strip.** Shortening a section with `-` deletes everything starting in the bar that goes, as `d` over it would (undo brings it back), and never shortens below a bar. A new empty section from `i` / `a` is 4 bars, or the count (`8a`). New sections are named `Section` and one more than the highest number among sections named that way, so they count up in the order they're made even when that leaves gaps; any name stays unique, numbered if another section has it ("Chorus 2"). `i` / `a` then open the `:` prompt with `section-rename` and the new name, to type over or keep with Enter, and `c` does the same for the section under the cursor; editing it in the inspector with `e` comes later. Yanked or deleted sections go in the one unnamed register, as Vim keeps characters and lines in one; `p` with bars in it on the strip, or with sections in it on the tracks, says what it holds rather than guessing. Until sections can be given colours, neighbouring sections on the strip alternate between two shades. A song always keeps at least one section, so deleting every one is refused. As built: the strip is the fourth row above the tracks, after the markers' names, the meters and the bar numbers; section names moved there from the ruler, which now names markers only. `section-strip` (`gs`), `move-left` / `move-right` (`Alt-h` / `Alt-l`), `join` (`J`), `insert-before` / `insert-after` (`i` / `a`), `change` (`c`), `register-put-before` (`P`) and `yank-line` (`Y`, as `yy`) are commands of their own, which other views refuse until they have a use for them; `+` / `=` and `-` step a section's length, as they step a field in the event list. `section-rename NAME` renames the section at the cursor from any view. Deleting or yanking sections puts them in the register, and the strip's operators work as on lines: `dd` and `x` this section, `d2l` this and the next, `d$` to the last.

**The outline view (decided October 2026)**

A vertical list of the song's sections (`, S o`), for the running order of long songs with many sections, which is also the cue sheet:

```
     ▶  1  Intro          1–4     4/4   F1
        2  Verse 1        5–20    4/4   F2
     ◆  3  Chorus        21–28    4/4   F3   2 takes
        4  Instrumental  29–52    7/8   F4
```

Columns: number, name, bar range, the conductor's meter, cue key and takes (later, linked sections and repeats, e.g. "Chorus ×2", like an MPC's song mode). Its keys are the strip's, turned upright: `j` / `k` the next / previous section, `Alt-j` / `Alt-k` move the section down / up the running order, and `x`, `Y`, `dd`, `yy`, `p`, `P`, `J`, `i`, `a`, `c`, `e` and `.` as on the strip. `Enter` / `Shift-Enter` play from or cue the section, `Shift-F1` to `Shift-F12` assign that cue slot, `F1` to `F12` cue the section assigned during playback, and `Esc` or `, S o` go back. While playing, `▶` marks the section playing and `◆` the one cued next, so the outline doubles as a performance view. Later, a setlist view one level up could list songs the same way (see Ideas: live rig mode).

**Resizing.** Text size stays fixed when the window resizes; the grid gains or loses cells. In the arrangement and event list, extra columns show more time at the same zoom (the piano roll instead stretches its page of bars to the width), extra rows show more tracks or pitches, and panels such as the velocity lane keep fixed heights while the main area takes the rest. `Ctrl-+` / `Ctrl--` change the font size, and with it the cell size. So that resizing is smooth rather than jumping a cell at a time (decided in milestone 3), regions are laid out in pixels: bars at the top are anchored to the top edge and bars at the bottom to the bottom edge, so they follow the window edge a pixel at a time, and right-aligned status segments follow the right edge. The main area takes everything between, including a partly visible last row and column, clipped, which are revealed gradually as the window grows. The cursor and scrolling count only fully visible rows and columns, so the cursor never sits on a half-hidden one.

**Grid regions and bars (decided in milestone 3).** Only the views you move around cell by cell (event list, arrangement, piano roll) are grid regions, laid out in cells. The bars (status line, inspector, command line, hint menu) aren't navigated as a grid, so they're laid out as segments of text at pixel positions instead:

- **A terminal look.** Bars use the same font and theme colours as everything else, with no bevels, gradients, borders or OS-style controls: a bar is a line of text on a theme-coloured background, like a Vim status line, so it blends in.
- **Groups anchored to edges.** Segments flow left to right in a left, centre or right group. The left group is anchored to the left edge, the right group to the right edge (so it follows the window edge a pixel at a time) and the centre group to the centre. Each segment keeps its own colours and attributes, and the bar's background fills its whole width.
- **Padding.** A margin between each group and its window edge (half a cell's width by default) and a minimum gap between groups (two cells by default), as settings.
- **Too little room.** The right group is kept whole, and the left group gives way: its last segment is cut short with `…`, and a segment that can't show at least a few characters is dropped. If the right group alone doesn't fit, it is cut from its left end the same way. Per-segment priorities can come with the configurable status line (milestone 9).
- **Fields without a grid.** Segments can be focusable fields, so `Tab` moves between the inspector's fields without a grid underneath.
- A bar is one line of text high, plus padding, and is anchored to its window edge (see Resizing). Bars that aren't text, such as the piano roll's ruler, are pixel regions that stretch with the width.

**Minimum window size (decided in milestone 3).** The window can't be dragged smaller than 120 × 30 cells (80 × 24 at first; raised in milestone 4, when the inspector's fields and the track's bar needed the width, to near the size Modal opens at), a setting (`set minsize=120x30`) that follows the font size: if a larger font puts the window below the minimum, the window grows to fit, as far as the screen allows. The default bars fit at the minimum, so the overflow rule above is a safety net for long content (a long track name, a message) rather than a routine sight.

Splits (later): two views side by side, e.g. arrangement above event list, as with Vim windows.

**Inspector**

- **As built in milestone 4.** Every view has the same order from the top (decided in milestone 4): the view's own bar first, as a title, then the inspector, then the view. The event list's bar labels its values, `Track 1: Bass   Channel: 1   Port: Pro-800 ... Section: Section 1   Bars: 1–8`, labels dim and values in the text colour; the piano roll will have a bar of its own like it. The inspector is a panel of two rows on a tint of its own (the `panel` colour), so it doesn't read as another bar: the kind of row (Note, CC, Section…) in the accent colour, then each property's heading, dim, above its value, with a quarter of a line's padding above and below. Each column has a fixed width, as wide as its longest value can be (a position as wide as the song's last, a length 15 characters, a controller 25), so the columns stay put as the cursor moves from row to row. It shows what a row can't: a note's end as well as its length, a controller's standard name (`74 Brightness`, from a table in the core the piano roll's lanes and the palette will share), text and SysEx whole, and the time from the song's start (`1:02.250`). The track and channel are left to the view's bar, until a view shows several tracks at once (the arrangement, a split piano roll), when the inspector shows them too. On a row that isn't an event it says what the row is: a section's bars and length, a tempo in BPM and milliseconds per quarter (`594.059 ms`, exact to the microsecond the tempo is stored in, which as a raw number was hard to read), a meter, a marker, or at the end the song's length in bars and time. The bar stays separate from the inspector, so the piano roll can use the same inspector. Half a line of space separates the bar, the inspector and the list, rather than boxes or rules that cost whole rows. The view itself is the one area on the `background` colour; everything around it, the gaps included, is on the `chrome` colour, which the built-in themes make the bars' own colour (decided in milestone 4). So the view bar and the gaps run together as one strip rather than stripes, the inspector sits in it as a block of its own, and the view stands out as darker in the dark theme and paler in the light one. A chrome halfway between the background and the bars was tried, and left the bars looking like stripes. The status line ends with the song's file, `song.mid (imported)` for an imported MIDI file (which saving doesn't write over), or `No file`, and `[+]` while it has changes not saved, as in Vim.

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

Themes, key bindings and the status bar are all defined in a plain-text command file from the start, so the defaults are just shipped configuration. The file uses the same command language as the `:` command line: every command a file takes can be typed there, and the prompt runs any other command too (see The command registry in use). It can be re-run with `:source` without restarting.

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
- **Key bindings.** Per-mode bindings and leader sequences, rebindable like Vim's `map`. Each binding names a command and a hint label for the leader menu. Commands are named in lower case with hyphens, by what they act on: `cursor-left` moves the cursor, so a command moving notes left can't be mistaken for it (decided in milestone 3). The modes are Normal, Visual, Insert, operator-pending and command line. The defaults aim for stable muscle memory and won't be reshuffled between versions. Commands come from the command registry (see Architecture).
- **Config files (decided in milestone 3).** The user's command file is `~/.config/modal/modalrc` on Linux and `%APPDATA%\Modal\modalrc` on Windows, found through the platform layer. The shipped defaults (a default command file and the light and dark themes) are built into the program, so it runs with no files installed; the user's file runs after them. `set theme=NAME` starts again from the dark theme, then runs `themes/NAME` from the config directory, or else the built-in theme of that name, so a theme of your own need only set the colours it changes; `highlight` lines after it override single colours. The built-in files are ordinary command files in the repository (`assets/config/modalrc` with the default key bindings, `assets/themes/dark` and `light`, after Tokyo Night and Tokyo Night Day), built into the program. An error reports the file and line, and the rest of the file still runs; at startup errors go to stderr, and the first shows in the status line in the warning colour until the next key. Until the full command line exists (milestone 6), files take only the commands that exist so far, `set`, `highlight`, the `map` family and `source`, and the `:` prompt those and a few of its own (see The `:` prompt). User-facing details are in `docs/configuration.md`.
- **Command file syntax (decided in milestone 3).** A command a line; a line starting with `"` is a comment, as in Vim. Arguments are separated by spaces, and as in a shell, double quotes keep spaces in an argument (`set font="C:/My Fonts/Mono.ttf"`), with `\"` and `\\` inside; but a map command's keys are taken as they are, since `"` is a key. `highlight ROLE #rrggbb` sets one role's colour, so the cursor's text is `cursor-text` rather than a foreground half of a `cursor` group: roles are named in lower case with hyphens, one colour each. The map commands are `nmap`, `vmap`, `imap`, `omap` and `cmap`, each `KEYS COMMAND ["HINT"]`, with `nunmap` and so on; there is no plain `map` (Vim's covers three modes, which surprises), and no `noremap`, since bindings name commands rather than other keys. `source FILE` runs a file relative to the one running it.
- **Settings registry and generated config (decided in milestone 3).** Every setting is registered with its name, type, default, range and a one-line description, which later also drive `:set` completion, hint labels and error messages. Setting names are lower case without hyphens, as Vim's options are (`timeoutlen`, `fontsize`), while command names have hyphens (`cursor-left`). The first settings are `theme`, `font`, `fontsize`, `minsize` and `timeoutlen`. `modal --write-config FILE` (with `--force` to overwrite) and, with the `:` prompt, `:write-config FILE` (`:write-config!` to overwrite) write every setting, key binding and colour, in sections, each with a comment saying what it does, and list the commands keys can be bound to. What differs from the defaults is written as commands (settings with their current values, key bindings as `nmap` and `nunmap` lines against the built-in `modalrc`, colours as `highlight` lines against the chosen theme); the rest is commented out at its defaults, so a user changes only what they want and keeps later improvements to the rest, and what they're using now, including a size set with `Ctrl-+`, is kept (decided in milestone 3). Read back, the file gives the same config. A relative FILE is in the config directory for `:write-config` (the program's own current directory means little when started from a menu) and the current directory for `modal --write-config`, which reads `modalrc` first. It writes to a named file rather than to stdout, since its one use is to start a config file, and on Windows `modal` has no console. It is never written automatically.
- **The command registry in use (decided in milestone 4).** The default key bindings stay in the built-in `modalrc`, the only place they're written; the registry looks up a command's current keys from the live keymap when it shows them (the hint menu and the palette), so they show a user's own bindings. For now an entry gives its arguments as a usage string (`FILE`, `NAME=VALUE...`); typed parameters with defaults come in milestone 6, with completion, the palette and the transforms. Every command can be run at the `:` prompt by name or alias, with `!` where it takes one, and a command that takes a count takes it as its argument (`:cursor-down 3`), since `:3` will mean "go to bar 3". Only the config commands (`set`, `highlight`, the `map` family and `source`) can go in a config file; any other command there is an error saying it can't be used in a config file, since the views don't exist yet when the file runs and moving a cursor at startup means nothing. So "anything you can type there can go in the file" now holds for the config commands, and the prompt takes more. The program describes every command before the config is read, since the config binds keys to them; the views and the window attach what runs theirs once they exist, and keys and the `:` line both run commands through the registry. A view that registers its commands can't be copied or moved.
- **The `:` prompt (decided in milestone 3).** `:` opens a prompt in the bottom bar, in command-line mode, whose editing keys are `cmap` bindings in the built-in `modalrc` (Enter, Escape, Backspace, Delete, the arrows, Home, End, `Ctrl-U`, Up and Down); any other character is typed, and Backspace on an empty line closes it, as in Vim. Digits are counts only in Normal, Visual and operator-pending modes, so they're typed at the prompt. Up and Down recall the session's lines; Tab completion waits for the full command line (milestone 6). The prompt runs the command-file commands, `set NAME?` to show settings, `source` with no file to run `modalrc` again (over the current state, as Vim's does), `write-config`, and `q` or `quit`. Settings, bindings and colours apply at once. Errors show in the warning colour and results in the `message` colour.
- **Completion and pick lists wherever there's a fixed set (decided in milestone 4, built in milestone 6 or, where the views need it, sooner).** Wherever a value comes from a known set, Modal offers it rather than making you remember it: command names, settings and their values (a theme, on or off), output ports, tracks, sections, markers, files. At the `:` prompt, Tab completes the command or argument under the cursor, cycling through matches, with the matches shown above the prompt. A command run without an argument it needs (`:port`, `:e` with no file aside, which has the system's dialog) opens a pick list of the choices, palette-style, filtered as you type and chosen with Enter; one whose argument is optional still says what it is when there's no list to pick from. Each command's parameters in the registry name their kind (a port, a track, a setting's value and so on), and each kind has one provider of its choices, so the prompt, the pick list, the palette and the hint menu all offer the same ones. Until then, `:port` alone lists the ports in the status line.
- **Status bar.** Built from named segments (mode, position, tempo, time signature, key, loop/punch, record state, MIDI activity, pending keys, message), each with a format string, arranged left and right in configuration, like a Vim or Doom Emacs modeline.

## Input and recording

Recorded events are timestamped by the engine at arrival, never by the UI, so recording accuracy is independent of screen activity.

- **Real-time recording:** optional count-in, metronome as MIDI notes to a chosen port, input quantise off by default. Recording goes to the armed track(s); thru to the track's output so you hear the right sound while playing.
- **Loop recording:** each pass is kept as a separate take; version 1 merges or keeps the last, take lanes come later. Looping a section is the usual way in (see Sections: looping, takes and count-in).
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

**Sections: looping, takes and count-in (decided October 2026)**

- **Looping:** `, L` loops the section the cursor is in; a run of sections selected on the strip loops as a range.
- **Passes:** loop recording over a section handles each pass by a setting: **takes**, each pass kept as a separate take of the section on the armed track, or **overdub**, each pass merged into one layer (MPC-style), building a part up pass by pass. While overdubbing, holding an erase key (to decide) and pressing notes on the MIDI keyboard removes those pitches as the playhead passes.
- **Takes** are stored with the section, per track, so they move with it. `]t` / `[t` go through the current section and track's takes, auditioning each; one take is the active one, and a command makes the current take active and can discard the others. Comping (combining the best parts of several takes) comes later.
- **Count-in for takes,** a setting: the first pass only, then seamless looping; every pass, with a count-in (metronome only, the music silent) between passes to give time to reset; or a pre-roll before each pass, playing the last few bars of the previous section so the player comes in with the song's context. Count-ins use the conductor's meter. Each pass, with its count-in or pre-roll, is one undo step.

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
- **Cueing: queued section jumps (decided October 2026).** During playback a jump to another section can be queued, to happen at a musical boundary. Move the cursor to any section (which doesn't interrupt playback) and `Shift-Enter` cues a jump to it, or `:cue Chorus` by name. Cue slots give instant cues: `F1` to `F12` by default (to decide), reassigned for the session with `:cue F3 Bridge` or saved with the song. Cues are ordinary commands, so they can go in macros or on keys, such as a key that cues Verse 2 then Chorus; a fuller playlist of sections, like an MPC's song mode, could grow from this. The jump happens at the end of the current bar or of the current section (a setting, to decide; `:cue Chorus now` overrides it). If looping, the loop moves to the new section. The status line shows the cue, and cueing it again, or `:cue cancel`, cancels it.
- **MIDI clock.** A keyboard-driven transport and sync settings screen chooses which ports send MIDI clock, Start/Stop/Continue and Song Position Pointer. These settings are stored in the song, with port names resolved through each machine's port aliases.

* **Panic.** `Ctrl-.` (a full stop, literally) stops playback and sends All Notes Off and resets controllers on every channel of every port playback has used, in every mode. The interim command-line tools use `!` while playing and a `--panic` option instead, since terminals can't reliably detect `Ctrl-.`.
* **Recording input.** Armed tracks listen to all MIDI inputs by default; each track can optionally be set to one input.
* **MIDI thru.** On by default, so you hear the armed track's sound while playing; can be switched off globally or per track.
* **How thru behaves (decided in milestone 2).** Thru works whether or not the song is playing, so sounds can be tried before recording. It goes to every armed track with thru on, including muted and un-soloed ones, since thru is how a track being recorded is heard. Each gets the track's channel, transpose and velocity offset (so you hear what playback will sound like) but not its time offset. The engine remembers where each held thru note and pedal went (up to 1,024 notes and 256 pedals; a note-on beyond that isn't sent), so its note-off or pedal-up goes to the same place even if the track is disarmed or changed meanwhile. Stop leaves held thru notes alone, since their note-offs will follow; panic ends them, and reaches thru's ports too. Thru delay, from arrival to just after the send call, is measured separately from playback lateness.

## Metronome and count-in

The metronome is part of milestone 2, since recording depends on it.

- **Sound.** Sent as MIDI notes to a chosen port and channel (e.g. a drum module, or a plugin via Carla), with separate notes and velocities for the downbeat and other beats. Once in-process audio exists, a built-in click becomes an option.
- **When it plays.** Off, during recording only (default), or always.
- **Which meter it follows.** The conductor's meter by default; optionally the cursor's track, for practising a part in a different meter.
- **Count-in.** None, 1 bar (default), 2 bars, or a custom number of beats, in the meter at the record start point.
- **Pre-roll.** As an alternative to a count-in, playback starts a set number of bars before the record point, so you play along with the song before recording begins; useful with punch in.
- **Subdivision.** Optional clicks on eighths or sixteenths for slow tempos.
- **Defaults and generation (decided in milestone 2).** Channel 10; downbeat note 76 (Hi Wood Block) at velocity 110, other beats note 77 (Low Wood Block) at velocity 80, each a 1/32 note long. The port is chosen when recording until settings exist (milestone 9). The scheduler generates the clicks from the reference meter and tempo map rather than the plan's events, so they carry on past the song's end; they are never offset, muted or soloed. Count-in uses the tempo and reference meter at the record point.
- **Clicks (decided in milestone 2).** One click per beat, the beat being the time signature's denominator (so 6/8 clicks six eighths), with the first beat of each bar accented; other divisions, such as dotted quarters in 6/8 or the subdivisions above, come later. Playback starting mid-beat first clicks on the next beat. A click is cut short where the next beat comes sooner than its length. When recording, the metronome carries on past the song's end until stopped; in plain playback with the metronome always on, it stops when the song ends (with the last note-off), since there it is a timing reference for the song rather than a practice click. Whether this playback records (for "during recording only") is decided on the control side when the plan is compiled, so the engine needs no recording flag.
- **Count-in and pre-roll (decided in milestone 2).** One song setting chooses none, a count-in (1 bar by default, or a custom number of beats) or a pre-roll (1 bar by default). A count-in always clicks, whatever the metronome's "when" setting, since otherwise there's nothing to count with; without a metronome port there is no count-in. It comes before the record point with the song silent, at the tempo there, and runs the record point's meter backwards, so its accents fall where bars would start: from beat 3 of 4/4, a 1-bar count-in clicks beats 3, 4, 1 (accented) and 2. It is planned on the control side and carried with the play request, like chased events, so the engine allocates nothing; its clicks are at fixed times, so a tempo edit during a count-in doesn't move them. If a track's negative time offset reaches further back than the count-in, playback starts that much earlier. A pre-roll goes back the length of the bars before the record point's bar, keeping the position in the bar, and starts at bar 1 if there aren't enough bars.
- **Quick toggles.** On the `, m` leader menu (metronome on/off, count-in length, pre-roll), and as status-line segments.

## File formats

Songs are saved in a plain-text format designed to be read, diffed and version-controlled with git. Standard MIDI Files are the interchange format with everything else.

**Native song format (sketch; details settled in milestone 4)**

```
modal-song 1
song "Untitled" ppqn=960
track 1 "Bass"  ch=1  port="Pro-800"
track 2 "Drums" ch=10

section "Intro" bars=4
  tempo 1:1:0 120
  meter 1:1:0 4/4
  marker 3:1:0 "fix this fill"
  track 1
    1:1:0  note E1  v=96  len=1/8
    1:3:0  note E1  v=88  len=1/8.
  track 2
    meter 1:1:0 7/8
    1:1:0  note C1  v=110  len=1/16

section "Verse 1" bars=16 colour=blue
  …
```

- One event per line, positions as bar:beat:tick so diffs are readable. Absolute positions within a section mean inserting an event changes only one line, and positions relative to the section mean moving or duplicating a section changes nothing inside it.
- Ports are stored by name, with a mapping file per machine, so songs move between computers.
- **Format (decided for milestone 4, and for sections in October 2026).** The first line is `modal-song 1`, the format's version. Then `song "Title" ppqn=960`, then the tracks, each a `track` line with its settings (`ch=1 port="…"`, `transpose=+2`, `mute`, `grid=1/16`, `len=grid` and so on). Then the sections in order, each a `section "Name"` line with its length as `bars=N` in the conductor's bars and its settings (`colour=…`), followed by the conductor's `tempo` and `meter` lines and the section's `marker` lines, positioned in the conductor's bars, and then each track's content in the section: its own `meter` lines, if it has its own meter there (a track with none follows the conductor; there is no shorthand for a meter matching the conductor's, so changing the conductor never changes how other tracks are written), then its events, positioned in its own bars. Positions are relative to the section: its first bar is bar 1. A track with nothing in a section is left out of it. How a section refers to a track is settled with the format's details. Note lengths are note fractions where exact (`len=1/8.`), else ticks (`len=437`), since a length in bars depends on the meter. Tempo is in BPM with as many decimals as it takes to read back to the exact stored microseconds per quarter note. SysEx is hex bytes (`sysex F0 43 … F7`). Text is in double quotes with `\"` and `\\`, and `\xNN` for bytes that aren't UTF-8, as an imported file's raw text may have. A line starting with `"` is a comment, as in `modalrc`. Files end in `.modal`.
- **Song file details (decided in milestone 4).** After `song`, a `metronome` line (`when=off|recording|always ch=10 downbeat=E5 downbeatvel=110 beat=F5 beatvel=80 len=1/32`, and `port="…"` if set) and a `countin` line (`mode=none|countin|preroll bars=1 beats=0 preroll=1`) give the song's settings in full. Each track line is `track ID "Name" ch=N`, then only what differs from a new track's settings: `port="…"`, `mute`, `solo`, `arm`, `thru=off`, `chase=off`, `transpose=-12`, `velocity=+10`, `offset=-40ms`, and the piano roll's grid and default note, `grid=1/8t`, `len=1/4` and `vel=90` (milestone 5; a default length taken from a note of no common length is a fraction such as `len=437/3840`). The ID is the track's stable ID, saved as it is, gaps and all, and the lines' order is the display order, so moving a track changes only line order. In a section, its `meter` lines come first (always at least one, at `1:1:0`, with any remainder bar written as a change), then `tempo` (always one at `1:1:0`) and `marker`; then a block for each track with anything there, in display order: `track ID`, its own `meter` lines if it has its own meter, then its events. Events are `POS note PITCH v=V len=L` (with `rel=R` when the release velocity isn't 64), `cc C V`, `program P`, `bend V` (-8192 to 8191), `pressure V`, `polypressure PITCH V`, `sysex F0 … F7`, and `text`, `copyright`, `instrument`, `lyric` and `cue` with quoted text. Pitches are named with middle C as C4, with sharps written and flats read. A note length is a note fraction when its denominator divides 768 (down to 1/256 notes and their triplets), otherwise ticks. Positions are `bar:beat:tick` from the section's start, in the bars of the track's own meter or the conductor's. Indentation is only for reading. Anything wrong stops the file being read, with the line given (`song.modal, line 12: unknown event type "notee"`), rather than skipping what can't be read, since a song file is Modal's own data and nothing should be lost without a word. A file at another resolution is read at its own and then rescaled: bar lines land exactly, and positions and note ends round to the nearest tick, separately, so notes that touched still touch.
- **A port that isn't there (decided for milestone 4).** A track keeps its port's name whether or not the port is found. On loading, a track whose port isn't found plays to nothing, the status line says which ports are missing, and `:port` reassigns it; Modal never re-routes a track to another port by itself. Port aliases (milestone 9) then map a name to a different port on each machine.
- Saves are atomic (write a temporary file, then rename), with optional numbered backups.

**Other files**

- **SMF import/export:** type 0 and 1, preserving tempo map, markers and track names.
- **SMF and sections (decided October 2026).** Export writes the conductor's tempo and meter, and each section's start as a marker meta event, so other software sees the structure. Import makes one section ("Section 1") holding everything, with the file's tempo and time signatures going to the conductor and every track following it, and its markers as bookmarks; turning those markers into sections is an option.
- **Markers and cue points (decided in milestone 4).** Export writes bookmarks as cue point meta events, so they can be told apart from section starts, and writes no section markers for a song of one section, since there's no structure to show (so an imported file exported again gains nothing). Import makes markers bookmarks, and cue points too when they're in a conductor track (one with no channel events, which then needn't become a track); a cue point among a track's own events stays one of its text events, as before. The section is the fewest whole bars that hold everything, including the last note's end.
- **SMF export (decided in milestone 2).** Type 1 by default, type 0 as an option, at the song's PPQN. The first track holds the title, tempo map, the reference meter (now the conductor's) and the markers; then one track per Modal track, with its name and channel. Events are written as stored: transpose, velocity offset, time offset and mute/solo aren't applied, so the file matches the event list. Port names aren't written until port aliases exist.
- **SMF export details (decided in milestone 2).** Each Modal track gets a channel prefix as well as its name; a type 0 file has only the title as its track name. At each tick come note-offs, then other events in stored order (so a program change precedes the notes it applies to), then note-ons, then the note-offs of zero-length notes, so a note ending and another starting on the same pitch and tick don't cut each other off. Running status is used, and reset after meta and SysEx events. Each track ends at its last event or note end, and the first track at the latest of all, since some programs take the song's length from it. Overlapping notes of one pitch are written as stored; reading them back pairs note-offs first on, first off, which sounds the same in Modal's playback. Tempos slower than an SMF holds (about 3.58 BPM) are clamped with a warning; a resolution above 32,767 or a gap between events over 2^28 ticks can't be written. Files are written to a temporary file and renamed into place. Empty tracks are written, but Modal's own import skips tracks with no events.
- **SMF import rules (milestone 1).** Files are rescaled to the song's PPQN, rounding each note's start and end separately so touching notes still touch. Note-ons and note-offs pair first on, first off; notes still sounding at the end of a track end there. Each file track becomes one track per channel it uses, named "Name (ch N)" when split; its SysEx and text go to the first of those, or to a track of their own if it has no channel events. A track with only tempo, meter, markers and a name (a conductor track) doesn't become a track; in a type 0 file, or a type 1 file whose first track is a conductor track, the first track's name is the song title. Tempo, time signatures (applied to every track) and markers are read from any track. Running status is accepted across meta and SysEx events, split SysEx packets are joined, and escapes other than complete SysEx messages are dropped. Not supported: type 2 files and SMPTE-timed files. Ignored: port and channel prefixes, SMPTE offset, key signature, sequencer-specific events. Text is kept as the file's raw bytes. Damaged files are read as far as possible, and every problem worked around is reported as a warning.
- **Configuration and keymaps:** plain text, in the style of `.vimrc` / `.muttrc`, reloadable without restarting.
- **File commands (decided for milestone 4).** `:e FILE` opens a song, or imports a `.mid` file; `:w` saves, and `:w FILE` saves as; `:export FILE.mid` writes a Standard MIDI File; `:new` starts a new song; `:q` refuses while there are unsaved changes, `:q!` quits anyway and `:wq` saves and quits; `modal FILE` opens a song from the command line. `:e` with no file, and `:w` on a song never saved, show the system's open and save dialogs, through SDL3 (native on Windows, the desktop portal or `zenity` on Linux), much as GUI Vim does.
- **Unsaved changes and overwriting (decided in milestone 4, as Vim does).** `:e` and `:new` refuse while there are unsaved changes ("Unsaved changes: :w saves them, or :e! discards them"), and `:e!` and `:new!` go ahead anyway. `:w FILE` and `:export FILE` refuse to write over a file that exists, other than the song's own, unless given `!` (`:w! FILE`); the system's save dialog asks about overwriting itself. Closing the window (its close button or Alt-F4) with unsaved changes asks, in a system dialog, Save, Don't save or Cancel; without changes it just closes. A new song, or one just opened, is unmodified, so leaving it asks nothing. The window's title names the song as the status line does: `song.modal [+] – Modal`, `song.mid (imported) – Modal` or `Untitled – Modal`.
- **A new song (decided in milestone 4).** `modal` with no file, and `:new`, start an empty song: one empty track, `Track 1` on channel 1, the conductor at 120 BPM in 4/4, and one section of 8 bars, as the core makes a song. `modal --demo` opens the made-up demo song instead, for trying things out.
- **File commands, as built in milestone 4.** `edit` (`e`), `write` (`w`), `export`, `new`, `quit` (`q`) and `wq` are registry commands, run at the prompt; `quit` moved there from the prompt's own commands, so it can check for unsaved changes. A FILE is the rest of the line, quotes around it dropped, relative to the current directory, and given `.modal` (or `.mid` for `export`) if it has no extension. `:w FILE` makes FILE the song's file, as saving as does. The file dialogs offer songs and MIDI files to open, songs or MIDI files to save, and start beside the song's file (or the file it was imported from); SDL answers them on another thread, so the answer comes back to the window's event loop as an event. Saving from the closing question goes through `:wq`, so the window closes once the song is saved, after the save dialog if it needs one. Opening a song looks its ports up again, naming any not found.
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
- **Stop options.** All Notes Off and sustain reset on stop are scheduler settings, set by `modal-play` flags, until settings exist (milestone 9). Each is sent once per destination and channel the plan's tracks use.
- **Engine commands.** Play, stop and panic go to the engine through one ordered command queue, so a quick play then stop can't be reordered. The chased events for a play are computed on the control side and travel with the plan snapshot, so the engine never allocates them. The engine keeps the snapshot it replaced alive until its next update, so it can finish with the old plan while switching. When the song ends, the engine reports it and stays in play until told to stop; what happens then is up to the client (`modal-play` exits). The engine reports the tick at which each play stopped (decided in milestone 2), so a take ends exactly where the stop took effect rather than where the control side thought it was. On each pass the engine takes in input before carrying out commands, since that input arrived before the commands take effect: a note that arrives just before a stop is carried out belongs to the take, even if the stop was given first.
- **Input and thru (decided in milestone 2).** Each input port feeds the engine through its own fixed-size lock-free queue of messages timestamped on arrival. The engine wakes as soon as input arrives (the clock has a wake call: an event waited on alongside the timer on Windows, an eventfd polled alongside an absolute timerfd on Linux), sends it thru to the armed tracks' ports and channels, and stamps it with its song tick and its time into the song (so the recorder can take a track's time offset off exactly), since only the engine knows the current time-to-tick mapping exactly, including tempo edits during a take. Stamped input goes back to the control side through a queue (4,096 chunks; what doesn't fit is counted), where the recorder turns it into events. All input goes back, playing or not, marked with the play it arrived during; the control side decides what a take keeps, so a note pressed just before playback and still held at the record point is known about. Input before the song's start (a count-in or lead-in from bar 1) is stamped with ticks before 0, at the starting tempo, so the recorder can tell it from input on the downbeat; the displayed position still stops at 0.
- **Send lateness.** Measured from each message's due time to just after it was handed to the MIDI layer: the engine's wake-up accuracy plus the driver call. This is the figure behind the ~1 ms rule for switching to OS-timestamped delivery; the time a message leaves the cable is what the loopback test measures. It is also reported in its two parts, wake lateness (from the due time to the start of the send call) and send time (in the call), so a slow timer and a slow driver can be told apart. Each is a count, the maximum and a histogram in 50 µs steps up to 20 ms.

- **Engine library (`core`):** song model, scheduler, recorder, SMF and native file I/O. No UI dependencies; usable from a command-line player and from tests.
- **Platform layer:** MIDI outputs behind the scheduler's small message-sink interface, timestamped MIDI input and port lookup by name, all over libremidi; plus thread-priority, clock, console-key and config-directory helpers per OS.
- **Grid UI:** a cell-grid abstraction (characters, colours, attributes) plus optional pixel regions for the piano roll, velocity lanes and arrangement timelines. Rendered with SDL3 now; a terminal backend stays possible later.
- **Rendering (decided in milestone 3).** SDL3's 2D renderer, with text through SDL_ttf (FreeType, without HarfBuzz, since a fixed grid needs no shaping). Each glyph is rendered once into a texture atlas and each cell is drawn from it. Box drawing lines (light and heavy) and block elements are drawn as exact rectangles rather than from the font, as terminals do, since the font's own glyphs leave seams between cells and its partial blocks aren't exact eighths; the shades are flat tints rather than dither patterns, for density shading. Pixel regions are rectangles of whole cells, so they fit the layout, inside which a view draws filled rectangles and lines at exact pixel positions, clipped to the region, in the same pass as the text. The window redraws only when something changes, synced to the display: every frame while the playhead moves, otherwise only on input or a resize, so an idle window takes no CPU.
- **Cell size and high DPI (decided in milestone 3).** The font size is in points, scaled by the display's pixel density. Cells are a whole number of pixels, from the font's advance and line height, so the grid never blurs or drifts. Moving to a display with another scale, or `Ctrl-+` / `Ctrl--`, renders the atlas again. On Windows, SDL3 works in real pixels and reports the display's scaling (such as 150%) as the display scale, so the font is scaled by that and the text is crisp. The window opens at a size in cells (120 × 36 by default), so it's the same size relative to its text on any display.
- **Text size keys (decided in milestone 3).** `Ctrl-+` and `Ctrl--` (`font-bigger`, `font-smaller`) change the text a point at a time, with counts, within `fontsize`'s range of 4 to 72, and `Ctrl-0` (`font-reset`) goes back to the size the config sets; plus is bound as `<C-=>`, `<C-S-=>` and `<C-+>` for the layouts that type it differently, and the keypad's as `<C-kPlus>`, `<C-kMinus>` and `<C-k0>`. The window keeps its size and the grid gains or loses cells, as in a terminal; it grows only to stay at the minimum size. The `fontsize` setting follows the change for the session, but nothing is written to the user's files. The new size and cell size show in the status line in the `message` colour until the next key. These are the window's commands, run by the program around the view, which hands it commands it doesn't have. Thin lines and the space beside them are a hairline, a seventh of a cell's width and at least a pixel, so drawing keeps its proportions at any size.
- **Keys with Ctrl or Alt (decided in milestone 3).** SDL's key code has Shift applied, so the key's own character comes from its scancode in the keyboard layout, unshifted: Ctrl-Shift-d is `<C-S-d>`. The keypad types its characters, so a binding of `+` works from either plus key, except with Ctrl or Alt, when nothing is typed and its keys arrive as `<kPlus>`, `<kMinus>` and `<k0>` to `<k9>`, Vim's names for them.
- **Keys (decided in milestone 3).** Printable keys are bound by the character they type, so `|`, `"` or `D` work whichever key types them on any layout. Ctrl and Alt combinations and special keys are bound by key code, written in Vim notation (`<C-a>`, `<Esc>`, `<S-Tab>`, `<F5>`). Physical scancodes are kept for the later playable computer keyboard. The main modifier is abstract, so it can be Cmd on a Mac. As in Vim, a key that only starts longer bindings waits for the next key, and one that is both a binding and the start of a longer one waits up to `timeoutlen` (half a second by default, a setting) before running the shorter; pending keys show in the status line, with the count. A key that continues no binding runs the keys before it if they're a binding, and is then taken afresh; otherwise those keys and the count are given up, and Escape gives up whatever is waiting. With Ctrl or Alt a letter is the same in either case, so `<C-d>` and `<C-D>` are both Ctrl-d as in Vim, and Ctrl-Shift-d is written `<C-S-d>`. A typed character comes from the system's text input, which follows the keyboard layout and dead keys; with Ctrl or Alt, which type no text, it is the key's own unshifted character, with Shift as a modifier. Ctrl and Alt held together are taken as AltGr, which types text on many layouts, so they can't be bound together with a character.
- **UI code layout (decided in milestone 3).** `src/ui` holds the cell grid, layout, key handling, keymaps and the config parser, with no SDL, so all of it is unit-tested directly; `src/ui/sdl` holds the renderer and key translation, smoke-tested through SDL's offscreen video driver in CI; `apps/modal` is the program. `src/ui` links the core library, for ticks, meters and the song model.
- **Test view (decided in milestone 3; replaced by the event list in milestone 4, the arrange page's code and tests staying for milestone 5).** Three pages, gone through with Tab. The grid page has a 1000 × 1000 grid of cells labelled with their row and column, ten characters wide, under fixed column numbers and beside fixed row numbers, as the event list will have fixed headers; above it, a pixel region shows four bars of 4/4 over bars of 7/8 on the same time scale, with bar numbers placed to the pixel and notes. The arrange page sketches the arrangement (see Views) with a made-up song: sections in several meters changing on every track at once, a single extra 2/4 bar, two tracks in meters of their own, and four densities of notes; since milestone 4 its sections are real ones (the extra bar ending the first chorus, and the second chorus named "Chorus 2"), drawn where the markers were, with the two tracks' bars starting again at each section; with `+` (or `=`) and `-` to zoom through seven zoom levels, from a sixteenth note to four whole notes in a column, a quarter note by default. The samples page shows each theme role, the text attributes and the characters the views use. Keys go through the keymap to named commands, with counts as in Vim: `hjkl` and the arrow keys (`cursor-left` and so on), `0` (or Home) and `$` (or End, `{count}$` ending on a later row), `gg` and `G` (`go-first`, `go-last`, or with a count the row or bar counted), `Ctrl-D`/`Ctrl-U` by half a screen and `Ctrl-F`/`Ctrl-B` (or Page Down and Page Up) by a screen less two rows, as Vim does, Ctrl with `hjkl` or the arrow keys to move and scroll with the cursor, so it stays in place on screen until the view can't scroll further (`cursor-left-scroll` and so on, decided in milestone 3; on the arrange page by the current track's bars, the view then starting anywhere in time so the cursor doesn't shift, rather than at a column), `z` to centre the view on the cursor (`scroll-centre`) and `zz` to reset the zoom (`zoom-reset`), which also try out `timeoutlen`, since `z` waits to see if another `z` follows. Escape, or a key with no binding, gives up a count. The cursor stays in the rows and columns fully shown, scrolling only as far as it must, including when the window shrinks. The `:` prompt and the text-size keys work on every page.
- **Editor:** the grammar parser, operators, motions, views and undo history, operating on the song model.
- **The command registry (decided October 2026, built early in milestone 4).** Every action is defined once, in a central registry, before many commands exist. Each entry has a unique name (used on the command line and in config files), a title and short description, aliases for palette search, the views and modes where it applies, its parameters (names, types, defaults and how to ask for them), and its handler; its keys come from the keymap, the defaults from the built-in `modalrc` (decided in milestone 4; see The command registry in use). The keymap and config bindings (`nmap`, `leader`), the hint menu (its labels are titles), the `:` command line and its completion, the palette, macros and `.`, `--write-config`'s list of commands, and a generated command reference all come from it, so a new command appears everywhere at once and none of them can fall out of date. The `:` commands handled specially so far (`set`, the `map` family, `write-config` and the rest) move into it too.
- **Snapshots and sections.** Songs share unchanged sections and tracks between snapshots. Each snapshot also carries its flattened timeline (see Data model), built off the engine thread, which the playback plan is compiled from.

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

- **Modal's own ports (decided in milestone 4).** On Linux, Modal creates output ports of its own, which a plugin host or another program connects to like any other port: `ownports` of them (1 by default; 0 for none, up to 16), named `ownportname` (`Modal`), so one is `Modal` and several are `Modal 1`, `Modal 2` and so on, each with 16 channels of its own. Tracks play to them by name like any port (`:port Modal 2`); tracks without a port still play to the `port` setting's, or the first port found, since which port suits is up to each setup. Changing the settings opens new ports at once; ports no longer wanted stay open until Modal restarts, since one the engine may be sending to can't safely be closed. Built after milestone 4. WinMM can't create ports, so on Windows a virtual cable driver such as loopMIDI does the job for now, with Modal playing to it by name, until Windows MIDI Services (see To decide).

- **Clock.** The engine's clock is the steady clock (`CLOCK_MONOTONIC` on Linux, the performance counter on Windows). MIDI input is timestamped on the same clock when it arrives, so recorded and loopback times compare directly with send times. The sleep never wakes early. Before an event, the engine sleeps until a margin before it (1.5 ms by default) and busy-waits the rest, since the first Windows loopback run (October 2026, Babyface, WinMM) showed sleeping alone woke 99% of the time within 0.95 ms, at worst 1.7 ms late. The engine always tells the clock when the next event is due, so the spin starts a margin before it even when that falls inside a routine 1 ms wake-up; a command arriving during a spin waits for it, so commands are seen within the margin rather than 1 ms. With the spin, the engine's own waking on that machine (measured without MIDI) went from 99% within 0.85 ms to 99% within 0.05 ms. The margin is a `--spin` option on the command-line tools, for measuring.
- **Measured timing, Windows.** Loopback runs by Neil, October 2026, on one PC with a Babyface interface through WinMM and a MIDI cable, using `modal-loopback`'s default stream (2,000 notes, one every 10 ms):

  |  | No spin | Spin (1.5 ms) |
  | --- | --- | --- |
  | Latency, due to back: median / p99 / max | 1.46 / 1.95 / 2.38 ms | 1.10 / 1.36 / 1.79 ms |
  | Jitter: p99 − median / standard deviation | 0.49 / 0.21 ms | 0.26 / 0.07 ms |
  | Send lateness: p99 / max | 0.90 / 1.29 ms | 0.35 / 0.54 ms |
  | of which waking: p99 / max | 0.80 / 1.19 ms | within 0.05 / 0.04 ms |

  All messages came back, in order. With the spin, what remains is the driver call (up to about 0.5 ms) and the interface and cable (about 1 ms, mostly the 0.96 ms a 3-byte message takes on the wire). Both are well under the 1 ms threshold for OS-timestamped delivery, so Windows stays engine-timed.
- **Measured recording, Windows.** A `modal-loopback --record` run by Neil, October 2026, on the same setup with the spin, using the default stream at 120 BPM and 960 PPQN (a tick is about 0.52 ms): all 2,000 notes were recorded, none missing or extra, and every one at the tick its arrival time gives. Recorded positions were 1 to 3 ticks late (median 2), which is the loopback latency (median 1.12 ms, max 1.84 ms) rounded down to a tick, and recorded lengths were within one tick of those sent, from rounding each end. A take from a MIDI keyboard through `modal-record` was also checked in the saved file.
- **Measured timing, Linux.** Loopback runs by Neil, October 2026, on a ThinkPad T480 with Ubuntu 26.04.1 (kernel 7.0, a desktop kernel with no real-time tuning) and a Roland UM-ONE, its OUT cabled to its IN, through ALSA, with `modal-loopback`'s default stream (2,000 notes, one every 10 ms) and a Release build:

  |  | Spin (1.5 ms) | No spin | Chords of 3, spin |
  | --- | --- | --- | --- |
  | Latency, due to back: median / p99 / max | 2.12 / 2.26 / 4.61 ms | 2.26 / 2.39 / 4.89 ms | 2.50 / 3.70 / 5.12 ms |
  | Jitter: p99 − median / standard deviation | 0.14 / 0.08 ms | 0.13 / 0.29 ms | 1.20 / 0.82 ms |
  | Send lateness: p99 / max | 0.05 / 0.06 ms | 0.15 / 0.47 ms | 0.05 / 0.47 ms |
  | of which waking: p99 / max | within 0.05 / 0.03 ms | 0.10 / 0.44 ms | 0.05 / 0.47 ms |

  All messages came back, in order. Nearly all the latency is the interface and cable, about 1 ms of it the 3-byte message on the wire and the rest USB and the UM-ONE; Modal's own send lateness is within 0.05 ms for 99% of messages, with an occasional wake about half a millisecond late (under 1% of messages) on this untuned kernel. The chords' larger jitter is the later notes of each chord queuing on the wire, about 1 ms a message, not scheduling. The spin halves waking lateness at p99 (0.05 against 0.10 ms) but changes little at the cable. All of it is well under the 1 ms threshold for OS-timestamped delivery, so Linux stays engine-timed too. The waking row's "within 0.05" above its 0.03 ms maximum was the percentile being reported as its 50 µs step's edge; it is now never more than the maximum.
- **Measured recording, Linux.** A `modal-loopback --record` run by Neil, October 2026, on the same setup with the spin, at 120 BPM and 960 PPQN: all 2,000 notes recorded, none missing or extra, every one at the tick its arrival gives. Recorded positions were 2 to 5 ticks late (median 3, 1.56 ms) against a loopback latency median of 2.03 ms, the arrival rounded down to a tick, and lengths within 1 to 3 ticks of those sent, from rounding each end.
- **Opening ports by name.** A port name given by the user picks out the port with exactly that name, or else the one port with that name ignoring case, or else the one port whose name contains it ignoring case. If several match equally well, or none, the error lists the candidates or all the ports. Ports are opened before playback starts; devices appearing and disappearing during a session come later.
- **SysEx on Windows blocks.** libremidi's WinMM backend waits until a SysEx message has been fully sent, about 0.3 ms per byte at MIDI's speed, and may allocate to copy it. Accepted for milestone 1, since SysEx usually comes at the start of a song. The fix (a SysEx thread per port, keeping later messages to that port behind it, or Windows MIDI Services) stays inside the MIDI backend, behind the scheduler's message sink.
- **Engine thread priority.** `SCHED_FIFO` at priority 70 on Linux; the MMCSS "Pro Audio" task at high priority on Windows. If it can't be raised, the engine runs anyway and says why.

**Build and tooling**

- C++20, CMake, dependencies via vcpkg in manifest mode with a pinned baseline: SDL3, SDL\_ttf (with FreeType, without HarfBuzz), libremidi, Catch2 v3 for tests. Fonts, the built-in `modalrc` and the built-in themes are built into the program from `assets/` by a small CMake step, so it runs with no files installed.
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

1. **Engine and MIDI layer.** Song model, SMF import, playback through libremidi, command-line player. Loopback timing tool with jitter measurements on Linux and Windows. *Done, October 2026, measured on Windows and Linux.*
2. **Recording.** Timestamped input, record to track, metronome, count-in and pre-roll, SMF export. Command-line recorder, verified against the loopback tool. *Done, October 2026, measured on Windows and Linux.*
3. **Grid renderer.** SDL3 window, cell grid, font rendering, cursor, scrolling, high-DPI, keyboard input layer, keymap and theme files, resizable grid. Until the event list arrives in milestone 4, the program shows a test view (decided in milestone 3): a large scrollable grid of labelled cells under a pixel-region demo of bar lines in two meters, a sketch of the arrangement with a made-up song, and samples of each theme role, with counted Vim motions, a `modalrc` with settings, key bindings and themes, text-size keys and a minimal `:` prompt. *Done, October 2026, except the Linux checks.*
4. **Event list view and core grammar.** The command registry, sections and the conductor track in the song model, navigation, motions, delete/yank/put, undo, transport keys, native song format save/load, inspector panel. First version usable for real work. *Done, October 2026, except the Linux checks:* the command registry; sections, the conductor and the `.modal` format; the document with undo; the event list with its motions, operators, register and field editing; the inspector; playback with the play-from marker, rewind and `:port`; the file commands and dialogs; and `:section`. Modal's own MIDI output ports on Linux followed (see Modal's own ports).
5. **Piano roll and arrangement views.** The shared cursor and selection model first, then the arrangement as the home view, the piano roll with its velocity lane, view switching, the section strip, and `D` / `P`, which ripple in time. Built on milestone 4's grammar (motions, `d` / `y` / `p`, counts, undo), so the views come before the rest of the grammar (decided October 2026): much of that grammar is about time and pitch (`ib`, `ic`, Visual blocks across tracks, transpose, quantise, velocity), and should be shaped by the views it's used in rather than by the event list alone.
6. **Full grammar,** across all three views. Remaining operators, text objects and MIDI transforms (velocity and pedalling tools among them), Visual modes, search, leader key and hint menu, command palette, counts, `.` repeat, registers, macros, command line with completion. Parts that depend on no view (the hint menu, the palette, completion) may come earlier, where the views need them.
7. **More for the views.** The outline view, ghost notes, split view, controller lanes beyond velocity, drum maps and folding.
8. **Step entry, loop and punch recording.** Take handling, section looping and takes, cueing.
9. **Polish for daily use.** Configurable status bar, settings, port aliases, backups, a menu bar, performance pass, documentation.

Later: in-process plugin hosting, patterns, MIDI clock in, take lanes, splits, scripting, macOS.

## Ideas (not scheduled)

- **A menu bar, for those who don't know Vim (planned for milestone 9, October 2026).** A conventional way in beside the keys: File, Edit, View and Play menus, File holding Open, Save, Export and Exit. SDL3 has no native menus, so it would be drawn on the grid like SPG's own menus or Turbo Vision's, the same on Windows and Linux: opened with a key (F10, say) or the mouse, every item a command from the registry with its keys shown beside it, so the menus can't drift from the keymap. Hideable (`set menubar`) for those who'd rather have the row.

Distinctive features to consider after version 1. Not committed to any milestone.

**Finding and developing ideas**
- **Musical search and replace:** `/` searches by interval pattern or rhythm, in any key; `n` jumps between matches; `:s` substitutes across the song.
- **Linked variations:** copies that stay linked to the original, optionally with a transform (inverted, transposed, half speed); editing the original updates every variation.
- **Retrospective capture:** MIDI input is always buffered; one key turns the last few minutes of playing into a take.

**Recording**
- **Half-speed recording:** record at half the tempo (or another fraction) and play back at full speed, for passages too fast to play in; similar uses to step entry.
- **Comping:** combining the best parts of a section's takes (see Sections: looping, takes and count-in).

**Rhythm and time**
- **Polytempo:** a track can run at a ratio of the song tempo (e.g. 3:2) for phasing and tempo canons.
- **Free-time recording:** record without a click, then tap or mark downbeats to fit a tempo map and bar lines.
- **Tuplets and generators:** arbitrary and nested tuplets (7:4, 5:3) as grid settings; Euclidean rhythm transform.
- **Stretch and compress:** a transform that scales a range of bars in time, by a percentage or to a target length in beats or bars.

**Song structure and editing**
- **Linked sections:** "Chorus 2" as a linked instance of "Chorus 1", so editing either changes both, optionally with a transform (transposed, varied); see Linked variations.
- **Song playlist mode:** an ordered list of section references, as an alternative to a strictly linear arrangement.
- **Track loops:** as in SPG, a track can loop (shown by an `L` at its end); a starting point to build on.

**Harmony and arranging**
- **Chord track:** a chord-progression lane used by step entry, transpose-in-key and reharmonisation.
- **Arranging checks:** optional warnings for out-of-range notes per instrument, out-of-key notes, overlapping same-pitch notes, parallel fifths.
- **MusicXML export:** to Dorico, keeping keys and per-staff meters.

**Versions and annotation**
- **Section alternatives:** alternative versions of a section (Chorus A / Chorus B), switchable and auditionable in place.
- **Text lanes:** lyrics, rehearsal notes and cues at song positions.
- **To-do markers:** markers flagged as to-dos, gathered in a to-do list to jump between.

**Later**
- **Live rig mode:** send program changes and setup messages per song section, stepped through with a footswitch; with a setlist view above the outline view, listing a gig's songs with the same keys, and cue keys per song or section.

## To decide

Settled one milestone at a time, at the start of that milestone's work. Add to this list as new questions come up.

**Milestones 1–2: engine and recording**

- [x] Scheduler look-ahead. The engine sends each event itself at its exact time. Since the playback plan already holds every event's send time, there is no window to prepare: the engine sends whatever is due when it wakes, and any edit, mute or tempo change during playback applies to every event not yet sent. If milestone 1's loopback measurements show too much jitter on a platform (p99 send lateness or loopback jitter above about 1 ms), that backend switches to OS-timestamped delivery (around 50 ms ahead, recalled on stop or edit). The MIDI interface is designed to allow either. Windows measured well under the threshold (see Measured timing, Windows); Linux is still to be measured (below).
- [x] Panic key: `Ctrl-.`.
- [x] Record input: all MIDI inputs by default, optional per-track input. MIDI thru on by default, changeable.
- [x] Milestone 2's recording, pedal, metronome, export and tool decisions (October 2026): see Input and recording, Playback and recording behaviour, Metronome and count-in, Other files, Architecture, and Build and tooling.
- [x] Build tooling: vcpkg (simplest CMake and MSVC integration, all planned libraries in its registry) and Catch2 v3 (its generators make tests at several PPQNs simple).
- [x] Linux timing and recording, measured by Neil in October 2026 on a native install with a Roland UM-ONE and a loopback cable: plain, with chords, without the spin and with `--record` (see Measured timing, Linux). Linux stays engine-timed, as Windows does.
- [ ] Windows MIDI Services. vcpkg's libremidi port is built without its Windows MIDI Services backend, so Windows uses WinMM for now. Using Windows MIDI Services (loopback ports, multi-client access) would need a local overlay port that enables it. Shelved for now, since WinMM measures well (October 2026). It would also give Modal ports of its own on Windows (see Platform specifics); until then, a virtual cable driver such as loopMIDI does the job, with Modal playing to it by name.
- [x] An imported SMF's time signatures apply to all tracks; a change that falls mid-bar starts a remainder bar. (Moved up from milestones 3–4, since import needs it.)

**Milestones 3–4: grid and first editing view**

- [ ] Linux checks of the grid renderer, on Neil's native install under both X11 and Wayland: the window and its live resizing, display scaling, text input (dead keys, AltGr), Ctrl and keypad keys, the config directory, and the offscreen tests' look. Milestone 3 is otherwise done.
- [ ] Windows: dragging the window between two displays with different scaling (the font is rendered again for the new scale), if two such displays are to hand.

- [x] Milestone 3's rendering, font, DPI, key, config and test-view decisions (October 2026): see Views, Customisation, Architecture and Build and tooling.

- [x] Song file grammar (decided for milestone 4): see File formats.
- [ ] Default keymap: the full set of single-key bindings, designed for muscle memory. Milestone 4 binds only its own keys (see Editing grammar and Views); the whole map is reviewed in milestone 6, once the leader menu can hold the rest.
- [x] Undo granularity (decided for milestone 4): see Registers, macros and undo.
- [ ] Cursor mapping when switching between arrangement, piano roll and event list. Milestone 4 has only the event list, so this comes first in milestone 5, with the shared cursor and selection.
- [x] Default note length keys: `+` / `_` dropped; `, n` takes the current note's as the default note, and `:len` sets it (decided for milestone 5). `=` / `-` lengthen and shorten the current note.

* [x] Remainder bars are always the exact leftover (e.g. 1/4, 5/16). To get something else, the user selects the bars and applies a meter again, e.g. a 3/4 bar plus a 1/4 remainder re-barred as one 4/4 bar.
* [ ] How the arrangement ruler and per-track bar lines are drawn when meters differ, at different zoom levels. The approach is settled (time-based columns, each track's bar lines in a pixel region; see Views), and milestone 3's test view sketches the blocks, gaps, markers and ruler; the zoom levels and ruler details are confirmed in milestone 5.

- [x] Delete can leave silence (`d`) or close the gap (`D`); put can merge (`p`) or insert (`P`).
- [x] Cut-at-edges key is `|` (rarely used, so its reach doesn't matter; rebindable like everything else).

- [x] Content storage (October 2026): per section, with a flattened timeline for playback and the views; see Data model.
- [x] Bar lines realign at section boundaries, with bar numbers running on through the song; a split mid-bar makes a remainder bar, so sections are whole bars on every track (October 2026).
- [x] A conductor track holds the tempo and reference meter, rather than a reference-track setting; tracks follow its meter unless they have their own, section by section (October 2026).
- [x] Markers belong to their section, with names unique within it; duplicated sections are numbered (October 2026).
- [x] Ripple is section-local (October 2026); see How far ripple reaches.
- [x] The command registry is built early in milestone 4 (October 2026); see Architecture.
- [x] SMF export writes section starts as marker meta events and bookmarks as cue point events, so import can tell them apart (decided in milestone 4; see Markers and cue points).
- [ ] Whether import offers to turn a file's markers into sections by default, or only on request (milestone 5, with the section strip).
- [ ] Notes sustaining past a section's end when sections are reordered: kept whole (the default), or an option to cut them at the section's end?
- [ ] For `D` within a section: leave the silence at the section's end (the default), or an option to fill it by looping or repeating the section's last bar?

**Milestone 5: piano roll and arrangement views**

- [x] The shared cursor and selection model (decided for milestone 5): one cursor shown by each view, Backspace to go back, and the selection as an area; see Views.
- [ ] Scale lock, its toggle key (`, t s` proposed) and the semitone motion that ignores it: moved to a later milestone (decided for milestone 5). A key and scale may change from bar to bar, so scale lock needs something like a key or chord track beside the conductor, to be designed first. Until then `k` / `j` move by semitones.
- [x] Grid finer / coarser: `zh` / `zl`, and `z` with Left / Right (decided for milestone 5, the way round settled in use), in the `z` family with the zoom keys, since Ctrl with `hjkl` scrolls; `Ctrl-t` toggles triplets.
- [x] Velocity used by `o`: a per-track default note, after Sequencer Plus Gold (decided for milestone 5, in principle): a length and a velocity, saved with the track, which new notes take, set from a note: a key takes the current note's length and velocity as the track's default (decided October 2026; the key is `, n`, for now). Changing the default on an empty spot with the note keys was proposed and dropped, since there's no note to see there, so a length or velocity would have to be judged from numbers rather than by eye. Deterministic, as drum programming needs, rather than copying the note before.
- [x] `zz` centres on the cursor in the views, as Vim's centres the line (October 2026), rather than the test view's `z` to centre and `zz` to reset the zoom; zoom reset needs another key.
- [x] The semitone motion that ignores scale lock: waits with scale lock (above).
- [x] Velocity step keys in the piano roll: `Ctrl-A` / `Ctrl-X` step the current note's velocity, with counts, as they step values in the event list and numbers in Vim (decided for milestone 5); `^` comes with milestone 6. They're bound to `number-increase` / `number-decrease`, apart from `+` / `-`: in the event list they step the field under the cursor, in the piano roll the current note's velocity, by 1 a press, with counts for more (decided October 2026).
- [x] The length of a new empty section from `i` / `a`: 4 bars, or the count (decided for milestone 5).
- [ ] Section strip: keys for selecting several sections (with Visual mode, milestone 6).

**Milestone 6: full grammar**

- [ ] Whether `#` in the palette, in the arrangement, moves the cursor to the track or opens it in the piano roll.
- [ ] Clearing search highlights: a key as well as `:noh` (`, /` proposed).
- [ ] Whether `g/` takes the page, the section or the track by default.
- [ ] `Ctrl-p` in Insert mode: open the palette, or keep it for step entry? `, :` and the Ctrl-Space leader reach the palette in Insert mode either way.
- [ ] Where the palette's frecency ranking is stored (the config directory, say), and whether it can be reset.
- [ ] Palette prefixes for takes, transform presets and other later kinds of item.
- [ ] Velocity transforms: the default soft-knee width (how near the limits the easing starts), the default averaging window for compress, expand and shape, and interactive step sizes per transform.
- [ ] Re-pedal gaps: how long before and after a chord change, in ticks or milliseconds.

**Milestone 7: more for the views**

- [ ] Whether ghost notes are coloured by their track or all dimmed alike.
- [ ] An idea: Enter on a selection of several tracks in the arrangement opens them together in the piano roll, one to edit and the others as ghosts.
- [ ] The most split panes allowed, and the smallest useful pane height.
- [ ] Folding: the whole track (default) or the current section, and whether rows used only by ghost tracks are kept.
- [ ] Drum map file format, and where reusable maps are kept.
- [ ] Whether the outline view has `J` to join, where `J` would naturally mean down (perhaps `, S j` only there).

**Milestone 8: recording**

- [ ] What happens to a section's takes when it is resized: cut short, kept beyond the end, or ask?
- [ ] Cue slots: `F1` to `F12` given to sections 1 to 12 automatically, or none until assigned; and the erase-while-looping key.
- [ ] When a cued jump happens by default: the end of the bar or the end of the section.

**Later milestones**

- [ ] Default parameters for each transform, and how they appear in the hint menu and command line.
- [x] Behaviour when a song's saved port isn't present on this machine: the track keeps the name and plays to nothing, the status line names the missing ports, and `:port` reassigns it (decided for milestone 4; see A port that isn't there).

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
- [x] Sections are the song's structure and markers are bookmarks (October 2026); see Sections and markers.
- [x] The velocity operator is `^`, since `v` is Visual mode, and `!` is the general transform operator (October 2026).
- [ ] Vim uses `,` to repeat an `f` search backwards; pick another key for that (e.g. `\`), together with the scale-lock key (see To decide, milestone 5).
- [x] Piano roll navigation, grid and default note length (October 2026): a crosshair cursor; `{` / `}` keep the position in the bar, with bar lines on `[[` / `]]`; `gg` / `G` go to the start and end of the track; pitch motions and scale lock; Alt plus a motion moves notes; a per-track grid and default note length; the piano roll shows pages of whole bars. See Editing grammar and Views.
- [x] Licence: GPL-3.0-or-later (October 2026), which keeps forks open and allows JUCE's AGPLv3 terms if plugins are hosted with it. The repository is public on GitHub; third-party licences are in `licenses/`, and every C++ file carries an SPDX line.
- [x] The name is Modal (October 2026), settled with the repository public, knowing that Modal Electronics (synthesisers) and Modal Labs (cloud computing) use the name too.

* [x] Language: C++20 throughout. C# was considered, but GC pauses in the real-time engine, weak Linux MIDI support in .NET libraries, and C++-only plugin hosting all favour C++, as does day-to-day familiarity.
