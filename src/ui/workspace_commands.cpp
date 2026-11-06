// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspace_commands.hpp"

#include <modal/ui/arrange_view.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/piano_roll_view.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr CommandInfo atPrompt(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .modes = modeBit(Mode::CommandLine)};
}

// The commands every view shares, which the workspace runs itself.
constexpr std::array kSharedCommands{
    CommandInfo{.name = "operator-delete",
                .description = "Delete the events a motion or object takes (d]b, dib), or dd "
                               "this bar's",
                .count = true},
    CommandInfo{.name = "operator-yank",
                .description = "Yank the events a motion or object takes (y]b, yib), or yy "
                               "this bar's",
                .count = true},
    CommandInfo{.name = "operator-transpose-up",
                .description = "Transpose the notes a range or object takes up a semitone (>ib), "
                               "or {count} semitones (3>ib); >> this bar's",
                .count = true},
    CommandInfo{.name = "operator-transpose-down",
                .description = "Transpose the notes a range or object takes down a semitone "
                               "(<ib), or {count} semitones; << this bar's",
                .count = true},
    CommandInfo{.name = "operator-shift-later",
                .description = "Shift the events a range or object takes later by a grid step, "
                               "keeping how far each is off the grid (t]b), or {count} steps; tt "
                               "this bar's. In the piano roll, its notes",
                .count = true},
    CommandInfo{.name = "operator-shift-earlier",
                .description = "Shift the events a range or object takes earlier by a grid step, "
                               "keeping how far each is off the grid (T]b), or {count} steps; TT "
                               "this bar's",
                .count = true},
    CommandInfo{.name = "operator-quantize",
                .description = "Quantise the starts of the notes a range or object takes to the "
                               "grid (qib); qq this bar's",
                .count = true},
    CommandInfo{.name = "operator-velocity",
                .description = "Set or change the velocity of the notes a range or object takes, "
                               "asking how: a value (80), a change (+10, -10) or a percentage "
                               "(80%); ^^ this bar's",
                .count = true},
    CommandInfo{.name = "operator-humanize",
                .description = "Humanise the notes a range or object takes: move each a little "
                               "at random in time and velocity, by up to humanizetime and "
                               "humanizevel; ~~ this bar's",
                .count = true},
    CommandInfo{.name = "operator-transform",
                .description = "Transform the events a range or object takes, choosing the "
                               "transform from the menu then shown (!ib, then a key); on a "
                               "selection, at once",
                .count = true},
    CommandInfo{.name = "visual",
                .description = "Visual mode: select from here as the cursor moves; v again or "
                               "Escape ends it",
                .here = {"Visual mode: select bars from here as the cursor moves, on every "
                         "track; v again or Escape ends it",
                         "Visual mode: select sections from here as the cursor moves; v again or "
                         "Escape ends it",
                         "Visual mode: select grid steps from here as the cursor moves, at every "
                         "pitch; v again or Escape ends it",
                         "Visual mode: select events from here as the cursor moves; v again or "
                         "Escape ends it"}},
    CommandInfo{.name = "visual-rows",
                .description = "Visual mode by whole rows; V again or Escape ends it",
                .here = {"Visual mode by whole tracks, at every time until an object sets it "
                         "(Vis); V again or Escape ends it",
                         "Visual mode, as v on the strip; V again or Escape ends it",
                         "Visual mode by pitch rows across the whole track, until an object sets "
                         "the time (Vis); V again or Escape ends it",
                         "Visual mode, as v in the event list; V again or Escape ends it"}},
    CommandInfo{.name = "visual-box",
                .description = "Visual mode by a box; Ctrl-v again or Escape ends it",
                .here = {"Visual mode by a box of tracks by bars; Ctrl-v again or Escape ends "
                         "it",
                         "Visual mode, as v on the strip; Ctrl-v again or Escape ends it",
                         "Visual mode by a box of pitches by grid steps; Ctrl-v again or Escape "
                         "ends it",
                         {}}},
    CommandInfo{.name = "visual-again",
                .description = "Go back to the last Visual selection, selecting it again"},
    CommandInfo{.name = "register-use",
                .description = "Name the register for the next yank, delete or put by a letter "
                               "after it (\"ayy, \"ap); a capital adds to it (\"Ayy)"},
    CommandInfo{.name = "registers", .description = "Say what the registers hold", .keys = false},
    CommandInfo{.name = "macro-record",
                .description = "Record the keys typed into a macro named by a letter after it "
                               "(Qa), a capital adding to it (QA); while recording, end it"},
    CommandInfo{.name = "macro-play",
                .description = "Play the macro named by a letter after it (@a), or @ the last "
                               "again, {count} times, as one undo step",
                .count = true},
    CommandInfo{.name = "macros", .description = "Say what the macros hold", .keys = false},
    CommandInfo{.name = "mark-set",
                .description = "Mark the cursor's place, its track, time and in the piano roll "
                               "its pitch, with a letter after it, a to z (ma)"},
    CommandInfo{.name = "mark-go",
                .description = "Go to the place a letter after it marks ('a), or with ' back to "
                               "before the last jump; after an operator, the range to it (d'a)",
                .motion = true},
    CommandInfo{.name = "marks", .description = "Say where the marks are", .keys = false},
    CommandInfo{.name = "mark-delete",
                .description = "Delete marks by their letters: a, abc, or a-d (a-z for all)",
                .usage = "LETTERS",
                .keys = false},
    CommandInfo{.name = "cursor-mark",
                .description = "To the mark ('a), as the range an operator takes",
                .keys = false,
                .motion = true,
                .palette = false},
    CommandInfo{.name = "find-next",
                .description = "Go to the next note of the pitch class named after it, any "
                               "octave (fE, fF#, fBb), {count} times; after an operator, the "
                               "range to it and through it",
                .count = true,
                .motion = true},
    CommandInfo{.name = "find-previous",
                .description = "Go to the note before of the pitch class named after it, any "
                               "octave (FE, FF#, FBb), {count} times; after an operator, the "
                               "range from it",
                .count = true,
                .motion = true},
    CommandInfo{.name = "find-repeat",
                .description = "The last find again, the way it went, {count} times; after an "
                               "operator, the range to it",
                .count = true,
                .motion = true},
    CommandInfo{.name = "find-repeat-back",
                .description = "The last find again, the other way, {count} times; after an "
                               "operator, the range to it",
                .count = true,
                .motion = true},
    CommandInfo{.name = "search",
                .description = "Search the track for events: a pitch (C3, or C in any octave), "
                               "vel<60, len<1/16, cc64 (cc01), cc1=127 or bend, every term "
                               "matching; / and Enter alone clears the matches"},
    CommandInfo{.name = "search-next",
                .description = "Go to the next match of the last search, {count} times, "
                               "going round past the end; after an operator, the range up to it",
                .count = true,
                .motion = true},
    CommandInfo{.name = "search-previous",
                .description = "Go to the match before, of the last search, {count} times, "
                               "going round past the start; after an operator, the range from it",
                .count = true,
                .motion = true},
    CommandInfo{.name = "search-here",
                .description = "Search for the pitch under the cursor, or its controller, "
                               "forwards, {count} times",
                .title = "Search for this pitch",
                .count = true},
    CommandInfo{.name = "search-here-back",
                .description = "Search for the pitch under the cursor, or its controller, "
                               "backwards, {count} times",
                .title = "Search back for this pitch",
                .count = true},
    CommandInfo{.name = "select-bar", .description = "Select this bar, as vib does"},
    CommandInfo{.name = "select-section", .description = "Select this section, as vis does"},
    CommandInfo{.name = "select-track", .description = "Select the track, as vit does"},
    CommandInfo{.name = "select-visible", .description = "Select what's shown"},
    CommandInfo{.name = "select-matches-bar",
                .description = "Select the matches of the last search in this bar"},
    CommandInfo{.name = "select-matches-section",
                .description = "Select the matches of the last search in this section"},
    CommandInfo{.name = "select-matches-track",
                .description = "Select the matches of the last search on the track"},
    CommandInfo{.name = "select-matches-visible",
                .description = "Select the matches of the last search that are shown"},
    CommandInfo{.name = "select-matches",
                .description = "Keep only the matches of the last search in the selection"},
    CommandInfo{.name = "object-match",
                .description = "The match of the last search at the cursor, or the next (gn): "
                               "an operator changes it, then . does the next and n skips one",
                .object = true},
    CommandInfo{.name = "select-match",
                .description = "Go to the match of the last search at the cursor, or the next, "
                               "and select it"},
    CommandInfo{.name = "search-clear",
                .description = "Clear the matches of the last search",
                .aliases = "noh nohlsearch"},
    CommandInfo{.name = "cursor-match-next",
                .description = "To the next match (n), as the range an operator takes",
                .count = true,
                .keys = false,
                .motion = true,
                .palette = false},
    CommandInfo{.name = "cursor-match-previous",
                .description = "From the match before (N), as the range an operator takes",
                .count = true,
                .keys = false,
                .motion = true,
                .palette = false},
    CommandInfo{.name = "cursor-find-next",
                .description = "To the next note found (fE), as the range an operator takes",
                .count = true,
                .keys = false,
                .motion = true,
                .palette = false},
    CommandInfo{.name = "cursor-find-previous",
                .description = "From the note found before (FE), as the range an operator takes",
                .count = true,
                .keys = false,
                .motion = true,
                .palette = false},
    CommandInfo{.name = "repeat",
                .description = "Repeat the last change (.), with its range or object, as one "
                               "undo step; a count replaces the one it had",
                .count = true},
    CommandInfo{.name = "palette",
                .description = "The command palette: find a command by typing part of its name "
                               "or what it does, and run it"},
    CommandInfo{.name = "palette-forget",
                .description = "Forget how often and how lately commands were run from the "
                               "palette, which ranks them there",
                .keys = false},
    CommandInfo{.name = "cut-at-edges",
                .description = "Cut the notes crossing a range's edges, so an operator takes "
                               "only the parts inside: before an operator for it alone (|d]b), "
                               "or in Visual mode on or off for the selection"},
    CommandInfo{.name = "visual-delete-time",
                .description = "In Visual mode, take the selection's time out, closing the gap: "
                               "from the song, when it's on every track",
                .modes = modeBit(Mode::Visual)},
    CommandInfo{.name = "visual-other-end",
                .description = "In Visual mode, go to the selection's other end",
                .modes = modeBit(Mode::Visual)},
    CommandInfo{.name = "operator-line",
                .description = "After an operator, this bar's events, or {count} bars' from it",
                .modes = modeBit(Mode::OperatorPending),
                .count = true},
    CommandInfo{.name = "object-bar-contents",
                .description = "After an operator, this bar's events (ib), or {count} bars' "
                               "from it",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .count = true,
                .object = true},
    CommandInfo{.name = "object-bar",
                .description = "After an operator, this bar as time (ab), which d takes out, "
                               "closing the gap; or {count} bars from it",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .count = true,
                .object = true},
    CommandInfo{.name = "object-section-contents",
                .description = "After an operator, this section's events (is); or {count} "
                               "sections' from it",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .count = true,
                .object = true,
                .here = {"After an operator, this section's events on this track (is); or "
                         "{count} sections' from it",
                         "After an operator, this section's events on every track (is); or "
                         "{count} sections' from it",
                         "After an operator, this section's notes on this track (is); or "
                         "{count} sections' from it",
                         "After an operator, this section's events on this track (is); or "
                         "{count} sections' from it"}},
    CommandInfo{.name = "object-section",
                .description = "On the section strip, after an operator, this section itself, "
                               "on every track (as), which d takes out of the song and y yanks "
                               "as a section; or {count} sections from it",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .count = true,
                .object = true},
    CommandInfo{.name = "object-chord",
                .description = "After an operator, the chord holding the note under the cursor "
                               "(ic): the notes starting with it; or {count} chords from it",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .count = true,
                .object = true},
    CommandInfo{.name = "object-track",
                .description = "On a track, after an operator, this track's events (it), from "
                               "the start to the end",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .count = true,
                .object = true,
                .here = {"After an operator, this track's events (it), from the start to the "
                         "end, or {count} tracks' from it",
                         {},
                         "After an operator, this track's notes (it), from the start to the end",
                         {}}},
    CommandInfo{.name = "object-song",
                .description = "On the section strip, after an operator, the whole song (ie), "
                               "every section on every track",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .object = true},
    CommandInfo{.name = "object-note",
                .description = "After an operator, the note or event under the cursor (in)",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .count = true,
                .object = true},
    CommandInfo{
        .name = "undo", .description = "Undo the last change, or {count} changes", .count = true},
    CommandInfo{.name = "redo",
                .description = "Redo the change undone last, or {count} changes",
                .count = true},
    CommandInfo{.name = "cancel", .description = "Cancel"},
    CommandInfo{.name = "play-toggle",
                .description = "Play from the play-from marker, or stop where it is, the cursor "
                               "landing there"},
    CommandInfo{.name = "play-stop-back",
                .description = "Stop, the cursor going back to where it was when playback "
                               "started; or play, as Space does"},
    CommandInfo{.name = "play-cursor",
                .description = "Put the play-from marker at the cursor, and play from there"},
    CommandInfo{.name = "play-from-set", .description = "Put the play-from marker at the cursor"},
    CommandInfo{.name = "panic",
                .description = "Stop, and send All Notes Off and Reset All Controllers"},
    CommandInfo{.name = "playhead-follow",
                .description = "Follow the playhead again, after moving away from it"},
    CommandInfo{.name = "port",
                .description = "Set the track's output port, by its name or part of it; or "
                               "with no name, show it and list the ports",
                .usage = "[NAME]",
                .keys = false,
                .choices = {Choices::Port}},
    CommandInfo{.name = "event-insert",
                .description =
                    "Insert an event at the cursor: cc 7 100, program 5, bend -200, "
                    "pressure 60, polypressure C4 60, sysex F0 7D 01 F7, or text, "
                    "lyric, cue, copyright or instrument with words in quotes; or tempo 96, "
                    "the conductor's",
                .usage = "EVENT",
                .choices = {Choices::EventKind}},
    CommandInfo{.name = "section-insert-before",
                .description = "Insert a new section before the cursor's, of {count} bars (4), "
                               "asking for its name",
                .count = true},
    CommandInfo{.name = "section-insert-after",
                .description = "Insert a new section after the cursor's, of {count} bars (4), "
                               "asking for its name",
                .count = true},
    CommandInfo{.name = "section-join",
                .description = "Join the cursor's section with the next, or {count} sections "
                               "into one",
                .count = true},
    CommandInfo{.name = "section-split",
                .description = "Start a new section at the cursor's bar line, splitting the "
                               "section it's in; with no name, named Section and a number, then "
                               "asking for a name, as i on the section strip does",
                .usage = "[NAME]"},
    CommandInfo{.name = "bar-delete",
                .description = "Delete the cursor's bar, or {count} bars from it, on every track, "
                               "closing up; they go in the register as a section",
                .count = true,
                .range = true},
    CommandInfo{.name = "bar-insert",
                .description = "Insert an empty bar, or N, before the cursor's bar on every "
                               "track, in the meter there or one given (2 3/4)",
                .usage = "[N] [METER]",
                .count = true,
                .choices = {Choices::Meter, Choices::Meter, Choices::Meter}},
    CommandInfo{.name = "label",
                .description = "Put a label named NAME at the cursor, or rename the one there; "
                               "with no name, asking for one, with its name to edit",
                .usage = "[NAME]",
                .keys = false},
    CommandInfo{.name = "label-delete",
                .description = "Delete the label at the cursor, or every label in a selection",
                .keys = false,
                .range = true},
    CommandInfo{.name = "label-to-section",
                .description = "Start a section at the label at or before the cursor, named "
                               "after it, at the bar line of its bar; the label goes. On a "
                               "selection, at every label in it (vie, the whole song), asking "
                               "about those off a bar line, as one undo step",

                .range = true},
    CommandInfo{.name = "section-rename",
                .description = "Rename the section at the cursor; with no name, asking for one, "
                               "with its name to edit",
                .usage = "[NAME]",

                .choices = {Choices::Section}},
    CommandInfo{.name = "event-yank",
                .description = "Yank the note or event under the cursor, as yin does, or the "
                               "section on the section strip",
                .count = true},
    CommandInfo{.name = "keys-show",
                .description = "Show the keys that do something here, in groups: q or Escape "
                               "closes it, j and k turn its pages"},
    CommandInfo{.name = "tracks-mute-toggle",
                .description = "Mute or unmute the selected tracks, as the cursor's track isn't",
                .modes = modeBit(Mode::Visual)},
    CommandInfo{.name = "tracks-solo-toggle",
                .description = "Solo the selected tracks or stop, as the cursor's track isn't",
                .modes = modeBit(Mode::Visual)},
    CommandInfo{.name = "tracks-chase-toggle",
                .description = "Chase notes on the selected tracks or stop, as the cursor's "
                               "track doesn't",
                .modes = modeBit(Mode::Visual)},
    CommandInfo{.name = "track-mute-toggle",
                .description = "Mute or unmute this track",
                .modes = modeBit(Mode::Normal)},
    CommandInfo{.name = "track-solo-toggle",
                .description = "Solo this track, or stop soloing it",
                .modes = modeBit(Mode::Normal)},
    CommandInfo{
        .name = "track-chase-toggle",
        .description =
            "Toggle note chasing on this track: start notes already held at the playback position",
        .modes = modeBit(Mode::Normal)},
    CommandInfo{.name = "rewind-toggle",
                .description = "Turn rewind on or off: whether the play-from marker stays put "
                               "when playback stops, or moves to where it stopped"},
    CommandInfo{.name = "grid-advance-toggle",
                .description = "Turn gridadvance on or off: whether O moves on by the grid step "
                               "or the note's length"},
    CommandInfo{.name = "command-line", .description = "Open the : command line"},
    CommandInfo{.name = "view-tracks",
                .description = "Show track settings, at track {count} if given",
                .count = true},
    CommandInfo{.name = "view-arrangement", .description = "Show the arrangement"},
    CommandInfo{.name = "view-piano-roll", .description = "Show the piano roll"},
    CommandInfo{.name = "view-events", .description = "Show the event list"},
    CommandInfo{.name = "view-conductor",
                .description = "Show the conductor's list in the event list: its tempos, "
                               "meters, sections and labels"},
    CommandInfo{.name = "view-back", .description = "Go back to the view shown before"},
    atPrompt("command-line-run", "Run the command line"),
    atPrompt("command-line-cancel", "Close the command line"),
    atPrompt("command-line-backspace", "Delete the character before the cursor"),
    atPrompt("command-line-delete", "Delete the character at the cursor"),
    atPrompt("command-line-left", "Move the cursor left"),
    atPrompt("command-line-right", "Move the cursor right"),
    atPrompt("command-line-start", "Move the cursor to the start"),
    atPrompt("command-line-end", "Move the cursor to the end"),
    atPrompt("command-line-clear", "Clear the command line"),
    atPrompt("command-line-next", "Set the field and go to the next, in the inspector"),
    atPrompt("command-line-previous", "Set the field and go to the one before, in the inspector"),
    atPrompt("command-line-page-down", "A screen on in the matches of completion"),
    atPrompt("command-line-page-up", "A screen back in the matches of completion"),
    atPrompt("command-line-older", "Recall the line run before"),
    atPrompt("command-line-newer", "Recall the line run after"),
};

