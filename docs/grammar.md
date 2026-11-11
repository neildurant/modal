# The editing grammar

This is how editing in Modal works now: every key, what it takes and where. The reasons behind it, and how it got this way, are in [design.md](design.md) and [decisions.md](decisions.md).

Editing follows Vim's model: an operator, then what it acts on, a range or an object, with counts. `d]b` deletes to the next bar line, `>ib` transposes this bar up a semitone, `3qq` quantises three bars. `.` repeats the last change.

Every key here is a binding in the built-in `modalrc` and can be changed (see [configuration.md](configuration.md)). Two things teach the keys as you go. After any key that starts longer ones (`g`, `z`, `]`, the leader `,`, or an operator), a pause shows the **hint menu** of what can follow. `, ?` lists **every key that does something where the cursor is**, in groups.

## Where the cursor is

The cursor is one place in the song, a track, a time and in the piano roll a pitch, shown by each view in its own way. What a key does depends on where it is:

- **The arrangement's tracks:** bars on tracks. `ga` shows the arrangement.
- **The section strip,** above the arrangement's tracks: the song's sections. `gs` goes to it (or `k` from the first track), `j` back.
- **The piano roll:** one track's notes, by time and pitch. `gr`, or Enter on a bar in the arrangement.
- **The event list:** one track's events as rows. `ge`; `gc` shows the conductor's list (tempo and meter changes), as track 0.
- **The Tracks view:** track settings in columns. `gt` or `, v t` opens it; `3gt` chooses track 3.
- **The Ports view:** this machine's MIDI ports. `gm` or `, v m` opens it.

`Backspace` goes back to the view before. Ctrl-PageDown and Ctrl-PageUp go to the next and previous track, with counts moving that many tracks. The event list includes the conductor as track 0; the piano roll wraps among note tracks; the Tracks table stops at its first/last row.

Some keys mean different commands in different places: `J` joins sections on the strip and goes down an octave in the piano roll. A key that does nothing where the cursor is says where it does something.

## Modes

- **Normal:** moving and operating. The default.
- **Visual:** selecting before acting (`v`, `V`, `Ctrl-v`).
- **Operator-pending:** after an operator, waiting for what it takes.
- **The command line:** `:` for named commands (`:quantize 1/16 swing=55`), and the questions commands ask.
- **Insert:** step entry, to come with milestone 8.

## Moving

### The cursor and time

| Keys | Moves |
| --- | --- |
| `h` `l` (or arrows) | A grid step in the piano roll (off a note at the cursor's pitch in one), a bar in the arrangement, a field in the event list, a section on the strip |
| `j` `k` | A pitch step in the piano roll, a track in the arrangement, a row in the event list |
| `0` `$` | The first and last positions listed for each view below |
| `gg` `G` | The start and end of the song (the first and last sections on the strip); `12G` bar 12, or section 12 on the strip |
| `{` `}` | The same place a bar back or on (`4}`) |
| `[b` `]b` | The bar line before, the next bar line |
| `(` `)` | The chord before, the next chord (events starting within `chordwindow` of each other) |
| `[s` `]s` | A section's start before, the next |
| `[l` `]l` | The label before, the next |
| `Tab` `Shift-Tab` | The next note's start at any pitch, and the one before (piano roll); past empty bars to the next bar with events, and the one before (arrangement) |

The ends that `0` and `$` go to depend on the view:

| Where | `0` | `$` |
| --- | --- | --- |
| Arrangement, on a track | The song's first bar | The song's last bar |
| Section strip | The first section | The last section |
| Piano roll | The current bar's start | The current bar's last grid step |
| Event list | The first field in the row | The last field in the row |
| Tracks view | The first track column | The last track column |

On an arrangement track, `d$` takes the events from the cursor's bar through the song's last bar on that track. `dib` takes just the current bar's contents. In the event list, `0` and `$` move between fields and aren't ranges after an operator.

Counts set a motion's distance, as `3l` or `2]b`; with `gg` and `G` they choose a bar, or a section on the strip. In the event list, the time motions go to the event at or after where they land. A bar motion into a shorter bar remembers where it wanted to be, as Vim does with columns.

### Pitch (piano roll)

`k` and `j` move a semitone, `K` and `J` an octave.

### Finding and searching

| Keys | Does |
| --- | --- |
| `f` `F` and a note name | The next or previous note of that pitch class, any octave: `fE`, `fF#`, `fbb` (B flat); the letter in either case |
| `;` / `Alt-;` | The last find again, the way it went / the other way |
| `/` | Search the track: `/C1`, `/C`, `/>A3`, `/A3-A5`, `/vel<60`, `/len<1/32`, `/cc64>63`, `/bend`, terms together (`/C1 vel<60`); `/outside D dorian` and `/inside D dorian` |
| `n` `N` | The next and previous match, round past the ends, keeping the search's way |
| `*` `#` | Search forwards and back for the exact pitch under the cursor |
| `/` Enter, `:noh`, `, S c` | Clear the matches |

A find or search counts (`3fE`, `3n`). After an operator, `f` takes the range through the note it finds, `F` from it, and `n` up to the match. A `#` or `b` straight after a find's letter makes it a sharp or flat; after an operator it does the operator again to the sharp or flat, as one change, and Escape then undoes it.

### Marks

`ma` marks the cursor's place with a letter, saved with the song; `'a` (or `` `a ``) goes there, and after an operator is the range to it (`d'a`). `''` goes back to before the last jump. `:marks` lists them, `:mark-delete a` deletes one.

