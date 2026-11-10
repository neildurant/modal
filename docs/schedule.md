# Schedule

The ongoing plan for Modal, agreed 9 November 2026. This is the source of truth for milestone order and work status. [design.md](design.md) specifies behaviour, [decisions.md](decisions.md) records reasons, and [status.md](status.md) holds the current handoff and validation results.

## Keeping this plan current

Use standard Markdown checkboxes with an explicit state:

- [ ] **To do** — scheduled, not started.
- [ ] **Doing** — actively being worked on; add a short progress or blocker note.
- [x] **Done** — implemented and validated, with any required UI trial accepted. Record evidence in status.md.

Change an item's state when work starts or finishes, and update status.md in the same change. Split large items into reviewable steps before implementation. Keep completed items here as history. Work on one milestone at a time; agree changes to scope or order before recording them here. A milestone is complete when its items are Done and required checks are complete; state outstanding manual checks explicitly. Future items remain To do until work actually starts. Someday maybe items are deliberately unscheduled, not the next tasks in a queue.

## Completed foundations

- [x] **Done** — 1. Engine and MIDI layer; timing measured on Linux and Windows.
- [x] **Done** — 2. Recording engine and command-line recorder; measured on Linux and Windows.
- [ ] **To do** — 3–6. Finish the outstanding manual desktop checks; the grid renderer, editing views and full grammar are implemented. Linux X11/Wayland checks and Windows mixed-display scaling checks remain; see status.md.
- [x] **Done** — 7. Tracks view: settings, whole-track copying, musical offsets and timing inspector, bank/program setup, track reordering, playback quantise and Freeze. UI trials accepted.

## 8. Basic GUI recording

First make it possible to record new material in Modal for daily use. Reuse the measured engine and recorder; step entry, loops and take management follow later.

- [x] **Done** — Fix input replacement so held Thru notes/pedals are released and the engine acknowledges reset before IDs are reused; all six Linux configurations pass and the functional trial was accepted on 10 November 2026.

