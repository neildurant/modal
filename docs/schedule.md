# Schedule

The ongoing plan for Modal, agreed 9 November 2026. This is the source of truth for milestone order and work status. [design.md](design.md) specifies behaviour, [decisions.md](decisions.md) records reasons, and [status.md](status.md) holds the current handoff and validation results.

## Keeping this plan current

Use standard Markdown checkboxes with an explicit state:

- [ ] **To do** — scheduled, not started.
- [ ] **Doing** — actively being worked on; add a short progress or blocker note.
- [x] **Done** — implemented and validated, with any required UI trial accepted. Record evidence in status.md.

Change an item's state when work starts or finishes, and update status.md in the same change. Split large items into reviewable steps before implementation.

Put each sub-step on its own indented checklist line with an explicit **To do**, **Doing** or **Done** state. Keep parent entries short; link to the design and handoff for behaviour, trial history and validation details. Keep completed items here as history. Work on one milestone at a time; agree changes to scope or order before recording them here. A milestone is complete when its items are Done and required checks are complete; state outstanding manual checks explicitly. Future items remain To do until work actually starts. Someday maybe items are deliberately unscheduled, not the next tasks in a queue.

## User manual

The user manual is being brought forward from milestone 14, agreed 16 November 2026, initially on the `user-manual` branch. The full draft was accepted 16 November 2026; further manual work takes place on `main` alongside application development.

