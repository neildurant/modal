# Status

Where Modal stands, for whoever picks up the work next. Keep it current: update it as work lands, and when a session ends.

*Last updated 13 November 2026: milestone 8 is done. Rebarring step 1, inserting a meter and rebarring the song's meter, is accepted and published. Step 2, rebarring selected tracks and `:meter follow` (`, s f`), is committed locally and awaiting trial: Windows Debug (972 tests) and WSL Clang Debug pass, and formatting passes; it isn't pushed yet. Then step 3, the tint for bars with a meter of their own, then adding tracks, then Mute, Solo and Arm in the arrangement (schedule.md). See [schedule.md](schedule.md) for work order.*

## Current handoff

Read [schedule.md](schedule.md) for the current milestone, item states and next work. Read [design.md](design.md) for behaviour and unresolved choices. The sections below preserve validation evidence and accepted trials; their historical feature summaries do not define the current specification.

## Pending

- **The Linux checks** for milestones 3 to 6, on Neil's laptop, under X11 and Wayland: the window and its resizing, display scaling, text input (dead keys, AltGr), Ctrl and keypad keys, the configuration directory, the offscreen renders' appearance, and the views. These need a manual native-desktop check.
- **A two-display check on Windows**: dragging the window between displays with different scaling.

## Review findings

### Review of `415b665..5e2885b` (10 November 2026)

Pulled and reviewed all nine commits since the schedule publication: GUI MIDI input/arming/Thru, port filtering, bar and prompt layout changes, completion navigation, and step 8.2's design choices. One actionable finding was identified; its approved fix and accepted trial are recorded below:

- **Changing `inputs` can leave Thru notes and pedals held.** `Playback::openInputs()` closes all input ports and reuses their numeric InputIds without asking the engine to release the old inputs' held state. If a note or pedal is down on an input removed by the new setting, its release can never arrive. A stopped transport and subsequent plan updates do not clear Thru state; panic does. Reused IDs can also associate a replacement device's releases with the previous device's held notes. End the removed inputs' held state through the engine before reusing their IDs, with an acknowledged transition. Cover removing/replacing an input with held notes and pedals in a regression. Fixed by `d43a0e7`; validation and the accepted functional trial are recorded below.

A standalone C++ fake-clock harness confirms that a Thru note remains on across a plan update and idle stop when its old input no longer supplies a release, and is released by panic. The port-close/reassignment path was inspected in apps/modal.cpp; no actual MIDI ports or desktop were driven. The harness is in /tmp, not committed.

Validation: `tools/check-linux.sh linux-gcc-debug linux-clang-debug` passes, 857 registered tests per preset, no failures; whole-tree formatting passes. CI for tip `5e2885b` passed. The older run for `8da3d60` was cancelled and superseded by that successful tip run. Release/sanitizer/Windows results are from CI, not rerun locally for this review.

### Review of changes from 4 November to `cd49989`

Reviewed on 9 November 2026, including 4 November. All four fixes pass full Linux validation. The requested UI trials for moved-note deletion and refusing a selected nudge before zero were confirmed in the real window on 9 November 2026:

1. **Fixed: operators and replacement objects follow a moved selection.** The remembered origin and the selection before its object move with it; operators take the moved selection rather than resolving the old object again. GCC Debug regressions first reproduced the failure, then passed at both PPQNs for deletion, yanking, `gv`, colon transforms, replacement objects and a moved bar's time, with undo checked.
2. **Fixed: changing the editing grid does not repeat an already-sent quantised note.** Playback plans record the effective quantise grid, so the scheduler recognises changes when quantise follows the editing grid. A GCC Debug regression first reproduced the duplicate, then passed at all three core-test PPQNs, checking the note-ons and releases, an unsent note's new time, and an independent fixed quantise grid.
3. **Fixed: pedalling stays with a note left unquantised after a clash.** Pinned notes contribute a zero movement to the quantiser's movement table. A GCC Debug regression first reproduced the incorrect pedal times, then passed at all three core-test PPQNs for sustain, sostenuto and soft pedal, including a later pedal that should follow a moved note.
4. **Fixed: a selected nudge before the song is refused whole.** Batch validation sees the unclamped starts, so one note reaching before zero leaves the song, cursor, selection and undo history unchanged and reports the refusal. A GCC Debug regression first reproduced the partial move, then passed at both PPQNs and checked that the selection can still move in the other direction and undo. Single-note nudges retain their existing clamp at zero.

The initial review reproduced all four with a standalone C++ harness at both 960 and 96 PPQN. Permanent regressions now cover them in `piano_roll_view_test.cpp`, `scheduler_test.cpp` and `playback_plan_test.cpp`. The fix commits are `fecc99d` (moved selections), `91416c5` (effective quantise grid), `6803fcc` (pedal following) and `25d0c91` (batch nudges).

`tools/check-linux.sh` passes on the final implementation and regression tests: GCC Debug/Release, Clang Debug/Release, ASan/UBSan and ThreadSanitizer, with 822 registered tests per configuration, three platform/MIDI skips and no failures. Whole-tree formatting and `git diff --check` pass. `build/linux-gcc/apps/Debug/modal` is newer than every tracked application build input. No desktop automation was used. The confirmed UI trials do not establish MIDI hardware timing results.

Trial steps retained for reference, including additional cases covered by the automated tests. Run `build/linux-gcc/apps/Debug/modal --demo`, or use a disposable song:

- In the piano roll, on a note, try `vin`, Alt-Up, then `d`. It should delete the moved note. Undo restores it at its moved pitch, and another undo restores its original pitch. Repeat with Escape and `gv` before deleting, and with `ic` after moving to replace the selection with its chord.
- On a sparse bar, try `vib`, Alt-}, then `d`. Deletion should act at the moved selection's time. Undo the deletion and move afterwards.
- With notes at tick zero and later, try `ggv]b]b`, then Alt-Shift-Left. The whole nudge should be refused with every note, the cursor and the selection unchanged. Alt-Shift-Right should still move the selection; Escape and undo should restore it.
- If listening through MIDI, enable playback quantise with its grid following the editing grid, then change `:grid 1/16` to `:grid 1/4` during playback. Already-played notes should not repeat. The message tests also cover pedalling after a note left in place by a clash; hearing these cases on real hardware remains unmeasured.

### Earlier review fixes

All five findings from the review of `30f6301..64c0fce` are addressed. The six findings from the preceding review were already fixed.

1. **Fixed: `gv` restores the state needed to replace an object.** It now remembers the selection before its object as well as the selection and cursor. A regression test first reproduced whole-track deletion with `vit<Esc>gvibd` and `v2it<Esc>gvibd`, and after yanking instead of Escape; all now delete only the first bar's contents on the original track. The focused GCC Debug test passes at both PPQNs and checks undo.
2. **Fixed: a refused Visual put restores the song and retains its refusal message.** The replacement stops if deletion is refused, and rolls back cuts and deletion if the paste fails. Focused GCC Debug tests cover `p` and `P`, cutting at the selection's edges, unchanged registers and cursor, saved state and redo history, and refusing to delete every section. Existing successful replacement and undo tests also pass.
3. **Fixed: colon transforms resolve the same selected object as keyboard operators.** The event list counts and highlights only a chord's notes, and its Visual targets and deletion also exclude intervening controllers. GCC Debug regressions compare colon and keyboard filtering for one or two chords at both PPQNs, check the displayed selection, and verify that controllers remain. The existing cross-view object-equivalence tests pass.
4. **Fixed: command results distinguish refusal from warning severity.** Shared reporting handles key commands, prompt answers, colon commands and file results. Successful section-split and MIDI-import warnings no longer signal refusal; actual colon-command errors do. GCC Debug regressions cover continuing a macro after a split warning through both keys and the colon line, stopping on refusals at either severity, and importing MIDI successfully with a warning.
5. **Fixed: the grammar distinguishes motions and deletion by view.** A per-view table names the destinations of `0` and `$`, and the arrangement's `d$` is explained alongside `dib` for the current bar. The accounts of ranges and counts agree with it, including sections on the strip for `gg` and `G`. grammar.md and the README also state where Visual `D` is refused. Checked against the view implementations.

## Validation of the review fixes