### Views and scrolling

`zi` `zo` `z0` zoom in, out and back; `zf` follows the playhead again after moving away from it while playing. `Ctrl` with `h` `j` `k` `l` or the arrows moves the cursor and scrolls with it; `Ctrl-Shift-h` / `l` a page; `Ctrl-d` `Ctrl-u` half a page; `Ctrl-f` `Ctrl-b` a page; `zz` centres the cursor.

## Operators

| Key | Does |
| --- | --- |
| `d` `y` | Delete, yank |
| `>` `<` | Transpose up and down a semitone |
| `t` `T` | Shift later and earlier by a grid step |
| `q` | Quantise notes' starts to the grid; with pedalling there, as `quantizepedal` says (by default the pedalling goes with its notes) |
| `^` | Velocity: asks for a value (`80`), a change (`+10`) or a percentage (`80%`) |
| `~` | Humanise: a little at random in time (`humanizetime`) and velocity (`humanizevel`) |
| `!` | Transform: then a transform's key (see Transforms) |

- **Doubled, an operator takes this track's events in the cursor's bar:** `dd`, `yy`, `>>`, `qq`; `3dd` three bars.
- **Counts.** A count before an operator with a "how far" of its own is how far (`3>ib` up three semitones, `2t]b` two grid steps); one after it is the range's (`>3ib` three bars). For `d`, `y`, `q`, `^`, `~` and `!`, counts before and after multiply.
- **What each takes.** Transposing, velocity, quantising and humanising change notes only. Shifting moves every event in the range, so pedalling and controllers stay with their notes; in the piano roll, which shows only notes, it moves the notes.
- **Refused whole.** A change that would put a note out of range, an event before the song's start, or a note onto another of its pitch is refused, naming the note, rather than made in part.
- **While an operator waits,** the status line says what it will do ("Transpose up 3 semitones · then a range or object"), and the hint menu lists the objects and the motions apart, each with what it would take here.
- **`x` and `Y`** delete and yank the note or event under the cursor.

## Ranges

After an operator, a motion that lands on a boundary it names is a range, from the cursor up to that boundary: `d]b` the rest of the bar, `d[b` from the bar line before, `d]s`, `d]l`, `dG` to the end, `dgg` from the start, `dj` this event and the next, `d'a` to a mark, `dfE` through the next E, `dn` up to the next match. `{` and `}` are ranges too, as are `0` and `$` where they move in time; in the event list those two only move between fields. `Tab`, `(` and `)` only move, and say so.

## Objects

An object is a whole thing at the cursor, named by its letter: `i` is its contents, `a` the thing itself.

| Object | On a track (arrangement, piano roll, event list) | On the section strip |
| --- | --- | --- |
| `d` (doubled) | This track's events in the bar | — |
| `ib` | This bar's events | — |
| `ab` | This bar as time: `dab` takes it out, closing up to the section's end (not in the event list) | — |
| `in` | The note or event under the cursor | — |
| `ic` | Its chord (piano roll, event list) | — |
| `is` | This section's events on this track | This section's events on every track |
| `as` | — | This section itself: `das` takes it out of the song |
| `it` | This track's events (`2it` two tracks in the arrangement) | — |
| `ie` | — | The whole song, every track |
| `gn` | The match at the cursor, or the next | — |