- [x] **Done** — Publication prototype and revised samples accepted 16 November 2026. One Quarto source builds offline HTML and PDF with the agreed visual style, explanatory tone, example song and labelled offscreen illustrations. Validation evidence is in status.md.
- [x] **Done** — Initial full draft written and validated 16 November 2026: ten explanatory chapters and three appendices, with generated settings/transform references and ten reproducible figures. Full-draft review feedback on arrows/directions, section scope, commands/bindings, search paragraphs, independent letter names and native-file wording is applied and validated. Revised PDF/HTML accepted 16 November 2026; see status.md.
- [x] **Done** — Working procedures and regular upkeep added 16 November 2026: update the manual by user-visible feature completion, permit explicit related-commit follow-ups, review at milestone boundaries, check registry reference drift in normal tests, and build both formats in a dedicated CI workflow. Local builds/checks pass; the first hosted CI run is pending publication on `main`, recorded in status.md.

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
- [x] **Done** — Ports and aliases before linear recording; all three steps accepted on trial and published 11 November 2026. See [design.md](design.md#ports) and [status.md](status.md) for behaviour, trial refinements and the Linux port-name diagnosis.
    - [x] **Done** — Step 1: per-machine `ports` file, aliases, enabled ports and name resolution; remove the `inputs` and `port` settings without migration.
    - [x] **Done** — Step 2: Ports view, editing and following ports as they come and go; accepted after reworking defaults, On/Default columns and text fields.
    - [x] **Done** — Step 3: Tracks choosers by alias, clear Default/All and unavailable-port states, and Output/Input headings; accepted with `gm` opening Ports and unusable defaults warned of.
    - [x] **Done** — Resolve duplicate Linux MIDI port names, guard against virtual-port Thru feedback and refuse unmatched `inputs` at the prompt.
- [x] **Done** — Small fixes from the 8.2 hardware trial; accepted and published 11 November 2026. See [status.md](status.md).
    - [x] **Done** — Fixed-width `In` light showing the channel and last message other than note-off, so the status line stays still.
    - [x] **Done** — Complete setting values containing spaces in quotes.
    - [x] **Done** — Refuse `write-config FILE!`, explaining that overwrite is `:write-config! FILE`.

Start loopMIDI before Modal and a plugin host: Modal keeps the port it first opened.

- [x] **Done** — Accepted on trial and published 11 November 2026: `R` records from the marker after the count-in, clicking on the song's metronome port or the default output, Space stops, the take one undo step growing the song as needed; `, r m` replace or overdub; `● REC` / `● ODUB` and `Armed 2 Replace` in the status line. Linear recording into armed tracks, replace or overdub, starting at the chosen record position and continuing beyond the song's end; clear recording state in the transport/status line.
- [x] **Done** — Accepted after a second trial and published 11 November 2026, with the changes from the first: drop-down lists, Test buttons, count-in and pre-roll as a number and Bars/Beats, digits starting to type. `, m` keys, `:countin` and `:preroll`, the Metronome page, `● COUNT` / `● PRE` / `● REC` at one width, and the count-in and `No click` in the status line. GUI metronome controls, count-in and pre-roll; distinguish playback start from recording start.
- [x] **Done** — Accepted on trial and published 12 November 2026: the take shown as it's played, and edits to recorded tracks refused while recording. Held notes and pedals ending at the stop, and the pass as one undo step with the song growing, were already done with linear recording. Finish held notes and pedals correctly; show recorded material immediately; one pass, including song extension, is one undo step.
- [x] **Done** — Accepted on trial and published 12 November 2026. In replace mode, silence the tracks being recorded from the record point, as replace will leave them (agreed 12 November 2026; design.md, Recording in the GUI).
- [x] **Done** — Accepted on a second trial and published 12 November 2026, with a 150 ms minimum. Audition notes in the piano roll as the cursor lands on them, and as one is added or moved to a new pitch; the `audition` setting with `, T a`, grid-advance moving to `, T g` (agreed 12 November 2026).
- [x] **Done** — Status line arranged into fixed zones and panels; each step rendered, tried and published 12 November 2026. See [design.md](design.md), [decisions.md](decisions.md) and [status.md](status.md).
    - [x] **Done** — Step 1: theme roles and fixed-position dim/lit indicators on the right, with the click in the settings' blue.
    - [x] **Done** — Step 2: permanent message row above the status line for messages, hints, state, pending keys, search and the `:` prompt.
    - [x] **Done** — Step 3: panels on the left, the transport's state block and chips, and the piano roll's grid in its top bar; accepted on the second trial.
    - [x] **Done** — Step 4: agreed priorities when the window narrows.
- [x] **Done** — Accepted on trial and published 12 November 2026, ARM kept red. Complete unavailable-input and input-overflow handling for recording, building on the existing input-loss warning and safe stop/panic.
- [x] **Done** — Save the remaining track and song recording settings. Found already saved, 12 November 2026: the split between song and machine recorded in design.md, with no change needed.
- [x] **Done** — Accepted on trial and published 12 November 2026. Layout tweaks after use (agreed 12 November 2026): the status line's indicator groups further apart; the Tracks view's gap after the buttons wider and between Input and Output narrower; the Ports inspector's port and state first and About last, as wide on every row; the Metronome page spread out by group, its boxes lined up, velocities labelled and Test buttons as chips.
- [x] **Done** — Tests added and published 12 November 2026; the hardware checklist passed on Windows and Linux 13 November 2026 (status.md, Recording: coverage and hardware trials); two fixes found alongside it accepted on trial and published 13 November 2026. Automated coverage and real MIDI trials for the remaining recording features; preserve measured results in status.md.
- [x] **Done** — Rebarring and track meters, brought forward after milestone 8 for a composition in progress; all three steps accepted on trial and published 13 November 2026. See [design.md](design.md) (Inserting a meter and rebarring) and [status.md](status.md).
    - [x] **Done** — Step 1: insert a meter and rebar the song's meter over a `v` range, keeping notes at their times.
    - [x] **Done** — Step 2: rebar selected tracks with `Ctrl-v` or `V`, the shown track in piano roll/event list, and restore following with `:meter follow` (`, s f`).
    - [x] **Done** — Step 3: tint arrangement bars with their own track meter, toggle with `, T b`, show `own` after the meter and track meters in the piano-roll ruler.
- [x] **Done** — Accepted on trial 13 November 2026: `, t a` and `, t i` add tracks after or before the cursor's, `, t d` deletes and `, t r` renames (agreed 13 November 2026). After milestone 8, next: adding tracks, which until now needed a track copied, put and emptied (asked for 13 November 2026; `, t a` kept for it).
- [x] **Done** — Accepted on trial and published 14 November 2026, with the track rows shaded across the names and bars in place of the line after the names: the arrangement shows Mute, Solo and Arm from the Tracks view's shared columns, its header widening for them with names kept at 14; `M`, `S` and `A` on `V` or `Ctrl-v` tracks there; `A` arms, the Arm button showing `A`, and `R` while recording the track. After milestone 8, then: Mute, Solo and Arm buttons beside the arrangement's track names, the Tracks view's own columns with the same keys and meaning (agreed 12 November 2026; design.md, Shared columns, future configuration).
The input foundation precedes step entry and loops. Behaviour and unresolved recording choices are in [design.md](design.md#input-and-recording) and [To decide](design.md#to-decide). Related engineering work is listed below.

## 9. Controllers, drums and folding

Completed 19 November 2026. See [status.md](status.md#guided-event-insertion-19-november-2026) for the final validation.

- [x] **Done** — Editable velocity, controller, pitch bend and aftertouch lanes; all steps accepted on trial and published by 15 November 2026. See [design.md](design.md) (Editing in the lane) and [status.md](status.md).
    - [x] **Done** — Step 1: velocity lane navigation, value editing, chords fanned out, time selections and value boxes; accepted on the fifth trial.
    - [x] **Done** — Step 1b: chords as a whole with `gk` / `gj`, the inspector's range and `^ic`, including chord names in the inspector.
    - [x] **Done** — Step 2a: choose and navigate CC, pitch bend and aftertouch lanes, draw their events, change values and select; include cursor and nudge refinements from trial.
    - [x] **Done** — Step 2b: add, delete and move lane events; accepted on the second trial with held-value prompts and selection/box refinements.
    - [x] **Done** — Step 3: search highlighting in the lane, built with the lanes and confirmed by tests and trial.
- [x] **Done** — A taller controller lane for precise work (asked for 14 November 2026): `, c h` steps the track's lane from three rows to a quarter of the view, half, and back, saved with the track; accepted on trial 17 November 2026.
- [x] **Done** — Switch-controller regions for sustain, sostenuto, soft pedal and similar controllers, preserving imported half-pedal values. See [design.md](design.md) (Pedals in the lane and the notes) and [status.md](status.md); further ideas remain in Feature notes.
    - [x] **Done** — Part 1: draw regions, toggle values with `, c v`, navigate regions, inspect them and show sustain tails and tint; accepted and published 15 November 2026.
    - [x] **Done** — Part 2: edit regions with the familiar note keys; accepted on the second trial and published 16 November 2026, with grid-step `h` / `l` and value boxes refused among regions.
- [x] **Done** — Program-change event markers in the piano-roll ruler, delivered with bank/program setup; validation and the accepted trial are recorded in status.md.
- [x] **Done** — Thin keeps resets to 0 (asked for 16 November 2026, after Sequencer Plus Gold's): never removing a controller's, pitch bend's or aftertouch's event of 0 (or pitch bend's centre), so a reset survives thinning, as well as each gesture's first and last. Implemented with core/Workspace regressions; all six Linux configurations pass and the modulation trial was accepted 17 November 2026. See status.md.
- [x] **Done** — Controller ramps and shaping. Agreed behaviour is in [design.md](design.md) (Controller ramps; Shaping controller values), reasons in [decisions.md](decisions.md), and validation in [status.md](status.md).
    - [x] **Done** — Editable ramps; designed, built, tried and published 17–18 November 2026.
        - [x] **Done** — Step 1: persistent ramp definitions, source events, materialised playback events, validation and native-file storage.
        - [x] **Done** — Step 2: lane drawing and inspector editing with visual/audible preview; accepted after the trial refinements.
        - [x] **Done** — Step 3: timing keys, delete/yank/paste, recording taking precedence, protected points in transforms and clear structural refusals.
        - [x] **Done** — Step 4: manual chapter, worked example, illustration and key/reference updates; accepted on reading 18 November 2026.
        - [x] **Done** — Follow-up: mark and protect ramp points in the event list; accepted on trial 18 November 2026.
    - [x] **Done** — Shape existing controller values; designed 18 November 2026, each step tried before the next; completed 19 November 2026.
        - [x] **Done** — Step 1: scale, limit, invert and their commands/keys; accepted on trial and published 18 November 2026, with local Linux checks and hosted Linux/Windows CI passing.
        - [x] **Done** — Step 2: smooth and remove repeated values; accepted on trial and published 18 November 2026. See [status.md](status.md) for validation.
            - [x] **Done** — Core smoothing and exact-repeat removal, with multi-PPQN regressions and conservative context after reset/program/SysEx messages.
            - [x] **Done** — Commands, keys, lane/all defaults and Workspace regressions; functional trial accepted 18 November 2026.
            - [x] **Done** — Manual explanations, key/transform references, HTML/PDF builds and PDF review.
            - [x] **Done** — All six Linux configurations pass; application rebuilt for the local trial.
            - [x] **Done** — Functional trial accepted and published 18 November 2026, including repeated smoothing with `.` around a protected ramp; hosted CI passes.
        - [x] **Done** — Step 3: program remapping and controller retrograde; design and implementation plan approved 18 November 2026. See [status.md](status.md).
            - [x] **Done** — Program-change remapping implemented with regressions; clearer prompts accepted on trial.
            - [x] **Done** — Held-contour retrograde implemented; its event-selection span fixed and accepted on trial.
            - [x] **Done** — Lane timing targets and shared ramp ownership guards; `t` / `T` / `:stretch` in a lane (notes untouched) and the active-ramp refusal accepted on trial 19 November 2026.
            - [x] **Done** — Regressions, manual/reference updates, HTML/PDF builds and PDF review; all six Linux configurations pass and the application is rebuilt for trial.
            - [x] **Done** — Event-selection retrograde span: the last value lasts the gap before it, capped by the next event and bar end (revised after a second trial); accepted on trial. See [status.md](status.md).
            - [x] **Done** — Retrograde on `, c x` and `, x x`, `, x R` dropped (agreed 18 November 2026); accepted on trial.
            - [x] **Done** — Remap asks Type, From and To, mapping within one type; accepted on trial 19 November 2026, with its no-notes and ramp-message corrections.
            - [x] **Done** — Follow-up regressions, manual/reference updates and trials; Windows Debug and WSL Clang Debug pass locally, and hosted CI passes all six Linux configurations and Windows Debug/Release.
            - [x] **Done** — Final functional acceptance 19 November 2026 and publication through `a198f31`.
- [x] **Done** — General MIDI and reusable custom drum maps; behaviour and validation in [design.md](design.md#drum-maps) and [status.md](status.md).
    - [x] **Done** — Step 1: map data, shared General MIDI names, song persistence and reusable map files; seven new core tests and GCC/Clang Debug suites pass. See status.md.
    - [x] **Done** — Step 2: choosing maps, file management and names; accepted on trial 19 November 2026. See design.md and status.md.
        - [x] **Done** — Piano-roll labels and inspector names.
        - [x] **Done** — Map chooser, reusable-file load/save and same-name comparison/review actions.
        - [x] **Done** — Manual updates, HTML/PDF builds, dark/light renders and all six Linux configurations; see status.md.
        - [x] **Done** — Functional trial accepted with the SSD test material.
    - [x] **Done** — Step 3: names in navigation, search, selection and the palette. See [design.md](design.md#drum-maps).
        - [x] **Done** — Resolve pitch names from the current track and offer choices for ambiguous names.
        - [x] **Done** — Pitch-row selection and direct navigation by drum name, including search and the palette.
        - [x] **Done** — Trial refinements accepted: note counts and ordering in ambiguous choices, aligned MIDI numbers, and `gp` completion.
        - [x] **Done** — Note counts in the drum palette. Accepted on trial.
        - [x] **Done** — Regressions, manual updates and functional trial. See [status.md](status.md).
- [x] **Done** — In-app file picker, accepted on trial 19 November 2026. See [design.md](design.md#file-picker).
    - [x] **Done** — Folder panel, parent and subfolders, Places, filtering and path completion.
    - [x] **Done** — Song open/save and drum-map load/save, including overwrite confirmation.
    - [x] **Done** — Regressions, manual and accepted functional trial. See [status.md](status.md).
- [x] **Done** — File picker follow-up for `:write-config`, accepted on trial 19 November 2026.
- [x] **Done** — Piano-roll folding, accepted on trial 19 November 2026. See [design.md](design.md#drum-maps).
    - [x] **Done** — Occupied, named and combined rows, with whole-track and current-section scope.
    - [x] **Done** — Toggle, chooser, navigation and selection of displayed pitches.
    - [x] **Done** — Regressions, manual and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Drum-map Always show marks for folding.
    - [x] **Done** — Store the marked pitches in songs and reusable drum maps.
    - [x] **Done** — Toggle and show marks in the piano roll and saved-map review.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Single confirmation for replacing a saved drum map.
    - [x] **Done** — Confirm in the save comparison and keep the chooser's review actions.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Guided event insertion with `, e`. See [design.md](design.md#event-insertion).
    - [x] **Done** — Search by event type and enter its fields at the cursor.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).

Drum maps precede folding by named rows. Settle map storage, lane navigation and folding behaviour before their implementation. Several simultaneous controller lanes are scheduled with the later view work.

## 10. Songs, navigation and copying

- [x] **Done** — Open, select and close songs. See [design.md](design.md#open-songs).
    - [x] **Done** — Independent documents and undo, with open, select, close and save protection.
    - [x] **Done** — Playback on switch, chooser, regressions, manual and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Align locations in the open-song chooser. See [status.md](status.md).
- [x] **Done** — A `, f` Files leader group (agreed 20 November 2026). See [grammar.md](grammar.md#the-leader).
    - [x] **Done** — Bind open, open songs, new, save, save as, close and export; add the save-as command.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Keys for the next and previous open song, without the chooser (asked for 20 November 2026). See [design.md](design.md#the-files-group).
    - [x] **Done** — `]f` / `[f` and Ctrl-Tab / Ctrl-Shift-Tab, going round, with a count.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — `:export` without a filename uses the Folder panel instead of the system dialog. Accepted on trial.
- [x] **Done** — Restore each song's view and places on switching (agreed 20 November 2026). See [design.md](design.md#open-songs).
    - [x] **Done** — Keep each song's views and places, the view shown, its history, `''`, `gv` and the play-from marker.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Copying between songs through the shared registers (agreed 20 November 2026). See [design.md](design.md#copying-between-songs).
    - [x] **Done** — Sections: match track content by name, adding unmatched tracks with their settings. Accepted on trial.
    - [x] **Done** — Whole tracks: lay along time, re-cut at the destination's sections, adding a section for anything past the end, keeping their bars. Accepted on trial.
    - [x] **Done** — Whole bars (`yy`, `ab`, the arrangement's bars) carry their meter; other material lands in the destination's bars. Accepted on trial.
    - [x] **Done** — Regressions, manual updates and accepted functional trials for all three steps. See [status.md](status.md).
- [x] **Done** — Importing tracks from a file (`:track-import`, `:read`, `, f i`), agreed 20 November 2026. See [design.md](design.md#importing-tracks).
    - [x] **Done** — Read a `.modal` or `.mid` file from disk and import the tracks named by typed numbers and ranges.
    - [x] **Done** — The tick chooser with typed ranges, and `, f i`.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Default song folder and picker Places (agreed 20 November 2026). See [design.md](design.md#file-picker).
    - [x] **Done** — Add the `songfolder` setting, used where a song has no file of its own.
    - [x] **Done** — Places: Songs (with `(songfolder)`), This song's folder, Home, then Config, Drum maps and State.
    - [x] **Done** — Picker clarity from trial: the listing on a background of its own, `Folder:` labelled, no cursor while choosing a place.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Recent songs: a list kept between sessions, reached with `, f r` (asked for 13 November 2026, agreed 20 November 2026). See [design.md](design.md#recent-songs).
    - [x] **Done** — Remember songs opened and saved, up to `recentsongs`; the chooser with three aligned columns, Delete to remove, missing files offered for removal.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Song titles (scheduled 20 November 2026, design agreed 23 November 2026). See [design.md](design.md#song-titles).
    - [x] **Done** — `:song-title` and `, f t`, as an undo step. Accepted on trial.
    - [x] **Done** — Title and filename in the window's title bar and in Open songs. Accepted on trial.
    - [x] **Done** — Titles remembered and shown in Recent songs. Accepted on trial.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Numbered backups and safe save/close behaviour across modified buffers (design agreed 23 November 2026). See [design.md](design.md#backups-and-saving-every-song).
    - [x] **Done** — Backups on save, and the `backups` setting. Accepted on trial.
    - [x] **Done** — `:song-backups` and `, f b`. Accepted on trial.
    - [x] **Done** — `:wa`, `:qa`, `:wqa` and `:xa`. Accepted on trial.
    - [x] **Done** — The window's close question for several unsaved songs. Accepted on trial.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Jump history across positions, views and buffers (design agreed 23 November 2026). See [design.md](design.md#editing-grammar).
    - [x] **Done** — Within a song: jumps and view changes recorded, `Ctrl-o` / `Ctrl-i`, `:jumps`. Accepted on trial.
    - [x] **Done** — Across songs: switching recorded, `Ctrl-o` switching back, closed songs passed over. Accepted on trial.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Paste history, including cycling the material just put (design agreed 23 November 2026). See [design.md](design.md#editing-grammar).
    - [x] **Done** — The history: yanks and deletes fill `"1` to `"9`; `"3p`; `:registers` lists them. Accepted on trial.
    - [x] **Done** — `[p` and `]p` swapping straight after a put, one undo step with it; puts name the copy's place. Accepted on trial.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Vim's other registers (design agreed 23 November 2026; the numbered registers are the paste history's). See [design.md](design.md#editing-grammar).
    - [x] **Done** — The yank register `"0` and the black-hole register `"_`. Accepted on trial.
    - [x] **Done** — The system clipboard, `"+` and `"*`, as Modal's song text. Accepted on trial.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).
- [x] **Done** — Base16 and base24 schemes as themes (design agreed 24 November 2026). See [design.md](design.md#customisation).
    - [x] **Done** — Schemes as themes: `themes/NAME.yaml` and eight built in; dim text and the panel found by contrast. Accepted on trial.
    - [x] **Done** — `:theme-import FILE [NAME]`, writing a scheme as an ordinary theme. Accepted on trial.
    - [x] **Done** — Regressions, manual updates and accepted functional trial. See [status.md](status.md).

Large-song measurements remain under [Engineering work as needed](#engineering-work-as-needed).

## 11. Loops, punch recording and takes

Deliver in this order, building on linear recording:

- [x] **Done** — Loop playback over a section, selection or range between song labels.
    - [x] **Done** — Schedule repeated playback and MIDI state at the join.
    - [x] **Done** — Section, selection and label controls. Accepted on trial.
    - [x] **Done** — Manual and automated validation. See [status.md](status.md).
- [x] **Done** — Playback count-in and pre-roll. See [design.md](design.md#metronome-and-count-in).
    - [x] **Done** — Save independent song settings and use the lead-in once at playback start.
    - [x] **Done** — Aligned Before playback controls and first-pass lead-in. Accepted on trial.
    - [x] **Done** — Manual and automated validation for first-pass lead-in. See [status.md](status.md).
    - [x] **Done** — Optional lead-in on every playback loop pass. Accepted on trial.
- [x] **Done** — Punch recording using an armed range. See [design.md](design.md#input-and-recording).
    - [x] **Done** — Arm and clear a range from a section, Visual selection or two labels, with ruler and status indicators. Accepted on trial. See [status.md](status.md).
    - [x] **Done** — Record the armed span with `R` (design agreed 24 November 2026). See [design.md](design.md#input-and-recording).
        - [x] **Done** — From stopped: lead-in, punch-in, punch-out with playback carrying on, one undo step. Accepted on trial.
        - [x] **Done** — `R` while playing, before punch-in. Accepted on trial.
        - [x] **Done** — Manual updates and final validation. Accepted on reading.
- [x] **Done** — Arm playback loops separately from starting playback, with the steps before it (agreed 25 November 2026). See [design.md](design.md#input-and-recording) and [decisions.md](decisions.md#loop-and-punch-groups-saved-ranges-and-a-shorter-status-line-25-november-2026).
    - [x] **Done** — The palette finds a key with any modifiers: `<Space>` and `Space` find `<S-Space>` and `<C-Space>`. Accepted on trial.
    - [x] **Done** — Leader groups: punch on `, P P`, `, P l`, `, P d`; loops on `, L` as a group (`, L L`, `, L l`). Accepted on trial.
    - [x] **Done** — The punch range saved with the song, arming and clearing as undo steps. Accepted on trial.
    - [x] **Done** — A shorter status line: the file's name to `Ctrl-g`, `[+]` kept; `IN` as a light, the last message in the Ports view. Accepted on trial, with `+` for `[+]`.
    - [x] **Done** — Armed loops: `, L L`, `, L l`, `, L d` and `, L t`, Space and `Ctrl-Space`, the solid bracket and `LOOP`, saved with the song; changes while playing at the next Space. After trial: `↩ LOOP`, as the font has no `↻`, and `, P t` for punching, as `, L t` (asked for 25 November 2026). Accepted on trial, with the status line's indicators grouped by a dim line.
    - [x] **Done** — Changes to the loop while playing take effect at once, through a new engine command. Accepted on trial.
    - [x] **Done** — Manual updates and final validation. Accepted on reading.
- [x] **Done** — Loop recording, by the song's replace or overdub setting (decided 25 November 2026). See [design.md](design.md#input-and-recording).
    - [x] **Done** — `R` with a loop armed records it, each pass an undo step as the playhead wraps. Accepted on trial.
    - [x] **Done** — `R` while a loop plays, starting at the next pass. Accepted on trial.
    - [x] **Done** — Manual updates and final validation. Accepted on reading.
- [ ] **Doing** — Basic take management on `main`. See [status.md](status.md) for the existing behaviour and trial.
    - [x] **Done** — Takes per section and track, the take on each event, saved with the song.
    - [x] **Done** — Replace recordings make takes: Before, and each loop pass. Accepted on trial.
    - [x] **Done** — `]t` / `[t` choose a take, stopped or playing. Accepted on trial.
    - [ ] **Doing** — `, k o` keeps only the take playing; `, k d` discards it. Desktop MIDI trial deferred. See [status.md](status.md).
- [ ] **Doing** — Take stacks and range comping. See [design.md](design.md#take-stacks-and-comping).
    - [x] **Done** — Record the agreed stack, editing, join and Bake design and review mock-ups.
    - [x] **Done** — Settle the file model and compatibility. Accept ordinary songs and refuse the old test take format.
    - [ ] **Doing** — Stack, resolved playback, chase and export with saved songs. Automated checks passed. See [status.md](status.md).
    - [ ] **Doing** — Stack semantics for replace recording and choose, keep and discard. Desktop MIDI trial pending. See [status.md](status.md).
    - [x] **Done** — Show active ranges and the take being edited.
        - [x] **Done** — Label ranges in the arrangement and piano roll, and show the edit destination in the piano-roll header.
        - [x] **Done** — Add the section-strip count and event-list take column.
    - [ ] **Doing** — Take lanes and range comping.
        - [x] **Done** — Review the lanes, range selection and audition mock-ups.
        - [ ] **Doing** — Show source lanes and their place in the comp in both views.
        - [ ] **Doing** — Audition and assign a source over the selected range.
        - [ ] **To do** — Edit focused takes and copy selections between takes.
    - [ ] **To do** — Review audition and range-choice shortcuts with take lanes visible.
    - [ ] **To do** — Resolve note overlaps, controller values and program changes in the join inspector, with offscreen previews.
    - [ ] **To do** — Bake a comp to a chosen new or existing track.
    - [ ] **To do** — Palette support, manual updates and final validation.
- [ ] **To do** — Count-in and pre-roll choices between passes.

Resolve the related [design questions](design.md#to-decide) before implementation.

## 12. Miscellaneous features

- [ ] **To do** — Zen mode, like Doom's zen or Vim's Goyo: hide everything except the piano roll or arrangement and a minimal status line, for focused writing.
- [ ] **To do** — A song tabline: an optional one-row strip showing open songs, like Vim's tabline, with the current one highlighted and modified songs marked.
- [ ] **To do** — Note names in notes: when a note bar is wide enough, show its name inside it.
- [ ] **To do** — Expand-region: grow the selection note → chord → beat → bar → page → section → track, and shrink back. It's the friendliest selection tool you could offer newcomers, and cheap now that the text objects exist.
- [ ] **To do** — Magit-style transient menus for transforms with several options (window, threshold, amount, toggles), as a richer form of the hint menu.
- [ ] **To do** — Convenient default-note length and velocity controls, including the proposed length menu.
- [ ] **To do** — Mute selection - arrangement view. User can mute selections of bars from playing
- [ ] **To do** — Mute selection/note - piano roll view. User can mute selections of notes from playing
- [ ] **To do** — Mute selection/note - event list. User can mute selections of notes from playing
- [ ] **To do** — Merging tracks: combining several tracks' events into one, as Sequencer Plus Gold's Merge (compared 15 November 2026).
- [ ] **To do** — Moving a label to the cursor with `, l v`, choosing which (asked for and agreed 25 November 2026). See [design.md](design.md#data-model).
- [ ] **To do** — Tempo transforms, after Sequencer Plus Gold's (compared 15 November 2026): accelerando and ritardando across a range, written as steps as tempo changes are; scaling and shifting the tempo map over a range; thinning and removing repeated tempo changes.

## 13. Composition tools

- [ ] **To do** — Chord/key track, including changes over time, first, as the tools after it read the key and chords (moved here 24 November 2026).
- [ ] **To do** — Text phrase entry: type a phrase in a compact, ABC-like syntax at the command line or in a prompt, e.g. `c4 d e f | g2 g`, and have it inserted at the cursor in the current grid and key.
- [ ] **To do** — Chords by name: type Em9 or G/B to insert a voicing at the cursor, with the voicing style configurable (close, open, drop-2).
- [ ] **To do** — Voicing transforms: invert a chord up or down, drop-2, drop-3, spread or close the voicing, and move the top voice. These are bread and butter for a keyboard player, and they compose naturally with `ic` and `.`.
- [ ] **To do** — Change mode: move a phrase between modes by scale degree, e.g. major to Dorian, or minor to Phrygian. A quick way to recolour a theme.
- [ ] **To do** — Negative harmony: mirror a passage around the key's axis, turning major-sounding harmony into its minor mirror image. Related to the existing invert, but harmonically aware.
- [ ] **To do** — Voice-leading: take a sequence of chords, or chord-track symbols, and re-voice them so each moves as little as possible to the next, within a range you set.
- [ ] **To do** — A chord-name lane: a row showing the detected chord on each beat or bar across the page, e.g. Em9 · · · Cmaj7 · · · D/F# ·, so you can read the harmony of a dense piano part at a glance.
- [ ] **To do** — Chord substitutions: on a chord, offer common substitutes (relative minor, tritone sub, sus or add9 colours) to try in place, auditioned while looping.
- [ ] **To do** — Polyrhythm spans: fill a range with n evenly spaced notes against the bar, e.g. 5 against 4 or 7 over two bars, as a starting rhythm.
- [ ] **To do** — Transpose by relative intervals: (+m3, +5, −M2) in place of chromatic intervals.

## 14. Step entry and remaining input features

- [ ] **To do** — Detailed per-input channel, message-type and range filtering.
- [ ] **To do** — MIDI keyboard step entry: notes, chords, rests, ties and cursor advancement.
- [ ] **To do** — Computer-keyboard step entry and Insert mode, including `c` as change followed by entry.
- [ ] **To do** — Playable computer keyboard, with note-off on key release.

Build on milestone 8's input and monitoring. Settle the Insert-mode palette/step-entry keys before binding them.

## 15. Views and section navigation

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

## 16. Daily-use polish and version-1 completion

- [ ] **To do** — Configurable status bar and inspector.
- [ ] **To do** — Per-view column visibility and ordering.
- [ ] **To do** — Song-tempo controls with an explicit relationship to the conductor.
- [ ] **To do** — Port aliases and reusable instrument/program-name tables.
- [ ] **To do** — MIDI-device connection/disconnection during a session.
- [ ] **To do** — Windows MIDI Services and owned Windows ports, subject to backend feasibility.
- [ ] **To do** — Hideable conventional menu bar, using registry commands and their current bindings.
- [ ] **To do** — MIDI-export marker options, including labels as markers for programs that ignore cue points.
- [ ] **To do** — Remaining settings, performance review and necessary improvements.
- [ ] **To do** — Song-format compatibility policy between versions.
- [ ] **To do** — Documentation, command reference and configuration examples.
- [ ] **To do** — Complete outstanding desktop and hardware validation.

The version-1 completion point is here. Backups are earlier, in milestone 10. Reliability fixes land when needed. Review command-line polling and separation of specification from historical notes during polish.

## 17. Timing, harmony and musical tools

- [ ] **To do** — Arbitrary and nested tuplets.
- [ ] **To do** — Euclidean rhythm generation.
- [x] **Done** — Existing time-stretch transform with ratio/percentage input, delivered with the full grammar.
- [ ] **To do** — Extend time stretching/compression with a target length in beats or bars; settle range and ripple semantics before implementation.
- [ ] **To do** — Half-speed recording, independently usable before free-time tempo fitting.
- [ ] **To do** — Free-time recording, then fitting a tempo map and bar lines. One way to fit it, Sequencer Plus Gold's Tap-Tempo: play the track back and tap a note on each beat, the tempo map following the taps; it also locks onto music recorded without a click.
- [ ] **To do** — Release velocity tools: setting and adjusting note-off velocities over a range, as `^` does note-on velocities (Sequencer Plus Gold's Set-NoteOff and Adjust-NoteOff).
- [ ] **To do** — Splitting by more than pitch: by velocity, duration, whether on or off the grid, and every nth note (Sequencer Plus Gold's Modulus); and, going beyond it (its Pitch Map does one note at a time, as remap does), a pitch map remapping many pitches at once as a table. How it does these is in design.md, Feature notes.
- [ ] **To do** — Scale lock and a semitone motion that ignores it, after the chord/key track.
- [ ] **To do** — Track-wide ripple editing.

## 18. Takes, alternatives and reusable material

- [ ] **To do** — Section alternatives, auditionable in place.
- [ ] **To do** — Persistent musical notebook: named/tagged fragments with source date, song, section, key, tempo and meter.
- [ ] **To do** — Yank or record into the notebook; search by name, tag or source, audition and put fragments into songs.
- [ ] **To do** — Reusable snippets/templates, using fragment metadata such as key and length.
- [ ] **To do** — Optional fitting to the destination key and timing, using milestone 15's foundations.
- [ ] **To do** — Undo tree.

Notebook design details and open questions are in [design.md](design.md#feature-notes). It does not require retrospective capture.

## 19. Alternative song structures and views

Each is a substantial, separately reviewed step:

- [ ] **To do** — Independent track loops.
- [ ] **To do** — Song playlist mode using ordinary section references, without linked sections or patterns.
- [ ] **To do** — Tracker view, beginning with the existing linear song model; settle off-grid notes, lengths and chords before implementation.
- [ ] **To do** — MusicXML export, using the meter and harmonic metadata established earlier.

## 20. Sound hosting and configuration tools

- [ ] **To do** — In-process VST3/CLAP hosting, with a separate design review for audio and real-time architecture.
- [ ] **To do** — Theme editor.
- [ ] **To do** — Shareable configuration packs.

## Engineering work as needed

These support the milestones rather than forming a separate feature milestone. Start them when the related feature or a measurement justifies the work.

- [x] **Done** — Extract status-bar presentation from Workspace into an independent UI component supplied with a value snapshot. Preserve indicators, styling and width priorities; add direct tests and retain Workspace integration coverage. All six Linux configurations pass; publication before the visual trial was explicitly requested 16 November 2026. The deferred visual trial was accepted 17 November 2026. See status.md.

- [x] **Done** — Extract recording-session lifecycle ownership before multiple-song support: one owner for the pending take, preview, recorded-track guard and undo commit; file commands and window closing consult that session. Preserve the current recording and stop-first behaviour. Implemented with fake-clock and Workspace coverage; all six Linux configurations pass and the practical UI trial was accepted 16 November 2026. See status.md.
- [x] **Done** — Extract pedal-region queries and editing rules into core, preserving behaviour. All six Linux configurations pass and the UI trial was accepted 16 November 2026; see [status.md](status.md).
    - [x] **Done** — Step 1: shared region descriptions and queries.
    - [x] **Done** — Step 2: editing operations with whole-batch validation and typed refusals. The view retains keys, grid/nudge destinations, selection, undo and rendering.
- [x] **Done** — Four fixes from the 16 November 2026 review, with regressions, all six Linux configurations passing and the UI trial accepted 16 November 2026. See [status.md](status.md).
    - [x] **Done** — Pending-take file safety, including delayed open-dialog handling.
    - [x] **Done** — Recording across live tempo edits.
    - [x] **Done** — Selected pedal-region collision handling.
    - [x] **Done** — Playback completion attributed to the correct session.
- [x] **Done** — Shared inspector editing across views; accepted on trial and published 17 November 2026. See [status.md](status.md) and [design.md](design.md) (Editing in the inspector).
    - [x] **Done** — Shared `InspectorEditor` and drawing for keys, buttons, choice lists, tint and dimming; each view keeps its own fields and setting rules.
    - [x] **Done** — Manual explanation and links from the affected views, delivered with the ramps' manual and accepted 18 November 2026.
- [ ] **To do** — Resolve blocking Windows SysEx delivery if needed for input/recording.
- [ ] **To do** — Measure large-song edits and playback-plan rebuilds with milestone 10; optimise only where warranted.
- [ ] **To do** — Improve note-off scheduling if density measurements justify it.
- [ ] **To do** — Separate test executables when build/test costs warrant it.
- [ ] **To do** — Reduce command-line polling where useful during polish.
- [ ] **To do** — Further separate current specification from historical decisions during polish.

## Someday maybe — unscheduled

No milestone or implementation commitment. Keep these unchecked unless they are explicitly promoted into the schedule.

- [ ] **Unscheduled** — Punch recording within a loop (suggested 25 November 2026): with a punch range inside an armed loop, `R` records only the punch range on each pass, to retake a fill while hearing the bars around it.
- [ ] **Unscheduled** — Adjust transforms by ear while a selection loops, with transform presets and palette support where needed (deferred 24 November 2026). See [design.md](design.md#midi-transforms).
- [ ] **Unscheduled** — Saved sessions (suggested 20 November 2026): reopen a set of songs together, each at the view and place it was left, across program runs.
- [ ] **Unscheduled** — View snapshots (suggested 20 November 2026): save how a song is being looked at (view, places, zoom, folds) and restore it later, without changing the music. Shares a value-type capture of each view's place with saved sessions; song switching could then use the same capture.
- [ ] **Unscheduled** — A paste-history panel while swapping (suggested 23 November 2026): straight after a put, a few lines above the message row list the paste history, newest first, the copy in the song highlighted; `[p` and `]p` move the highlight, and the next other key closes it. A new kind of panel, as `:registers`' listing closes on the next key.
- [ ] **Unscheduled** — Playback drum-map translation (kept unscheduled 19 November 2026): suggest source-to-destination drum pitches from names and aliases, review ambiguous/unmatched guesses, and apply an accepted mapping at playback while preserving written notes. Playback/audition/export details need a separate design before implementation.
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
