# Status

Where Modal stands, for whoever picks up the work next. Keep it current: update it as work lands, and when a session ends.

*Last updated 5 November 2026, while addressing the review findings after the handoff push `fd81caa`.*

## Milestones

| Milestone | State |
| --- | --- |
| 1. Engine and MIDI layer | Done; timing measured on Windows and Linux |
| 2. Recording | Done; measured on Windows and Linux |
| 3. Grid renderer | Done, except the Linux checks |
| 4. Event list and core grammar | Done, except the Linux checks |
| 5. Piano roll and arrangement | Done, except the Linux checks |
| 6. Full grammar | Done (November 2026), except the Linux checks |
| 7. More for the views, and several songs | Next: not started |

What each milestone holds is in [design.md](design.md), under Milestones.

## Pending

- **Five findings from the completed review of `30f6301..64c0fce`, tracked below.** All six findings from the preceding review passed their reproductions. Address the remaining findings before starting milestone 7. Local fixes still need the final Linux checks and a UI trial before pushing.
- **The Linux checks** for milestones 3 to 6, on Neil's laptop, under X11 and Wayland: the window and its resizing, display scaling, text input (dead keys, AltGr), Ctrl and keypad keys, and the views. Neil runs these.
- **A two-display check on Windows**: dragging the window between displays with different scaling.

## Review findings

1. **Fixed locally: `gv` restores the state needed to replace an object.** It now remembers the selection before its object as well as the selection and cursor. A regression test first reproduced whole-track deletion with `vit<Esc>gvibd` and `v2it<Esc>gvibd`, and after yanking instead of Escape; all now delete only the first bar's contents on the original track. The focused GCC Debug test passes at both PPQNs and checks undo.
2. **A refused Visual put can delete the selection and report success.** Yank C4, then put over a selected D4 where an unselected C4 already occupies the destination. The selection is deleted before the paste is refused for overlap. `Workspace::putOverSelection()` needs to validate the whole replacement or roll it back, and preserve the refusal message. The partial edit predates the reviewed changes; the unconditional success message is new.
3. **Colon transforms do not use the exact object selected.** In the event list, select a chord whose note rows surround a controller row. `vic!f`, answering `cc1`, leaves that controller alone; `vic:filter cc1` deletes it. `Workspace::transformLine()` uses the bounding Visual rows instead of the remembered object, and the displayed selection includes the intervening row too.
4. **A successful warning can still stop a macro.** A section split on a track's bar line that cuts a conductor bar short succeeds with `CommandResult::warning`, which is still treated as a refusal. For example, `:section-split Middle<Enter>4l` stops after the split when it produces that warning. Command outcome still needs separating from message severity in `CommandResult` and its consumers.
5. **grammar.md describes `0` and `$` too broadly.** The arrangement uses the song's first and last bars, the section strip its first and last sections, the piano roll the current bar's bounds, and the event list its fields. The motion table should distinguish these, especially because the motions can follow destructive operators.

## Next

Resolve the open review findings. Then milestone 7: the outline view, ghost notes, split view, controller lanes beyond velocity, drum maps and folding, and several songs open at once. Start by proposing a plan, raising its "To decide" items in design.md (ghost note colours, split panes, folding, the drum map format, the outline's `J`) first.

## Contribution environment checked on 5 November 2026

- This session runs on Linux (Ubuntu 26.04), with GCC 15.2, Clang 21.1.8, CMake 4.2.3, Ninja 1.13.2, clang-format 22.1.8 and GitHub CLI 2.46.0. The required Linux development packages are installed.
- vcpkg is installed at `/home/ndurant/vcpkg`, checked out at the manifest's baseline `357b1a6f8343064ca982a20771e420fcecfe7efc`. `VCPKG_ROOT` is set in `.zshenv` (including non-interactive Zsh sessions) and `.bashrc`. All four Linux configure presets are ready under `build/`, with the pinned dependencies installed.
- **Validation passed:** GCC Debug and Release, Clang Debug and Release, ASan/UBSan and ThreadSanitizer, through `tools/check-linux.sh`, with four build and test jobs. Each configuration registers 703 tests; three platform/MIDI tests are skipped, with no failures. Whole-tree formatting also passes. Tests include SDL offscreen rendering; real desktop and MIDI hardware checks remain pending above.
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