- [x] **Done** — Step 8.1: GUI MIDI input and the machine's input-port selection, independent track arming, per-track Thru while stopped or playing, and Arm/Thru buttons in Tracks. Tried and published; validation is in status.md.
- [x] **Done** — Before 8.2: fix moved Visual selections, including off-grid snapping and separate undo steps for separate selections; all six Linux configurations pass, and the real-window trials were accepted on 10 November 2026.
- [x] **Done** — Before 8.2: extract input-port ownership, ID mapping and asynchronous reset into a component tested with fake ports; six focused tests and GCC/Clang Debug pass.
- [x] **Done** — Before 8.2: bound and fairly schedule engine input processing, preserve SysEx/recording stamps and keep collecting until the backlog is drained; all six Linux configurations pass.
- [x] **Done** — Step 8.2: each track's input selection and Input column, input activity/last-note display, and the machine-wide Thru control. Behaviour is agreed in [design.md](design.md#per-track-input-and-monitoring); all six Linux configurations pass; trial accepted and publication authorised on 10 November 2026; the hardware checks deferred then were done on Windows on 10 November 2026 (see status.md).
- [ ] **To do** — Follow up the duplicate MIDI input names reported during the 8.2 trial; agree distinguishable port labels before implementation (diagnosis in status.md).
- [ ] **To do** — Found in the 8.2 hardware trial on Windows (10 November 2026), to settle once the trial is finished: (a) guard against Thru feedback through a virtual port such as loopMIDI, where listening to every input also hears the port tracks play to (suspected cause of Thru not reaching a plugin host; confirm first); (b) make the `In` light fixed-width so the status line doesn't move, and consider showing the last message of any type but note-offs, with its channel (`In  1 C#4  127`, `In 10 CC64 127`, `Bend`, `Prog`, `Pres`); (c) have `write-config` refuse a file name ending in `!`, saying `!` goes after the command (`:write-config! modalrc`), since `:write-config modalrc!` wrote a file called `modalrc!`.
- [ ] **To do** — Also from the 8.2 trial (10 November 2026), agreed: (d) the `inputs` setting's description and configuration.md give examples of several ports, one quoted string with commas (`set inputs="Xkey 1, loopMIDI Port 2"`); (e) fix completing a setting's value that has spaces, which went in unquoted (`set inputs=Xkey 1 `, read as `inputs=Xkey` and a setting `1`), and have `inputs=` offer the input ports, again after each comma, and `port=` the output ports; (f) in both port columns, a port saved with the song but not found shows in the warning colour, as the chooser already shows it, and the defaults, `Default` (not blank) and `All` (was `All inputs`, the heading saying what it's all of), show dim, as following the default rather than set on the track; the choosers say `Default` and `All` too; (g) three blank cells after the Output column, as its longest name otherwise runs into Input; (h) the headings become `Output` and `Input`, the inspector's About still saying Output port and Input port. Also noted: start loopMIDI before Modal and the plugin host, as Modal keeps the port it first opened.
- [ ] **To do** — Proposed after the 8.2 hardware trial (10 November 2026), to confirm: at the `:` prompt, refuse `set inputs=` when any name matches no port, keeping the old value and saying which ("No input port matches 'Xkye'; inputs left as 'Xkey 1'"), as a typo is likely there; the same for `set port=`. In `modalrc` at startup, keep accepting with a warning, since a device may be off, and dropping the setting would fall back to every input and risk Thru feedback through a virtual port.
- [ ] **To do** — Linear recording into armed tracks, replace or overdub, starting at the chosen record position and continuing beyond the song's end; clear recording state in the transport/status line.
- [ ] **To do** — GUI metronome controls, count-in and pre-roll; distinguish playback start from recording start.
- [ ] **To do** — Finish held notes and pedals correctly; show recorded material immediately; one pass, including song extension, is one undo step.
- [ ] **To do** — Complete unavailable-input and input-overflow handling for recording, building on the existing input-loss warning and safe stop/panic.
- [ ] **To do** — Save the remaining track and song recording settings.
- [ ] **To do** — Automated coverage and real MIDI trials for the remaining recording features; preserve measured results in status.md.

The input foundation precedes step entry and loops. Behaviour and unresolved recording choices are in [design.md](design.md#input-and-recording) and [To decide](design.md#to-decide). Related engineering work is listed below.

## 9. Controllers, drums and folding

- [ ] **To do** — Editable lanes for CCs, pitch bend and aftertouch, with selections, transforms and search highlighting.
- [ ] **To do** — Switch-controller regions for sustain, sostenuto, soft pedal and similar controllers; preserve imported half-pedal values.
- [x] **Done** — Program-change event markers in the piano-roll ruler, delivered with bank/program setup; validation and the accepted trial are recorded in status.md.
- [ ] **To do** — General MIDI and reusable custom drum maps; names in the piano roll, inspector, navigation, search, selection and palette.
- [ ] **To do** — Folding modes for occupied pitches, named drum-map pitches even when empty, and their union; whole-track or current-section scope.
- [ ] **To do** — Pitch-row selection and direct navigation to named pitches.
- [ ] **To do** — Improve the temporary `, e` event-insertion interface alongside the new editing routes.

Drum maps precede folding by named rows. Settle map storage, lane navigation and folding behaviour before their implementation. Several simultaneous controller lanes are scheduled with the later view work.

## 10. Songs, navigation and copying

- [ ] **To do** — Multiple open songs, buffer switching and buffer list/open/close commands.
- [ ] **To do** — Independent document, cursor, view and undo state per song; explicit playback behaviour when switching.
- [ ] **To do** — Shared registers for events, bars, tracks and sections copied between songs.
- [ ] **To do** — Cross-song mapping for different section layouts, meters, tempos and PPQN.
- [ ] **To do** — `:read FILE` to bring tracks into the current song.
- [ ] **To do** — Numbered backups and safe save/close behaviour across modified buffers.
- [ ] **To do** — Jump history across positions, views and buffers.
- [ ] **To do** — Paste history, including cycling the material just put.
- [ ] **To do** — Related numbered, yank and black-hole registers; system clipboard support after settling its representation.
- [ ] **To do** — Song-format compatibility policy between versions.

Resolve the related [design questions](design.md#to-decide) before implementation. Large-song measurements support this milestone; see Engineering work as needed.

## 11. Step entry and remaining input features

- [ ] **To do** — Detailed per-input channel, message-type and range filtering.
- [ ] **To do** — MIDI keyboard step entry: notes, chords, rests, ties and cursor advancement.
- [ ] **To do** — Computer-keyboard step entry and Insert mode, including `c` as change followed by entry.
- [ ] **To do** — Convenient default-note length and velocity controls, including the proposed length menu.
- [ ] **To do** — Playable computer keyboard, with note-off on key release.

Build on milestone 8's input and monitoring. Settle the Insert-mode palette/step-entry keys before binding them.

## 12. Loops, punch recording and takes

Deliver in this order, building on linear recording:

- [ ] **To do** — Loop playback over a section, selected sections or a selected range.
- [ ] **To do** — Adjust transforms by ear while a selection loops, with transform presets and palette support where needed.
- [ ] **To do** — Punch recording using labels or selection boundaries.
- [ ] **To do** — Loop recording: accumulating overdubs or separate takes, including erase-while-looping.
- [ ] **To do** — Basic take management: audition, choose an active take, discard alternatives, and keep takes with their section; palette support for takes.
- [ ] **To do** — Count-in and pre-roll choices between passes, with each pass an undo step.

Resolve the related [design questions](design.md#to-decide) before implementation. Take lanes and comping follow in milestone 16.

## 13. Views and section navigation

- [ ] **To do** — Ghost notes, with remembered per-track choices.
- [ ] **To do** — Open selected arrangement tracks as one editable track plus ghosts.
- [ ] **To do** — Split piano-roll panes synchronised in time, with independent pitch scrolling.
- [ ] **To do** — Several controller lanes displayed simultaneously, and tempo lanes.
- [ ] **To do** — Outline view sharing the section strip's editing operations.
- [ ] **To do** — Section cueing, cue slots and queued jumps, available from existing views as well as the outline.
- [ ] **To do** — Section recolouring and remaining section-length commands.
- [ ] **To do** — Broader search across arrangement tracks.
- [ ] **To do** — Hand-picked note selections.
- [ ] **To do** — Remaining pitch-navigation commands: next sounding note above/below and direct pitch jumps.

Resolve the related [design questions](design.md#to-decide) before implementation. Cueing follows the transport work and can be delivered independently of the outline.

## 14. Daily-use polish and version-1 completion

- [ ] **To do** — Configurable status bar and inspector.
- [ ] **To do** — Per-view column visibility and ordering.
- [ ] **To do** — Song-tempo controls with an explicit relationship to the conductor.
- [ ] **To do** — Port aliases and reusable instrument/program-name tables.
- [ ] **To do** — MIDI-device connection/disconnection during a session.
- [ ] **To do** — Windows MIDI Services and owned Windows ports, subject to backend feasibility.
- [ ] **To do** — Hideable conventional menu bar, using registry commands and their current bindings.
- [ ] **To do** — MIDI-export marker options, including labels as markers for programs that ignore cue points.
- [ ] **To do** — Remaining settings, performance review and necessary improvements.
- [ ] **To do** — Documentation, command reference and configuration examples.
- [ ] **To do** — Complete outstanding desktop and hardware validation.

The version-1 completion point is here. Backups are earlier, in milestone 10. Reliability fixes land when needed. Review command-line polling and separation of specification from historical notes during polish.

## 15. Timing, harmony and musical tools

- [ ] **To do** — Arbitrary and nested tuplets.
- [ ] **To do** — Euclidean rhythm generation.
- [x] **Done** — Existing time-stretch transform with ratio/percentage input, delivered with the full grammar.
- [ ] **To do** — Extend time stretching/compression with a target length in beats or bars; settle range and ripple semantics before implementation.
- [ ] **To do** — Half-speed recording, independently usable before free-time tempo fitting.
- [ ] **To do** — Free-time recording, then fitting a tempo map and bar lines.
- [ ] **To do** — Chord/key track, including changes over time.
- [ ] **To do** — Scale lock and a semitone motion that ignores it, after the chord/key track.
- [ ] **To do** — Track-wide ripple editing.

## 16. Takes, alternatives and reusable material

- [ ] **To do** — Take lanes and comping.
- [ ] **To do** — Section alternatives, auditionable in place.
- [ ] **To do** — Persistent musical notebook: named/tagged fragments with source date, song, section, key, tempo and meter.
- [ ] **To do** — Yank or record into the notebook; search by name, tag or source, audition and put fragments into songs.
- [ ] **To do** — Reusable snippets/templates, using fragment metadata such as key and length.
- [ ] **To do** — Optional fitting to the destination key and timing, using milestone 15's foundations.
- [ ] **To do** — Undo tree.

Notebook design details and open questions are in [design.md](design.md#feature-notes). It does not require retrospective capture.

## 17. Alternative song structures and views

Each is a substantial, separately reviewed step:

- [ ] **To do** — Independent track loops.
- [ ] **To do** — Song playlist mode using ordinary section references, without linked sections or patterns.
- [ ] **To do** — Tracker view, beginning with the existing linear song model; settle off-grid notes, lengths and chords before implementation.
- [ ] **To do** — MusicXML export, using the meter and harmonic metadata established earlier.

## 18. Sound hosting and configuration tools

- [ ] **To do** — In-process VST3/CLAP hosting, with a separate design review for audio and real-time architecture.
- [ ] **To do** — Theme editor.
- [ ] **To do** — Shareable configuration packs.

## Engineering work as needed

These support the milestones rather than forming a separate feature milestone. Start them when the related feature or a measurement justifies the work.

- [ ] **To do** — Resolve blocking Windows SysEx delivery if needed for input/recording.
- [ ] **To do** — Measure large-song edits and playback-plan rebuilds with milestone 10; optimise only where warranted.
- [ ] **To do** — Improve note-off scheduling if density measurements justify it.
- [ ] **To do** — Separate test executables when build/test costs warrant it.
- [ ] **To do** — Reduce command-line polling where useful during polish.
- [ ] **To do** — Further separate current specification from historical decisions during polish.

## Someday maybe — unscheduled

No milestone or implementation commitment. Keep these unchecked unless they are explicitly promoted into the schedule.

- [ ] **Unscheduled** — Musical search/replace by interval or rhythm pattern.
- [ ] **Unscheduled** — Linked variations and linked sections.
- [ ] **Unscheduled** — Retrospective capture.
- [ ] **Unscheduled** — Polytempo.
- [ ] **Unscheduled** — Arranging checks.
- [ ] **Unscheduled** — Text lanes.
- [ ] **Unscheduled** — Live rig mode and setlists.
- [ ] **Unscheduled** — Patterns/phrases.
- [ ] **Unscheduled** — MIDI learn.
- [ ] **Unscheduled** — MIDI clock input and MTC.
- [ ] **Unscheduled** — MIDI clock output and transport/sync settings; removed from version-1 scope.
- [ ] **Unscheduled** — MIDI 2.0.
- [ ] **Unscheduled** — Lua scripting and scripted transforms.
- [ ] **Unscheduled** — SPG song import.
- [ ] **Unscheduled** — macOS support; retain the existing portability practices.