// The meters :bar-insert offers before any others the song has or were typed.
constexpr std::array<core::TimeSignature, 13> kOfferedMetersTable{{{2, 4},
                                                                   {3, 4},
                                                                   {4, 4},
                                                                   {5, 4},
                                                                   {6, 4},
                                                                   {7, 4},
                                                                   {3, 8},
                                                                   {5, 8},
                                                                   {6, 8},
                                                                   {7, 8},
                                                                   {9, 8},
                                                                   {11, 8},
                                                                   {12, 8}}};

} // namespace

std::span<const CommandInfo> sharedCommands()
{
    return kSharedCommands;
}

std::span<const core::TimeSignature> offeredMeters()
{
    return kOfferedMetersTable;
}

std::span<const CommandInfo> Workspace::commands()
{
    // The views' own commands, then the shared ones.
    static const std::vector<CommandInfo> all = []
    {
        std::vector<CommandInfo> result;
        const auto add = [&](std::span<const CommandInfo> commands)
        {
            for (const auto& info : commands)
            {
                if (std::ranges::find(result, info.name, &CommandInfo::name) == result.end())
                {
                    result.push_back(info);
                }
            }
        };
        add(EventListView::commands());
        add(ArrangeView::commands());
        add(PianoRollView::commands());
        add(kSharedCommands);
        // The transforms', from their table, in every view.
        for (const auto& spec : transformSpecs())
        {
            result.push_back(CommandInfo{
                .name = spec.name,
                .description = spec.description,
                .aliases = spec.aliases,
                .usage = spec.usage,
                .count = true,
                .range = true,
                .where = onlyIn({Where::Tracks, Where::Strip, Where::PianoRoll, Where::EventList}),
                .choices = spec.settings.empty()
                               ? std::array<Choices, 3>{}
                               : std::array{Choices::Option, Choices::Option, Choices::Option}});
        }
        // Where the views' commands do something, kept here beside each other, since a view
        // runs commands others describe; a test runs each binding offered in each place.
        using enum Where;
        const std::array<std::pair<std::string_view, std::uint8_t>, 121> where{{
            {"tracks-mute-toggle", onlyIn({TrackList})},
            {"tracks-solo-toggle", onlyIn({TrackList})},
            {"tracks-chase-toggle", onlyIn({TrackList})},
            {"event-insert", onlyIn({Tracks, PianoRoll, EventList, TrackList})},
            {"track-mute-toggle", onlyIn({Tracks, PianoRoll, EventList, TrackList})},
            {"track-solo-toggle", onlyIn({Tracks, PianoRoll, EventList, TrackList})},
            {"track-chase-toggle", onlyIn({Tracks, PianoRoll, EventList, TrackList})},
            {"scroll-page-left", onlyIn({Tracks, PianoRoll})},
            {"visual", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"visual-rows", onlyIn({Tracks, Strip, PianoRoll, EventList, TrackList})},
            {"visual-box", onlyIn({Tracks, Strip, PianoRoll})},
            {"cut-at-edges", onlyIn({Tracks, PianoRoll})},
            {"visual-again", onlyIn({Tracks, Strip, PianoRoll, EventList, TrackList})},
            {"scroll-page-right", onlyIn({Tracks, PianoRoll})},
            {"operator-humanize", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-velocity", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-transpose-up", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-transpose-down", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-shift-later", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-shift-earlier", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-quantize", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-transform", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"section-rename-strip", onlyIn({Strip})},
            {"section-insert-before-strip", onlyIn({Strip})},
            {"section-insert-after-strip", onlyIn({Strip})},
            {"go-section-strip", onlyIn({Tracks, Strip})},
            {"section-join-strip", onlyIn({Strip})},
            {"octave-down", onlyIn({PianoRoll})},
            {"note-lengthen", onlyIn({PianoRoll})},
            {"note-shorten", onlyIn({PianoRoll})},
            {"note-lengthen-fine", onlyIn({PianoRoll})},
            {"note-shorten-fine", onlyIn({PianoRoll})},
            {"velocity-up", onlyIn({PianoRoll})},
            {"velocity-down", onlyIn({PianoRoll})},
            {"section-lengthen", onlyIn({Strip})},
            {"section-shorten", onlyIn({Strip})},
            {"bar-events-next", onlyIn({Tracks})},
            {"bar-events-previous", onlyIn({Tracks})},
            {"move-left", onlyIn({PianoRoll})},
            {"move-right", onlyIn({PianoRoll})},
            {"section-move-earlier", onlyIn({Strip})},
            {"section-move-later", onlyIn({Strip})},
            {"put-merge", onlyIn({Tracks, PianoRoll, EventList})},
            {"put-insert", onlyIn({Tracks, PianoRoll})},
            {"put-ending", onlyIn({Tracks, PianoRoll, EventList})},
            {"put-merge-at-pitch", onlyIn({PianoRoll})},
            {"put-insert-at-pitch", onlyIn({PianoRoll})},
            {"put-after", onlyIn({Strip, TrackList})},
            {"put-before", onlyIn({Strip, TrackList})},
            {"zoom-in", onlyIn({Tracks, Strip, PianoRoll})},
            {"zoom-out", onlyIn({Tracks, Strip, PianoRoll})},
            {"zoom-reset", onlyIn({Tracks, Strip, PianoRoll})},
            {"field-edit", onlyIn({EventList, TrackList})},
            {"bar-open", onlyIn({Tracks})},
            {"section-play", onlyIn({Strip})},
            {"field-increase", onlyIn({EventList, TrackList})},
            {"field-decrease", onlyIn({EventList, TrackList})},
            {"field-increase-fine", onlyIn({EventList, TrackList})},
            {"field-decrease-fine", onlyIn({EventList, TrackList})},
            {"number-increase", onlyIn({EventList, TrackList})},
            {"number-decrease", onlyIn({EventList, TrackList})},
            {"note-add", onlyIn({PianoRoll, EventList})},
            {"cursor-chord-next", onlyIn({PianoRoll, EventList})},
            {"cursor-chord-previous", onlyIn({PianoRoll, EventList})},
            {"cursor-bar-next-scroll", onlyIn({Tracks, PianoRoll})},
            {"cursor-bar-previous-scroll", onlyIn({Tracks, PianoRoll})},
            {"cursor-bar-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-bar-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-bar-line-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-bar-line-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-section-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-section-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-label-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-mark", onlyIn({Tracks, PianoRoll, EventList})},
            {"find-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"search", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-bar", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-section", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-track", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-visible", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-matches-bar", onlyIn({Tracks, PianoRoll, EventList})},
            {"object-match", onlyIn({Tracks, PianoRoll, EventList})},
            // An object never reaches beyond the cursor's place: a track, or on the strip
            // every track (decided November 2026).
            {"object-section", onlyIn({Strip})},
            {"object-song", onlyIn({Strip})},
            {"object-track", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-match", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-matches-section", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-matches-track", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-matches-visible", onlyIn({Tracks, PianoRoll, EventList})},
            {"select-matches", onlyIn({Tracks, PianoRoll, EventList})},
            {"search-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"search-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"search-here", onlyIn({PianoRoll, EventList})},
            {"search-here-back", onlyIn({PianoRoll, EventList})},
            {"cursor-match-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-match-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"find-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"find-repeat", onlyIn({Tracks, PianoRoll, EventList})},
            {"find-repeat-back", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-find-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-find-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-label-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-left-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-right-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-up-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-down-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-centre", onlyIn({Tracks, PianoRoll, EventList, TrackList})},
            {"scroll-half-down", onlyIn({Tracks, PianoRoll, EventList, TrackList})},
            {"scroll-half-up", onlyIn({Tracks, PianoRoll, EventList, TrackList})},
            {"scroll-page-down", onlyIn({Tracks, PianoRoll, EventList, TrackList})},
            {"scroll-page-up", onlyIn({Tracks, PianoRoll, EventList, TrackList})},
            {"cursor-note-next", onlyIn({PianoRoll})},
            {"cursor-note-previous", onlyIn({PianoRoll})},
            {"octave-up", onlyIn({PianoRoll})},
            {"note-add-advance", onlyIn({PianoRoll})},
            {"default-note-set", onlyIn({PianoRoll})},
            {"grid-finer", onlyIn({PianoRoll})},
            {"grid-coarser", onlyIn({PianoRoll})},
            {"grid-triplets", onlyIn({PianoRoll})},
            {"grid", onlyIn({PianoRoll})},
            {"len", onlyIn({PianoRoll})},
        }};
        for (auto& info : result)
        {
            if (const auto it = std::ranges::find(
                    where, info.name, &std::pair<std::string_view, std::uint8_t>::first);
                it != where.end())
            {
                info.where = it->second;
            }
            else if (info.name.starts_with("move-"))
            {
                info.where = onlyIn({PianoRoll}); // the Alt keys move notes
            }
        }
        const std::array<std::pair<std::string_view, std::string_view>, 15> trackDescriptions{{
            {"cursor-left", "Move to the previous track field"},
            {"cursor-right", "Move to the next track field"},
            {"cursor-down", "Move down a track"},
            {"cursor-up", "Move up a track"},
            {"cursor-first", "Move to the first track field"},
            {"cursor-last", "Move to the last track field"},
            {"go-first", "Go to the first track, or track {count}"},
            {"go-last", "Go to the last track, or track {count}"},
            {"visual-rows", "Select whole tracks, including settings and all section content"},
            {"operator-yank", "Yank selected whole tracks, or yy this track"},
            {"operator-delete", "Delete explicitly selected whole tracks; use V first"},
            {"event-yank", "Yank this whole track, including its settings"},
            {"put-after",
             "Insert whole tracks after this row; in Visual mode, replace the selected tracks"},
            {"put-before",
             "Insert whole tracks before this row; in Visual mode, replace the selected tracks"},
            {"inspector-edit", "Edit the selected track field"},
        }};
        for (auto& info : result)
        {
            for (const auto& [name, description] : trackDescriptions)
                if (info.name == name)
                    info.here[static_cast<std::size_t>(TrackList)] = description;
            if (info.where == 0 &&
                (info.object || info.name == "event-delete" || info.name == "event-delete-line" ||
                 info.name == "visual-delete-time" || info.name == "bar-delete" ||
                 info.name == "bar-insert" || info.name.starts_with("section-") ||
                 info.name == "label-to-section"))
                info.where = onlyIn({Tracks, Strip, PianoRoll, EventList});
        }
        return result;
    }();
    return all;
}

} // namespace modal::ui
