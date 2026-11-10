# Status

Where Modal stands, for whoever picks up the work next. Keep it current: update it as work lands, and when a session ends.

*Last updated 9 November 2026: milestone 8 has started. Its first step (MIDI input, arming and Thru in the GUI) passed its UI trial, with a MIDI keyboard on Windows; a change to the status line awaits its trial; see Basic GUI recording, below. The ongoing plan is in [schedule.md](schedule.md). Current cross-platform results are in [CI for main](https://github.com/neildurant/modal/actions/workflows/ci.yml?query=branch%3Amain).*

## Milestones

| Milestone | State |
| --- | --- |
| 1. Engine and MIDI layer | Done; timing measured on Windows and Linux |
| 2. Recording | Done; measured on Windows and Linux |
| 3. Grid renderer | Done, except the Linux checks |
| 4. Event list and core grammar | Done, except the Linux checks |
| 5. Piano roll and arrangement | Done, except the Linux checks |
| 6. Full grammar | Done (November 2026), except the Linux checks |
| 7. Tracks and playback transforms | Done; Tracks steps and Freeze complete, UI trials confirmed |
| 8. Basic GUI recording | Doing; step 8.1 (input, arming, Thru) done and tried |

Milestone order and To do/Doing/Done item states are maintained in [schedule.md](schedule.md). Behaviour and open design questions remain in [design.md](design.md).

## Pending

- **The Linux checks** for milestones 3 to 6, on Neil's laptop, under X11 and Wayland: the window and its resizing, display scaling, text input (dead keys, AltGr), Ctrl and keypad keys, and the views. Neil runs these.
- **A two-display check on Windows**: dragging the window between displays with different scaling.

## Review findings

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
- **Prompts on a row of their own (built, awaiting its trial):** `:`, `/`, questions and the palette's line go on a row above the status line, which nothing covers. The palette is top down, its line at the top, the best first, a fixed `palettelines` high (10) with a count (`3/12`); completion's matches are a popup over the word, the first nearest the line, Up going on; after a first try, Down from none chosen starts at the top one shown, and the arrows stop at the ends. Next: the piano roll's grid to the status line and its new-note defaults to the inspector.
- **Commands:** `, r a` (`track-arm-toggle`) and `, r t` (`track-thru-toggle`) from any view with a track, and `tracks-arm-toggle` / `tracks-thru-toggle` on `V` rows; Enter on the buttons too. Arming while others are armed says "Armed: X; also armed: Y" as a warning.
- **Tests:** Windows Debug passes (851 tests, 2 platform skips), after the first-look changes too: arming messages and undo, Visual rows, Thru's look and messages, the inspector's descriptions for every column and no name there, the gap before Quantise, input taken on waking and at the settle deadline, and lost input reported once. Tracks tests now go to columns by field rather than by counts. The dark and light Tracks samples were rendered offscreen and looked at; the light theme's arm/thru colours were separated after that.
- **Not checked:** the exclusion of Modal's own ALSA ports, which needs native Linux (no ALSA sequencer in WSL or CI). Note that listening to every input can loop Thru round a loopback port, such as loopMIDI on Windows, if a track also plays to it; the `inputs` setting then names the ports to use.

## Next

Next is the trial of the prompt row, then the piano roll's grid and new-note defaults (agreed 10 November 2026), then step 8.2: each track's own input, input activity and controls for trying an instrument. Linear recording with `R`, the metronome, count-in and pre-roll follow. Step entry and loops remain later milestones.

Controller lanes, drum maps and folding follow in milestone 9, including folding to named drum-map rows even without events. Multiple songs, backups, jump history and paste history follow in milestone 10. Outline and other additional views move later. MIDI clock output and transport/sync are now explicitly unscheduled, alongside the other someday maybe items. Column configuration and song-tempo fallback wait for polish. No whole-track Loop column is planned. The recent playback features still need the real MIDI trials noted above.

Schedule documentation checks: local Markdown links, the milestone sequence, checkbox states, whole-tree formatting and `git diff --check` pass. This change only updates documentation; no application builds or tests were rerun.

## Contribution environment checked on 5 November 2026

- This session runs on Linux (Ubuntu 26.04), with GCC 15.2, Clang 21.1.8, CMake 4.2.3, Ninja 1.13.2, clang-format 22.1.8 and GitHub CLI 2.46.0. The required Linux development packages are installed.
- vcpkg is installed at `/home/ndurant/vcpkg`, checked out at the manifest's baseline `357b1a6f8343064ca982a20771e420fcecfe7efc`. `VCPKG_ROOT` is set in `.zshenv` (including non-interactive Zsh sessions) and `.bashrc`. All four Linux configure presets are ready under `build/`, with the pinned dependencies installed.
- **Setup baseline at `887c49d`:** GCC Debug and Release, Clang Debug and Release, ASan/UBSan and ThreadSanitizer passed through `tools/check-linux.sh`, with four build and test jobs. Each configuration registered 703 tests; three platform/MIDI tests were skipped, with no failures. Whole-tree formatting also passed. The expanded suite's results are above. Tests include SDL offscreen rendering; real desktop and MIDI hardware checks remain pending.
- **Sandbox details:** Catch2's discovery script prefers `XDG_RUNTIME_DIR` over `TMPDIR`; `/run/user/1001` is read-only in the sandbox. Clear `XDG_RUNTIME_DIR` and set `TMPDIR=/tmp` for build scratch files. Initial sandbox attempts also hit LeakSanitizer's restriction on running under tracing. The affected checks passed when rerun outside the sandbox; no sanitizer checks were disabled.
- **Build policy agreed November 2026:** Codex runs native Linux builds and tests and skips local Windows builds. CI continues checking Windows. AGENTS.md now records this; a completed native Clang check satisfies the equivalent WSL check.
- **GitHub access verified:** the repository API reports push permission, a dry-run push to `main` succeeded, and CI results are accessible through `gh`. CI for `887c49d` passed. No changes were pushed during setup.
- The contribution setup changed only AGENTS.md and this status page. No product code changed. There are no outstanding Linux compiler or sanitizer checks for the code at `887c49d`.

## Things to know

- **The grammar** is described as it is in [grammar.md](grammar.md); the reasons for it are in [decisions.md](decisions.md). design.md's Editing grammar section holds only its principles and what's still to come. design.md's other sections (Views, MIDI transforms and the rest) still mix their current spec with dated decisions; splitting them the same way is open, if wanted.
- **Commands** are registered with a `CommandInfo` (name, description, per-view descriptions in `here`, where it's offered in the `where` table in `src/ui/workspace_commands.cpp`), bound in `assets/config/modalrc`, and run by the `Workspace` or the view. A key bound to several commands means the one offered where the cursor is.
- **Refusals:** a command that doesn't do what was asked calls `refuse()` (which stops a macro); `setMessage()` only reports.
- **Visual selections made by an object** remember it (`Selection::object`), and operators on them run as that operator with that object; a table-driven test in `tests/ui/visual_test.cpp` checks `v{count}{object}d` against `d{count}{object}` everywhere.
- **Strip-only commands** carry a `-strip` suffix (`section-join-strip`), so the generic ones (`section-join`, under `, s`) can work from any view.
- **Building on Windows:** Neil's machine has the WSL distro `Ubuntu-24.04` for the Linux builds (`/opt/vcpkg` there). See AGENTS.md for the checks before pushing.