An object never reaches beyond the cursor's place: on a track, that track; on the strip, every track. One not offered where the cursor is says where it is. A count takes more (`d2is`, `>3ic`). To the operators that change events in place, `ab` is `ib` and `as` is `is`.

With `gn`, an operator changes the next match and moves on to the one after; `.` changes it too and `n` skips it.

## Visual mode

| Key | Selects |
| --- | --- |
| `v` | From here as the cursor moves: bars on every track (arrangement), grid steps at every pitch (piano roll), events (event list), sections (strip) |
| `V` | Whole rows: complete tracks with settings (Tracks view), track contents at every time (arrangement), pitch rows across the whole track (piano roll); in the event list and on the strip, as `v` |
| `Ctrl-v` | A box: tracks by bars (arrangement), pitches by grid steps (piano roll) |

- Its own key again, or Escape, ends it; another of them changes to it. `o` goes to the other end; `gv` selects the last selection again.
- **An object selects exactly what an operator would take with it,** so `v` then an object then an operator does what the operator does with the object: `visd` is `dis`, `v2itd` is `d2it`. Moving the cursor, or `V`, `v` or `Ctrl-v`, lets go of the object; another object replaces it. After `V` or `Ctrl-v`, which chose the rows, an object sets only their time (`Vjis`).
- **Operators act at once** on the selection: `d` and `x` delete what's selected (`d` alone for complete tracks in the Tracks view), `y` and `Y` yank it, the others change it, a count before them saying how far.
- **`D` takes the selection's time out,** closing up within each section. The event list refuses it, as does a piano-roll selection restricted to some pitches; use `v` in the piano roll to select time at every pitch.
- **`p` and `P` put** the register in place of the selection, as one change.
- **`|` cuts notes crossing the edges** for the selection, so operators take only the parts inside; before an operator in Normal mode, for that one (`|d]b`).
- **`:` on a selection** runs a command that takes a range over it (`gsvie:label-to-section` for the whole song).
- **Selecting from the leader,** `, S`: `b` the bar, `s` the section, `t` the track, `v` what's shown; with a capital (`, S B`, `S`, `T`, `V`) only the search's matches in it, which operators then take; `, S m` in Visual mode keeps only the matches in what's selected; `gn` selects the match at the cursor or the next.

## Putting and registers

| Key | On arrangement tracks and in the piano roll | On the strip | In Visual mode |
| --- | --- | --- | --- |
| `p` | Over what's there (`put-merge`) | Sections after this one | In place of the selection |
| `P` | In, moving the rest of the section later (`put-insert`), asking to lengthen it if it must | Sections before this one | In place of the selection |
| `Alt-p` `Alt-P` | As `p` and `P`, at the cursor's pitch too (piano roll only) | — | — |