- `tools/check-linux.sh` passed with four build and test jobs: GCC Debug and Release, Clang Debug and Release, ASan/UBSan and ThreadSanitizer. Each configuration registers 710 tests; three platform/MIDI tests are skipped, with no failures. Whole-tree formatting and `git diff --check` pass.
- The implementation commits are `bb978c3` (`gv`), `26036c2` (refused Visual put), `8f7c1d0` (exact object selections) and `ea95549` (command outcomes). They and the documentation correction were published through `9f2a94b`; [CI passed on Linux and Windows](https://github.com/neildurant/modal/actions/runs/42132483888), including the Linux sanitizers. No local Windows build was required.
- `build/linux-gcc/apps/Debug/modal` has been rebuilt and is newer than all tracked application build inputs. The UI trial below was confirmed by the user. MIDI hardware behaviour has not been tested in this session.

## UI trial for the review fixes

The UI trial below was confirmed on 5 November 2026, completing the check required by AGENTS.md before pushing. The fixes follow `fd81caa`, which published the handoff and passed CI on Linux and Windows.

- On an arrangement track with notes in several bars, try `vit<Esc>gvibd`, and then undo. Only the first bar of that track should be cleared. Repeat with `v2it` and with `y` instead of Escape before `gv`.
- In the piano roll, yank a C4, then select a D4 with `vin` where another C4 already occupies the destination. Try `p` or `P`: the overlap message should remain, and the song should be unchanged, including the D4. Existing redo history should remain available.
- In the event list, select a chord with a controller row between its note rows using `vic`. Only the notes should be highlighted and counted. `:filter cc1` should leave an intervening CC1 alone, just as `!f` followed by `cc1` does; repeat for `v2ic`.
- With a track in 3/4 and the conductor in 4/4, record a macro that splits a section at the track's second bar and then moves right. The warning about a short conductor bar should still appear, and the macro should continue moving. A refused split at an existing section start should stop it.

## Tracks view: first implementation

The first step of milestone 7 is complete: `40a8f84` adds the core support, `3030d68` the view, editing and timing inspector, and `cb85a4f` the revised layout and track controls. The revised UI trial was confirmed on 6 November 2026 and the step published through `cc09eea`. The agreed design is in design.md and decisions.md; keys and editing are in grammar.md.

- The table exposes number, name, mute, solo, chase, output port, channel, transpose, velocity offset and time offset. Mute/Solo/Chase show `-` or coloured `M`/`S`/`C` buttons; numeric values align on the right. Stable field identities, shared formatting/editing and one ordered column list keep later column visibility or reordering separate from track state.
- `gt` and `, v t` open Tracks; `3gt` chooses track 3. Ctrl-PageDown / Ctrl-PageUp switch tracks. Musical position and pitch are retained when switching views.
- `V` selects whole rows. Enter assigns a property across them, steps change each relative to its own value, and the batch validates and undoes together. Names are edited individually. `Vy` / `Vjy` copy whole tracks; Normal `p` / `P` insert copies after/before, Visual `p` / `P` replace selected rows, and `Vd` deletes explicitly selected tracks. Copying preserves settings, routing, mute/solo, editing defaults, section events and per-section meter ownership, with fresh IDs and unique names. Marks are not duplicated. Named registers also support whole tracks.
- Offsets retain milliseconds or musical ticks through save/load and recording/playback. Inputs include `-40ms`, `+120t` and `+1/16-22t`; the agreed bounds are ±1000 ms or ±64 whole notes. Musical offsets rescale with PPQN and follow tempo changes at both note ends.
- The piano-roll and event-list inspectors group pitch/velocities on the left, aligned Start/Length coarse/fine pairs in the middle, and exact timing on the right. `timingunit=grid` is the agreed default; a fixed fraction also works without moving notes. `e` edits the inspector; event-list Enter retains cell editing, as does `e` on conductor rows.
- The first whole-track clipboard requires matching PPQN and section names, order and lengths. Incompatible pastes are refused before changing the song. Wider mapping between songs remains future work.
- Playback fixes found while exercising track settings: muting/rerouting the last track on a channel releases held pedals, and rerouting ends its old notes. A channel shared by another audible track keeps its pedal state. Detaching a stopping player also clears the view's playback state, avoiding a crash on a later view switch/wake.

### Validation

- Follow-up presentation checks pass: GCC Debug/Release, Clang Debug/Release, ASan/UBSan and ThreadSanitizer, each with 741 registered tests, three platform/MIDI skips and no failures. Whole-tree formatting and `git diff --check` pass. New regressions cover aligned coarse/fine pairs, a visible editor at narrow widths, exact offset display at both PPQNs, toggle rendering, per-track Chase, and Tab through track columns.
- Initial implementation: the full `tools/check-linux.sh` passed on the completed implementation: GCC Debug/Release, Clang Debug/Release, ASan/UBSan and ThreadSanitizer. Each configuration registers 735 tests, with three platform/MIDI tests skipped and no failures. Whole-tree formatting and `git diff --check` also pass.
- `build/linux-gcc/apps/Debug/modal` has been rebuilt after the implementation commits and checked to be newer than every application build input. The first UI trial produced the presentation feedback above; the revised layout was accepted on 6 November 2026.
- Hidden offscreen sample renders pass for Tracks, the event list, arrangement and piano roll, including dark/light themes. The revised inspector and both table themes were inspected, including active M/S/C buttons and fraction-plus-fine offsets; this caught and fixed duplicated glyphs under the new buttons.
- The [initial publication's CI](https://github.com/neildurant/modal/actions/runs/42157664843) passed formatting and every Linux job. MSVC rejected two test helpers' fallback returns after Catch2's unconditional `FAIL` as unreachable code (`C4702`). The helpers now use checked lookups; product behaviour is unchanged. After the correction, the full local Linux check passed again: all six configurations, 741 registered tests each, three skips and no failures. The follow-up push reruns the cross-platform matrix, including Windows Debug/Release; its result is available in [CI for main](https://github.com/neildurant/modal/actions/workflows/ci.yml?query=branch%3Amain).
- No desktop automation or MIDI hardware tests have been run. Local Windows builds are skipped under the agreed policy.

### Confirmed UI trial

The revised UI was accepted on 6 November 2026. These are the trial steps, retained for reference; the acceptance does not establish MIDI hardware timing results. Run `build/linux-gcc/apps/Debug/modal --demo` (or open a song).

1. `gt`, then `j/k` and `h/l`; Enter edits a field. Try the Mute/Solo/Chase buttons, channel, transpose and velocity offset. Enter accepts `M`/`S`/`C`, `-`, or on/off; Tab commits and moves along columns. Space plays, `ga/ge/gr` switch views, Ctrl-PageDown / Ctrl-PageUp switch tracks, and Backspace returns. Resize the window with the time-offset column selected.
2. On time offset (`$`), enter `+1/16-22t`, then `-40ms`; confirm the musical value displays as a fraction plus fine ticks, then undo and redo, save and reopen. Confirm that stored note positions remain unchanged. Real MIDI output is useful for auditioning these settings, but has not been measured here.
3. `Vy`, then `p` or `P`, and undo. Try `Vjy` for two tracks and `Vp` for replacement. In a numeric column, `Vj` then Enter or `+` edits both selected tracks in one undo step. Copies preserve routing, mute and solo as agreed.
4. In the piano roll or event list, `e` and Tab through the pitch/velocity, coarse/fine and exact-timing groups. Edit Start step/fine and Length steps/fine; undo. Compare `:set timingunit=1/16` and `:set timingunit=grid`. Check the grouped inspector is readable and leaves enough editing space.

## Refinements after the Tracks step

Tried in the real window and published: Enter and `M`/`S` for the track buttons, including on a `V` selection, time offsets kept as typed, the reworked note inspector with Time in the status line, the arrangement's inspector and section strip, a Name column that grows with long names, a visible replace state, Backspace clearing a highlighted value, and values that stay in place when a field opens. The decisions are in decisions.md, under Tracks, inspector and arrangement refined after use. `tests/support/no_crt_dialogs.cpp` also keeps MSVC's debug assertion dialogs out of test runs.

## Bank and program setup

Tried in the real window and published: the Program column with its bank fields, a setup sent at each playback start and on an edit, tracks catching up when they become audible during playback, the setup in MIDI export, and program changes marked in the piano roll's ruler. The reasons are in decisions.md, under Numeric bank and program setup. The tests check the messages sent; how instruments respond on real MIDI hardware hasn't been recorded here.

## The Tracks table edited in place

Tried in the real window and published: the demo song with every kind of event, the event-sign colour, tempo in the status line and the arrangement's ruler, the Tracks table edited in its cells with bank, grid and note-default columns, the port list, and `:event-insert` on `, e`, which also sets a tempo, as a stop-gap to refine later. The piano roll's ruler marks tempo changes as the arrangement's does. The reasons are in decisions.md, under The Tracks table edited in place.

## Track reordering

Tried in the real window and published: track reordering, with `, t o` and Alt-Up and Alt-Down, in the Tracks view and the arrangement. The reasons are in decisions.md, under Reordering tracks.

## Playback quantise

Tried in the real window and published: the `Q` button and Quantise column with its settings in the inspector, notes that clash left where they are rather than the whole track, the tightened table layout and the editing hints in the status line, toggles giving their state in the hint menu, the piano roll's moving keys acting on a selection, `, t q`, `, T q` to bypass and `, T m` for the piano roll's marks, and changes while playing that neither repeat nor drop notes. The reasons are in decisions.md, under Playback quantise. As with the bank and program setup, the tests check the messages sent and their times; hearing it on real MIDI hardware is still to do.

## Freeze

Implemented and tried, following the rules agreed on 9 November 2026. Freeze applies all or one playback transform to the whole current track, or selected Tracks rows, through a list opened with `, t f` or `:track-freeze`. The timing-order restriction, batch validation, invalid-result refusals, section ownership and rounding rules are recorded in design.md, decisions.md and grammar.md. Design: `4e1515a`; core operation and shared playback rules: `87bcb0b`; chooser and batch command: `8e000f7`. The initial behaviour and the availability, navigation and confirmation refinements were accepted in the real window on 9 November 2026.

After the first trial, the chooser now previews the full batch for each choice, dimming invalid results (including starts before zero) with their reason, and inactive choices with "Nothing to freeze". A zero offset is inactive in either unit. One unchanged selected row does not disable a choice with work on another row. Cached reasons update with the song, bypass and transport, and confirmation and direct commands prepare again before editing. The availability refinement (`73e7e0f`) was accepted in the real window, then `1e5f659` allowed Up/Down to visit every entry, including dimmed ones. Further feedback settled Enter on dimmed entries: `e0b342b` makes it do nothing and keep the list open without a status message; the dimmed entry already gives the reason. Direct colon commands still report refusals. This final confirmation adjustment was tried and accepted.

- The 24 focused GCC Debug tests pass, with 803 assertions. Core timing tests run at 960, 6720 and 96 PPQN; Workspace tests run at 960 and 96. They cover matching playback messages and times, independent transform choices, retaining settings, pitch and start refusals, millisecond rounding and collisions, section ownership and song growth, quantise fallback, the list and disabled choices, atomic batches, undo/redo, cancellation, bypass, transport restrictions and dot repeat. The availability regressions cover negative musical and millisecond offsets, inactive choices including musical zero, mixed active/inactive rows, and revalidation after the song, bypass or transport changes. Navigation regressions reproduce the earlier skipping and unwanted refusal message, visit every entry when all are dimmed, check movement back to disabled All and verify that Enter on every dimmed choice preserves the song, cursor and undo history, leaves the list open and produces no status message or warning styling. Direct-command refusal messages are also checked.
- Full `tools/check-linux.sh` passed at `73e7e0f`: GCC Debug/Release, Clang Debug/Release, ASan/UBSan and ThreadSanitizer, each with 845 registered tests, three platform/MIDI skips and no failures. For the subsequent menu-only refinements, `tools/check-linux.sh linux-gcc-debug linux-clang-debug` passes, each with 846 registered tests and the same three skips. Whole-tree formatting and `git diff --check` pass. Release and sanitizer checks were last run at `73e7e0f`. No local Windows build is required; CI will check Windows after publication.
- The chooser was rendered and inspected through SDL's offscreen driver at widths of 1200 and 640 pixels in both dark and light themes, including the negative-offset and no-active-transform cases after the refinement. The before-zero reason and "Nothing to freeze" labels are readable. No real desktop was driven, and MIDI hardware timing has not been tested.
- `build/linux-gcc/apps/Debug/modal` has been rebuilt and checked to be newer than every tracked application build input.

### Confirmed dimmed-entry UI trial

The final silent Enter behaviour was accepted on 9 November 2026. These trial steps are retained for reference; the acceptance does not establish MIDI hardware timing results:

1. With a negative offset and notes at tick zero, open `, t f`. All should be dim with its before-zero reason. Enter on it should do nothing, show no status message and leave the list open. With Q off, check Time offset too. Up/Down should still visit every entry.
2. With transpose, velocity and time offsets zero and Q off, all five choices should be dim with "Nothing to freeze". Up/Down should still move through every entry, stopping at the ends. Enter on each should be equally inert, with no status message, change or undo step; Escape should close the list.

### Initial Freeze trial steps, retained for reference

Run `build/linux-gcc/apps/Debug/modal --demo`, or use a disposable song, with playback stopped:

1. In Tracks (`gt`), give a track Transpose `+12`, Velocity offset `+10` and Time offset `+1/16`, and turn playback quantise on (`, t q`). Open `, t f`: All should be selected, Up/Down should choose, and Time offset should be dim with "Freeze quantise first". Escape should change nothing. Try resizing and both themes.
2. Choose Quantise alone. Q should turn off while transpose, velocity and time offset stay set. The notes should take the quantised timing. Reopen the list: Time offset should now be available. Undo should restore the notes and Q together. Direct commands such as `:track-freeze quantise` should give the same result.
3. Choose All. The notes should take the transposed pitches, adjusted velocities and offset timing; the chosen playback transforms should reset, with quantise parameters retained. Undo and redo should restore events and settings together. Bank/program setup and routing should stay set.
4. Give two tracks different playback settings, select their whole rows with `Vj`, then `, t f`. Freeze should act on both as one undo step. A refused batch, such as a transpose outside MIDI range or a negative offset moving an event before zero, should change neither track and explain why.
5. Space should keep its transport meaning outside the chooser. Freeze while playing or awaiting a stop should be refused. With quantise bypass on and Q enabled on a chosen track, All and Quantise should be dimmed; Transpose and Velocity should remain available. Playback comparisons through real MIDI are useful, but remain unmeasured here.

## Basic GUI recording

Milestone 8's decisions were settled on 9 November 2026 (design.md, Recording in the GUI; decisions.md, Input and recording): the `inputs` setting in `modalrc`, every input by default; independent arming, with a warning naming other armed tracks; `R` to record from the play-from marker; the `, r` group; replace/overdub as a song setting shown in the status line. Still to settle: each track's own input and the remaining saved settings.

### Step 8.1: MIDI input, arming and Thru (done, tried 9 November 2026)

- **Inputs:** `modal` opens the input ports the `inputs` setting names (comma-separated, matched as `:port` matches), or every one, up to 16, reopening them when the setting changes; names not found are reported in the status line. Modal's own ALSA ports are left out of the input and output lists, so Thru can't loop through them.
- **Collecting input:** each input's thread wakes the window through `Window::waker()` (one SDL event at a time, so idle stays idle), and the workspace takes what the engine passed back then, again 10 ms later, and every frame while playing, through `Player::takeInput()`. Nothing is kept yet; recording will. Lost input (dropped or not passed back) is reported once as a warning.
- **Tracks view:** Arm (`R`) and Thru (`T`) buttons after Mute and Solo. As requested during the step, the buttons lost their headings and sit closer (4 cells), each showing its letter grey when off; Thru on an unarmed track is its letter in its colour, unfilled. The inspector starts with "About", a short description of the column under the cursor. New theme roles `track-arm` and `track-thru`.
- **After the first look:** Arm became bright red and Mute a lighter pink, as Arm's orange sat too close to Thru; the inspector no longer ends with the track's name, which the bar above shows; Q's description reads "Quantise notes live during playback"; and Time offset keeps two blank cells before Quantise. Tried with a MIDI keyboard on Windows: arming and Thru work as expected, Thru taking the track's transpose and velocity offset. Pushed as `c96cfbf`.
- **Where things go, after the trial (tried and pushed 10 November 2026; CI passed for `87bfb0a`):** the top bar is where the cursor is (track, section, bars, position, time, meter; Tracks the track alone), the inspector what's under it, and the status line the transport: ■ and the play-from marker stopped, ▶ and the playhead playing, with meter, tempo and section, in a fixed place after the mode, messages after it. Fields are padded so nothing moves; the top bar leaves out quantise, program, bank, grid and note, bars, then section where it's narrow.
- **Prompts on a row of their own (tried and pushed 10 November 2026):** `:`, `/`, questions and the palette's line go on a row above the status line, which nothing covers. The palette is top down, its line at the top, the best first, a fixed `palettelines` high (10) with a count (`3/12`); completion's matches are a popup over the word, the first nearest the line, Up going on; after a first try, Down from none chosen starts at the top one shown, and the arrows stop at the ends.
- **The piano roll's grid and new note (tried and pushed 10 November 2026):** the grid is on the status line with the settings (`Grid 1/16`, padded), and the note `o` would add is stacked at the inspector's right, `New note`, `Length:`, `Velocity:`, dim, left out where a note's fields need the room. The top bar no longer has them.
- **Commands:** `, r a` (`track-arm-toggle`) and `, r t` (`track-thru-toggle`) from any view with a track, and `tracks-arm-toggle` / `tracks-thru-toggle` on `V` rows; Enter on the buttons too. Arming while others are armed says "Armed: X; also armed: Y" as a warning.
- **Tests:** Windows Debug passes (851 tests, 2 platform skips), after the first-look changes too: arming messages and undo, Visual rows, Thru's look and messages, the inspector's descriptions for every column and no name there, the gap before Quantise, input taken on waking and at the settle deadline, and lost input reported once. Tracks tests now go to columns by field rather than by counts. The dark and light Tracks samples were rendered offscreen and looked at; the light theme's arm/thru colours were separated after that.
- **Not checked:** the exclusion of Modal's own ALSA ports, which needs native Linux (no ALSA sequencer in WSL or CI). Note that listening to every input can loop Thru round a loopback port, such as loopMIDI on Windows, if a track also plays to it; the `inputs` setting then names the ports to use.

### Input replacement fix (10 November 2026; trial accepted)

Changing `inputs` now closes the old ports, requests an engine reset, and polls on the GUI deadline without blocking. The engine drains queued input again inside the reset command, releases held Thru notes and pedals, and publishes an acknowledgement before any replacement port may reuse an input ID. A full command queue is retried; repeated settings changes use the latest requested inputs when ready. Playback is neither stopped nor globally reset. The implementation follows the approved plan; selective port reopening remains outside this fix.

Four new fake-clock regressions pass (177 assertions in total), covering held notes on multiple inputs, sustain/sostenuto/soft pedal, queued old input, preservation of stamped input, ID reuse and successive acknowledgements, command-queue saturation, playback continuing without a controller reset, and a final callback arriving after the normal input pass. Core musical cases run at all three test PPQNs. Validation passes in GCC Debug/Release, Clang Debug/Release, ASan/UBSan and ThreadSanitizer: 861 registered tests per configuration, three expected platform/MIDI skips and no failures. The full check initially hit LeakSanitizer's sandbox tracing restriction during ASan test discovery; rerunning `tools/check-linux.sh linux-clang-asan` outside the sandbox passed, without disabling sanitizer checks. Whole-tree formatting and `git diff --check` pass. The GCC Debug `modal` executable is newer than every tracked application build input. No local Windows build was run; CI will check Windows after the accepted hardware trial and publication. Implementation: `d43a0e7`. The functional trial was accepted on 10 November 2026 and publication requested. CI results after publication are available through the main-branch CI link above.

Trial accepted on 10 November 2026. Steps retained for reference, using `build/linux-gcc/apps/Debug/modal` (rebuilt and checked): arm a track with Thru on, hold a note and/or sustain pedal on input A, and change `:set inputs=...` to exclude A (use another available input, or a deliberately unmatched name). The old notes and pedals should release immediately; the window should remain responsive. Change back to A and confirm that fresh notes release normally. Repeat while the song plays: playback should continue. Trial acceptance does not establish measured latency or jitter results.

## Hardening before step 8.2 (10 November 2026)

The approved sequence is complete: moved-selection membership, a testable input session, then bounded/fair input service. The selection and undo-boundary trials were accepted on 10 November 2026, with publication authorised. Step 8.2 is now in progress, as recorded below. The schedule owns the item states.

**Moved selections.** `6cdac30` retains the original notes after movement instead of collecting neighbours newly inside the rectangle. Escape/gv, deletion, yanking, colon transforms, pitch boxes, whole pitch rows, either active end, explicit selection extension, atomic collision refusal and undo are covered at 960 and 96 PPQN. The initial permanent regression failed in 16 checks before the fix. A follow-up off-grid case first failed in eight checks: snapping changes the spacing of retained notes, so an edit could miss the last one. The selection span now covers every retained start without selecting unrelated notes inside the larger bounds. One older moved-bar deletion test was corrected because it expected the same accidental deletion of a neighbour.

**Undo refinement after the trial.** Moving selections through neighbours was confirmed correct on 10 November 2026. A new trial finding showed two different Visual move interactions undoing as one step. The permanent Workspace regression first failed in 50 checks at 960 and 96 PPQN. `Document::breakMerge()` now ends merging without an edit or history change, and Visual lifecycle/retargeting paths call it. Focused tests pass for separate selections, Escape/gv, object/leader selection, Visual-kind changes and cursor extension, with undo/redo and saved state checked; core tests retain redo and explicit outer groups. Full Linux validation passes: 881 registered tests per configuration in GCC Debug/Release, Clang Debug/Release, ASan/UBSan and ThreadSanitizer, with three expected skips per configuration and no failures. The ASan/UBSan check was rerun outside the sandbox after LeakSanitizer failed under tracing. Whole-tree formatting and `git diff --check` pass; the GCC Debug executable is newer than all tracked application build inputs. No desktop or MIDI hardware was driven. The undo refinement in `de86783` was accepted in the real-window trial on 10 November 2026: separate Visual move interactions undo separately, while repeated moves within one interaction remain grouped.

**Input session.** `9b3ae8c` moves ownership, active name-to-ID lookup and asynchronous reset/deadline handling from apps/modal.cpp into `MidiInputSession` in the platform library. Six hardware-free tests pass (66 assertions), including final callbacks during close, delayed/full-queue reset acknowledgements, pending settings edits, matching and opening failures, stable IDs and the engine port limit. No UI setting or behaviour changed in this extraction.

**Bounded input.** `f179b58` captures finite per-port queue lengths, services up to 32 whole messages normally, and yields after eight per port. Captured backlog is drained before transport changes to retain recording play/tick/time; whole SysEx messages keep their framing. Producers refilling queues cannot extend the captured work indefinitely. Accepted/processed message counts keep the GUI collecting every batch until the engine is finished, even when the engine has not run at the first settle deadline. Regressions cover queue snapshots, bounds, all-port fairness, playback under load, Stop with a refilling producer, recording stamps, SysEx at a budget boundary and idle GUI backlog completion. Transport drains are bounded by captured queue capacities, not by the ordinary 32-message limit; these tests establish progress and attribution, not measured hardware latency.

**Validation before the undo refinement.** `tools/check-linux.sh` passes in GCC Debug/Release, Clang Debug/Release, ASan/UBSan and ThreadSanitizer: 878 registered tests per configuration, three expected platform/MIDI skips and no failures. Whole-tree formatting, `git diff --check`, local Markdown links/anchors and the schedule's milestone order/unscheduled list pass. `build/linux-gcc/apps/Debug/modal` is rebuilt and newer than all tracked application build inputs. No real desktop was driven and no local Windows build or hardware timing/flood measurement was run. The hardening trials are now accepted; CI will check Windows on publication. The earlier published MIDI reset fix and its byte-array compiler correction passed CI through `da765fd` (details below).

**Accepted selection trial / hardware checks for reference:** run the rebuilt `build/linux-gcc/apps/Debug/modal --demo`, or a disposable song. Move a Visual selection through neighbouring notes and back using Alt-arrows; neighbours must stay put and unselected. Try v, Ctrl-v and V, then delete/yank, Escape/gv and undo. Include off-grid notes, where snapping can spread them apart. Confirm that deliberately extending/replacing a selection still selects the new area. With MIDI hardware, check Thru and changing `inputs` while holding a note/pedal, stopped and during playback; held old input should release and playback continue. Trial acceptance will not establish measured latency or jitter.

## Step 8.2: per-track input and monitoring (10 November 2026)

The approved plan is implemented locally, including the confirmed initial **All inputs** choice (no separate default port). Each track saves an optional `input` name. Playback plans and the recorder resolve named inputs against the actual open-port mapping; an unavailable named input receives nothing and its recording track is left untouched, including replace mode. The GUI refreshes routing after asynchronous input replacement completes. The command-line recorder supplies the same resolver.

Tracks has an Input column after Output port, using the existing chooser with active input ports and All inputs. Missing saved names remain selectable and dimmed, with an inspector warning; resolved partial names are not marked missing. A Visual track selection applies one input choice as one undo step. Tab now proceeds from Output port to Input, then Channel.

Every view's status line shows `In` and the last positive-velocity note-on's pitch and velocity. Any input message lights the indicator for 150 ms, with a deadline to redraw it dimmed when idle; note-offs and controllers preserve the last note. Machine-wide `thru`, on by default, is a modalrc setting switched with `, T t`, with `Thru off` shown while disabled. It changes no song or undo state, and held notes/pedals still release to their original destinations while new sends are blocked.

**Validation.** `tools/check-linux.sh` passes GCC Debug/Release, Clang Debug/Release, ASan/UBSan and ThreadSanitizer: 891 registered tests per configuration, three expected platform/MIDI skips and no failures. Whole-tree formatting and `git diff --check` pass, and 34 local document links resolve. The hidden offscreen sample passes (19 assertions); Tracks renders were inspected in dark and light themes. The GCC Debug executable is newer than all tracked application build inputs. The changes are committed as `ede73ee` and `4d22eff`. The available trial checks were accepted on 10 November 2026, with publication authorised despite deferring the MIDI input-light check until hardware is available. Windows CI is to be checked on publication. New tests cover per-input channel-message routing and retained releases, global Thru off/on, input resolution/missing/restored IDs without silencing stored playback, per-track recording, quoted-name song round-tripping, chooser filtering/cancel/All inputs/undo, Visual track application, missing-input styling and valid aliases, and status activity in every view with idle expiry. The old Output-port Tab and width tests now account for Input; the tempo-status test uses the default minimum window width to accommodate the additional status display. No real desktop or MIDI hardware has been driven.

**Accepted trial / deferred hardware checks for reference:** run `build/linux-gcc/apps/Debug/modal --demo` or a disposable song. In Tracks, choose All inputs or a specific device under Input, including on a Visual track selection; check Escape, Tab, undo/redo and save/reopen. Play notes and controllers and check the `In` light/last note in each view. Arm tracks with Thru on; if two devices are available, verify each named input reaches only its chosen track and All inputs hears both. With a note and pedal held, toggle `, T t` off and release them: releases should still arrive, new notes should not sound, and the input monitor should remain active. Toggle back on. Change `inputs` and check named routing resumes correctly when replacement completes; an unavailable saved input should be dimmed with a warning and never fall back to All inputs. Hardware latency and jitter have not been measured.

**Duplicate input names reported during the trial.** `modal --demo` reported `MIDI inputs: 'input' matches several MIDI input ports: input, input`. The platform lists a port by libremidi's display name (falling back to its port name), discarding the device/client name. All-input enumeration deduplicates those labels, then opening by name re-enumerates the raw ports and refuses the ambiguous exact match. Thus two distinct ports with the same short name cannot be opened; this is possible with software ports and does not imply attached hardware. No hardware was accessed to diagnose it. A follow-up should use one consistent set of distinguishable labels for listing, matching and opening, qualifying duplicate labels with device/client information and retaining ambiguity rather than arbitrarily choosing a device. The naming change needs an approved plan before implementation.

## The Ports view, steps 1 and 2 (11 November 2026; reworked after trial, accepted and published)

Agreed on 10 November 2026 after the 8.2 hardware trial (design.md, Ports). Step 1 (`6c3c424`): a `PortTable` (src/ui/ports.cpp) holds each input and output seen on this machine, its alias, whether it's enabled and the default output, kept in `ports` in the state directory; ports seen for the first time start disabled. Tracks' port names resolve by alias, then detected name, to an enabled port that's there, or say why not (missing or disabled), and only enabled inputs are listened to. The platform labels ports one way for listing, matching and opening, with the device's name in brackets where two share a name, fixing the duplicate "input" ports seen on Linux. The `inputs` and `port` settings are removed, with no migration. Step 2: the Ports view (`, v m`, `:ports`), with E and D buttons, aliases edited in place, `x` forgetting a port that's gone, an inspector saying what each column is, and `:ports-refresh`; libremidi's observer wakes the window when ports come or go, and opening the view looks again.

**Validation.** Windows Debug passes 904 tests; new tests cover the table (first sight, presence, resolution order, ambiguity, disabled and missing, the default output, the file's round trip and bad lines), port labels, the input session's empty list, and the view driven by keys (opening, moving past headings, E and D, aliases and their uniqueness, forgetting, the inspector). Dark and light renders of the view were looked at. The hot-plug watcher and real ports need the trial.

**First trial, 11 November 2026, and the rework (a third local commit).** Asked for after trying steps 1 and 2: more space between the groups; each group with its own column headings; **On** and **Default** headings, with a green `✓` / grey `✗` and a radio button (`•` / `◦`; the font's `●` overflows the cell) instead of the E and D letter buttons; a default input as well as output, so a track's input chooses Default (new tracks' choice), All (saved as `*`) or a port; and a `field` theme colour behind free-text fields (aliases, track names, section names, text events) so an empty one shows. The Tracks cells and bar now say `Default` and `All`; the rest of step 3 (dim defaults, warning colour, headings, gap) is still to do. Windows Debug passes 905 tests (903, 2 skipped), WSL linux-clang-debug 906 with no warnings; renders of the view and Tracks in both themes looked at. The second trial, the same day, accepted the layout and behaviour, including the hot-plug watcher, the default input and the field backgrounds. Step 3 (the Tracks cells: dim defaults, warning colour, headings, gap) is next, then the small fixes in schedule.md, before linear recording.

**To try:** delete the `set inputs=` and `set port=` lines from `modalrc` (now unknown settings). Start Modal: it should say no MIDI ports are enabled. `, v m`: enable the Xkey input and the loopMIDI output (Enter on E), give them aliases, make loopMIDI the default output (Enter on D), and check Thru and playback reach the host. Plug a device in and out: the list should follow, an absent enabled port saying "not connected" in the warning colour. In Tracks, the choosers should offer only enabled ports, by alias. Restart: the choices should be kept, in `%LOCALAPPDATA%\Modal\ports`.

## Inserting a meter and rebarring, step 1 (13 November 2026; accepted and published)

**Accepted** on trial, 13 November 2026: tried in several places, in Normal and Visual modes and mid-bar, making remainder bars, all as expected.

Designed 13 November 2026 (design.md, Inserting a meter and rebarring; decisions.md). `, b m` (`:meter 3/4`) puts in a meter at the cursor, to the next meter change or the section's end, for what the cursor's track follows, the prompt naming what it will change and the message after where it stops; in Visual mode, `, b m` (`:rebar 3/4`) rebars a `v` range in the arrangement, or sections on the strip, in the song's meter, naming tracks with their own meter, which keep it. Neither moves a note; each is one undo step. Underneath, a new `Song::setMeter` sets the meter over a span for the conductor or one track, section by section: the meter before resumes at the span's end, a remainder bar after it where the span ends inside one of the old bars, so later bars stay where they were. It already serves a track's own meter, for step 2. Rebarring selected tracks on their own (`Ctrl-v`, `V`, the piano roll and event list) says it isn't built yet.

**Validation.** Windows Debug passes 966 tests; WSL linux-clang-debug 967, no warnings; formatting clean. New core tests, at three resolutions: a span rebarred with remainder bars before and after and nothing moved, tracks following it and one with its own meter left alone; a span from inside a bar; a span over two sections, each rebarred apart; one track's meter set in one section only, the other still following; and refusals. Workspace tests driving keys: `, b m` from bar 3 of the demo's Intro, the prompt and message, the Verse's meter untouched, every event where it was, undone with `u`; on the Ostinato, its own meter changed and the song's not; a `v` rebar of bars 2 to 3 naming Ostinato and Clave as keeping theirs; `:meter 5/4`; and a `Ctrl-v` box refused for now.

**To try:** in the arrangement, put the cursor in a bar and `, b m`, reading the prompt, then a meter: the bars should change from there to the next meter change or the section's end, nothing moving, the message saying where it stopped; try it mid-bar in the piano roll. Select bars with `v` and `, b m` to rebar them, the bars after the range keeping their places. On your composition, check the bar lines fall where you want.

## Recording: coverage and hardware trials (12 November 2026; checklist passed 13 November 2026)

**Hardware checklist passed** on Windows and Linux, 13 November 2026, run by Neil: every item, including the `modal-loopback --record` re-runs. Found alongside it, and fixed (accepted on trial and published 13 November 2026): the metronome set to always clicked only while recording, as the player gave the plan the click's port only then; and a new song from the palette kept the old song's play-from marker, now put back at the start for a new or opened song. Each has a test that failed before the fix. Windows Debug passes 957 tests; WSL linux-clang-debug 958, no warnings.

Milestone 8's last item (schedule.md). The audit of recording's tests against the design found the core well covered (the recorder's and count-in's tests all at three resolutions, nearly all the scheduler's, engine's, player's and plan's) and the workspace's end-to-end tests short in places. Added, as workspace tests driving `R` with keys through the engine: a take at 96 PPQN landing on its beat; a take through a tempo change landing where the music is; the marker after a take, back at the record point with rewind and where it stopped without, the next take starting there; two takes in a row, each an undo step, the second replacing the first in replace mode and kept with it in overdub; a pre-roll take, what's played before the record point left out and a note held across it starting there; overdub with a pedal pressed in the count-in, starting at the record point with what was there kept; and two armed tracks recorded at once. All passed as written, finding no fault. Windows Debug passes 955 tests; WSL linux-clang-debug 956, no warnings.

**Hardware checklist**, for Neil on Windows and on the Linux laptop with the UM-ONE. Record each result here, with the date and system.

1. **Recording accuracy, re-run** as the input path changed after October (bounded and fair input taking, input sessions, named inputs, the engine woken at once): as October's were, with the default stream and spin and a Release build: on Windows, with the Babyface's out cabled to its in, `cmake --build --preset windows-msvc-release` then `build\windows-msvc\apps\Release\modal-loopback.exe --port "Babyface" --record`; on Linux, with the UM-ONE's, `cmake --build --preset linux-gcc-release` then `build/linux-gcc/apps/Release/modal-loopback --port "UM-ONE" --record` (`modal-ports` lists the names, which `--port` matches a part of). October's results, for comparison (design.md, Measured recording): Windows all 2,000 notes, 1 to 3 ticks late, median 2; Linux all 2,000, 2 to 5 ticks late, median 3. Expect the same, and nothing lost, out of order or unexpected.
### Results for Windows, 12/11/2026:
Out: Babyface Midi Port 1 1
In:  Babyface Midi Port 1 1

Recording 2000 notes, one every 10 ms, spin margin 1.50 ms...

Sent 4000 messages; received 4000 of them. Lost 0, out of order 0, unexpected 0.

  Latency (due to back) min  1.02 ms  median  1.12 ms  p99  1.27 ms  max  2.76 ms
  Jitter                p99 - median 0.15 ms   standard deviation 0.06 ms
  After the send call   min  0.92 ms  median  1.02 ms  p99  1.17 ms  max  2.51 ms

  Send lateness         99% within  0.20 ms  max 0.37 ms
    waking              99% within  0.05 ms  max 0.12 ms
    sending             99% within  0.20 ms  max 0.36 ms

Recorded 2000 of 2000 notes. Missing 0, extra 0.

  Recorded late by      min  0.52 ms  median  1.04 ms  p99  1.04 ms  max  2.60 ms
    in ticks            min     1     median     2     p99     2     max     5
  Recorded longer by    min -1.56 ms  median  0.00 ms  p99  0.52 ms  max  0.52 ms
  Off arrival time      0 recorded at a tick other than the one they arrived at

### Results for Windows, 12/11/2026
Out: UM-ONE MIDI 1
In:  UM-ONE MIDI 1

Recording 2000 notes, one every 10 ms, spin margin 1.50 ms...

Sent 4000 messages; received 4000 of them. Lost 0, out of order 0, unexpected 0.

  Latency (due to back) min  1.27 ms  median  1.64 ms  p99  2.37 ms  max  2.78 ms
  Jitter                p99 - median 0.72 ms   standard deviation 0.22 ms
  After the send call   min  1.26 ms  median  1.62 ms  p99  2.34 ms  max  2.76 ms

  Send lateness         99% within  0.05 ms  max 0.53 ms
    waking              99% within  0.05 ms  max 0.51 ms
    sending             99% within  0.05 ms  max 0.06 ms

Recorded 2000 of 2000 notes. Missing 0, extra 0.

  Recorded late by      min  1.04 ms  median  1.56 ms  p99  2.08 ms  max  2.60 ms
    in ticks            min     2     median     3     p99     4     max     5
  Recorded longer by    min -1.04 ms  median  0.00 ms  p99  1.04 ms  max  1.04 ms
  Off arrival time      0 recorded at a tick other than the one they arrived at

2. **A take from a keyboard:** arm a track, `R`, play to the click through the count-in and on, Space; play it back and check it sounds as played, and that `u` undoes it.
Verified 13/11/2026
3. **Pre-roll and overdub:** a pre-roll take from bar 5, playing along; then the same bars in overdub, adding to it. In replace mode, check the track goes silent from the record point.
Verified 13/11/2026
4. **Sustain pedal** held across the record point and across the stop: the take should start and end with it, nothing left held.
Verified 13/11/2026
5. **Two tracks armed** on different channels and sounds: one take onto both, each playing its own sound.
Verified 13/11/2026
6. **Unplug the keyboard mid-take,** then stop: the stop message should say the input was lost, and what was played before it kept.
Verified 13/11/2026
7. **A flood:** sweep the mod wheel and pitch bend hard, with aftertouch if the keyboard sends it, through a take. Nothing should be lost; if anything is, `IN` should turn the warning colour and the stop message say how many.
Verified 13/11/2026 - I was unable to get it to the point where anything was detected as lost.
8. **Thru while recording:** whether it feels immediate through the count-in and the take.
Verified 13/11/2026

## Layout tweaks after use (12 November 2026; accepted and published)

**Accepted** on trial, 12 November 2026: the groups apparent, Tracks more balanced, the Ports inspector more solid, and the Metronome page more balanced. The Metronome page's tighter spacing for short windows didn't show in the trial, as the smallest window there left room; it stays as a safeguard for windows of fewer rows.

Agreed 12 November 2026 (schedule.md, milestone 8; decisions.md, Layout tweaks after use). The status line's indicator groups are three cells apart, a cell within. The Tracks view has three blank cells after the Q button and two between Input and Output. The Ports inspector shows Port (padded to the longest known name), then State (`Enabled`, `Disabled`, `Not connected`), then About. The Metronome page has a blank row between its groups and half a row between rows, the half rows and then the blank ones going where the window's too short; Channel's, the notes' and the counts' boxes are the same width; velocities are labelled `Vel`; the Test buttons are chips.

**Validation.** Windows Debug passes 948 tests; WSL linux-clang-debug 949, no warnings; formatting clean. New and changed tests: the Tracks view's gaps after the buttons and between Input and Output; the Ports inspector's Port, State and About headings staying put as the cursor moves along and between rows of ports of different lengths and states. A new `[.sample]` render, `a sample of the Metronome page and the Ports and Tracks views`, draws the three at 1600 by 900 and at the smallest window, looked at in the dark theme.

**To try:** look at the status line's indicators; the Tracks view's ports; the Ports view, moving up, down and along, watching the inspector stay still; and the Metronome page (`, m s`) in a large window and a small one.

## Inputs that can't be used, and input lost, in recording (12 November 2026; accepted and published)

**Accepted** on trial, 12 November 2026, with `ARM` kept red, the warning messages being alarm enough, and published.

Milestone 8's unavailable-input and input-overflow item (design.md, Inputs that can't be used, and input lost). `R` refuses when no armed track can record, naming each and why, and otherwise names those recording nothing in a warning. Input lost during a take puts `IN` in the warning colour until recording stops, and either that or an input going missing is said with `Recorded onto…`, as a warning. The Tracks view's check for a port that can't be used moved to `ports.hpp` (`portTrouble`, with `portTroubleText` for messages), so `R` and the Input column agree. `ARM` was first in the warning colour too; after trying it, it stays red, the pink reading as less serious (decided 12 November 2026).

**Validation.** Windows Debug passes 947 tests; WSL linux-clang-debug 948, no warnings; formatting clean. New tests: `R` refused with one and with two armed tracks whose inputs can't be used, `ARM` staying red, and recording onto the one that can with the other named; a flood of input during a take putting `IN` in the warning colour and the count said as it stops, `IN` clear after; and the default input unplugged mid-take being said as it stops.

**To try:** arm a track and set its input (Tracks, Input column) to a port that's unplugged or disabled: `R` should refuse, saying why. Arm a second track whose input works and press `R`: it should record that one and name the other. While recording, unplug the keyboard, then stop: the stop message should say the input was lost.

## Narrow windows (12 November 2026; accepted and published)

**Accepted** on trial, 12 November 2026, as adapting well to the window's size, and published.

Step 4 of rearranging the status line (design.md, The status line's indicators). The whole line now gives way as one, in the agreed order: the file's name, the input's last message, the section cut short to six characters with `…`, short words for the indicators, the settings, the section, the `IN` light, the meter, then the tempo. The mode, the transport's state, place and time, and the recording group always stay; below about 61 columns even those don't fit, and the line is cut at its end as before.

**Validation.** Windows Debug passes 945 tests; WSL linux-clang-debug 946, no warnings; formatting clean. A new test narrows the window a column at a time from 220 to 61 columns and checks that nothing comes back once gone, that each part goes in its turn, that everything has gone by the narrowest, and that the mode, transport, position and `ARM` stay throughout. The `[.sample]` render now draws the line at ten widths from 1900 down to 600 pixels, with a section named `Introduction` so the cutting shows; it was looked at.

**To try:** widen the window fully, then narrow it slowly, watching each part go in its turn and the transport and recording group staying; rename a section to something long (in the arrangement) to see it cut short with `…` before it goes.

## The left's panels and the grid in its bar (12 November 2026; accepted and published)

**Accepted** on a second trial, 12 November 2026: the chips help the layout, and the STOP/REC block keeps everything in place; published.

**First trial,** 12 November 2026: the grid in the top bar, and its giving way, as wanted; but the transport moved as recording began, and with so many fields packed close the bar's gaps looked like marks on a light strip. Now the transport starts with a block as wide whatever it says (`■ STOP`, `▶ PLAY`, `● COUNT`, `● PRE`, `● REC`, `● ODUB`), and every field on the status line is a chip, its background a little short of the row's top and bottom (the new `kInset` attribute, drawn by the window), with the message row in the bar's colour: chosen from four rendered ways. Windows Debug passes 944 tests (942, 2 skipped), WSL linux-clang-debug 945 (942, 3 skipped), no warnings; new tests check the state block keeps the meter in place stopped, counting in and recording, the status line's fields are inset, and an inset background is drawn short of the cell's top and bottom.

Step 3 of rearranging the status line. The left side's parts each sit on a panel in the `indicator` colour: the transport (the symbol, position and time), the meter, the tempo and the section. Recording shows `● COUNT`, `● PRE`, `● REC` or `● ODUB` as a block lit in `indicator-arm` red before the transport, rather than in the warning colour on the bar. The piano roll's grid has left the status line for the piano roll's top bar, after the output (`Grid: 1/16`), dropped there after the section's bars and before the section when the bar's narrow (the drop priorities renumbered to fit it); the views' `gridStatus` has gone. The status line is now the same in every view.

**Validation.** Windows Debug passes 943 tests (941, 2 skipped); WSL linux-clang-debug 944 (941, 3 skipped), no warnings. A new test checks the transport, meter, tempo and section are on panels and the count-in block is lit red; the grid test now finds it in the top bar and not the status line; the transport tests find the symbol within its panel. Renders of the left side stopped, with a message, counting in, in the piano roll and narrower, and of the piano roll's top bar, were looked at in both themes.

**Tried:** look at the status line as a whole, chips on a band of two rows, in both themes (`:set theme=light`); then the left of the status line in each view, stopped and playing, and record (`R`) through the count-in or pre-roll into recording: each part should sit on its panel without moving, the recording block lit red. In the piano roll, the grid should be in the top bar after the output, changing with `zh`, `zl` and `Ctrl-t`, and gone from the status line; narrowing the window should drop it after the section's bars.

## The message row (12 November 2026; accepted and published)

**Accepted** on trial, 12 November 2026, as feeling more solid and grounded, and published.

Step 2 of rearranging the status line (design.md, The message row). The window has two rows along the bottom: the message row, always there on the new `message-line` panel, and the status line under it. The row holds a message (a warning with a mark at its left edge), or else the freeze choice's or a track move's keys, a hint while a field is edited, what an operator waiting will do, or the Visual selection and its warnings; at its right, the search and its count, keys waiting, and `recording @a`. The `:` prompt is drawn on it; the palette's matches, the hint menu, completions, listings, the freeze choice and long messages stand on it. The status line holds the mode, the transport and place, the piano roll's grid and the indicators, so the indicators give way only to the window's width. The view is a row shorter.

**Validation.** Windows Debug passes 942 tests (940, 2 skipped); WSL linux-clang-debug 943 (940, 3 skipped), no warnings. A new test checks the row is always there just above the status line, holds a long message, a field's hint, a Visual selection and the prompt, and that the status line's indicators stay where they were through each; tests that read messages, hints or the prompt from the status line, or counted the frame's regions or the view's rows, now read the row or allow for it. Renders of the row with a message, a warning, the prompt and Visual mode were looked at in both themes.

**Tried:** edit the count-in on the Metronome page (`, m s`), whose hint pushed the indicators before: it should show on the row above, the indicators staying put. Then look at messages (`u`, `, T a`), a warning (an edit refused while recording), the `:` prompt, `Ctrl-p`'s palette, the hint menu (`,`), a Visual selection, an operator waiting (`d`), keys waiting (`2`), a search (`/`), and `Qa` recording a macro: each on the row, the status line still.

## The status line's indicators (12 November 2026; accepted and published)

**Accepted** on trial, 12 November 2026: the built-in colours rather than the variants, with the click lit in the settings' blue, as it may be left on while composing, and published. Found in the trial: a long hint, such as the Metronome page's `How long the count is, 1-99`, made the indicators give way, as text on the left counts against their room (messages don't). Proposed next, instead of the order first planned: a permanent message row above the status line for everything whose length varies (messages, hints, state, pending keys, the search's count) and the `:` prompt, leaving the status line fixed-width things only; then the left's panels; then narrow windows.

The first of four steps rearranging the status line (schedule.md, milestone 8; design.md, The status line's indicators). The right side is now a row of indicators, each always in its place at a fixed width, dim or lit: `ARM`, `REPLACE` / `OVERDUB`, the count-in, `CLICK`; `THRU`, `AUDITION`, `REWIND`, `Q BYPASS`; `IN` with the last message; the file's name. Eleven theme roles (`indicator`, `indicator-text`, `indicator-lit-text` and one per indicator) are in both built-in themes. As they're wider than what they replace, they give way where the line's too narrow, in provisional steps (the file's name, the input's message, short words, the settings, the `IN` light), measured without any message, which goes above when it doesn't fit; so at 120 columns the file's name has gone and a short message more often goes above. `Armed 1 Replace  Count-in 1 bar`, `Thru off`, `Audition off`, `Quantise bypassed`, `No click` and `Click` are gone from the status line.

Renders (`[.sample]` "a sample of the status line's indicators") show the line as a song opens, armed with audition off, and narrower, in both themes, with two colour variants: the settings in a muted blue, and each indicator its own colour. Every lit indicator shares one text colour, so a variant with dark text on the settings also puts it on the red `ARM`.

**Validation.** Windows Debug passes 941 tests (939, 2 skipped); WSL linux-clang-debug builds without warnings, none failing. New and changed tests: each indicator dim, lit or warning as its state changes (rewind, thru, audition, quantise bypass, arm, replace and overdub, count-in and pre-roll in bars and beats, the click lit, and warning without a port), short words when narrower, and the indicators keeping their places as arming, the record mode, the count-in and audition change; the input light lit and dim with the file's name staying put as the message changes.

**Tried:** look at the status line as a song opens, then arm a track (`, r a`), switch replace and overdub (`, r m`), cycle the count-in (`, m c`), turn audition, rewind and thru on and off (`, T a`, `, T Space`, `, T t`), bypass quantise (`, T q`), and play a note in: each indicator should light and dim in place, nothing moving. Narrow the window to see them give way. Say which colours work, or which of the variants appeals; they're theme roles, so trying others is quick.

## Auditioning notes in the piano roll (12 November 2026; accepted and published)

**First trial,** 12 November 2026: good, but for a 25 ms note on a guitar instrument that went on sounding; Modal sent its note-off 25 ms after the note-on, and the instrument, which gives each note a string, missed it (a piano played it as expected). Auditions now last at least 150 ms, so very short notes are heard too. **Accepted** on a second trial the same day, the note sounding properly on the guitar instrument, and published.

The last of the three steps. `Player::auditionNote` plays one of a track's notes through `Engine::audition` on the track's port and channel, transposed and with its velocity offset as playback would, for its length at its place in the tempo map, at least 150 ms and at most a second. The piano roll asks the workspace (`ViewHost::auditionNote`) as `o` or `O` adds a note (the first of several) and as a single note moves to a new pitch. After each key, the workspace compares the note under the piano roll's cursor, by track, tick and pitch, with the one after the last key, and plays it if it's another, unless the song changed (an edit or undo), Visual mode is on, or it's playing, stopping or recording; switching views notes the note the cursor opens on, so it isn't heard. The `audition` setting (on) and `, T a` (`audition-toggle`) turn it off, with `Audition off` in the status line; grid-advance moved to `, T g`.

**Validation.** Windows Debug passes 940 tests (938, 2 skipped); WSL linux-clang-debug 941 (938, 3 skipped), no warnings. New tests: the player's audition at both PPQNs (port, channel, transpose, velocity offset, the note's length, the 150 ms minimum and the one-second cap, a missing port and a note transposed out of range refused); through the workspace on a fake clock, landing on a note heard once and not again at a key that leaves the cursor there, the next heard; `, T a` turning it off with the status line saying so, the note opened on not heard, a Visual selection silent; `o` and `Alt-k` heard, `Alt-l` and undo not; nothing but the click during a count-in.

**Tried:** in the piano roll, Tab, `j` / `k` and `h` / `l` onto notes, and switch tracks: each note landed on should play once, on the track's sound, for 150 ms to a second, the next cutting it off; moving within a long note shouldn't play it again. `o` and `O` should play the note added, `Alt-k` / `Alt-j` the note at its new pitch; `Alt-h` / `Alt-l` and undo shouldn't. In Visual mode, nothing. While playing or recording, nothing. `, T a` turns it off (`Audition off` in the status line) and on again; `, T g` is now grid advance. `set noaudition` in modalrc should start with it off.

## Recorded tracks silent while replacing (12 November 2026; accepted and published)

The second of the three steps. `PlanOptions` gains `replacing` and `replacingFrom`: those tracks play only what comes before the record point, by where an event is stored and where it plays, notes cut short there and a pedal-up there for each pedal still down. The player fills them from the recorder's tracks (those armed with a usable input, fixed for the pass) and the record mode at its start, so arming another track or switching the mode partway doesn't change what's silent. Finishing a pass now plans the song again for playing, so the tracks are heard again, and the click goes back to how it plays outside recording, even when nothing was recorded and the song didn't change (before, the recording plan, with its click, stayed until the next edit).

**Validation.** Windows Debug passes 935 tests (933, 2 skipped); WSL linux-clang-debug 936 (933, 3 skipped). New tests: the plan for a replaced track at both PPQNs (controller values and pedals before the record point kept, a note cut there, sustain let up there, later events gone, other tracks and overdub as stored); through the workspace on a fake clock, a track with notes early on sends no note-ons while recording in replace, is heard when played after the pass is undone, and is heard while overdubbing.

**Accepted** on trial, 12 November 2026, in replace and overdub, with pre-roll and undo, and published. Next is auditioning notes in the piano roll.

**Tried:** arm a track with notes in replace mode and record over it: from the record point it should be silent, other tracks playing on; with a pre-roll, it should play until the record point. Stop and play: it should be heard again, even after a pass where nothing was played. In overdub, it should be heard while recording. Arm a track whose input isn't connected: it should play on.

## The take shown as it's played (12 November 2026; accepted and published)

The first of three steps agreed on 12 November 2026 (schedule.md, milestone 8): show the take as it's played; then silence recorded tracks in replace mode; then audition notes in the piano roll. Held notes and pedals ending at the stop, and the pass as one undo step, were already built with linear recording, and now have a workspace test too.

- **The preview:** `Document` has a preview layer: `song()` is what the views show, `underlying()` the song that edits, undo, saving, export and the listener use. While recording, the workspace sets the preview to `Player::previewTake`, which is `Recorder::finish` at the playhead, made again each frame of the record phase. Places that copy the whole song and write it back (the Metronome page, `, m m` / `, m c`, Visual put's restore, the search's redo) copy `underlying()`. The preview is made at once and in place, so a reference to `song()` stays good through an edit, as one to the song does.
- **The guard:** `Document::setGuard` refuses an edit, undo or redo whose result it objects to, leaving the song and history as they were, and keeps the reason; `Workspace::report` refuses with it after any command. While recording, it objects to a change to a recorded track's events.

**Validation.** Windows Debug passes 933 tests (931, 2 skipped). New tests: the document's preview (shown, made again on a change, edits and the listener on the song beneath, cleared) and guard (edit, undo and redo refused with the history kept, other changes made); through the workspace on a fake clock, a held note growing in what's shown while the song is untouched, old events going as the playhead passes in replace mode and the rest kept, a note and pedal held at the stop ending there, and edits and undo refused on the recorded track while the next track's edits are kept, with undo afterwards going back through both.

**Accepted** on trial, 12 November 2026, in replace and overdub, and published. Next is silencing the recorded tracks in replace mode.

**Tried:** arm a track with notes on it, in replace mode, and record in the piano roll and the arrangement. Notes should appear as they're played, a held one growing; the old notes should go as the playhead passes; recording past the end should grow the song as it goes. While recording, `x` on the recorded track and `u` should be refused, saying it's being recorded; an edit on another track should work. Stop, and `u` should take the whole pass away. Then the same in overdub, the old notes staying.

## Drop-down lists and the Metronome page after trial (11 November 2026; accepted and published)

**Accepted** on a second trial, 11 November 2026, with the metronome controls and page below, and published; Windows Debug passes 928 tests (926, 2 skipped) at `ee965b0`. Next is the following milestone 8 item in schedule.md.

The first trial of the metronome commits (`26b8a6a`, `1615355`) went well, and asked for these changes:

- **Drop-down lists:** `ChoiceList` (`choice_list.hpp`) is one drop-down list for every field with fixed choices. The Tracks view's port list is moved onto it unchanged, and `InspectorEditor` opens one for any `InspectorField` with `choices`. A short list has no filter: `j` / `k` move and a letter jumps. A port list narrows to what's typed. Every list field shows `▾`.
- **Fields using the lists:** quantise's Moves, and on the Metronome page Plays, Port, Before and Bars/Beats.
- **Metronome page:**
  - A Test button on each click.
  - The count-in and pre-roll are each a number, then a Bars/Beats list.
  - Typed fields have a cell for the cursor.
  - A digit on a number field starts typing it (`View::typeToEdit`).
- **Pre-roll in beats:** `CountInSettings::preRollBeats`, `prerollbeats=` in the song file only when set, and `:preroll 3 beats`.
- **Keys:** `, m p` removed; `, v p` became `, v r`.
- **After a second trial:** list fields have the `field` background, as typed fields do, on the Metronome page, in the inspector and in the Tracks view's Input and Output columns.
- **Ports view:** Enter on a port's detected name does nothing; it used to open the alias's editor out of sight, holding the cursor. The cursor stops at Port, no longer going on to the headingless status.

**Validation:**
- Windows Debug: 928 test cases (926 passed, 2 skipped).
- WSL linux-clang-debug: 926 test cases (923 passed, 3 skipped).
- New tests: choosing from the lists (Escape, letters, `j` / `k`), the port list narrowing and saying nothing matches, Bars/Beats keeping the number, digits starting to type, a three-digit velocity, the Test buttons, quantise Moves, `, v r`, pre-roll in beats in recording, the file and `:preroll`.
- Renders of the page, with the port list open, and of the Tracks view's arrows looked at.

**To try:**
- `, m s`: Enter on Plays, Before, Port and Bars/Beats to see the lists.
- Type a digit on Channel or a velocity without Enter.
- Type `127` into a velocity.
- Press Enter on each Test button.
- Set a pre-roll in beats and record.
- Check quantise Moves (`$`, Enter, Shift-Tab) and the Tracks Output/Input arrows.
- `, v r`.

## Metronome controls and the Metronome page (11 November 2026; accepted and published)

Two commits on `bd459f9`. First: `, m m` cycles the metronome (off, while recording, always), `, m c` what comes before recording (nothing, count-in, pre-roll), `:countin` and `:preroll` set lengths; the engine reports when it's counting in (`EngineStatus::countingIn`), so the status line shows `● COUNT`, `● PRE`, then `● REC` / `● ODUB`, all one width; armed, the right shows what comes before recording (dropped first when the line's full, measured without messages) and `No click` in the warning colour. Second: the Metronome page (`MetronomeView`, `, m s`, `:metronome`, `, m p` on Port), every setting a row edited in place with Up/Down steps, the click's General MIDI drum name beside its note, and a change of note or velocity played through a new engine command, `Engine::audition`, which sends a note and ends it when due (or at the next, panic or quit).

**Validation.** Windows Debug passes 920 tests (918, 2 skipped), WSL linux-clang-debug too: the engine's audition, the `, m` keys and `:countin` / `:preroll` with their errors and undo, `COUNT` then `REC` at the same place, `PRE` through a pre-roll, `No click`, and the page's editing, stepping, choosing the port and hearing a changed click. Dark and light renders of the page looked at.

**To try:** `, m s`: change the downbeat's note with Enter then Up and Down, hearing each; set the port to your drum plugin's output (`, m p`); set a 2-bar count-in, record, and watch `● COUNT` give way to `● REC` without the transport moving; try a pre-roll (`, m c`) and see `● PRE`. Unset the default output in `gm` and check `No click` shows while a track is armed.

## Linear recording in the GUI (11 November 2026; accepted and published)

`Player::record` plans the pass with the metronome (the song's metronome port, or the default output) and the song's count-in or pre-roll, starts the engine and keeps a `Recorder`, fed by `takeInput`; recording carries on past the song's end. When the engine says where it stopped, the workspace applies the take to the song as it is then (`Recorder::finish(Song, Tick)`) as one undo step, `Record onto NAME`. `R` records (refused with nothing armed, while playing, or already recording), `, r m` switches the song's `recordMode` (saved as `record mode=overdub`), and the status line shows `● REC` / `● ODUB` in place of the transport's symbol, and `Armed N Replace` beside the `In` light while stopped. The recorder now takes an empty input as the default input and `*` as every input, as the plan does.

**Validation.** Windows Debug passes 912 tests (910, 2 skipped): workspace tests through the engine on a fake clock for recording after a count-in that clicks on channel 10, the take as one undo step, replace and overdub, the refusals, stopping in the count-in, and recording past the song's end; the recorder's inputs by default, All and name; the record mode's file line and its errors.

**Trial, 11 November 2026:** recording, replace and overdub, undo and recording past the end worked with the keyboard. Fixed after it: the arrangement didn't show the take until shown again (the take is put in outside a command, so the views weren't told); `R` with nothing armed now says `, r a` arms "a track", as "this one" made no sense from the strip; the `, r` menu's titles say they toggle. No click was heard at first because no default output was set, so Modal said there'd be no click; with one set, the count-in and click reached the host. Accepted.

**Next:** the remaining milestone 8 items in schedule.md, starting with the GUI metronome controls, count-in and pre-roll.

## Small fixes from the 8.2 trial (11 November 2026; accepted and published)

Three commits on `84d2cf4`: the `In` light shows the last channel message in but a note-off, with its channel, always 13 characters (`In  1 C4      96`, `In 10 CC64   127`, `Prog`, `Pres`, `PPrs`, `Bend +8191`), the player keeping the message (`Player::lastInput`) and `inputText` formatting it; Tab completes a setting's value in quotes when it has spaces (`set ownportname="My Modal"`); `:write-config modalrc!` is refused, saying `!` goes after the command. The wider light leaves less room at the status line's left, so a 60-column test of short messages now runs at 70.

**Validation.** Windows Debug passes 908 tests (906, 2 skipped), WSL linux-clang-debug 909: the player's kept message by kind, `inputText` for each kind and its width, the status line keeping still across messages, completion's quoting and the refused `!`.

**Accepted** on trial with the keyboard, and published. Next is linear recording (schedule.md, milestone 8).

## The Ports view, step 3 (11 November 2026; accepted and published)

The Tracks cells, as design.md (Ports, The Tracks choosers) has them: the heading **Output** (was Output port), with three blank cells before Input; `Default` and `All` dim; a port that can't be used in the warning colour (was dim), the inspector saying why: not found, disabled (and `(disabled)` in the chooser), no default set, the default not found or disabled, and for All no input enabled or none of them connected. The cells now ask the ports table (`TracksView::portTrouble`) rather than the player's missing names. Also `gm` opens the Ports view, as asked during the work.

**Validation.** Windows Debug passes 906 tests (904, 2 skipped): a new Tracks test covers each colour, inspector warning and the `(disabled)` label; the two transport tests of missing ports now use a ports table; the Ports view test opens it with `gm`. Dark and light renders of Tracks with ports set (`-tracks-ports`) were looked at.

**After trying it, the same day:** accepted. Asked for then, and done: the Input cell's About text says `Input:` as Output's says `Output:`; Input comes before Output, the three cells between them; the bar says `Output:` (was `Port:`), and the inspector `Output not found` / `Output disabled`. The bar, two characters longer, leaves out the section at 120 columns, so the bar test runs at 124. Windows Debug passes 906 (904, 2 skipped); renders looked at.

**Accepted** after trying the swap, and published; the Ports view is done. Next are the small fixes from the 8.2 trial in schedule.md, then linear recording.

## Next

Step 8.2 is published and passed CI (`69a833a`). **Hardware checks on Windows, 10 November 2026** (Xkey keyboard, loopMIDI to a plugin host; `modal.exe` from `69a833a`, Windows Debug passing 890 tests): the `In` light and last note work in every view; per-track Input, Thru, and changing `inputs` while a note and pedal are held all work, with a warning for an unmatched name. Thru first appeared not to work: Modal was listening to every input, loopMIDI's included, so Thru fed back through it, and loopMIDI had been started after Modal; with `inputs` set to the keyboard and loopMIDI started first, playback and Thru reached the host. The follow-ups found are in schedule.md's milestone 8 list: a Ports view was agreed afterwards to replace the `inputs` and `port` settings (design.md, Ports), before linear recording, and takes in several of them. No timing was measured. The next scheduled implementation is linear GUI recording in [schedule.md](schedule.md#8-basic-gui-recording); the duplicate-port-name issue below is a follow-up to resolve first. The completed hardening is published through `7a77fb6` and passed [CI on Linux and Windows](https://github.com/neildurant/modal/actions/runs/42849396506). Its input/monitoring behaviour remains agreed in [design.md](design.md#per-track-input-and-monitoring). Broader editor refactoring is separate from the approved hardening sequence.

The documentation ownership cleanup on 10 November 2026 removed parallel milestone/progress lists from design.md and this handoff. Historical decisions were preserved in decisions.md; unresolved design choices remain in design.md. The schedule separates delivered input/Arm/Thru work from step 8.2 and records the already-delivered program markers and ratio/percentage stretch transform. This changes no agreed feature order. All 80 settled checklist entries were preserved; document links/anchors, numbering, the unscheduled list, formatting and diff checks were verified.

## Contribution environment checked on 5 November 2026

- This session runs on Linux (Ubuntu 26.04), with GCC 15.2, Clang 21.1.8, CMake 4.2.3, Ninja 1.13.2, clang-format 22.1.8 and GitHub CLI 2.46.0. The required Linux development packages are installed.
- vcpkg is installed at `/home/ndurant/vcpkg`, checked out at the manifest's baseline `357b1a6f8343064ca982a20771e420fcecfe7efc`. `VCPKG_ROOT` is set in `.zshenv` (including non-interactive Zsh sessions) and `.bashrc`. All four Linux configure presets are ready under `build/`, with the pinned dependencies installed.
- **Setup baseline at `887c49d`:** GCC Debug and Release, Clang Debug and Release, ASan/UBSan and ThreadSanitizer passed through `tools/check-linux.sh`, with four build and test jobs. Each configuration registered 703 tests; three platform/MIDI tests were skipped, with no failures. Whole-tree formatting also passed. The expanded suite's results are above. Tests include SDL offscreen rendering; real desktop and MIDI hardware checks remain pending.
- **Sandbox details:** Catch2's discovery script prefers `XDG_RUNTIME_DIR` over `TMPDIR`; `/run/user/1001` is read-only in the sandbox. Clear `XDG_RUNTIME_DIR` and set `TMPDIR=/tmp` for build scratch files. Initial sandbox attempts also hit LeakSanitizer's restriction on running under tracing. The affected checks passed when rerun outside the sandbox; no sanitizer checks were disabled.
- **Build policy agreed November 2026:** Codex runs native Linux builds and tests and skips local Windows builds. CI continues checking Windows. AGENTS.md now records this; a completed native Clang check satisfies the equivalent WSL check.
- **GitHub access verified:** the repository API reports push permission, a dry-run push to `main` succeeded, and CI results are accessible through `gh`. CI for `887c49d` passed. No changes were pushed during setup.
- The contribution setup changed only AGENTS.md and this status page. No product code changed. There are no outstanding Linux compiler or sanitizer checks for the code at `887c49d`.

## Things to know

- **The grammar** is described as it is in [grammar.md](grammar.md); the reasons for it are in [decisions.md](decisions.md). design.md's Editing grammar section holds only its principles and what's still to come. The remaining feature sections retain some dated context; work order and progress are maintained only in schedule.md.
- **Commands** are registered with a `CommandInfo` (name, description, per-view descriptions in `here`, where it's offered in the `where` table in `src/ui/workspace_commands.cpp`), bound in `assets/config/modalrc`, and run by the `Workspace` or the view. A key bound to several commands means the one offered where the cursor is.
- **Refusals:** a command that doesn't do what was asked calls `refuse()` (which stops a macro); `setMessage()` only reports.
- **Visual selections made by an object** remember it (`Selection::object`), and operators on them run as that operator with that object; a table-driven test in `tests/ui/visual_test.cpp` checks `v{count}{object}d` against `d{count}{object}` everywhere.
- **Strip-only commands** carry a `-strip` suffix (`section-join-strip`), so the generic ones (`section-join`, under `, s`) can work from any view.
- **Building on Windows:** Neil's machine has the WSL distro `Ubuntu-24.04` for the Linux builds (`/opt/vcpkg` there). See AGENTS.md for the checks before pushing.

## CI follow-up for input replacement (10 November 2026)

[CI for `76d8ba0`](https://github.com/neildurant/modal/actions/runs/42798631597) passed every Linux job, but MSVC rejected an implicit conversion from the pedal test's integer initializer list to `uint8_t` (C4244, warnings treated as errors). The regression now uses an explicitly typed `std::array<std::uint8_t, 3>`; no product behaviour changed. Native GCC Debug and Clang Debug both pass all 861 registered tests after this correction, with the same expected platform/MIDI skips; whole-tree formatting and `git diff --check` pass. The correction is isolated from the local documentation-ownership cleanup. [CI for `da765fd`](https://github.com/neildurant/modal/actions/runs/42800733689) passed every Linux and Windows job.
