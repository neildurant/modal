# Status

Where Modal stands, for whoever picks up the work next. Keep it current: update it as work lands, and when a session ends.

*Last updated 6 November 2026: the first Tracks view implementation is built locally; all six Linux configurations pass and the UI trial is pending. Nothing from this step has been pushed.*

## Milestones

| Milestone | State |
| --- | --- |
| 1. Engine and MIDI layer | Done; timing measured on Windows and Linux |
| 2. Recording | Done; measured on Windows and Linux |
| 3. Grid renderer | Done, except the Linux checks |
| 4. Event list and core grammar | Done, except the Linux checks |
| 5. Piano roll and arrangement | Done, except the Linux checks |
| 6. Full grammar | Done (November 2026), except the Linux checks |
| 7. More for the views, and several songs | First Tracks view step built locally; UI trial pending |

What each milestone holds is in [design.md](design.md), under Milestones.

## Pending

- **Tracks view UI trial:** the initial settings, whole-track clipboard and timing inspector below. Keep these changes local until tried, as AGENTS.md requires.
- **The Linux checks** for milestones 3 to 6, on Neil's laptop, under X11 and Wayland: the window and its resizing, display scaling, text input (dead keys, AltGr), Ctrl and keypad keys, and the views. Neil runs these.
- **A two-display check on Windows**: dragging the window between displays with different scaling.

## Review findings

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

Built locally as the first step of milestone 7: `40a8f84` adds the core support and `3030d68` the view, editing and timing inspector. The agreed design is in design.md and decisions.md; keys and editing are in grammar.md. These commits have not been pushed.

- The table exposes number, name, mute, solo, output port, channel, transpose, velocity offset and time offset; the inspector also edits note chasing. Stable field identities, shared formatting/editing and one ordered column list keep later column visibility or reordering separate from track state.
- `gt` and `, v t` open Tracks; `3gt` chooses track 3. Ctrl-PageDown / Ctrl-PageUp switch tracks. Musical position and pitch are retained when switching views.
- `V` selects whole rows. Enter assigns a property across them, steps change each relative to its own value, and the batch validates and undoes together. Names are edited individually. `Vy` / `Vjy` copy whole tracks; Normal `p` / `P` insert copies after/before, Visual `p` / `P` replace selected rows, and `Vd` deletes explicitly selected tracks. Copying preserves settings, routing, mute/solo, editing defaults, section events and per-section meter ownership, with fresh IDs and unique names. Marks are not duplicated. Named registers also support whole tracks.
- Offsets retain milliseconds or musical ticks through save/load and recording/playback. Inputs include `-40ms`, `+120t` and `+1/16-22t`; the agreed bounds are ±1000 ms or ±64 whole notes. Musical offsets rescale with PPQN and follow tempo changes at both note ends.
- The piano-roll and event-list inspectors show coarse/fine timing below exact values. `timingunit=grid` is the agreed default; a fixed fraction also works without moving notes. `e` edits the inspector; event-list Enter retains cell editing, as does `e` on conductor rows.
- The first whole-track clipboard requires matching PPQN and section names, order and lengths. Incompatible pastes are refused before changing the song. Wider mapping between songs remains future work.
- Playback fixes found while exercising track settings: muting/rerouting the last track on a channel releases held pedals, and rerouting ends its old notes. A channel shared by another audible track keeps its pedal state. Detaching a stopping player also clears the view's playback state, avoiding a crash on a later view switch/wake.

### Validation

- The full `tools/check-linux.sh` passed on the completed implementation: GCC Debug/Release, Clang Debug/Release, ASan/UBSan and ThreadSanitizer. Each configuration registers 735 tests, with three platform/MIDI tests skipped and no failures. Whole-tree formatting and `git diff --check` also pass.
- `build/linux-gcc/apps/Debug/modal` has been rebuilt after the implementation commits and checked to be newer than every application build input. The UI trial below remains pending; nothing from this step has been pushed.
- Hidden offscreen sample renders pass for Tracks, the event list, arrangement and piano roll, including dark/light themes. Tracks and piano-roll images were inspected; this caught and fixed initial horizontal scrolling that hid the track-number column.
- No desktop automation or MIDI hardware tests have been run. Local Windows builds are skipped under the agreed policy; CI follows after the UI trial and push.

### What to try

Run `build/linux-gcc/apps/Debug/modal --demo` (or open a song).

1. `gt`, then `j/k` and `h/l`; Enter edits a field. Try mute/solo, channel, transpose and velocity offset. Space plays, `ga/ge/gr` switch views, Ctrl-PageDown / Ctrl-PageUp switch tracks, and Backspace returns. Resize the window with the time-offset column selected.
2. On time offset (`$`), enter `+1/16-22t`, then `-40ms`; undo and redo, save and reopen. Confirm that stored note positions remain unchanged. Real MIDI output is useful for auditioning these settings, but has not been measured here.
3. `Vy`, then `p` or `P`, and undo. Try `Vjy` for two tracks and `Vp` for replacement. In a numeric column, `Vj` then Enter or `+` edits both selected tracks in one undo step. Copies preserve routing, mute and solo as agreed.
4. In the piano roll or event list, `e` and Tab through to the Timing row. Edit Start step/fine and Length steps/fine; undo. Compare `:set timingunit=1/16` and `:set timingunit=grid`. Check the extra inspector row feels useful and leaves enough editing space.

## Next

After the Tracks UI trial, address feedback, push the passing local step and watch CI. Then settle the next step's open questions before implementation:

1. Numeric bank/program setup: display conventions, playback setup and event precedence, live changes and conflicts on shared channels. Instrument program tables with names are future work.
2. Playback quantise: independent grid, note lengths, pedals, live changes, bypass and committing.
3. Freeze is agreed in principle, with scope, out-of-range results, negative offsets, section boundaries and rounding still open. Column configuration and song-tempo fallback are separate open questions. No whole-track Loop column is planned.

The rest of milestone 7 remains the outline view, ghost notes, split view, controller lanes beyond velocity, drum maps and folding, and several songs open at once. Raise each feature's open questions before implementing the parts they affect.

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