- What was taken as bars (`dd`, `ib`, `ab`, the arrangement's bars) goes to the start of the cursor's bar; anything else to the cursor. The event list has `p` but not `P`.
- **At the cursor's pitch,** the notes keep their intervals to the pitch the cursor was on when they were taken (in Visual mode, the pitch the selection was started on; taken outside the piano roll, their lowest), so a riff or chord shape moves to another pitch.
- **`:put-ending`** (no key) puts the register over what's there so it ends at the cursor, or at its bar line for bars: a sweep ending on a downbeat.
- After a put the status line says what, how and where ("Put 4 events in Bass at bar 3, over what's there").
- **Registers:** `"a` before a yank, delete or put uses register a; `"A` adds to it. `:registers` lists them.

## Whole tracks and their settings

In the Tracks view, `j` / `k` select rows and `h` / `l` select columns. `0` / `$` go to the first/last column; `gg` / `G` go to the first/last track, or track N with a count. Page keys scroll through rows. Space keeps its transport meaning.

- The hint menu gives a toggle's name and what it's set to now, such as `Rewind: on` or `Mute this track: off`.
- While a field is edited, here and in every view, the status line says what it takes and what the keys do; Up and Down step a value that can be stepped.
- Enter or `e` edits the selected field in its cell; while it's highlighted whole, typing replaces the value and Backspace or Delete clears it, and a key that moves the cursor keeps it for editing. Enter commits, Escape leaves it, and Tab/Shift-Tab commit and move to the next/previous editable track column, wrapping at the ends.
- **Output** (after Input, `▾` at its right) is chosen from a list at the cell, with Default first: typing narrows it, whatever the case, Up and Down move through it, and Enter or Tab takes the one chosen, or Escape leaves it. Only what's listed can be chosen, `:port NAME` still taking a name. `Default` and `All` are dim in the table, as following the machine's choice. A port that can't be used is in the warning colour, with the inspector saying why: not found, disabled (`(disabled)` in the chooser rather than `(not found)`), no default set, the default not found or disabled, or for All no input enabled or connected; it stays until something else is chosen.
- Grid, Length and Vel are what the piano roll uses for the track: its editing grid, and a new note's length and velocity. Length is a note length, or `grid` to follow the grid.
- The inspector says first what the column under the cursor is, such as "Mute: silence this track in playback", much as a tooltip would.
- Mute, Solo, Arm, Thru, Chase and Quantise are adjacent columns of buttons, without headings: each shows its letter, `M` `S` `R` `T` `C` `Q`, grey when off and filled with its colour when on. Enter or `e` toggles a button straight away, with no editor, and Tab passes over them. On a `V` selection, every selected row takes the opposite of the cursor's row, with Enter or, in this view, `M`, `S` and `, t m/s/c`. Chase is per track: when enabled it starts notes already held at the playback position.
- Arm and Thru are buttons after Mute and Solo, working the same way: `R` when the track is armed to record, and `T` when MIDI input is sent through to it. Thru on a track that isn't armed shows its `T` in its colour but unfilled, since only armed tracks hear the input. `, r a` arms or disarms the current track, and `, r t` switches its Thru, from any view with a track, or every row of a `V` selection. Arming a track while others are armed names the others in the status line, in case they were left armed by mistake.
- **Input**, before Output, three cells apart, chooses **Default** (the initial choice: the default input set in the Ports view), **All** (every enabled input) or one of the enabled input ports. Enter opens the chooser; typing filters, Up/Down choose, Enter applies, Escape leaves, and Tab applies and goes on. A Visual track selection takes the same choice with one undo step. The choice is saved with the song; unavailable inputs are in the warning colour with an inspector warning and receive nothing. The status line shows an `In` activity light and the last message in, but a note-off, with its channel (`In 10 CC64   127`), in every view. `, T t` switches machine-wide Thru, through the `thru` setting; `Thru off` remains visible while disabled. Held notes and pedals can still finish at their original destinations.
- MSB, LSB and Program are `-` until set, and clearing the field unsets it. A `•` after Program means the track has program changes of its own, and the warning colour that another track on its port and channel asks for something different. Stepping starts from unset at 0. An edit is sent at once, unless the track's own program change is in force during playback, when the status line says it waits for the next play.
- `:event-insert EVENT` puts an event of any kind but a note in the cursor's track at its time, from any view with a track, or `, e` asks for it, with Tab completing the kind: `cc 7 100`, `program 5`, `bend -200`, `pressure 60`, `polypressure C4 60`, `sysex F0 7D 01 F7`, or `text`, `lyric`, `cue`, `copyright` or `instrument` with words in quotes. `tempo 96` sets the conductor's tempo from there on, from any view, replacing a change already at that time.
- **Playback quantise:** the `Q` button, or `, t q` from any view with a track, quantises it as it plays, keeping its settings when off. Enter on the Quantise column edits them in the inspector: Tab and Shift-Tab go between Grid, Strength, Swing and Moves, Up and Down step one (the grid through the triplets too), typing replaces it (`grid` for the track's editing grid), and Enter or Escape leaves. Moves (`▾`) is a drop-down list: Up and Down choose, a letter goes to the choice starting with it, and Enter or Tab takes it. `, T q` bypasses quantise for every track, to compare, and `, T m` shows or hides the piano roll's faint marks of where quantised notes will be heard.
- **Reordering tracks,** in the Tracks view and the arrangement, on the cursor's track or a selection: `, t o` starts a `MOVE` mode, where `j` / `k` or Down / Up move them through the others, with a count, `gg` and `G` go to the first and last place, Enter accepts, as one undo step, and ends any selection, and Escape puts them back, keeping the selection, with no undo step. Every other key does nothing, transport included. Alt-Up and Alt-Down (or `Alt-k` and `Alt-j`) move them at once instead, with a count, and repeated moves of the same kind undo together within that unchanged selection. Ending Visual mode or redefining the selection starts a new undo step for its next moves, including after Escape and `gv`. A track's settings, events, meters and marks go with it. The conductor stays first.
- `M` and `S` (or `, t m` and `, t s`) mute and solo the current track, and `, t c` toggles its chasing, from the Tracks view, the arrangement's tracks, the piano roll and the event list. Each is an undoable edit. The conductor has none of these, and the section strip has no current track. The Name column grows with the longest name, to 32 characters or as far as the table still fits the window; a longer name is cut short there, and is whole in the bar above.
- `=` / `-` and Ctrl-A / Ctrl-X step numeric fields. For musical offsets these add the track's editing grid to the note fraction, keeping the fine ticks, while `+` / `_` step the fine ticks alone. An offset given in plain ticks stays in ticks. Millisecond offsets step by 1 ms. In the button columns, an upward step sets on and a downward step sets off.
- Time offsets accept `-40ms`, `+120t`, `+1/16` and composites such as `+1/16-22t`. These are absolute settings, including their sign; the last example is +218 ticks at 960 PPQN. Limits are ±1000 ms or ±64 whole notes. Musical and millisecond units retain their meaning through save/load. A musical value stays as it was typed, on screen and in the song file: `+1/16-33t` is not rewritten around a nearer fraction. A change of PPQN keeps the fraction and scales the fine ticks.
- `V` selects complete rows. Enter assigns the field to all selected tracks; steps change each track relative to its own value. The entire edit is refused if any row would be invalid. Each batch is one undo step; names are edited individually.
- `Vy` copies one whole track, `Vjy` two; `yy` or `Y` also copies the current track. Settings, editing defaults, all section events and per-section meter ownership are included. `Vd` deletes explicitly selected tracks; `dd` and event-deletion keys do not delete whole tracks.
- Normal `p` / `P` insert copies after/before the current row, with new IDs and unique numbered names. Visual `p` / `P` replace selected rows as one undo step. Routing and mute/solo are preserved. Named registers work (`V"ay`, `"ap`); appending adds rows. Event contents and sections remain separate payloads, and cannot be pasted as whole tracks.
- The first implementation requires matching PPQN, section names, order and lengths when pasting. An incompatible paste leaves the song and register unchanged. `yit` in the editing views still takes only event contents.

## Freezing playback transforms

`, t f` or `:track-freeze` opens a list: **All**, Transpose, Velocity, Quantise and Time offset. Up/Down choose any entry, including dimmed ones, stopping at the first and last entries. Enter freezes an available choice; on a dimmed choice it does nothing, shows no status message and keeps the list open. Escape cancels. All is selected initially. Unavailable choices stay dimmed with their reason. `:track-freeze all`, `transpose`, `velocity`, `quantise` or `time` makes the choice directly (`quantize` and `offset` are aliases); a refused direct command still reports why.

Freeze takes the whole current track, or selected whole rows in Tracks; leave other Visual selections first. Playback must be stopped. All selected tracks validate and change together, as one undo step restoring both events and settings.

The list checks the result of each choice, dimming invalid results with their reason, such as an event starting before zero. It also dims a choice with "Nothing to freeze" when no selected track has that transform active; All is dimmed when none has any active transform. A zero offset is inactive in any unit, and retained quantise parameters are inactive while Q is off. A choice stays available when one selected track has work and another has none, provided every row can accept it. Enter and direct commands check the same rules again before editing.

Only the chosen transform is applied and reset. All applies quantise before time offset. Quantise turns off but keeps its parameters; other playback settings, bank/program setup and routing stay as they are. Time offset alone requires quantise off: freeze quantise first, or choose All. Quantise and All require bypass off where a chosen track has quantise enabled.

A pitch outside MIDI range or an event before the song refuses the whole edit. Events can cross sections, keeping notes whole and growing the last section when needed. Millisecond offsets capture the current tempo map, rounding each end to the nearest tick, ties earlier; a new collision or a positive note collapsing to zero length refuses the edit. Quantise's notes left in place and pedal-order fallback are reported, as in playback.

## Coarse and fine note timing

The note inspector in the piano roll and event list has the Note heading above Pitch, Velocity and Off Vel on the left. To their right, Start, Length and Ends each have a row across a coarse value, a signed fine value and an exact value, under Units, Fine(t) and B:B:T, with the colons lined up. Elapsed time is in the bar above, with the cursor's bars, beats and ticks and the meter, in every view with a timeline. Units follow the track grid by default; `:set timingunit=1/16` fixes them, and `:set timingunit=grid` follows the grid again. Changing units never moves notes.

A value stays in place as editing starts. `e` opens the inspector on Pitch at first, then remembers the last field. Tab/Shift-Tab move through pitch/velocities, then along the Start, Length and Ends rows, Up/Down step them, Enter commits and Escape leaves. Start and end steps count from one, within the bar the time falls in, and changing an end changes only the note's length; length steps count from zero. Fine is signed ticks from the nearest coarse step (ties use the earlier step). Coarse edits preserve that fine remainder. Fine entries such as `-22t` are absolute remainders. The exact Position and Length fields retain their existing input forms. In the event list, Enter still edits the current cell directly; `e` also keeps cell editing on conductor rows.

## Time and ripple

- `dib` deletes and leaves silence; `dab` takes the bar out, the rest of the section closing up, silence gathering at the section's end. `P` puts in, moving the rest of the section later. Ripple stays inside the section, so the rest of the song stays aligned.
- **Bars on every track,** changing the song's length: `, b d` (`:bar-delete`) deletes the cursor's bar (`2, b d` two), keeping them in the register as a section; `, b i` (`:bar-insert`) inserts empty bars before it, asking how many and in what meter, Enter taking one bar in the meter there.

## Notes in the piano roll

| Keys | Does |
| --- | --- |
| `o` | Add the track's default note at the cursor |
| `O` | Add it and move on, past its end, or with `gridadvance` (`, T a`) to the next grid step; `8O` eight in a row |
| `Alt-h` `Alt-l` | Move the note onto the grid line before, the next |
| `Alt-H` `Alt-L` | Nudge it earlier, later (the `nudge` setting, in milliseconds) |
| `Alt-{` `Alt-}` | Move it a bar |
| `Alt-k` `Alt-j`, `Alt-K` `Alt-J` | Move it a semitone, an octave (or Alt with the arrows) |
| `=` `-` | Move its end to the grid step after, before |
| `+` `_` | Lengthen, shorten it by a nudge |
| `Ctrl-a` `Ctrl-x` | Velocity up, down |

On a Visual selection, the moving and length keys act on every note it takes, refused as a whole if a note would overlap another of its pitch, and the selection moves with them; repeated moves of the same kind undo together within that unchanged selection. Ending Visual mode or redefining the selection starts a new undo step for its next moves, including after Escape and `gv`. After the first move, it retains those notes: passing over another note does not collect it, and snapping off-grid notes keeps the bounds around all their new starts. Deleting, yanking and transforms take that same moved set, including after Escape and `gv`. Deliberately extending the selection, changing its Visual kind or choosing another object selects an area again.
| `e` | Edit its fields in the inspector: Tab and Shift-Tab go round, Enter sets, Escape leaves |

**The grid and default note,** each track's own: `zh` `zl` finer and coarser, `Ctrl-t` triplets on and off, `:grid 1/16`, `:len 1/8`; `, n` takes the current note's length and velocity as the default note.

## Sections

- **On the strip:** `h` `l` go between sections; `x` and `Y` delete and yank one; `p` and `P` put after and before; `Alt-h` `Alt-l` move one a place; `J` joins it with the next; `+` `-` lengthen and shorten it by a bar; `i` `a` insert one before and after; `c` renames it; `e` edits its name and length in the inspector; Enter plays from it.
- **From any view, on the cursor's section,** `, s`: `r` rename, `i` and `a` insert one before and after, `j` join with the next, `s` start one at the cursor's bar line, `l` start one at the label before.
- A new section's name is asked first, with "Section N" to edit; Enter puts it in named, as one change, and Escape puts none.

## Repeating, undo and macros

- `u` and `Ctrl-r` undo and redo, going back to where each change was made. Each change is one step.
- `.` repeats the last change, with its range and answers; `5.` with a count of five in place of its own.
- `Qa` records keys into macro a until `Q` (`QA` adds to it); `@a` plays it, `@@` the last again, `3@a` three times. A macro stops where a command is refused, whatever its message, and only there. `:macros` lists them.

## Transforms

`!` then a range and a key, or the leader's `, x` and a key then a range, or `:` and the transform's name. Those that ask, ask each setting in turn, Enter taking the default shown.

- **Pitch and time:** `t` transpose by semitones, `T` in a key, `i` invert, `I` in a key, `R` retrograde, `q` quantise (asking how), `s` stretch, `n` note lengths, `h` humanise, `y` split at a note to a track.
- **Velocity:** `v` velocity, `l` lift, `L` lower, `M` limit, `r` ramp, `c` compress, `e` expand, `p` phrase.
- **Chords and controllers:** `a` accent chords' top notes, `b` balance chords, `C` compress chords, `E` expand chords, `m` remap a controller or note, `d` thin controller density, `f` filter (delete matches).
- **Pedalling,** `, x P`: `r` re-pedal at chord changes, `t` re-time the pedalling, `b` pedal each bar or beat, `c` clean up, `x` clear a pedal, `o` offset, `k` bake the sustain into notes.

`2!!` transforms this bar and the next.

## The leader

The leader is `,` (the `leader` setting). Its groups:

| Keys | Group |
| --- | --- |
| `, b` | Bars on every track: `d` delete, `i` insert |
| `, e` | Insert an event at the cursor, asking which |
| `, s` | Sections, on the cursor's section |
| `, t` | Tracks: `m` mute, `s` solo, `c` chase, `q` quantise as it plays, `o` reorder, `f` freeze playback transforms |
| `, v m` `gm` | The Ports view: this machine's MIDI ports, to enable, alias and choose the default input and output |
| `, r` | Recording: `a` arm this track, `t` its Thru, `m` replace or overdub |
| `, m` | Metronome: `m` off, while recording or always; `c` count-in, pre-roll or neither; `s` the Metronome page |
| `, S` | Select |
| `, x` | Transforms |
| `, v` | Views: `t` Tracks, `a` arrangement, `r` piano roll (as `gr`), `e` event list |
| `, T` | Toggles: `Space` rewind, `a` grid advance |
| `, n` | Default note from this one |
| `, :` | The command palette (also `Ctrl-p`) |
| `, ?` | The keys here |

## Playing

`Space` plays from the play-from marker (▶) or stops where it is; `Shift-Space` stops and goes back to where the cursor was; `m Space` puts the marker at the cursor; `Ctrl-Space` puts it there and plays; `, T Space` turns rewind on and off; `Ctrl-.` panics. `R` records onto the armed tracks from the marker, after the song's count-in, and `Space` stops, the take going in as one undo step; `, r m` switches the song between replacing what's there and overdubbing it. `:countin 2`, `:countin 3 beats`, `:countin off`, `:preroll 2` and `:preroll 3 beats` set what comes before recording.

## The Metronome page

`, m s` or `:metronome`. `j` / `k` move between settings and `h` / `l` between a row's fields: a click's note, velocity and Test button, or a count-in's or pre-roll's number and Bars or Beats. Enter edits a typed value in place, Up and Down step it, Enter sets it and Escape leaves it; a digit on a number field starts typing it straight away. Fields marked `▾` open a drop-down list with Enter: Up and Down or `j` / `k` choose, a letter goes to a choice starting with it (the port's list narrows to what's typed instead), Enter takes it and Escape leaves it. Enter on Test plays the click, as changing its note or velocity does. Each change is an undo step.

## Questions and lines typed into

- **A yes-or-no question** takes `y`, `n` or Escape; any other key shows it again.
- **A question at the prompt** shows its default in brackets, which Enter takes ("how many (1)").
- **In any line typed into,** Escape gives it up; Backspace on an empty line does nothing, so a press too many can't throw away what's being asked.
- **`Ctrl-p`** opens the command palette: type part of a command's name or what it does. A first `@`, `#`, `'` or `?` searches sections, tracks, labels and marks, or settings. What's typed is at the palette's top, the best match first under it; Down and Ctrl-n go to the next, Up and Ctrl-p back.
- The `:` prompt, search and questions are typed on a row of their own above the status line, which nothing covers; Tab's matches show in a small popup above the word, the first nearest the line; Up goes on through them and Down back, Down from none chosen starting at the top one shown.
