// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/pitch.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/palette.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <map>
#include <tuple>
#include <utility>

namespace modal::ui
{

namespace
{

constexpr Style kBarText{Role::BarText, Role::Bar, kPlain};
constexpr Style kCursor{Role::CursorText, Role::Cursor, kPlain};

constexpr CommandInfo atPrompt(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .modes = modeBit(Mode::CommandLine)};
}

// Motions that only move the cursor, rather than landing on a boundary they name, so aren't
// ranges after an operator (see Ranges, objects and putting in the design): a bar's length on,
// the end of whatever the view's row is, and notes and chords, which objects take.
constexpr std::array<std::string_view, 8> kMovesOnly{
    "cursor-bar-next",  "cursor-bar-previous",  "cursor-first",      "cursor-last",
    "cursor-note-next", "cursor-note-previous", "cursor-chord-next", "cursor-chord-previous"};

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
                .description = "Move the events a range or object takes later by the grid step "
                               "(t]b), or {count} steps; tt this bar's. In the piano roll, its "
                               "notes",
                .count = true},
    CommandInfo{.name = "operator-shift-earlier",
                .description = "Move the events a range or object takes earlier by the grid "
                               "step (T]b), or {count} steps; TT this bar's",
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
    CommandInfo{.name = "transform-transpose",
                .description = "Transpose the notes a range or object takes by a number of "
                               "semitones, asking how many (+12, -3); :transpose N those of the "
                               "selection, or else of the cursor's bar",
                .aliases = "transpose",
                .usage = "[SEMITONES]",
                .count = true,
                .range = true},
    CommandInfo{.name = "transform-velocity-lift",
                .description = "Lift the velocities of the notes a range or object takes a "
                               "percentage of the way towards 127, keeping their order and "
                               "proportions, asking how far (25%); :vel-lift N% those of the "
                               "selection, or else of the cursor's bar",
                .aliases = "vel-lift",
                .usage = "[PERCENT]",
                .count = true,
                .range = true},
    CommandInfo{.name = "transform-velocity-lower",
                .description = "Lower the velocities of the notes a range or object takes a "
                               "percentage of the way towards the floor (velfloor), keeping "
                               "their order and proportions, asking how far (25%)",
                .aliases = "vel-lower",
                .usage = "[PERCENT]",
                .count = true,
                .range = true},
    CommandInfo{.name = "transform-velocity-limit",
                .description = "Keep the velocities of the notes a range or object takes "
                               "between two values, asking which (20-110)",
                .aliases = "vel-limit",
                .usage = "[LOW-HIGH]",
                .count = true,
                .range = true},
    CommandInfo{.name = "transform-velocity-ramp",
                .description = "A crescendo or diminuendo over the notes a range or object "
                               "takes, by time: velocities set from one value to another "
                               "(60-100), or their own scaled from one percentage to another "
                               "(80%-120%)",
                .aliases = "vel-ramp",
                .usage = "[FROM-TO]",
                .count = true,
                .range = true},
    CommandInfo{.name = "transform-velocity-compress",
                .description =
                    "Compress the velocities of the notes a range or object takes, pulling each a "
                    "percentage of the way towards the average of those around it "
                    "(50%): evens out the playing, keeping its swells; window= how "
                    "far around (1bar, 2bars, 1/2), centre= local, average or a "
                    "velocity, threshold= only those above (>90) or below (<30)",
                .aliases = "vel-compress",
                .usage = "[PERCENT] [window=1bar] [centre=local] [threshold=>N]",
                .count = true,
                .range = true,
                .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{.name = "transform-velocity-expand",
                .description =
                    "Expand the velocities of the notes a range or object takes, pushing each a "
                    "percentage further from the average of those around it (50%): "
                    "sharpens accents; takes compress's options",
                .aliases = "vel-expand",
                .usage = "[PERCENT] [window=1bar] [centre=local] [threshold=>N]",
                .count = true,
                .range = true,
                .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{
        .name = "transform-velocity-shape",
        .description =
            "Shape the velocities of the notes a range or object takes, scaling the contour of "
            "their averages over the window (130% bigger swells, 70% "
            "flatter), keeping each note's place on it",
        .aliases = "vel-shape",
        .usage = "[PERCENT] [window=1bar]",
        .count = true,
        .range = true,
        .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{
        .name = "transform-chord-accent",
        .description =
            "Accent the top note of each chord a range or object takes, up by an amount and the "
            "others down by it (10), to bring out a melody",
        .aliases = "chord-accent",
        .usage = "[AMOUNT]",
        .count = true,
        .range = true},
    CommandInfo{.name = "transform-chord-balance",
                .description =
                    "Balance each chord a range or object takes by pitch: a straight slope of "
                    "velocities from its lowest note to its highest (70-95), or "
                    "reversed to bring out the bass (95-70)",
                .aliases = "chord-balance",
                .usage = "[BOTTOM-TOP]",
                .count = true,
                .range = true},
    CommandInfo{
        .name = "transform-chord-compress",
        .description =
            "Compress each chord a range or object takes, pulling its velocities a percentage of "
            "the way together about their average (50%)",
        .aliases = "chord-compress",
        .usage = "[PERCENT]",
        .count = true,
        .range = true},
    CommandInfo{
        .name = "transform-chord-expand",
        .description =
            "Expand each chord a range or object takes, pushing its velocities a percentage "
            "further apart about their average (50%)",
        .aliases = "chord-expand",
        .usage = "[PERCENT]",
        .count = true,
        .range = true},
    CommandInfo{.name = "transform-transpose-key",
                .description = "Transpose the notes a range or object takes by degrees of a key's "
                               "scale, asking for the "
                               "key (the last used, at Enter) and how many (+2, -3); notes "
                               "outside the scale keep their distance above its note below",
                .aliases = "transpose-key",
                .usage = "[DEGREES] [key=KEY]",
                .count = true,
                .range = true},
    CommandInfo{
        .name = "transform-invert",
        .description =
            "Invert the notes a range or object takes, mirroring their pitches about a pivot, "
            "chromatically, asking which (the first note, at Enter)",
        .aliases = "invert",
        .usage = "[PIVOT]",
        .count = true,
        .range = true},
    CommandInfo{
        .name = "transform-invert-key",
        .description =
            "Invert the notes a range or object takes within a key's scale, mirroring them by its "
            "degrees about a pivot, asking for the key and the pivot",
        .aliases = "invert-key",
        .usage = "[PIVOT] [key=KEY]",
        .count = true,
        .range = true},
    CommandInfo{
        .name = "transform-retrograde",
        .description =
            "Reverse the order in time of the notes a range or object takes, mirroring their "
            "rhythm",
        .aliases = "retrograde",
        .count = true,
        .range = true},
    CommandInfo{.name = "visual",
                .description = "Visual mode: select from here as the cursor moves, a range of "
                               "time on every track; v again or Escape ends it"},
    CommandInfo{.name = "visual-rows",
                .description = "Visual mode by whole rows: tracks in the arrangement, at every "
                               "time until an object sets it (Vis); V again or Escape ends it"},
    CommandInfo{.name = "visual-box",
                .description = "Visual mode by a box: tracks by bars in the arrangement, pitches "
                               "by grid steps in the piano roll; Ctrl-v again or Escape ends it"},
    CommandInfo{.name = "visual-again", .description = "Select the last Visual selection again"},
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
                               "octave (fE, fF#, fBb), {count} times; after a find, the next "
                               "again; after an operator, the range to it and through it",
                .count = true,
                .motion = true},
    CommandInfo{.name = "find-previous",
                .description = "Go to the note before of the pitch class named after it, any "
                               "octave (FE, FF#, FBb), {count} times; after a find, the one "
                               "before again; after an operator, the range from it",
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
                .description = "Select the match of the last search at the cursor, or the next"},
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
                .description = "After an operator, this section's events on this track (is), "
                               "or on every track on the section strip; or {count} sections' "
                               "from it",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .count = true,
                .object = true},
    CommandInfo{.name = "object-section",
                .description = "After an operator, this section itself (as), which d takes out "
                               "of the song and y yanks as a section; or {count} sections from "
                               "it",
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
                .description = "After an operator, this track's events (it), from the start to "
                               "the end; in the arrangement, or {count} tracks' from it",
                .modes = modeBit(Mode::OperatorPending) | modeBit(Mode::Visual),
                .count = true,
                .object = true},
    CommandInfo{.name = "object-song",
                .description = "After an operator, the whole song (ie): every track in the "
                               "arrangement, this track's events elsewhere",
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
    CommandInfo{.name = "marker-set", .description = "Put the play-from marker at the cursor"},
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
    CommandInfo{.name = "section-split",
                .description = "Start a new section at the cursor's bar line, splitting the "
                               "section it's in; with no name, named Section and a number, then "
                               "asking for a name, as i on the section strip does",
                .usage = "[NAME]",
                .keys = false},
    CommandInfo{.name = "bar-delete",
                .description = "Delete the cursor's bar, or {count} bars from it, on every track, "
                               "closing up; they go in the register as a section",
                .count = true,
                .keys = false,
                .range = true},
    CommandInfo{.name = "bar-insert",
                .description = "Insert an empty bar, or N, before the cursor's bar on every "
                               "track, in the meter there or one given (2 3/4)",
                .usage = "[N] [METER]",
                .keys = false,
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
                .keys = false,
                .range = true},
    CommandInfo{.name = "section-rename",
                .description = "Rename the section at the cursor; with no name, asking for one, "
                               "with its name to edit",
                .usage = "[NAME]",
                .keys = false,
                .choices = {Choices::Section}},
    CommandInfo{.name = "yank-this",
                .description = "Yank the note or event under the cursor, as yin does, or the "
                               "section on the section strip",
                .count = true},
    CommandInfo{.name = "keys-show",
                .description = "Show the keys that do something here, in groups: q or Escape "
                               "closes it, j and k turn its pages"},
    CommandInfo{.name = "rewind-toggle",
                .description = "Turn rewind on or off: whether the play-from marker stays put "
                               "when playback stops, or moves to where it stopped"},
    CommandInfo{.name = "grid-advance-toggle",
                .description = "Turn gridadvance on or off: whether O moves on by the grid step "
                               "or the note's length"},
    CommandInfo{.name = "command-line", .description = "Open the : command line"},
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
constexpr std::array<core::TimeSignature, 13> kOfferedMeters{{{2, 4},
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

// A register's letter, as its lower case.
char32_t lowerLetter(char32_t letter)
{
    return letter >= U'A' && letter <= U'Z' ? letter - U'A' + U'a' : letter;
}

bool isShared(std::string_view name)
{
    return std::ranges::find(kSharedCommands, name, &CommandInfo::name) != kSharedCommands.end();
}

// The kind of selection v, V and Ctrl-v make.
SelectionKind selectionKind(std::string_view command)
{
    return command == "visual"        ? SelectionKind::Range
           : command == "visual-rows" ? SelectionKind::Rows
                                      : SelectionKind::Box;
}

} // namespace

std::optional<bool> editLine(CommandLine& line, std::string_view command)
{
    if (command == "command-line-backspace")
    {
        return line.backspace();
    }
    if (command == "command-line-delete")
    {
        return line.erase();
    }
    if (command == "command-line-older")
    {
        return line.older();
    }
    if (command == "command-line-newer")
    {
        return line.newer();
    }
    if (command == "command-line-left")
    {
        line.left();
    }
    else if (command == "command-line-right")
    {
        line.right();
    }
    else if (command == "command-line-start")
    {
        line.start();
    }
    else if (command == "command-line-end")
    {
        line.end();
    }
    else if (command == "command-line-clear")
    {
        line.clear();
    }
    else
    {
        return std::nullopt;
    }
    return true;
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
        // Where the views' commands do something, kept here beside each other, since a view
        // runs commands others describe; a test runs each binding offered in each place.
        using enum Where;
        const std::array<std::pair<std::string_view, std::uint8_t>, 115> where{{
            {"scroll-page-left", onlyIn({Tracks, PianoRoll})},
            {"visual", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"visual-rows", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"visual-box", onlyIn({Tracks, Strip, PianoRoll})},
            {"cut-at-edges", onlyIn({Tracks, PianoRoll})},
            {"visual-again", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"scroll-page-right", onlyIn({Tracks, PianoRoll})},
            {"operator-humanize", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-velocity", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-transpose-up", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-transpose-down", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-shift-later", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-shift-earlier", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-quantize", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"operator-transform", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-transpose", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-lift", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-lower", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-limit", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-ramp", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-transpose-key", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-invert", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-invert-key", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-retrograde", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-compress", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-expand", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-shape", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-chord-accent", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-chord-balance", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-chord-compress", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-chord-expand", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"change", onlyIn({Strip})},
            {"insert-before", onlyIn({Strip})},
            {"insert-after", onlyIn({Strip})},
            {"go-section-strip", onlyIn({Tracks, Strip})},
            {"join", onlyIn({Strip})},
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
            {"move-left", onlyIn({Strip, PianoRoll})},
            {"move-right", onlyIn({Strip, PianoRoll})},
            {"register-put-before", onlyIn({Tracks, Strip, PianoRoll})},
            {"zoom-in", onlyIn({Tracks, Strip, PianoRoll})},
            {"zoom-out", onlyIn({Tracks, Strip, PianoRoll})},
            {"zoom-reset", onlyIn({Tracks, Strip, PianoRoll})},
            {"field-edit", onlyIn({Tracks, Strip, EventList})},
            {"field-increase", onlyIn({EventList})},
            {"field-decrease", onlyIn({EventList})},
            {"field-increase-fine", onlyIn({EventList})},
            {"field-decrease-fine", onlyIn({EventList})},
            {"number-increase", onlyIn({EventList})},
            {"number-decrease", onlyIn({EventList})},
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
            {"cursor-find-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-find-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-label-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-left-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-right-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-up-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-down-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-centre", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-half-down", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-half-up", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-page-down", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-page-up", onlyIn({Tracks, PianoRoll, EventList})},
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
        return result;
    }();
    return all;
}

Keymap Workspace::defaultKeymap()
{
    Config config;
    CommandRegistry all;
    all.add(commands());
    all.add(windowCommands());
    ConfigReader reader{config, all};
    reader.runText(builtInModalrc(), "built-in modalrc", {});
    assert(reader.errors().empty()); // the tests run the built-in modalrc too
    return config.keymap;
}

Workspace::Workspace(CommandRegistry& registry, core::Document& document, Keymap keymap,
                     std::int64_t timeout)
    : commands_(registry)
    , document_(document)
    , eventList_(std::make_unique<EventListView>(document, static_cast<ViewHost&>(*this)))
    , arrange_(std::make_unique<ArrangeView>(document, static_cast<ViewHost&>(*this)))
    , pianoRoll_(std::make_unique<PianoRollView>(document, static_cast<ViewHost&>(*this)))
    , current_(eventList_.get())
    , keymap_(std::move(keymap))
    , keys_(timeout)
{
    keys_.setTakesCharacter([this](std::string_view command) { return takesCharacter(command); });
    for (const auto& info : commands())
    {
        registry.add(info,
                     [this, name = info.name](const CommandCall& call) -> CommandResult
                     {
                         if (name == "port")
                         {
                             return port(call.arguments);
                         }
                         if (name == "section-split")
                         {
                             return section(call.arguments);
                         }
                         if (name == "mark-delete")
                         {
                             return deleteMarks(call.arguments);
                         }
                         if (name == "label")
                         {
                             return label(call.arguments);
                         }
                         if (name == "label-delete")
                         {
                             return deleteLabels();
                         }
                         if (name == "label-to-section")
                         {
                             return labelToSection();
                         }
                         if (name == "bar-delete")
                         {
                             return barDelete(call.count);
                         }
                         if (name == "bar-insert")
                         {
                             return barInsert(call.arguments);
                         }
                         if (name == "section-rename")
                         {
                             return renameSection(call.arguments);
                         }
                         if (name.starts_with("transform-") && !call.line.empty())
                         {
                             return transformLine(name, call.arguments);
                         }
                         if (auto result = view().runLine(name, call.arguments))
                         {
                             return *result;
                         }
                         if (mode_ == Mode::Visual)
                         {
                             if (const auto done = runVisual(name, call.count))
                             {
                                 return {*done};
                             }
                         }
                         if (name == "mark-go")
                         {
                             awaiting_ = Awaiting{std::string{name}, call.count};
                             return {true};
                         }
                         if (name == "find-next" || name == "find-previous")
                         {
                             return {startFind(name == "find-next", call.count)};
                         }
                         if (name == "search-next" || name == "search-previous")
                         {
                             const bool next = name == "search-next";
                             if (!search_)
                             {
                                 operator_.reset();
                                 mode_ = mode_ == Mode::OperatorPending ? Mode::Normal : mode_;
                                 setMessage("No search yet: " + keysOf("search") + " searches",
                                            MessageKind::Information);
                                 return {true};
                             }
                             if (mode_ == Mode::OperatorPending)
                             {
                                 return {runOperator(next == searchForward_
                                                         ? "cursor-match-next"
                                                         : "cursor-match-previous",
                                                     call.count)};
                             }
                             return {goToMatch(next, call.count)};
                         }
                         // After an operator, every command is its motion, or gives it up.
                         if (mode_ == Mode::OperatorPending)
                         {
                             return {runOperator(name, call.count)};
                         }
                         if (isShared(name))
                         {
                             return {runShared(name, call.count).value_or(false)};
                         }
                         return {runViewCommand(name, call.count)};
                     });
    }
    buildOperatorKeymap();
}

Workspace::~Workspace() = default;

void Workspace::setKeymap(Keymap keymap)
{
    keymap_ = std::move(keymap);
    buildOperatorKeymap();
}

const Keymap& Workspace::keymapHere() const
{
    resolveKeymaps();
    return keymapHere_;
}

const Keymap& Workspace::operatorKeymapHere() const
{
    resolveKeymaps();
    return operatorKeymapHere_;
}

void Workspace::resolveKeymaps() const
{
    // Keys bound to several commands mean the one that does something here (J joins sections
    // on the strip and goes down an octave in the piano roll).
    const auto here = where();
    if (resolvedFor_ == here)
    {
        return;
    }
    const auto offeredHere = [this](const std::string& command) { return offered(command); };
    keymapHere_ = keymap_.resolved(offeredHere);
    operatorKeymapHere_ = operatorKeymap_.resolved(offeredHere);
    resolvedFor_ = here;
}

void Workspace::buildOperatorKeymap()
{
    resolvedFor_.reset();
    operatorKeymap_ = keymap_;
    for (const auto& [keys, binding] : keymap_.bindings(Mode::Normal))
    {
        const auto* info = commands_.find(binding.command);
        // An operator's own key again takes the cursor's bar (dd, >>); another's is refused.
        const bool wanted =
            isOperator(binding.command) || binding.command == "cancel" || (info && info->motion);
        if (wanted && keymap_.match(Mode::OperatorPending, keys).binding == nullptr)
        {
            operatorKeymap_.bind(Mode::OperatorPending, keys, binding);
        }
    }
    // Visual mode's own keys, then the objects, which select what they take (vis), then
    // Normal mode's, but for keys that start an object's (i, a).
    const auto free = [&](const std::vector<Key>& keys)
    {
        const auto match = operatorKeymap_.match(Mode::Visual, keys);
        return match.binding == nullptr && !match.longer;
    };
    for (const auto& [keys, binding] : keymap_.bindings(Mode::OperatorPending))
    {
        const auto* info = commands_.find(binding.command);
        if (info != nullptr && info->object && free(keys))
        {
            operatorKeymap_.bind(Mode::Visual, keys, binding);
        }
    }
    for (const auto& [keys, binding] : keymap_.bindings(Mode::Normal))
    {
        if (free(keys))
        {
            operatorKeymap_.bind(Mode::Visual, keys, binding);
        }
    }
}

const std::vector<core::Event>& Workspace::registerEvents() const
{
    static const std::vector<core::Event> none;
    return register_.tracks.empty() ? none : register_.tracks.front();
}

std::string Workspace::pending() const
{
    std::string text = cutNext_ ? keysOf("cut-at-edges") : std::string{};
    if (registerName_)
    {
        text += keysOf("register-use") + encodeUtf8(std::u32string(1, *registerName_));
    }
    if (operator_)
    {
        text = (operator_->count > 0 ? std::to_string(operator_->count) : std::string{}) +
               keysOf(operator_->name);
    }
    if (awaiting_)
    {
        text += keysOf(awaiting_->command);
    }
    text += keys_.pending();
    if (text.empty() && naming_ && naming_->redo)
    {
        // An operator's find that a sharp or flat may yet change (dfC).
        text = (naming_->redo->op.count > 0 ? std::to_string(naming_->redo->op.count)
                                            : std::string{}) +
               keysOf(naming_->redo->op.name) +
               keysOf(naming_->forward ? "find-next" : "find-previous") + findName_;
    }
    if (text.empty() && findRepeats_)
    {
        text = keysOf(findForward_ ? "find-next" : "find-previous") + findName_;
    }
    return text;
}

void Workspace::resize(int width, int height, CellSize cell)
{
    // Called before every frame, so only a new size scrolls to the cursor, not playback.
    cell = {std::max(cell.width, 1), std::max(cell.height, 1)};
    if (width == width_ && height == height_ && cell.width == cell_.width &&
        cell.height == cell_.height)
    {
        return;
    }
    width_ = width;
    height_ = height;
    cell_ = cell;
    layOut();
}

void Workspace::layOut()
{
    if (width_ == 0)
    {
        return;
    }
    // The view's own bar and the inspector, each with half a line after it, then the view, and
    // the status line at the bottom.
    layout_ = layoutWindow(width_, height_, cell_, view().topHeights(cell_), 1, cell_.height / 2);
    view().resize(layout_, width_, cell_);
}

void Workspace::refresh()
{
    eventList_->refresh();
    arrange_->refresh();
    pianoRoll_->refresh();
}

void Workspace::showSong()
{
    eventList_->showSong();
    arrange_->showSong();
    pianoRoll_->showSong();
    history_.clear();
    layOut();
}

ViewKind Workspace::shown() const
{
    return current_ == arrange_.get()     ? ViewKind::Arrangement
           : current_ == pianoRoll_.get() ? ViewKind::PianoRoll
                                          : ViewKind::EventList;
}

void Workspace::show(ViewKind kind)
{
    switch (kind)
    {
    case ViewKind::Arrangement:
        switchTo(*arrange_, true);
        break;
    case ViewKind::EventList:
        switchTo(*eventList_, true);
        break;
    case ViewKind::PianoRoll:
        switchTo(*pianoRoll_, true);
        break;
    }
}

void Workspace::open(ViewKind kind)
{
    switchTo(kind == ViewKind::Arrangement ? static_cast<View&>(*arrange_)
             : kind == ViewKind::PianoRoll ? static_cast<View&>(*pianoRoll_)
                                           : static_cast<View&>(*eventList_),
             true, true);
}

void Workspace::switchTo(View& view, bool remember, bool fresh)
{
    if (&view == current_)
    {
        if (fresh)
        {
            view.open(view.cursor());
        }
        return;
    }
    const auto cursor = current_->cursor();
    current_->stopPlayhead(std::nullopt);
    if (remember)
    {
        history_.push_back(current_);
    }
    current_ = &view;
    view.refresh();
    layOut();
    if (fresh)
    {
        view.open(cursor);
    }
    else
    {
        view.setCursor(cursor);
    }
    if (wasPlaying_)
    {
        showPlayhead();
    }
}

bool Workspace::handle(const Key& key, std::int64_t now)
{
    // Keys typed while a macro records go into it as typed (decided October 2026, as Vim's
    // q), but for those of the binding that ends it, which it takes off: those since nothing
    // was waiting.
    if (recording_ != 0)
    {
        if (keys_.pending().empty())
        {
            recordedBefore_ = recorded_.size();
        }
        recorded_.push_back(key);
    }
    return recordChange(key, now);
}

bool Workspace::recordChange(const Key& key, std::int64_t now)
{
    // The keys of each change, for . to repeat (decided for milestone 6, as Vim's .): those
    // typed from Normal mode until it's back there, with nothing waiting, if the song changed
    // meanwhile; not undo, redo or . itself, nor while . or a macro replays them.
    if (replaying_)
    {
        return handleKey(key, now);
    }
    // An operator's find that a sharp or flat may yet change (dfB) is done at any other key,
    // the change with it, the key not being part of it.
    bool changed = false;
    const bool accidental = key.named == NamedKey::None && (key.modifiers & (kCtrl | kAlt)) == 0 &&
                            (key.character == U'#' || key.character == U'b');
    if (naming_ && naming_->redo && !accidental)
    {
        lastTime_ = now;
        settleNaming();
        changed = true;
        endChange();
    }
    if (changeKeys_.empty())
    {
        versionBefore_ = document_.version();
    }
    changeKeys_.push_back(key);
    changed = handleKey(key, now) || changed;
    endChange();
    return changed;
}

void Workspace::endChange()
{
    // A command waiting for its letter (@a, "ap) isn't idle yet, so . repeats the whole of it.
    const bool idle = mode_ == Mode::Normal && keys_.pending().empty() && !operator_ &&
                      !question_ && !cutNext_ && !awaiting_ && !registerName_ && !naming_;
    if (idle && !changeKeys_.empty())
    {
        if (document_.version() != versionBefore_ && !notAChange_)
        {
            lastChange_ = std::move(changeKeys_);
        }
        changeKeys_.clear();
        notAChange_ = false;
    }
}

bool Workspace::takesCharacter(std::string_view command) const
{
    return command == "register-use" || command == "mark-set" || command == "mark-go" ||
           command == "find-next" || command == "find-previous" || command == "macro-play" ||
           (command == "macro-record" && recording_ == 0);
}

bool Workspace::characterAfter(std::string_view command, const Key& key, int count)
{
    if (key.named == NamedKey::Escape)
    {
        if (operator_)
        {
            operator_.reset(); // d' Escape gives the operator up too
            mode_ = Mode::Normal;
        }
        return true;
    }
    if (key.named != NamedKey::None || (key.modifiers & (kCtrl | kAlt)) != 0)
    {
        setMessage(keysFor(command) + (command.starts_with("find-") ? " takes a note name after it"
                                                                    : " takes a letter after it"),
                   MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        if (operator_)
        {
            operator_.reset();
            mode_ = Mode::Normal;
        }
        return true;
    }
    return withCharacter(command, key.character, count);
}

bool Workspace::withCharacter(std::string_view command, char32_t character, int count)
{
    if (command == "mark-set")
    {
        return setMark(character);
    }
    if (command == "mark-go")
    {
        return goToMark(character, count);
    }
    if (command == "find-next" || command == "find-previous")
    {
        return nameFind(command, character, count);
    }
    if (command == "macro-record" || command == "macro-play")
    {
        const bool play = command == "macro-play";
        if (play && character == U'@')
        {
            if (lastMacro_ == 0)
            {
                setMessage("No macro played yet to play again", MessageKind::Information);
                macroFailed_ = macroDepth_ > 0;
                return true;
            }
            return playMacro(lastMacro_, count);
        }
        if (!((character >= U'a' && character <= U'z') || (character >= U'A' && character <= U'Z')))
        {
            setMessage(play ? "Macros are named by letters, @a to @z, or @@ plays the last again"
                            : "Macros are named by letters, Qa to Qz, a capital adding to one",
                       MessageKind::Information);
            macroFailed_ = macroDepth_ > 0;
            return true;
        }
        const char32_t name = lowerLetter(character);
        if (play)
        {
            return playMacro(name, count);
        }
        recording_ = name;
        const auto* had = remembered_.macro(name);
        recorded_ = character != name && had != nullptr ? *had : std::vector<Key>{};
        recordedBefore_ = recorded_.size();
        return true;
    }
    if (command == "register-use")
    {
        if (!((character >= U'a' && character <= U'z') || (character >= U'A' && character <= U'Z')))
        {
            setMessage("Registers are named by letters, \"a to \"z, a capital adding to one",
                       MessageKind::Information);
            return true;
        }
        registerName_ = character;
        registerBefore_ = register_;
        return true;
    }
    return false;
}

EventRegister Workspace::appended(EventRegister to, const EventRegister& more)
{
    // Laid end to end (decided October 2026): after the span what's there took, its rests kept, or
    // without one, as from the event list's rows, at its last event's time; sections after
    // sections.
    if (to.empty())
    {
        return more;
    }
    if (!more.sections.empty() || !to.sections.empty())
    {
        to.sections.insert(to.sections.end(), more.sections.begin(), more.sections.end());
        return to;
    }
    core::Tick offset = to.span;
    if (offset == 0)
    {
        for (const auto& events : to.tracks)
        {
            for (const auto& event : events)
            {
                offset = std::max(offset, event.tick);
            }
        }
    }
    to.tracks.resize(std::max(to.tracks.size(), more.tracks.size()));
    for (std::size_t k = 0; k < more.tracks.size(); ++k)
    {
        for (auto event : more.tracks[k])
        {
            event.tick += offset;
            to.tracks[k].push_back(std::move(event));
        }
    }
    to.span = more.span > 0 ? offset + more.span : 0;
    to.bars = to.bars && more.bars;
    return to;
}

std::string Workspace::registerAbout(const EventRegister& reg)
{
    // What a register holds: "12 events on 2 tracks", "2 sections, Verse, Chorus".
    if (!reg.sections.empty())
    {
        std::string names;
        for (const auto& section : reg.sections)
        {
            names += (names.empty() ? "" : ", ") + section->name();
        }
        return std::to_string(reg.sections.size()) +
               (reg.sections.size() == 1 ? " section, " : " sections, ") + names;
    }
    std::size_t events = 0;
    std::size_t tracks = 0;
    for (const auto& list : reg.tracks)
    {
        events += list.size();
        tracks += list.empty() ? 0 : 1;
    }
    return eventCount(events) + " on " + std::to_string(tracks) +
           (tracks == 1 ? " track" : " tracks");
}

bool Workspace::sayRegisters()
{
    // Each holding something, the unnamed first, one a line: "a : 12 events on 2 tracks.
    std::vector<std::pair<std::string, std::string>> lines;
    if (!register_.empty())
    {
        lines.emplace_back("\"\"", registerAbout(register_));
    }
    for (const auto& [name, reg] : registers_)
    {
        if (!reg.empty())
        {
            lines.emplace_back("\"" + encodeUtf8(std::u32string(1, name)), registerAbout(reg));
        }
    }
    if (lines.empty())
    {
        setMessage("The registers are empty", MessageKind::Information);
        return true;
    }
    listing_ = std::move(lines);
    return true;
}

Region Workspace::listingRegion() const
{
    // Above the status line, as the hint menu is: each name right-aligned in the keys' colour,
    // a colon in one column, and what it is.
    const int margin = BarSpacing::forCell(cell_).margin;
    const int pad = cell_.height / 4;
    const int height = static_cast<int>(listing_.size()) * cell_.height + 2 * pad;
    const auto& status = layout_.bottom[0];
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    std::size_t width = 0;
    for (const auto& [name, text] : listing_)
    {
        width = std::max(width, decodeUtf8(name).size());
    }
    const auto room = static_cast<std::size_t>(std::max(
        (region.bounds.width - 2 * margin) / std::max(cell_.width, 1) - static_cast<int>(width) - 2,
        1));
    for (std::size_t i = 0; i < listing_.size(); ++i)
    {
        const auto& [name, said] = listing_[i];
        const auto shown = decodeUtf8(name);
        auto text = decodeUtf8(said);
        if (text.size() > room)
        {
            text = text.substr(0, room - 1) + U"…";
        }
        const int y = pad + static_cast<int>(i) * cell_.height;
        const auto x = [&](std::size_t column)
        { return margin + static_cast<int>(column) * cell_.width; };
        region.text.push_back(
            {x(width - shown.size()), y, shown, {Role::HintKey, Role::Panel, kPlain}});
        region.text.push_back({x(width), y, U":", {Role::Dim, Role::Panel, kPlain}});
        region.text.push_back({x(width + 2), y, text, {Role::Text, Role::Panel, kPlain}});
    }
    return region;
}

bool Workspace::setMark(char32_t character)
{
    // m Space is the play-from marker, which the keys bind apart, unless typed slowly.
    if (character == U' ')
    {
        return run("marker-set", 0).changed;
    }
    if (character < U'a' || character > U'z')
    {
        setMessage("Marks are named by letters, a to z", MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        return true;
    }
    const auto cursor = view().cursor();
    core::Mark mark{.letter = static_cast<char>(character),
                    .tick = cursor.tick,
                    .track = cursor.track,
                    .pitch = core::Mark::kNoPitch};
    if (where() == Where::PianoRoll && cursor.pitch)
    {
        mark.pitch = *cursor.pitch;
    }
    document_.edit("Mark " + std::string(1, mark.letter), view().place(),
                   [&](core::Song& song) { song.setMark(mark); });
    refresh();
    return true;
}

bool Workspace::goToMark(char32_t character, int count)
{
    // A mark's place, or with ' (or `) where the cursor was before the last jump, swapping
    // with it so '' goes back and forth. Without a pitch, the cursor keeps its own; without
    // a track (the conductor's), its track.
    const bool back = character == U'\'' || character == U'`';
    auto to = view().cursor();
    if (back)
    {
        if (!jumpedFrom_)
        {
            setMessage("No jump yet to go back from", MessageKind::Information);
            macroFailed_ = macroDepth_ > 0;
            return true;
        }
        to = *jumpedFrom_;
    }
    else
    {
        const auto* mark = character >= U'a' && character <= U'z'
                               ? document_.song().mark(static_cast<char>(character))
                               : nullptr;
        if (mark == nullptr)
        {
            setMessage(character >= U'a' && character <= U'z'
                           ? "Mark " + encodeUtf8(std::u32string(1, character)) + " isn't set"
                           : std::string{"Marks are named by letters, a to z"},
                       MessageKind::Information);
            macroFailed_ = macroDepth_ > 0;
            if (operator_)
            {
                operator_.reset();
                mode_ = Mode::Normal;
            }
            return true;
        }
        to.tick = mark->tick;
        if (mark->track)
        {
            to.track = mark->track;
        }
        if (mark->pitch != core::Mark::kNoPitch)
        {
            to.pitch = static_cast<std::uint8_t>(mark->pitch);
        }
    }
    if (operator_)
    {
        // The range to its time, as ]l's to a label's.
        jumpTime_ = to.tick;
        const bool done = runOperator("cursor-mark", count);
        jumpTime_ = -1;
        return done;
    }
    const auto from = view().cursor();
    view().setCursor(to);
    if (mode_ != Mode::Visual)
    {
        jumpedFrom_ = from;
    }
    return true;
}

bool Workspace::search(const std::string& text)
{
    // An empty search has nothing to match, so clears the matches (decided November 2026).
    const auto start = text.find_first_not_of(' ');
    if (start == std::string::npos)
    {
        search_.reset();
        return true;
    }
    const auto parsed = core::parseQuery(text, scales());
    if (!parsed.query)
    {
        setMessage(parsed.error, MessageKind::Warning);
        macroFailed_ = macroDepth_ > 0;
        return true;
    }
    search_ = parsed.query;
    searchText_ = text.substr(start, text.find_last_not_of(' ') - start + 1);
    searchForward_ = true;
    return goToMatch(true, 0);
}

bool Workspace::searchHere(bool forward, int count)
{
    // The exact pitch, as Vim's * takes the whole word (decided November 2026), or the
    // controller.
    const auto query = view().queryHere();
    if (!query)
    {
        setMessage("No note or controller here to search for", MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        return true;
    }
    search_ = query;
    searchText_ = query->kind == core::EventQuery::Kind::Controller
                      ? "cc" + std::to_string(*query->controller)
                  : query->kind == core::EventQuery::Kind::Bend ? std::string{"bend"}
                                                                : core::pitchName(*query->pitch);
    searchForward_ = forward;
    return goToMatch(true, count);
}

bool Workspace::goToMatch(bool next, int count)
{
    // A jump, as in Vim, so '' goes back from it.
    const bool forward = next == searchForward_;
    const auto command = forward ? "cursor-match-next" : "cursor-match-previous";
    const auto before = view().cursor();
    const auto moved = view().run(command, count);
    if (!moved)
    {
        return sayWhereFor(command);
    }
    if (wasPlaying_)
    {
        following_ = false;
    }
    if (view().cursor() == before)
    {
        const bool on = view().matchStatus(*search_).starts_with("match");
        setMessage(on ? "This is the only match for " + searchText_
                      : "No match for " + searchText_ + " on this track",
                   MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        return true;
    }
    if (mode_ != Mode::Visual)
    {
        jumpedFrom_ = before;
    }
    if (gnRun_)
    {
        const auto status = view().matchStatus(*search_);
        setMessage("Skipped to " + status + " · " + keysOf("repeat") + " changes it, " +
                       keysOf("search-next") + " skips again",
                   MessageKind::Information);
        gnKept_ = true;
    }
    return true;
}

bool Workspace::select(std::string_view command)
{
    // Lower case the bar, section, track or what's visible; the capitals (, s B) the matches
    // in it, a selection with the search as its filter (decided November 2026).
    const bool matches = command.starts_with("select-matches");
    if (matches && !search_)
    {
        setMessage("No search to select the matches of: " + keysOf("search") + " searches first",
                   MessageKind::Information);
        return true;
    }
    if (command == "select-matches")
    {
        if (!selection_)
        {
            setMessage("Nothing selected to keep the matches in", MessageKind::Information);
            return true;
        }
        selection_->filter = search_;
        return true;
    }
    const auto object = command.ends_with("bar")       ? "object-bar-contents"
                        : command.ends_with("section") ? "object-section-contents"
                        : command.ends_with("track")   ? "object-track"
                                                       : "object-visible";
    if (!selection_)
    {
        selection_ = Selection{.anchor = view().cursor(),
                               .kind = SelectionKind::Range,
                               .where = where(),
                               .index = view().selectionIndex(),
                               .span = std::nullopt,
                               .cut = false};
    }
    view().selectObject(object, 0, *selection_);
    selection_->filter = matches ? search_ : std::nullopt;
    mode_ = Mode::Visual;
    return true;
}

bool Workspace::onlyMatches(OperatorTarget& target)
{
    const auto& song = document_.song();
    std::size_t count = 0;
    for (auto& part : target.parts)
    {
        const auto& events = song.events(part.track);
        std::erase_if(part.events, [&](std::size_t i)
                      { return !core::matches(*selection_->filter, events[i].data, song.ppqn()); });
        count += part.events.size();
    }
    if (count == 0)
    {
        setMessage("No matches in the selection", MessageKind::Information);
        return false;
    }
    target.what = count == 1 ? std::string{"1 match"} : std::to_string(count) + " matches";
    return true;
}

bool Workspace::operateOnMatches(std::string_view op)
{
    // d and y on the matches in the selection: taken as a register holds them, from the
    // selection's start, as the selection would be, only fewer.
    auto target = view().target("visual", 0);
    if (!target || !onlyMatches(*target))
    {
        return true;
    }
    const auto span = view().selectionSpan();
    return takeTarget(op, *target, span ? span->first : 0, span ? span->second - span->first : 0);
}

bool Workspace::takeTarget(std::string_view op, const OperatorTarget& target, core::Tick from,
                           core::Tick span)
{
    const auto& song = document_.song();
    EventRegister taken;
    for (const auto& part : target.parts)
    {
        auto& events = taken.tracks.emplace_back();
        for (const auto i : part.events)
        {
            auto event = song.events(part.track)[i];
            event.tick -= from;
            events.push_back(std::move(event));
        }
    }
    taken.span = span;
    register_ = std::move(taken);
    if (op == "operator-yank")
    {
        setMessage(target.what + " yanked", MessageKind::Information);
        return true;
    }
    document_.edit("Delete " + target.what, view().place(),
                   [&](core::Song& edited)
                   {
                       for (auto part : target.parts)
                       {
                           std::ranges::sort(part.events);
                           edited.updateEvents(part.track,
                                               [&](core::EventList& events)
                                               {
                                                   for (auto it = part.events.rbegin();
                                                        it != part.events.rend(); ++it)
                                                   {
                                                       events.erase(*it);
                                                   }
                                               });
                       }
                   });
    refresh();
    setMessage("Deleted " + target.what, MessageKind::Information);
    return true;
}

bool Workspace::toMatchForGn()
{
    // The match at the cursor, unless gn has just changed it, or the next; whether there's one.
    if (!search_)
    {
        setMessage("No search yet: " + keysOf("search") + " searches", MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        return false;
    }
    if (!view().matchesHere(*search_) || (gnLast_ && *gnLast_ == view().cursor()))
    {
        const auto before = view().cursor();
        goToMatch(true, 0);
        if (view().cursor() == before && gnLast_ && *gnLast_ == before)
        {
            return false; // the only match, done already
        }
    }
    return view().matchesHere(*search_).has_value();
}

bool Workspace::operateOnMatch(std::string_view op, int amount)
{
    if (!toMatchForGn())
    {
        return true;
    }
    const auto part = *view().matchesHere(*search_);
    const auto status = view().matchStatus(*search_);
    const auto at = status.starts_with("match") ? status : std::string{"the match"};
    const auto count = part.events.size();
    OperatorTarget target{.parts = {part},
                          .what = count == 1 ? std::string{"1 match"}
                                             : std::to_string(count) + " matches",
                          .notesOnly = false};
    const auto here = view().cursor();
    const bool taking = op == "operator-delete" || op == "operator-yank";
    if (taking)
    {
        takeTarget(op, target, document_.song().events(part.track)[part.events.front()].tick, 0);
    }
    else
    {
        presetTarget_ = std::move(target);
        transform(op, amount, "object-match", 0);
        presetTarget_.reset();
    }
    if (op == "operator-yank")
    {
        setMessage("Yanked " + at, MessageKind::Information);
        return true;
    }
    gnLast_ = here; // a change, which . goes on from
    const auto done = (op == "operator-delete" ? "Deleted " : "Changed ") + at;
    // Then on to the next match, so it's seen before . changes it (decided November 2026).
    const auto remind = [this, done]
    {
        const auto from = view().cursor();
        view().run("cursor-match-next", 1);
        if (view().cursor() == from || !view().matchesHere(*search_))
        {
            setMessage(done + " · no other match", MessageKind::Information);
            return;
        }
        if (wasPlaying_)
        {
            following_ = false;
        }
        setMessage(done + " · on to the next: " + keysOf("repeat") + " changes it, " +
                       keysOf("search-next") + " skips it",
                   MessageKind::Information);
        gnRun_ = true;
        gnKept_ = true;
    };
    if (answer_)
    {
        // As ^ asks how far: once answered.
        answer_ = [inner = std::move(answer_), remind](const std::string& text)
        {
            inner(text);
            remind();
        };
        return true;
    }
    remind();
    return true;
}

bool Workspace::selectMatch()
{
    // gn without an operator selects the match, as in Vim, only it, by the search's filter.
    if (!toMatchForGn())
    {
        return true;
    }
    if (!selection_)
    {
        selection_ = Selection{.anchor = view().cursor(),
                               .kind = SelectionKind::Range,
                               .where = where(),
                               .index = view().selectionIndex(),
                               .span = std::nullopt,
                               .cut = false};
    }
    view().selectObject(where() == Where::Tracks ? "object-bar-contents" : "object-note", 0,
                        *selection_);
    selection_->filter = search_;
    mode_ = Mode::Visual;
    setMessage("The match selected · an operator changes it", MessageKind::Information);
    return true;
}

void Workspace::searchWrapped(bool forward)
{
    setMessage(forward ? "Past the last match, round to the first"
                       : "Before the first match, round to the last",
               MessageKind::Information);
}

bool Workspace::startFind(bool forward, int count)
{
    // After a find, f and F go to its next and previous match, as Doom Emacs's evil-snipe does
    // (decided October 2026); otherwise, and after an operator, they take a note name.
    if (findRepeats_ && mode_ != Mode::OperatorPending)
    {
        if (!goToFound(forward, count))
        {
            macroFailed_ = macroDepth_ > 0;
        }
        return true;
    }
    awaiting_ = Awaiting{forward ? "find-next" : "find-previous", count};
    return true;
}

bool Workspace::operateToFound(bool forward, int count)
{
    if (!runOperator(forward ? "cursor-find-next" : "cursor-find-previous", count))
    {
        sayNotFound(forward);
        return false;
    }
    return true;
}

bool Workspace::nameFind(std::string_view command, char32_t letter, int count)
{
    // The letter, in either case (decided November 2026, as capitals aren't needed to tell
    // notes apart). The cursor goes at once, and again from where it was if a sharp or flat
    // follows. So does an operator (decided November 2026, after waiting for the key timeout
    // first was too hurried): done again over the new range, as one undo step, if a sharp or
    // flat follows, however long after.
    const bool forward = command == "find-next";
    const char32_t upper = letter >= U'a' && letter <= U'g' ? letter - U'a' + U'A' : letter;
    if (upper < U'A' || upper > U'G')
    {
        setMessage(keysOf(command) + " takes a note name after it, such as " + keysOf(command) +
                       "E, " + keysOf(command) + "F# or " + keysOf(command) + "Bb",
                   MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        if (operator_)
        {
            operator_.reset();
            mode_ = Mode::Normal;
        }
        return true;
    }
    naming_ = Naming{.letter = upper, .forward = forward, .count = count, .from = view().cursor()};
    findQuery_ =
        core::EventQuery{.pitchClass = core::parsePitchClass(encodeUtf8(std::u32string(1, upper)))};
    findName_ = encodeUtf8(std::u32string(1, upper));
    if (operator_)
    {
        naming_->redo = Redo{.op = *operator_,
                             .song = document_.song(),
                             .unnamed = register_,
                             .registers = registers_,
                             .registerName = registerName_,
                             .registerBefore = registerBefore_,
                             .cut = cutNext_};
        document_.beginGroup({}, view().place());
        naming_->failed = !operateToFound(forward, count);
        return true;
    }
    naming_->failed = !goToFound(forward, count);
    return true;
}

bool Workspace::finishNaming(std::optional<char32_t> accidental)
{
    auto naming = *std::exchange(naming_, std::nullopt);
    auto name = encodeUtf8(std::u32string(1, naming.letter));
    if (accidental)
    {
        name += static_cast<char>(*accidental);
    }
    findQuery_ = core::EventQuery{.pitchClass = core::parsePitchClass(name)};
    findName_ = name;
    if (naming.redo)
    {
        // The operator again, from the song, registers and cursor before it, in the undo group
        // it began, which then ends as one step.
        auto& redo = *naming.redo;
        if (accidental)
        {
            document_.edit({}, view().place(), [&](core::Song& song) { song = redo.song; });
            register_ = std::move(redo.unnamed);
            registers_ = std::move(redo.registers);
            registerName_ = redo.registerName;
            registerBefore_ = std::move(redo.registerBefore);
            cutNext_ = redo.cut;
            refresh();
            view().setCursor(naming.from);
            operator_ = redo.op;
            mode_ = Mode::OperatorPending;
            if (!operateToFound(naming.forward, naming.count))
            {
                macroFailed_ = macroDepth_ > 0;
            }
        }
        else if (naming.failed && macroDepth_ > 0)
        {
            macroFailed_ = true;
        }
        document_.endGroup();
        refresh();
        return true;
    }
    // Again from where the find started, for the sharp or flat.
    view().setCursor(naming.from);
    if (!goToFound(naming.forward, naming.count))
    {
        macroFailed_ = macroDepth_ > 0;
    }
    return true;
}

void Workspace::settleNaming()
{
    // A name with nothing after it: an operator's done as it is, and a find that found nothing
    // fails a macro now.
    if (!naming_)
    {
        return;
    }
    if (naming_->redo)
    {
        finishNaming(std::nullopt);
        return;
    }
    if (naming_->failed && macroDepth_ > 0)
    {
        macroFailed_ = true;
    }
    naming_.reset();
}

bool Workspace::goToFound(bool forward, int count)
{
    // Not a jump, as Vim's f isn't, so '' stays where it was.
    const auto command = forward ? "cursor-find-next" : "cursor-find-previous";
    const auto before = view().cursor();
    findForward_ = forward;
    findRepeats_ = true;
    findKept_ = true;
    const auto moved = view().run(command, count);
    if (!moved)
    {
        findRepeats_ = false;
        sayWhereFor(command);
        return false;
    }
    if (wasPlaying_)
    {
        following_ = false;
    }
    if (view().cursor() == before)
    {
        sayNotFound(forward);
        return false;
    }
    return true;
}

void Workspace::sayNotFound(bool forward)
{
    // "No E after here in Bass".
    const auto& song = document_.song();
    const auto cursor = view().cursor();
    const auto* track = cursor.track ? song.track(*cursor.track) : nullptr;
    std::string text = "No " + findName_ + (forward ? " after here" : " before here");
    if (track != nullptr && !track->name.empty())
    {
        text += " in " + track->name;
    }
    setMessage(text, MessageKind::Information);
}

std::string Workspace::markAbout(const core::Mark& mark) const
{
    // Where it is: "5:1:000, Bass, C3", or "5:1:000, the conductor".
    const auto& song = document_.song();
    std::string text = positionText(song.meter(), mark.tick) + ", ";
    const auto* track = mark.track ? song.track(*mark.track) : nullptr;
    if (track == nullptr)
    {
        text += "the conductor";
    }
    else
    {
        const auto index = std::ranges::find_if(song.tracks(), [&](const auto& t)
                                                { return t->id() == *mark.track; }) -
                           song.tracks().begin();
        text += track->name.empty() ? "Track " + std::to_string(index + 1) : track->name;
    }
    if (mark.pitch != core::Mark::kNoPitch)
    {
        text += ", " + core::pitchName(static_cast<std::uint8_t>(mark.pitch));
    }
    return text;
}

bool Workspace::sayMarks()
{
    std::vector<std::pair<std::string, std::string>> lines;
    for (const auto& mark : document_.song().marks())
    {
        lines.emplace_back(std::string(1, mark.letter), markAbout(mark));
    }
    if (lines.empty())
    {
        setMessage("No marks set", MessageKind::Information);
        return true;
    }
    listing_ = std::move(lines);
    return true;
}

CommandResult Workspace::deleteMarks(std::string_view letters)
{
    // :mark-delete a, abc, a-d, as Vim's :delmarks; one undo step.
    std::string wanted;
    for (std::size_t i = 0; i < letters.size(); ++i)
    {
        const char c = letters[i];
        if (c == ' ')
        {
            continue;
        }
        if (c < 'a' || c > 'z')
        {
            return CommandResult::error("Marks are named by letters: a, abc, or a-d");
        }
        if (i + 2 < letters.size() && letters[i + 1] == '-')
        {
            const char last = letters[i + 2];
            if (last < c || last > 'z')
            {
                return CommandResult::error("Marks are named by letters: a, abc, or a-d");
            }
            for (char l = c; l <= last; ++l)
            {
                wanted += l;
            }
            i += 2;
            continue;
        }
        wanted += c;
    }
    if (wanted.empty())
    {
        return CommandResult::error("Which marks? :mark-delete a, abc, or a-d");
    }
    std::string gone;
    for (const char c : wanted)
    {
        if (document_.song().mark(c) != nullptr && gone.find(c) == std::string::npos)
        {
            gone += c;
        }
    }
    if (gone.empty())
    {
        return {false, "No marks there to delete", false};
    }
    std::string names;
    for (const char c : gone)
    {
        names += (names.empty() ? "" : ", ") + std::string(1, c);
    }
    auto title = (gone.size() == 1 ? "Delete mark " : "Delete marks ") + names;
    document_.edit(title, view().place(),
                   [&](core::Song& song)
                   {
                       for (const char c : gone)
                       {
                           song.removeMark(c);
                       }
                   });
    refresh();
    return {true, title.replace(0, 6, "Deleted"), false};
}

std::string Workspace::macroName(char32_t name)
{
    return "@" + encodeUtf8(std::u32string(1, name));
}

bool Workspace::endRecording()
{
    // What was typed, without the keys that ended it, kept in the remembered state.
    recorded_.resize(std::min(recordedBefore_, recorded_.size()));
    const auto name = std::exchange(recording_, 0);
    const auto count = recorded_.size();
    remembered_.setMacro(name, std::exchange(recorded_, {}));
    rememberedChanged();
    setMessage(count == 0 ? macroName(name) + " is empty"
                          : "Recorded " + macroName(name) + ", " + std::to_string(count) +
                                (count == 1 ? " key" : " keys"),
               MessageKind::Information);
    return true;
}

bool Workspace::playMacro(char32_t name, int count)
{
    // Its keys again as typed, count times, as one undo step (decided October 2026, as Vim's
    // @). A command failing in it, by a warning, a key that does nothing here or a motion that
    // can't move, stops it and any playing it, what was done staying, to undo or carry on
    // from; macros playing macros stop kMaxMacroDepth deep.
    const auto* keys = remembered_.macro(name);
    if (keys == nullptr)
    {
        setMessage(macroName(name) + " is empty", MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
        return true;
    }
    if (macroDepth_ >= kMaxMacroDepth)
    {
        setMessage("Macros playing macros stopped, " + std::to_string(kMaxMacroDepth) + " deep",
                   MessageKind::Warning);
        return true;
    }
    lastMacro_ = name;
    const auto macro = *keys; // as it was, whatever's recorded meanwhile
    // Changes in it are changes for . in it, as typed; after it, . repeats the @ itself.
    auto changeKeys = std::exchange(changeKeys_, {});
    const auto lastChange = lastChange_;
    const auto versionBefore = versionBefore_;
    const bool notAChange = std::exchange(notAChange_, false);
    const bool replaying = std::exchange(replaying_, false);
    ++macroDepth_;
    document_.beginGroup({}, view().place());
    for (int i = 0; i < std::max(count, 1) && !macroFailed_; ++i)
    {
        for (const auto& key : macro)
        {
            if (macroFailed_)
            {
                break;
            }
            recordChange(key, lastTime_);
        }
        // Keys left waiting for a longer binding run, as when their time is up.
        if (const auto deadline = keys_.deadline(); deadline && !macroFailed_)
        {
            const auto& keymap = mode_ == Mode::OperatorPending || mode_ == Mode::Visual
                                     ? operatorKeymapHere()
                                     : keymapHere();
            runAll(keys_.timeout(keymap, *deadline));
        }
        settleNaming(); // its undo group ending within the macro's
    }
    document_.endGroup();
    if (--macroDepth_ == 0)
    {
        macroFailed_ = false;
    }
    changeKeys_ = std::move(changeKeys);
    lastChange_ = lastChange;
    versionBefore_ = versionBefore;
    notAChange_ = notAChange;
    replaying_ = replaying;
    refresh();
    return true;
}

bool Workspace::sayMacros()
{
    // One a line, as the registers: "@a : 3>>j".
    std::vector<std::pair<std::string, std::string>> lines;
    for (const auto& [name, keys] : remembered_.macros())
    {
        lines.emplace_back(macroName(name), formatKeys(keys));
    }
    if (lines.empty())
    {
        setMessage("No macros recorded", MessageKind::Information);
        return true;
    }
    listing_ = std::move(lines);
    return true;
}

bool Workspace::repeatChange(int count)
{
    // The last change's keys again, as one undo step; a count replaces the one it began with.
    notAChange_ = true;
    if (lastChange_.empty())
    {
        setMessage("No change yet to repeat", MessageKind::Information);
        return true;
    }
    std::vector<Key> keys = lastChange_;
    if (count > 0)
    {
        const auto digit = [](const Key& key)
        {
            return key.named == NamedKey::None && key.modifiers == kNoModifiers &&
                   key.character >= U'0' && key.character <= U'9';
        };
        const auto counted = std::ranges::find_if_not(keys, digit);
        keys.erase(keys.begin(), counted);
        const auto digits = std::to_string(count);
        for (auto i = digits.size(); i-- > 0;)
        {
            keys.insert(keys.begin(), Key::typed(static_cast<char32_t>(digits[i])));
        }
    }
    replaying_ = true;
    document_.beginGroup({}, view().place());
    for (const auto& key : keys)
    {
        handleKey(key, lastTime_);
    }
    settleNaming();
    document_.endGroup();
    replaying_ = false;
    refresh();
    return true;
}

bool Workspace::handleKey(const Key& key, std::int64_t now)
{
    // f and F repeat a find while it's the last thing done, any other key ending that, but for
    // a count or keys waiting for more.
    findKept_ = false;
    gnKept_ = false;
    const bool changed = takeKey(key, now);
    if (!findKept_ && keys_.pending().empty())
    {
        findRepeats_ = false;
    }
    // gn's run through matches, the same; . and n keep it, and the keys of an answer to ^.
    if (!gnKept_ && keys_.pending().empty() && mode_ != Mode::CommandLine && !operator_)
    {
        gnRun_ = false;
    }
    // The transform menu given up (Escape, or a key it hasn't), its range with it.
    if (keys_.keys().empty())
    {
        transformTarget_.reset();
    }
    return changed;
}

bool Workspace::takeKey(const Key& key, std::int64_t now)
{
    lastTime_ = now;
    // A listing showing (:registers) takes the next key to close it, and nothing else.
    if (!std::exchange(listing_, {}).empty())
    {
        message_.clear();
        return true;
    }
    const bool hadMessage = !std::exchange(message_, std::string{}).empty();
    // A question asked takes the next key as its answer: y for yes, Escape to stop a run of
    // them, any other for no.
    if (auto question = std::exchange(question_, std::nullopt))
    {
        const bool escape = key.named == NamedKey::Escape;
        if (key.named == NamedKey::None && key.character == U'y' &&
            (key.modifiers & (kCtrl | kAlt)) == 0)
        {
            question->yes();
        }
        else if (escape && question->stop)
        {
            question->stop();
        }
        else if (!escape && question->no)
        {
            question->no();
        }
        else
        {
            setMessage("Nothing done", MessageKind::Information);
        }
        if (!question_)
        {
            endVisual(); // a selection the question was about
        }
        return true;
    }
    // The list of keys takes the keys while it shows: q, Escape or ? closes it, j and k (or the
    // arrows and Page Down and Up) turn its pages, and other keys do nothing.
    if (keysPage_)
    {
        const auto pages = static_cast<int>(keyPages(keyList()).size());
        const bool next = key == Key::typed(U'j') || key == Key::of(NamedKey::Down) ||
                          key == Key::of(NamedKey::PageDown) || key == Key::of(NamedKey::Right);
        const bool back = key == Key::typed(U'k') || key == Key::of(NamedKey::Up) ||
                          key == Key::of(NamedKey::PageUp) || key == Key::of(NamedKey::Left);
        if (key == Key::typed(U'q') || key == Key::of(NamedKey::Escape) || key == Key::typed(U'?'))
        {
            keysPage_.reset();
        }
        else if (next || back)
        {
            keysPage_ = std::clamp(*keysPage_ + (next ? 1 : -1), 0, std::max(pages - 1, 0));
        }
        return true;
    }
    // A command waiting for a character after it ("a): the next key, Escape giving it up.
    if (const auto waiting = std::exchange(awaiting_, std::nullopt))
    {
        const bool done = characterAfter(waiting->command, key, waiting->count);
        noteWaiting(now);
        return done;
    }
    // A find's name, which a sharp or flat straight after its letter goes on (fB, then b).
    if (naming_)
    {
        const bool plain = key.named == NamedKey::None && (key.modifiers & (kCtrl | kAlt)) == 0;
        if (plain && (key.character == U'#' || key.character == U'b'))
        {
            const bool done = finishNaming(key.character);
            noteWaiting(now);
            return done;
        }
        settleNaming();
        if (macroFailed_)
        {
            return true; // a find in a macro that found nothing, which stops it here
        }
    }
    // At the prompt, a character that starts no command-line binding is typed.
    if (mode_ == Mode::CommandLine && keys_.pending().empty() && key.named == NamedKey::None &&
        (key.modifiers & (kCtrl | kAlt)) == 0)
    {
        const std::vector<Key> keys{key};
        const auto match = keymap_.match(Mode::CommandLine, keys);
        if (match.binding == nullptr && !match.longer)
        {
            if (view().typing())
            {
                view().type(key.character);
            }
            else
            {
                const bool listed = completions().has_value();
                line_.insert(key.character);
                if (palette_)
                {
                    palette_->chosen = 0; // the best of what now matches
                }
                if (listed)
                {
                    refilter(); // the matches follow what's typed
                }
            }
            return true;
        }
    }
    const auto before = pending();
    const auto typedIn = mode_;
    const auto& keymap = mode_ == Mode::OperatorPending || mode_ == Mode::Visual
                             ? operatorKeymapHere()
                             : keymapHere();
    typedKeys_.assign(keys_.keys().begin(), keys_.keys().end());
    typedKeys_.push_back(key);
    const auto actions = keys_.key(keymap, mode_, key, now);
    const auto unbound =
        shownKeys(std::vector<Key>(keys_.unbound().begin(), keys_.unbound().end()));
    const bool changed = runAll(actions);
    // Keys after an operator that are no motion give the operator up, as in Vim, saying so;
    // and keys bound to nothing say so (decided October 2026, rather than nothing happening).
    if (mode_ == Mode::OperatorPending && actions.empty() && keys_.pending().empty())
    {
        if (!unbound.empty())
        {
            setMessage(unbound + " isn't a range or object after " + keysOf(operator_->name),
                       MessageKind::Information);
            macroFailed_ = macroDepth_ > 0;
        }
        operator_.reset();
        mode_ = Mode::Normal;
    }
    else if ((typedIn == Mode::Normal || typedIn == Mode::Visual) && !unbound.empty())
    {
        setMessage(unbound + " isn't a key here", MessageKind::Information);
        macroFailed_ = macroDepth_ > 0;
    }
    const bool hinted = hints().has_value();
    noteWaiting(now);
    // A message said, such as for a key bound to nothing, is drawn at once.
    return changed || hadMessage || !message_.empty() || pending() != before ||
           hinted != hints().has_value();
}

void Workspace::noteWaiting(std::int64_t now)
{
    // Keys waiting for more, an operator waiting for its range, or a find for its note name,
    // start the wait for the hint menu, or show it at once with no delay.
    if (keys_.keys().empty() && !operator_ && !findWaiting())
    {
        waitingSince_.reset();
        hintsShown_ = false;
        return;
    }
    if (!waitingSince_ || !hintsShown_)
    {
        waitingSince_ = now;
        hintsShown_ = hintDelay_ == 0 || transformTarget_.has_value(); // a menu, so at once
    }
}

bool Workspace::findWaiting() const
{
    return (awaiting_ && awaiting_->command.starts_with("find-")) || (naming_ && naming_->redo);
}

std::optional<std::vector<Workspace::Hint>> Workspace::hints() const
{
    if (!hintsOn_ || !hintsShown_ || (keys_.keys().empty() && !operator_ && !findWaiting()))
    {
        return std::nullopt;
    }
    // A find's note name: the letters, then a sharp or flat (decided November 2026, so f
    // says what it's waiting for).
    if (awaiting_ && awaiting_->command.starts_with("find-"))
    {
        return std::vector<Hint>{{.keys = "c d e f g a b", .title = "a note name, any octave"},
                                 {.keys = "# b", .title = "then sharp or flat"}};
    }
    if (findWaiting())
    {
        return std::vector<Hint>{{.keys = "#", .title = "sharp"}, {.keys = "b", .title = "flat"}};
    }
    if (keys_.keys().empty())
    {
        auto hints = operatorHints();
        return hints.empty() ? std::nullopt : std::optional{std::move(hints)};
    }
    const auto mode = keys_.mode();
    const auto& keymap =
        mode == Mode::OperatorPending || mode == Mode::Visual ? operatorKeymapHere() : keymapHere();
    auto hints = hintsAfter(keymap, mode, keys_.keys());
    if (hints.empty())
    {
        return std::nullopt;
    }
    return hints;
}

namespace
{

// The hint menu's order (decided October 2026, after Doom Emacs's): named keys first (<CR>,
// <Space>, <Tab>), then other characters, then letters, ignoring case, so t and T come together,
// t first.
auto hintOrder(const std::string& keys)
{
    const auto text = decodeUtf8(keys);
    const bool named = text.size() > 1 && text.front() == U'<';
    const char32_t first = text.empty() ? 0 : text.front();
    const bool letter = (first >= U'a' && first <= U'z') || (first >= U'A' && first <= U'Z');
    std::u32string folded = text;
    for (auto& c : folded)
    {
        if (c >= U'A' && c <= U'Z')
        {
            c = c - U'A' + U'a';
        }
    }
    return std::tuple{named ? 0 : letter ? 2 : 1, folded, text != folded};
}

// A key as the hint menu shows it: an arrow without modifiers as the arrow itself, so h and
// Left read together as h/←, and < as itself; the others in notation.
std::string hintKey(const Key& key)
{
    if (key.modifiers == kNoModifiers)
    {
        if (key.named == NamedKey::None && key.character == U'<')
        {
            return "<";
        }
        switch (key.named)
        {
        case NamedKey::Left:
            return "←";
        case NamedKey::Right:
            return "→";
        case NamedKey::Up:
            return "↑";
        case NamedKey::Down:
            return "↓";
        default:
            break;
        }
    }
    return formatKey(key);
}

// The keys of one command together, as h/← or <A-h>/<A-Left>: characters first, then
// characters with modifiers, then named keys, which sort first on their own.
void addKeys(std::vector<std::string>& keys, std::string more)
{
    keys.push_back(std::move(more));
    std::ranges::stable_sort(keys, {},
                             [](const std::string& k)
                             {
                                 const auto text = decodeUtf8(k);
                                 if (text.size() > 2 && text.front() == U'<')
                                 {
                                     const auto dash = text.find_last_of(U'-');
                                     const auto name =
                                         text.substr(dash == std::u32string::npos ? 1 : dash + 1);
                                     return name.size() == 2 ? 1 : 2; // a character and >
                                 }
                                 const bool arrow =
                                     text.size() == 1 && text[0] >= U'←' && text[0] <= U'↓';
                                 return arrow ? 2 : 0;
                             });
}

std::string joined(const std::vector<std::string>& keys)
{
    std::string text;
    for (const auto& k : keys)
    {
        text += (text.empty() ? "" : "/") + k;
    }
    return text;
}

} // namespace

Where Workspace::where() const
{
    switch (shown())
    {
    case ViewKind::Arrangement:
        return arrange_->onStrip() ? Where::Strip : Where::Tracks;
    case ViewKind::PianoRoll:
        return Where::PianoRoll;
    case ViewKind::EventList:
        break;
    }
    return Where::EventList;
}

bool Workspace::offered(std::string_view command) const
{
    const auto* info = commands_.find(command);
    return info == nullptr || info->where == 0 || (info->where & whereBit(where())) != 0;
}

std::vector<Workspace::Hint> Workspace::hintsAfter(const Keymap& keymap, Mode mode,
                                                   std::span<const Key> keys) const
{
    // Each key that can come next, once, with the title of its binding, or if it starts longer
    // ones, of its group; keys bound to one command together, as h/←.
    std::vector<Hint> hints;
    std::vector<std::vector<std::string>> keysOf; // each hint's keys
    std::vector<std::string> commands;            // and its command, if it's one
    std::vector<Key> previous;
    for (const auto& [sequence, binding] : keymap.bindings(mode))
    {
        if (sequence.size() <= keys.size() ||
            !std::ranges::equal(keys, std::span{sequence}.first(keys.size())) ||
            !offered(binding.command))
        {
            continue;
        }
        const std::vector<Key> next{
            sequence.begin(), sequence.begin() + static_cast<std::ptrdiff_t>(keys.size()) + 1};
        if (next == previous)
        {
            continue;
        }
        previous = next;
        const auto key = hintKey(keys_.typed(next.back()));
        Hint hint{.keys = key, .title = {}, .group = false};
        if (sequence.size() == next.size())
        {
            const auto* info = commands_.find(binding.command);
            hint.title = !binding.hint.empty() ? binding.hint
                         : info != nullptr     ? titleOf(*info)
                                               : binding.command;
            // A count, which the menu can't take, as N: "bar N".
            if (const auto at = hint.title.find("{count}"); at != std::string::npos)
            {
                hint.title.replace(at, 7, "N");
            }
        }
        // Keys that start longer bindings, some of which work here.
        const auto more = std::ranges::any_of(
            keymap.bindings(mode),
            [&](const auto& other)
            {
                return other.first.size() > next.size() &&
                       std::ranges::equal(next, std::span{other.first}.first(next.size())) &&
                       offered(other.second.command);
            });
        if (more)
        {
            hint.group = true;
            // Visual mode's keys are mostly Normal mode's, their groups' titles with them.
            auto title = keymap.groupTitle(mode, next);
            if (title.empty() && mode != Mode::Normal)
            {
                title = keymap.groupTitle(Mode::Normal, next);
            }
            if (hint.title.empty())
            {
                hint.title = title.empty() ? std::string{"…"} : title;
            }
        }
        const auto command =
            !hint.group && sequence.size() == next.size() ? binding.command : std::string{};
        if (const auto same = std::ranges::find(commands, command);
            !command.empty() && same != commands.end())
        {
            addKeys(keysOf[static_cast<std::size_t>(same - commands.begin())], key);
            continue;
        }
        hints.push_back(std::move(hint));
        keysOf.push_back({key});
        commands.push_back(command);
    }
    for (std::size_t i = 0; i < hints.size(); ++i)
    {
        hints[i].keys = joined(keysOf[i]);
    }
    std::ranges::stable_sort(hints, {}, [](const Hint& h) { return hintOrder(h.keys); });
    return hints;
}

std::vector<Workspace::Hint> Workspace::operatorHints() const
{
    // After an operator, the ranges and objects it can take, as whole keys (]b, ib), with what
    // each would take in this view: the cursor's bar first, then the objects, then the ranges.
    // Keys that only move the cursor aren't ranges, so aren't shown.
    std::vector<std::pair<int, Hint>> ranked;
    std::vector<std::string> seen;                      // the commands, as ranked has them
    std::vector<std::vector<std::string>> alternatives; // and their keys
    for (const auto& [sequence, binding] : operatorKeymapHere().bindings(Mode::OperatorPending))
    {
        const auto& command = binding.command;
        const auto* info = commands_.find(command);
        const bool line = isOperator(command);
        if (sequence.size() > 2 || info == nullptr || !(line || info->motion || info->object) ||
            std::ranges::find(kMovesOnly, command) != kMovesOnly.end())
        {
            continue;
        }
        if (line && command != operator_->name)
        {
            continue; // dd after d, yy after y, and no other operator
        }
        auto text = view().describeRange(operator_->name, line ? "operator-line" : command);
        if (!text)
        {
            continue;
        }
        auto written = shownKeys(sequence);
        // A command bound to several keys, shown once with them all, as l/→.
        if (const auto at = std::ranges::find(seen, command); at != seen.end())
        {
            addKeys(alternatives[static_cast<std::size_t>(at - seen.begin())], std::move(written));
            continue;
        }
        seen.push_back(command);
        alternatives.push_back({written});
        ranked.emplace_back(
            line           ? 0
            : info->object ? 1
                           : 2,
            Hint{.keys = std::move(written), .title = std::move(*text), .group = false});
    }
    for (std::size_t i = 0; i < ranked.size(); ++i)
    {
        ranked[i].second.keys = joined(alternatives[i]);
    }
    std::ranges::stable_sort(ranked, {}, [](const std::pair<int, Hint>& r)
                             { return std::tuple{r.first, hintOrder(r.second.keys)}; });
    std::vector<Hint> hints;
    for (auto& [rank, hint] : ranked)
    {
        hints.push_back(std::move(hint));
    }
    return hints;
}

std::vector<Workspace::KeyGroup> Workspace::keyList() const
{
    // Normal mode's bindings that do something here, and the objects after an operator, under
    // their sections, the sections in modalrc's order. A command shows once, with all its keys,
    // in the group its first binding is in, and commands doing the same here (= and + and
    // Ctrl-A stepping a field) together; a binding without a hint isn't listed beside one of its
    // command with one, as the keypad's and other keyboards' keys for bigger text aren't.
    struct Item
    {
        std::string title;
        std::vector<std::string> keys;
        std::vector<std::string> commands;
    };
    std::vector<std::pair<std::string, std::vector<Item>>> groups;
    for (const auto mode : {Mode::Normal, Mode::OperatorPending})
    {
        const auto bindings = keymapHere().bindings(mode);
        const auto hinted = [&](const std::string& command)
        {
            return std::ranges::any_of(
                bindings, [&](const auto& b)
                { return b.second.command == command && !b.second.hint.empty(); });
        };
        for (const auto& [sequence, binding] : bindings)
        {
            if (!offered(binding.command) || binding.command == "cancel" ||
                (binding.hint.empty() && hinted(binding.command)))
            {
                continue;
            }
            const auto written = shownKeys(sequence);
            const auto placed = [&]
            {
                for (auto& [title, items] : groups)
                {
                    for (auto& item : items)
                    {
                        if (std::ranges::find(item.commands, binding.command) !=
                            item.commands.end())
                        {
                            addKeys(item.keys, written);
                            return true;
                        }
                    }
                }
                return false;
            };
            if (placed())
            {
                continue;
            }
            const auto section =
                binding.section.empty() ? std::string{"Other keys"} : binding.section;
            const auto* info = commands_.find(binding.command);
            auto name = !binding.hint.empty() ? binding.hint
                        : info != nullptr     ? titleOf(*info)
                                              : binding.command;
            if (const auto at = name.find("{count}"); at != std::string::npos)
            {
                name.replace(at, 7, "N");
            }
            auto group = std::ranges::find(groups, section,
                                           &std::pair<std::string, std::vector<Item>>::first);
            if (group == groups.end())
            {
                groups.emplace_back(section, std::vector<Item>{});
                group = std::prev(groups.end());
            }
            auto& items = group->second;
            if (const auto same = std::ranges::find(items, name, &Item::title); same != items.end())
            {
                addKeys(same->keys, written);
                same->commands.push_back(binding.command);
                continue;
            }
            items.push_back({std::move(name), {written}, {binding.command}});
        }
    }
    std::vector<KeyGroup> result;
    for (auto& [title, items] : groups)
    {
        KeyGroup group{title, {}};
        for (auto& item : items)
        {
            group.keys.push_back(
                {.keys = joined(item.keys), .title = std::move(item.title), .group = false});
        }
        std::ranges::stable_sort(group.keys, {}, [](const Hint& h) { return hintOrder(h.keys); });
        result.push_back(std::move(group));
    }
    // In the order modalrc's sections come in, those it has none for last.
    const auto& order = keymap_.sections();
    std::ranges::stable_sort(result, {},
                             [&](const KeyGroup& group)
                             {
                                 const auto at = std::ranges::find(order, group.title);
                                 return at == order.end()
                                            ? order.size()
                                            : static_cast<std::size_t>(at - order.begin());
                             });
    return result;
}

std::vector<std::vector<Workspace::KeyColumn>>
Workspace::keyPages(const std::vector<KeyGroup>& groups) const
{
    // Down each column and then across, a group's title never last in a column, as many
    // columns to a page as fit across, each as wide as what's in it.
    const int margin = BarSpacing::forCell(cell_).margin;
    const int room = std::max((width_ - 2 * margin) / cell_.width, 1);
    const int rows = std::max(layout_.main.height / cell_.height - 3, 2);
    std::vector<KeyColumn> columns(1);
    for (const auto& group : groups)
    {
        if (static_cast<int>(columns.back().lines.size()) + 2 > rows)
        {
            columns.emplace_back();
        }
        if (!columns.back().lines.empty())
        {
            columns.back().lines.push_back({nullptr, nullptr}); // a line between groups
        }
        columns.back().lines.push_back({&group, nullptr});
        for (const auto& key : group.keys)
        {
            if (static_cast<int>(columns.back().lines.size()) >= rows)
            {
                columns.emplace_back();
            }
            columns.back().lines.push_back({&group, &key});
        }
    }
    for (auto& column : columns)
    {
        for (const auto& line : column.lines)
        {
            if (line.key != nullptr)
            {
                column.keys =
                    std::max(column.keys, static_cast<int>(decodeUtf8(line.key->keys).size()));
                column.width =
                    std::max(column.width, static_cast<int>(decodeUtf8(line.key->title).size()));
            }
        }
        column.width += column.keys + 3;
        for (const auto& line : column.lines)
        {
            if (line.key == nullptr && line.group != nullptr)
            {
                column.width =
                    std::max(column.width, static_cast<int>(decodeUtf8(line.group->title).size()));
            }
        }
    }
    std::vector<std::vector<KeyColumn>> pages(1);
    int used = 0;
    for (auto& column : columns)
    {
        if (!pages.back().empty() && used + column.width > room)
        {
            pages.emplace_back();
            used = 0;
        }
        used += column.width + 4;
        pages.back().push_back(std::move(column));
    }
    return pages;
}

Region Workspace::keysRegion() const
{
    // Over the view, on a panel: what it is and how to close it, then the page of columns, the
    // groups' titles in the accent colour, keys as in the hint menu.
    const auto groups = keyList();
    const auto pages = keyPages(groups);
    const auto page =
        static_cast<std::size_t>(std::clamp(*keysPage_, 0, static_cast<int>(pages.size()) - 1));
    const int margin = BarSpacing::forCell(cell_).margin;
    Region region{
        .bounds = layout_.main, .background = Role::Panel, .text = {}, .under = {}, .over = {}};
    const Style headingStyle{Role::Dim, Role::Panel, kPlain};
    const Style groupStyle{Role::Accent, Role::Panel, kBold};
    const Style keyStyle{Role::HintKey, Role::Panel, kPlain};
    const Style colonStyle{Role::Dim, Role::Panel, kPlain};
    const Style titleStyle{Role::Text, Role::Panel, kPlain};
    const auto x = [&](int column) { return margin + column * cell_.width; };
    const int pad = cell_.height / 4;
    std::string heading = "Keys in " + std::string{view().name()} + ": q or Escape closes";
    if (pages.size() > 1)
    {
        heading += "; j and k turn the pages (" + std::to_string(page + 1) + " of " +
                   std::to_string(pages.size()) + ")";
    }
    region.text.push_back({x(0), pad, decodeUtf8(heading), headingStyle});
    int left = 0;
    for (const auto& column : pages[page])
    {
        int row = 2;
        for (const auto& line : column.lines)
        {
            const int y = pad + row * cell_.height;
            if (line.key != nullptr)
            {
                const auto keys = decodeUtf8(line.key->keys);
                region.text.push_back(
                    {x(left + column.keys - static_cast<int>(keys.size())), y, keys, keyStyle});
                region.text.push_back({x(left + column.keys), y, U" : ", colonStyle});
                region.text.push_back(
                    {x(left + column.keys + 3), y, decodeUtf8(line.key->title), titleStyle});
            }
            else if (line.group != nullptr)
            {
                region.text.push_back({x(left), y, decodeUtf8(line.group->title), groupStyle});
            }
            ++row;
        }
        left += column.width + 4;
    }
    return region;
}

Region Workspace::hintRegion(const std::vector<Hint>& hints) const
{
    // On a panel above the status line, after Doom Emacs's (decided October 2026): the keys
    // typed and their group's title, then each key that can follow, right-aligned in its
    // column, a colon, and its title, groups marked + in a colour of their own. Columns go down
    // six rows and then across, each as wide as what's in it, so the menu gathers at the left;
    // with more than fit across, it takes more rows, up to the main area's height.
    const int margin = BarSpacing::forCell(cell_).margin;
    const int room = std::max((width_ - 2 * margin) / cell_.width, 1);
    const int most = std::max(layout_.main.height / cell_.height - 2, 1);
    const auto count = static_cast<int>(hints.size());
    struct Column
    {
        int keys = 0;
        int width = 0;
    };
    const auto layOut = [&](int rows)
    {
        std::vector<Column> columns;
        for (int i = 0; i < count; ++i)
        {
            if (i % rows == 0)
            {
                columns.emplace_back();
            }
            const auto& hint = hints[static_cast<std::size_t>(i)];
            auto& column = columns.back();
            column.keys = std::max(column.keys, static_cast<int>(decodeUtf8(hint.keys).size()));
            column.width = std::max(column.width, static_cast<int>(decodeUtf8(hint.title).size()) +
                                                      (hint.group ? 1 : 0));
        }
        int total = 0;
        for (auto& column : columns)
        {
            column.width += column.keys + 3; // " : "
            total += column.width + 4;
        }
        return std::pair{columns, total - 4};
    };
    int rows = std::min(std::max(count, 1), std::min(6, most));
    auto [columns, total] = layOut(rows);
    while (total > room && rows < std::min(count, most))
    {
        std::tie(columns, total) = layOut(++rows);
    }

    const int pad = cell_.height / 4;
    const int height = (rows + 1) * cell_.height + 2 * pad;
    const auto& status = layout_.bottom[0];
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    const Style keyStyle{Role::HintKey, Role::Panel, kPlain};
    const Style colonStyle{Role::Dim, Role::Panel, kPlain};
    const Style titleStyle{Role::Text, Role::Panel, kPlain};
    const Style groupStyle{Role::HintGroup, Role::Panel, kPlain};
    const Style headingStyle{Role::Dim, Role::Panel, kPlain};
    const auto x = [&](int column) { return margin + column * cell_.width; };
    const auto mode = keys_.mode();
    const auto& keymap =
        mode == Mode::OperatorPending || mode == Mode::Visual ? operatorKeymapHere() : keymapHere();
    auto heading = pending();
    if (keys_.keys().empty() && operator_)
    {
        // After an operator: its key's hint, Delete or Yank.
        for (const auto& [keys, binding] : keymapHere().bindings(Mode::Normal))
        {
            if (binding.command == operator_->name && !binding.hint.empty())
            {
                heading += "  " + binding.hint;
                break;
            }
        }
    }
    else if (const auto title = keymap.groupTitle(mode, keys_.keys()); !title.empty())
    {
        heading += "  " + title;
    }
    region.text.push_back({x(0), pad, decodeUtf8(heading), headingStyle});
    int left = 0;
    for (std::size_t c = 0; c < columns.size(); ++c)
    {
        const auto& column = columns[c];
        if (left + column.width > room && c > 0)
        {
            break; // more than fit, even so
        }
        for (int row = 0; row < rows; ++row)
        {
            const auto i = c * static_cast<std::size_t>(rows) + static_cast<std::size_t>(row);
            if (i >= hints.size())
            {
                break;
            }
            const auto& hint = hints[i];
            const auto keys = decodeUtf8(hint.keys);
            const int y = pad + (row + 1) * cell_.height;
            region.text.push_back(
                {x(left + column.keys - static_cast<int>(keys.size())), y, keys, keyStyle});
            region.text.push_back({x(left + column.keys), y, U" : ", colonStyle});
            region.text.push_back({x(left + column.keys + 3), y,
                                   decodeUtf8((hint.group ? "+" : "") + hint.title),
                                   hint.group ? groupStyle : titleStyle});
        }
        left += column.width + 4;
    }
    return region;
}

std::optional<std::int64_t> Workspace::deadline() const
{
    auto deadline = keys_.deadline();
    if (hintsOn_ && waitingSince_ && !hintsShown_)
    {
        const auto shown = *waitingSince_ + hintDelay_;
        deadline = deadline ? std::min(*deadline, shown) : shown;
    }
    if (wasPlaying_)
    {
        const auto frame = lastTime_ + kFrameInterval;
        deadline = deadline ? std::min(*deadline, frame) : frame;
    }
    return deadline;
}

bool Workspace::wake(std::int64_t now)
{
    lastTime_ = now;
    const auto before = keys_.pending();
    const auto& keymap = mode_ == Mode::OperatorPending || mode_ == Mode::Visual
                             ? operatorKeymapHere()
                             : keymapHere();
    typedKeys_.assign(keys_.keys().begin(), keys_.keys().end());
    bool changed = runAll(keys_.timeout(keymap, now));
    if (waitingSince_ && !hintsShown_ && now >= *waitingSince_ + hintDelay_)
    {
        hintsShown_ = true;
        changed = changed || hints().has_value();
    }
    if (keys_.keys().empty() && !operator_ && !findWaiting())
    {
        waitingSince_.reset();
        hintsShown_ = false;
    }
    if (wasPlaying_)
    {
        player_->poll();
        showPlayhead();
        changed = true;
    }
    return changed || keys_.pending() != before;
}

bool Workspace::runAll(const std::vector<Action>& actions)
{
    bool changed = false;
    fromKeys_ = true;
    for (const auto& action : actions)
    {
        changed = (action.withAfter ? characterAfter(action.command, action.after, action.count)
                                    : run(action.command, action.count).changed) ||
                  changed;
    }
    fromKeys_ = false;
    return changed;
}

CommandResult Workspace::run(std::string_view command, int count)
{
    if (command != "cut-at-edges" && !isOperator(command) && mode_ != Mode::OperatorPending)
    {
        cutNext_ = false; // | is for the next operator only
    }
    // A register named before ("a): a put puts from it, a yank or delete fills it, or with a
    // capital adds to it, the unnamed register getting what's taken either way, as in Vim.
    // (As a letter, 0 for none, rather than an optional, which GCC's release build takes for
    // uninitialised.)
    const char32_t named = command != "register-use" ? registerName_.value_or(0) : 0;
    const char32_t letter = lowerLetter(named);
    const auto letterText = encodeUtf8(std::u32string(1, letter));
    const bool put = command == "register-put" || command == "register-put-before";
    std::optional<EventRegister> unnamed;
    if (named != 0 && put)
    {
        const auto& from = registers_[letter];
        if (from.empty())
        {
            registerName_.reset();
            setMessage("Register " + letterText + " is empty", MessageKind::Warning);
            return {true};
        }
        unnamed = std::exchange(register_, from);
    }
    auto result = commands_.run(command, count, mode_);
    if (!result.message.empty())
    {
        setMessage(result.message,
                   result.warning ? MessageKind::Warning : MessageKind::Information);
    }
    if (named != 0 && put)
    {
        register_ = std::move(*unnamed);
        registerName_.reset();
    }
    else if (named != 0 && mode_ != Mode::OperatorPending && !awaiting_ && !answer_)
    {
        if (!(register_ == registerBefore_))
        {
            // Saying which register, and adding, what it now holds.
            auto& into = registers_[letter];
            if (letter == named)
            {
                into = register_;
                setMessage((message_.empty() ? std::string{"Taken"} : message_) +
                               ", into register " + letterText,
                           messageKind_);
            }
            else
            {
                into = appended(into, register_);
                setMessage("Added to register " + letterText + ", which now holds " +
                               registerAbout(into),
                           MessageKind::Information);
            }
        }
        registerName_.reset();
    }
    return result;
}

bool Workspace::runViewCommand(std::string_view command, int count)
{
    const auto cursor = view().cursor();
    if (const auto changed = view().run(command, count))
    {
        // Jumps, for '' to go back from (decided October 2026): to the song's ends, sections,
        // labels and marks, but not bars, which are steps.
        constexpr std::array<std::string_view, 6> kJumps{"go-first",
                                                         "go-last",
                                                         "cursor-section-next",
                                                         "cursor-section-previous",
                                                         "cursor-label-next",
                                                         "cursor-label-previous"};
        if (std::ranges::find(kJumps, command) != kJumps.end() && view().cursor() != cursor &&
            mode_ != Mode::Visual)
        {
            jumpedFrom_ = cursor;
        }
        // Moving the cursor or scrolling while playing lets go of the playhead (decided
        // October 2026, as in most sequencers): the view stays with the cursor, until
        // playback starts again or playhead-follow. A motion that leaves the cursor where it
        // is still brings the view back to it.
        const bool moves = command.starts_with("cursor-") || command.starts_with("scroll-") ||
                           command.starts_with("go-");
        if (wasPlaying_ && (moves || view().cursor() != cursor))
        {
            following_ = false;
        }
        // In a macro, a motion that can't move fails, as in Vim, ending a run at the song's
        // edge or the last note.
        if (macroDepth_ > 0 && command.starts_with("cursor-") && view().cursor() == cursor)
        {
            macroFailed_ = true;
        }
        return *changed;
    }
    return sayWhereFor(command);
}

bool Workspace::sayWhereFor(std::string_view command)
{
    // Where it does something instead, rather than where it doesn't, which reads as if it
    // were missing: "Note default is for the piano roll".
    const auto* info = commands_.find(command);
    auto title = info != nullptr ? titleOf(*info) : std::string{command};
    std::uint8_t where = info != nullptr ? info->where : 0;
    // Typed keys bound to several commands, none of them doing something here, say where each
    // does: "= is for the section strip, the piano roll and the event list".
    if (fromKeys_)
    {
        const auto mode = mode_ == Mode::Visual ? Mode::Visual : Mode::Normal;
        std::uint8_t theirs = 0;
        int bound = 0;
        bool ours = false;
        for (const auto& [keys, binding] : keymap_.bindings(mode))
        {
            if (keys == typedKeys_)
            {
                ++bound;
                ours = ours || binding.command == command;
                if (const auto* its = commands_.find(binding.command))
                {
                    theirs |= its->where;
                }
            }
        }
        if (ours && bound > 1)
        {
            title = shownKeys(typedKeys_);
            where = theirs;
        }
    }
    std::vector<std::string> places;
    constexpr std::array<std::pair<Where, std::string_view>, 4> kPlaces{{
        {Where::Tracks, "the arrangement's tracks"},
        {Where::Strip, "the section strip"},
        {Where::PianoRoll, "the piano roll"},
        {Where::EventList, "the event list"},
    }};
    for (const auto& [place, name] : kPlaces)
    {
        if ((where & whereBit(place)) != 0)
        {
            places.emplace_back(name);
        }
    }
    std::string text;
    for (std::size_t i = 0; i < places.size(); ++i)
    {
        text += (i == 0 ? "" : i + 1 == places.size() ? " and " : ", ") + places[i];
    }
    setMessage(places.empty() ? title + " does nothing here" : title + " is for " + text,
               MessageKind::Information);
    macroFailed_ = macroDepth_ > 0;
    return true;
}

std::optional<bool> Workspace::runShared(std::string_view command, int count)
{
    if (!offered(command))
    {
        return sayWhereFor(command); // as the views' own commands do
    }
    const int n = std::max(count, 1);
    if (command.starts_with("command-line-"))
    {
        return runCommandLine(command);
    }
    if (isOperator(command))
    {
        if (transformTarget_ && command != "operator-delete" && command != "operator-yank" &&
            command != "operator-transform")
        {
            presetTarget_ = std::exchange(transformTarget_, std::nullopt);
            const bool done = transform(command, n, "preset", 0);
            presetTarget_.reset();
            return done;
        }
        operator_ = PendingOperator{std::string{command}, count};
        mode_ = Mode::OperatorPending;
        return true;
    }
    if (command == "cut-at-edges")
    {
        cutNext_ = true; // for the next operator
        return true;
    }
    if (command == "palette")
    {
        palette_ = PaletteState{};
        mode_ = Mode::CommandLine;
        line_.open();
        answer_ = {};
        completing_.reset();
        // The command ranked first chosen at once, once there's one, so Ctrl-p Enter runs it
        // again (decided October 2026).
        if (const auto shown = paletteShown(); !shown.empty() && shown.front().rank > 0)
        {
            palette_->chosen = 0;
        }
        return true;
    }
    if (command == "palette-forget")
    {
        remembered_.forgetCommands();
        rememberedChanged();
        setMessage("The palette's ranking of commands is forgotten", MessageKind::Information);
        return true;
    }
    if (command == "visual" || command == "visual-rows" || command == "visual-box")
    {
        const auto kind = selectionKind(command);
        selection_ =
            Selection{.anchor = view().cursor(),
                      .kind = kind,
                      .where = where(),
                      .index = view().selectionIndex(),
                      .span = kind == SelectionKind::Rows ? view().rowsSpan() : std::nullopt,
                      .cut = false};
        mode_ = Mode::Visual;
        return true;
    }
    if (command == "visual-again")
    {
        if (!lastSelection_)
        {
            setMessage("No selection yet to select again", MessageKind::Information);
            return true;
        }
        selection_ = lastSelection_->first;
        view().setCursor(lastSelection_->second);
        mode_ = Mode::Visual;
        return true;
    }
    if (command == "repeat")
    {
        return repeatChange(count);
    }
    if (command == "register-use")
    {
        awaiting_ = Awaiting{std::string{command}, count};
        return true;
    }
    if (command == "registers")
    {
        return sayRegisters();
    }
    if (command == "macro-record" && recording_ != 0)
    {
        return endRecording();
    }
    if (command == "macro-record" || command == "macro-play")
    {
        awaiting_ = Awaiting{std::string{command}, count};
        return true;
    }
    if (command == "macros")
    {
        return sayMacros();
    }
    if (command == "mark-set")
    {
        awaiting_ = Awaiting{std::string{command}, count};
        return true;
    }
    if (command == "marks")
    {
        return sayMarks();
    }
    if (command == "undo" || command == "redo")
    {
        notAChange_ = true;
        bool changed = false;
        for (int i = 0; i < n; ++i)
        {
            changed = undoOrRedo(command == "undo") || changed;
        }
        return changed;
    }
    if (command == "cancel")
    {
        return false;
    }
    if (command == "play-toggle" || command == "play-stop-back" || command == "play-cursor" ||
        command == "marker-set" || command == "panic")
    {
        return transport(command);
    }
    if (command == "playhead-follow")
    {
        following_ = true;
        showPlayhead();
        return true;
    }
    if (command == "rewind-toggle")
    {
        // Through the setting, where there is one, so it stays set as other settings change.
        if (lineHandler_)
        {
            lineHandler_("set rewind!");
        }
        else
        {
            setRewind(!rewind_);
        }
        setMessage(rewind_ ? "Rewind on: the play-from marker stays put"
                           : "Rewind off: the play-from marker moves to where playback stops",
                   MessageKind::Information);
        return true;
    }
    if (command == "grid-advance-toggle")
    {
        // Through the setting too, as rewind's is.
        if (lineHandler_)
        {
            lineHandler_("set gridadvance!");
        }
        else
        {
            pianoRoll().setGridAdvance(!pianoRoll().gridAdvance());
        }
        setMessage(pianoRoll().gridAdvance() ? "O moves on by the grid step"
                                             : "O moves on by the note's length",
                   MessageKind::Information);
        return true;
    }
    if (command == "command-line")
    {
        mode_ = Mode::CommandLine;
        line_.open();
        answer_ = {};
        return true;
    }
    if (command == "search")
    {
        mode_ = Mode::CommandLine;
        std::swap(line_, searchLine_);
        line_.open();
        answer_ = {};
        searchPrompt_ = true;
        return true;
    }
    if (command == "search-here" || command == "search-here-back")
    {
        return searchHere(command == "search-here", count);
    }
    if (command == "search-clear")
    {
        search_.reset();
        return true;
    }
    if (command == "select-match")
    {
        return selectMatch();
    }
    if (command.starts_with("select-"))
    {
        return select(command);
    }
    if (command == "keys-show")
    {
        keysPage_ = 0;
        return true;
    }
    if (command == "yank-this")
    {
        // The section strip yanks its section; everywhere else it's yin.
        if (const auto done = view().run(command, count))
        {
            return *done;
        }
        return view().operate("operator-yank", "object-note", count);
    }
    if (command == "view-arrangement" || command == "view-events" || command == "view-piano-roll")
    {
        show(command == "view-arrangement" ? ViewKind::Arrangement
             : command == "view-events"    ? ViewKind::EventList
                                           : ViewKind::PianoRoll);
        return true;
    }
    if (command == "view-conductor")
    {
        // The event list's track 0, at the cursor's time.
        show(ViewKind::EventList);
        auto cursor = view().cursor();
        cursor.track.reset();
        cursor.pitch.reset();
        view().setCursor(cursor);
        return true;
    }
    if (command == "view-back")
    {
        if (history_.empty())
        {
            setMessage("No view to go back to", MessageKind::Information);
            return true;
        }
        auto* back = history_.back();
        history_.pop_back();
        switchTo(*back, false);
        return true;
    }
    return std::nullopt;
}

bool Workspace::runOperator(std::string_view command, int count)
{
    const bool cut = std::exchange(cutNext_, false);
    const auto op = *operator_;
    operator_.reset();
    mode_ = Mode::Normal;
    if (command == "cancel")
    {
        return true;
    }
    if (isOperator(command))
    {
        // Its own key again is the cursor's bar, as dd is.
        if (command != op.name)
        {
            setMessage(keysOf(command) + " isn't a range or object after " + keysOf(op.name),
                       MessageKind::Information);
            return true;
        }
        command = "operator-line";
    }
    if (command != "operator-line")
    {
        const auto* info = commands_.find(command);
        if (info == nullptr || !(info->motion || info->object))
        {
            return false;
        }
        if (std::ranges::find(kMovesOnly, command) != kMovesOnly.end())
        {
            setMessage(keysOf(command) + " only moves the cursor; for a range, use " +
                           keysOf("cursor-bar-line-next") + " or " + keysOf("cursor-section-next") +
                           ", or an object such as ib",
                       MessageKind::Information);
            return true;
        }
    }
    if (command == "object-match")
    {
        return operateOnMatch(op.name, std::max(op.count, 1));
    }
    if ((op.name == "operator-delete" || op.name == "operator-yank") &&
        command == "object-section" && where() != Where::Strip)
    {
        // das and yas: the section itself, as on the strip, from anywhere.
        return operateOnSections(op.name, std::max(op.count, 1) * std::max(count, 1));
    }
    // Those with no "how far" multiply the counts, as d and y do: 2qq quantises two bars
    // (decided November 2026).
    const bool howFar = op.name == "operator-transpose-up" ||
                        op.name == "operator-transpose-down" || op.name == "operator-shift-later" ||
                        op.name == "operator-shift-earlier";
    if (!howFar && op.name != "operator-delete" && op.name != "operator-yank")
    {
        const int bars =
            op.count == 0 && count == 0 ? 0 : std::max(op.count, 1) * std::max(count, 1);
        const auto run = [&] { return transform(op.name, 1, command, bars); };
        return cut ? cutThen(op.name, command, bars, run) : run();
    }
    if (op.name != "operator-delete" && op.name != "operator-yank")
    {
        // The count before the operator is how far (3>ib, up three semitones), the one after
        // it the range's (>3ib, three bars up one).
        const auto run = [&] { return transform(op.name, std::max(op.count, 1), command, count); };
        return cut ? cutThen(op.name, command, count, run) : run();
    }
    // Counts before the operator and after it multiply, as in Vim: 2d3j is d6j.
    const int total = op.count == 0 && count == 0 ? 0 : std::max(op.count, 1) * std::max(count, 1);
    const auto run = [&] { return view().operate(op.name, command, total); };
    return cut ? cutThen(op.name, command, total, run) : run();
}

bool Workspace::cutThen(std::string_view op, std::string_view motion, int count,
                        const std::function<bool()>& run)
{
    // Cutting the notes crossing the range's edges, then the operator on what's inside: d, D
    // and p after the cut, as one undo step under the operator's name; y on the song as it is,
    // what it yanks then cut to the range; the others cutting in their own change, so one that
    // asks first (^) cuts nothing if it's cancelled.
    const auto range = view().cutRange(motion, count);
    if (!range)
    {
        setMessage("Cutting at the edges is for the arrangement and the piano roll",
                   MessageKind::Information);
        return true;
    }
    if (op == "operator-yank")
    {
        const bool done = run();
        cutRegister(*range);
        return done;
    }
    if (op != "operator-delete" && op != "visual-delete-time")
    {
        cutting_ = range;
        const bool done = run();
        cutting_.reset();
        return done;
    }
    document_.beginGroup({}, view().place());
    cutSong(*range);
    const bool done = run();
    document_.endGroup();
    refresh();
    return done;
}

void Workspace::cutSong(const CutRange& range)
{
    document_.edit("Cut at the edges", view().place(),
                   [&](core::Song& song)
                   {
                       for (const auto id : range.tracks)
                       {
                           song.updateEvents(id,
                                             [&](core::EventList& events)
                                             {
                                                 const auto which = [&](const core::Note& note)
                                                 { return range.takes(note); };
                                                 events = core::cutAt(
                                                     core::cutAt(events, range.from, which),
                                                     range.to, which);
                                             });
                       }
                   });
}

void Workspace::cutRegister(const CutRange& range)
{
    // What y took, its ticks from the range's start, cut to it: the parts inside of the notes
    // sounding into it from before, and of those sounding on past its end.
    const auto& song = document_.song();
    const auto length = range.to - range.from;
    for (std::size_t k = 0; k < register_.tracks.size() && k < range.tracks.size(); ++k)
    {
        auto& taken = register_.tracks[k];
        for (auto& event : taken)
        {
            auto* note = std::get_if<core::Note>(&event.data);
            if (note != nullptr && range.takes(*note) && event.tick + note->duration > length)
            {
                note->duration = length - event.tick;
                note->releaseVelocity = core::Note{}.releaseVelocity;
            }
        }
        for (const auto& event : song.events(range.tracks[k]).all())
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            if (note != nullptr && range.takes(*note) && event.tick < range.from &&
                event.tick + note->duration > range.from)
            {
                auto inside = *note;
                inside.duration = std::min(event.tick + note->duration, range.to) - range.from;
                if (event.tick + note->duration > range.to)
                {
                    inside.releaseVelocity = core::Note{}.releaseVelocity;
                }
                taken.insert(taken.begin(), core::Event{0, inside});
            }
        }
    }
}

std::optional<bool> Workspace::runVisual(std::string_view command, int count)
{
    // Escape or v again ends it; o goes to the other end; an object selects what it takes.
    // Motions, and the rest, are as in Normal mode, the selection following the cursor.
    if (command == "cancel")
    {
        endVisual();
        return true;
    }
    if (command == "visual" || command == "visual-rows" || command == "visual-box")
    {
        // Its own key again ends it, as in Vim; another's changes to it.
        const auto kind = selectionKind(command);
        if (selection_->kind == kind)
        {
            endVisual();
        }
        else
        {
            selection_->kind = kind;
            selection_->span = kind == SelectionKind::Rows ? view().rowsSpan() : std::nullopt;
        }
        return true;
    }
    if (command == "cut-at-edges")
    {
        selection_->cut = !selection_->cut;
        return true;
    }
    if (command == "field-edit" && selection_->where == Where::Tracks)
    {
        // Enter on bars in the arrangement opens exactly those in the piano roll, on the
        // cursor's track; whole tracks open as Enter does.
        const auto span = view().selectionSpan();
        const bool whole = selection_->kind == SelectionKind::Rows && !selection_->span;
        auto cursor = view().cursor();
        endVisual();
        if (!span || whole)
        {
            return std::nullopt;
        }
        cursor.tick = span->first;
        switchTo(*pianoRoll_, true, true);
        pianoRoll_->openBars(cursor, span->first, span->second);
        return true;
    }
    if (command == "visual-other-end")
    {
        const auto other = selection_->anchor;
        selection_->anchor = view().cursor();
        selection_->index = view().selectionIndex();
        view().setCursor(other);
        return true;
    }
    if (isOperator(command) || command == "event-delete" || command == "yank-this" ||
        command == "visual-delete-time")
    {
        return operateOnSelection(command, count);
    }
    if (command == "register-put" || command == "register-put-before")
    {
        return putOverSelection();
    }
    if (const auto* info = commands_.find(command); info != nullptr && info->object)
    {
        view().selectObject(command, count, *selection_);
        return true;
    }
    return std::nullopt;
}

bool Workspace::operateOnSelection(std::string_view command, int count)
{
    // d and x delete what's in it, y and Y yank it, D takes its time out, and the other
    // operators change it, the count before them saying how far; then Visual mode ends.
    const auto selection = *selection_;
    const auto span = view().selectionSpan();
    if (selection.filter && command == "visual-delete-time")
    {
        endVisual();
        setMessage("D takes out the selection's time, not its matches", MessageKind::Information);
        return true;
    }
    const auto run = [&]
    {
        if (selection.filter && (command == "event-delete" || command == "operator-delete" ||
                                 command == "yank-this" || command == "operator-yank"))
        {
            return operateOnMatches(command == "event-delete" || command == "operator-delete"
                                        ? "operator-delete"
                                        : "operator-yank");
        }
        if (command == "visual-delete-time")
        {
            if (selection.kind == SelectionKind::Range && selection.where == Where::Tracks && span)
            {
                // On every track: out of the song, as :bar-delete takes bars.
                const auto result = deleteBars(span->first, span->second);
                setMessage(result.message,
                           result.warning ? MessageKind::Warning : MessageKind::Information);
                return true;
            }
            return view().operate("operator-delete", "visual-time", 0);
        }
        if (command == "event-delete" || command == "operator-delete")
        {
            return view().operate("operator-delete", "visual", 0);
        }
        if (command == "yank-this" || command == "operator-yank")
        {
            return view().operate("operator-yank", "visual", 0);
        }
        return transform(command, std::max(count, 1), "visual", 0);
    };
    const auto op = command == "event-delete" ? std::string_view{"operator-delete"}
                    : command == "yank-this"  ? std::string_view{"operator-yank"}
                                              : command;
    const bool done = selection.cut ? cutThen(op, "visual", 0, run) : run();
    // Ended once the view has taken it; while the operator asks something, at the prompt as ^
    // does or yes or no as q does about pedalling, it stays shown until the answer or Escape,
    // though what's changed is already taken (decided October 2026).
    if (!answer_ && !question_)
    {
        endVisual();
    }
    return done;
}

bool Workspace::putOverSelection()
{
    // p and P: what's in it goes, and the register goes in its place, as one undo step; the
    // register keeps what it had.
    const auto what = view().selectionStatus().value_or("the selection");
    const auto span = view().selectionSpan();
    if (selection_->filter)
    {
        endVisual();
        setMessage("p puts over the selection's time, not its matches", MessageKind::Information);
        return true;
    }
    if (register_.empty())
    {
        endVisual();
        setMessage("Nothing yanked or deleted to put", MessageKind::Warning);
        return true;
    }
    const bool strip = selection_->where == Where::Strip;
    if (register_.sections.empty() == strip)
    {
        endVisual();
        setMessage(strip ? "The register holds bars of tracks: put them on the tracks"
                         : "The register holds sections: put them on the section strip",
                   MessageKind::Warning);
        return true;
    }
    const auto saved = register_;
    const auto first = span ? document_.song().sectionAt(span->first) : 0;
    const auto cut = selection_->cut ? view().cutRange("visual", 0) : std::nullopt;
    document_.beginGroup("Put over " + what, view().place());
    if (cut)
    {
        cutSong(*cut);
    }
    view().operate("operator-delete", "visual", 0);
    register_ = saved;
    endVisual();
    if (strip)
    {
        // Before what followed them, or after the last.
        view().run(
            first < document_.song().sections().size() ? "register-put-before" : "register-put", 1);
    }
    else
    {
        if (span)
        {
            auto at = view().cursor();
            at.tick = span->first;
            view().setCursor(at);
        }
        view().run("register-put", 1);
    }
    document_.endGroup();
    refresh();
    return true;
}

void Workspace::endVisual()
{
    if (selection_)
    {
        lastSelection_ = std::pair{*selection_, view().cursor()};
    }
    selection_.reset();
    if (mode_ == Mode::Visual) // not once an operator has asked at the prompt, as ^ does
    {
        mode_ = Mode::Normal;
    }
}

bool Workspace::operateOnSections(std::string_view op, int count)
{
    const auto& song = document_.song();
    const auto range = sectionsFrom(song, view().cursor().tick, count);
    const auto what = sectionsText(song, range);
    register_ = EventRegister{};
    for (auto i = range.first; i < range.first + range.count; ++i)
    {
        register_.sections.push_back(song.sections()[i]);
    }
    if (op == "operator-yank")
    {
        setMessage(what + " yanked; p on the section strip puts it back", MessageKind::Information);
        return true;
    }
    if (range.count >= song.sections().size())
    {
        setMessage("A song keeps at least one section", MessageKind::Warning);
        return true;
    }
    document_.edit("Delete " + (range.count == 1 ? "section " : std::string{}) + what,
                   view().place(),
                   [&](core::Song& s) { s.removeSections(range.first, range.count); });
    refresh();
    return true;
}

std::string Workspace::shownKeys(const std::vector<Key>& keys) const
{
    std::string text;
    for (const auto& key : keys)
    {
        text += hintKey(keys_.typed(key));
    }
    return text;
}

std::string Workspace::keysOf(std::string_view command) const
{
    std::string shortest;
    for (const auto& [keys, binding] : keymapHere().bindings(Mode::Normal))
    {
        if (binding.command == command)
        {
            auto text = shownKeys(keys);
            if (shortest.empty() || text.size() < shortest.size())
            {
                shortest = std::move(text);
            }
        }
    }
    return shortest.empty() ? std::string{command} : shortest;
}

bool Workspace::undoOrRedo(bool undo)
{
    const auto step = undo ? document_.undo() : document_.redo();
    if (!step)
    {
        setMessage(undo ? "Nothing to undo" : "Nothing to redo", MessageKind::Information);
        return true;
    }
    // Back where the change was made: its track, time and scroll.
    view().restore(step->place);
    setMessage((undo ? "Undid: " : "Redid: ") + step->title, MessageKind::Information);
    return true;
}

std::optional<bool> Workspace::runCommandLine(std::string_view command)
{
    if (mode_ != Mode::CommandLine)
    {
        return false;
    }
    if (view().typing())
    {
        return view().runTyping(command);
    }
    if (palette_)
    {
        return runPalette(command);
    }
    if (command == "command-line-next" || command == "command-line-previous")
    {
        const bool options = answer_ && (keyPrompt_ || !transformOptions(answerCommand_).empty());
        return (!answer_ || options) && !searchPrompt_ &&
               complete(command == "command-line-next" ? 1 : -1);
    }
    if (searchPrompt_ && (command == "command-line-run" || command == "command-line-cancel" ||
                          (command == "command-line-backspace" && line_.text().empty())))
    {
        // Back to Normal mode, or Visual mode if searching from it, the cursor moving with
        // the selection; Escape leaves the matches as they were.
        searchPrompt_ = false;
        mode_ = selection_ ? Mode::Visual : Mode::Normal;
        const auto text = command == "command-line-run" ? line_.take() : std::string{};
        line_.clear();
        std::swap(line_, searchLine_);
        return command != "command-line-run" || search(text);
    }
    // While the matches show, Down and Up go round them too, as Tab and Shift-Tab do; Left and
    // Right go across the columns, and Page Down and Up a screen of them.
    if (const auto shown = completions())
    {
        if (command == "command-line-newer" || command == "command-line-older")
        {
            return complete(command == "command-line-newer" ? 1 : -1);
        }
        const auto columns = matchColumns(shown->matches);
        const auto rows = static_cast<std::ptrdiff_t>(columns.rows);
        if (command == "command-line-right" || command == "command-line-left")
        {
            return chooseMatch(command == "command-line-right" ? rows : -rows);
        }
        if (command == "command-line-page-down" || command == "command-line-page-up")
        {
            const auto page =
                rows * static_cast<std::ptrdiff_t>(columnsShown(columns, shown->scroll));
            return chooseMatch(command == "command-line-page-down" ? page : -page);
        }
        if (command == "command-line-backspace" || command == "command-line-delete")
        {
            const auto edited = editLine(line_, command);
            refilter();
            return edited;
        }
    }
    completing_.reset();
    if (command == "command-line-run" && answer_)
    {
        // An answer to a question, kept out of the : prompt's history; a key in its own.
        mode_ = Mode::Normal;
        auto text = encodeUtf8(line_.text());
        line_.clear();
        if (std::exchange(keyPrompt_, false))
        {
            line_.open(decodeUtf8(text));
            line_.take();
            std::swap(line_, keyLine_);
        }
        if (text.find_first_not_of(' ') == std::string::npos)
        {
            text = std::exchange(answerDefault_, {});
        }
        if (const auto answer = std::exchange(answer_, {}); !text.empty())
        {
            answer(text);
        }
        endVisual(); // a selection the question was about
        return true;
    }
    if (command == "command-line-run")
    {
        mode_ = Mode::Normal;
        const auto text = line_.take();
        if (selection_ && !text.empty())
        {
            // From Visual mode: only commands that take a range act on the selection.
            const auto& words = text;
            const auto start = words.find_first_not_of(" \t");
            const auto name = start == std::string::npos
                                  ? std::string{}
                                  : words.substr(start, words.find_first_of(" \t!", start) - start);
            const auto* info = commands_.find(name);
            if (info == nullptr || !info->range)
            {
                endVisual();
                setMessage(name + " doesn't take a selection", MessageKind::Warning);
                return true;
            }
        }
        if (lineHandler_ && !text.empty())
        {
            lineHandler_(text);
        }
        endVisual();
        return true;
    }
    if (command == "command-line-cancel" ||
        (command == "command-line-backspace" && line_.text().empty())) // as in Vim
    {
        mode_ = Mode::Normal;
        answer_ = {};
        if (std::exchange(keyPrompt_, false))
        {
            line_.clear();
            std::swap(line_, keyLine_);
        }
        endVisual(); // a selection the question or the line was about
        return true;
    }
    return editLine(line_, command);
}

void Workspace::setChoices(Choices kind, ChoiceSource source)
{
    sources_[static_cast<std::size_t>(kind)] = std::move(source);
}

std::vector<std::string> Workspace::choices(Choices kind, std::string_view word) const
{
    const auto& song = document_.song();
    std::vector<std::string> found;
    switch (kind)
    {
    case Choices::Command:
        // The names and aliases of the commands the prompt runs: not those only for the
        // prompt's own keys or after an operator.
        for (const auto& info : commands_.all())
        {
            if ((info.modes != 0 && (info.modes & modeBit(Mode::Normal)) == 0) ||
                !offered(info.name))
            {
                continue;
            }
            found.emplace_back(info.name);
            for (std::size_t at = 0; at < info.aliases.size();)
            {
                const auto end = std::min(info.aliases.find(' ', at), info.aliases.size());
                if (end > at)
                {
                    found.emplace_back(info.aliases.substr(at, end - at));
                }
                at = end + 1;
            }
        }
        std::ranges::sort(found);
        return found;
    case Choices::Section:
        for (const auto& section : song.sections())
        {
            found.push_back(section->name());
        }
        return found;
    case Choices::Role:
        for (std::size_t i = 0; i < kRoleCount; ++i)
        {
            found.emplace_back(roleName(static_cast<Role>(i)));
        }
        return found;
    case Choices::Meter:
    {
        // The built-in ones and those the user has chosen before, as one list in order by
        // denominator then numerator, so the one wanted can be found by scanning (decided October
        // 2026); not the song's own, which may be remainder bars of no use.
        std::vector<core::TimeSignature> meters{kOfferedMeters.begin(), kOfferedMeters.end()};
        for (const auto meter : remembered_.meters())
        {
            if (std::ranges::find(meters, meter) == meters.end())
            {
                meters.push_back(meter);
            }
        }
        std::ranges::sort(meters, {}, [](core::TimeSignature meter)
                          { return std::pair{meter.denominator, meter.numerator}; });
        for (const auto meter : meters)
        {
            found.push_back(std::to_string(meter.numerator) + "/" +
                            std::to_string(meter.denominator));
        }
        return found;
    }
    case Choices::Length:
        return {"1/1",  "1/2",  "1/4",  "1/8",  "1/16",  "1/32", "1/2.",
                "1/4.", "1/8.", "1/4t", "1/8t", "1/16t", "grid"};
    case Choices::Key:
    {
        // The tonic first, then the scale.
        const auto before = encodeUtf8(line_.text().substr(0, line_.cursor()));
        const auto start = before.find_first_not_of(' ');
        if (start == std::string::npos || before.find(' ', start) == std::string::npos)
        {
            for (const auto name : core::tonicNames())
            {
                found.emplace_back(name);
            }
            return found;
        }
        for (const auto& scale : scales())
        {
            if (std::ranges::find(found, scale.name) == found.end())
            {
                found.push_back(scale.name);
            }
        }
        return found;
    }
    case Choices::Option:
    {
        // The names, NAME= to go on with, and then the values of the one named.
        const auto options = transformOptions(optionsCommand());
        const auto equals = word.find('=');
        for (const auto& option : options)
        {
            if (equals == std::string_view::npos)
            {
                found.push_back(std::string{option.name} + "=");
                continue;
            }
            if (word.substr(0, equals) != option.name)
            {
                continue;
            }
            for (const auto value : option.values)
            {
                found.push_back(std::string{option.name} + "=" + std::string{value});
            }
            if (option.lengths)
            {
                for (const auto& length : choices(Choices::Length, {}))
                {
                    if (length != "grid")
                    {
                        found.push_back(std::string{option.name} + "=" + length);
                    }
                }
            }
        }
        return found;
    }
    case Choices::Port:
        if (portFinder_)
        {
            return portFinder_("").ports;
        }
        return {};
    default:
        break;
    }
    if (const auto& source = sources_[static_cast<std::size_t>(kind)])
    {
        return source(word);
    }
    return {};
}

std::optional<Workspace::Completions> Workspace::completions() const
{
    if (mode_ != Mode::CommandLine || view().typing() || palette_ || !completing_ ||
        completing_->line != line_.text() || completing_->matches.size() < 2)
    {
        return std::nullopt;
    }
    return Completions{completing_->matches, completing_->chosen, completing_->scroll};
}

Workspace::CompletingWord Workspace::completingWord() const
{
    const auto& text = line_.text();
    const auto word = wordAt(text, line_.cursor());
    CompletingWord result{word.start,
                          encodeUtf8(text.substr(word.start, line_.cursor() - word.start)),
                          Choices::Command};
    if (answer_)
    {
        // A transform's question: its options, wherever they're typed; or a key.
        result.kind = keyPrompt_ ? Choices::Key : Choices::Option;
        return result;
    }
    if (word.index > 0)
    {
        const auto first = wordAt(text, 0);
        auto name = encodeUtf8(text.substr(first.start, first.end - first.start));
        if (name.size() > 1 && name.ends_with('!'))
        {
            name.pop_back();
        }
        const auto* info = commands_.find(name);
        // Not for a command that does nothing here.
        result.kind = info == nullptr || !offered(info->name)
                          ? Choices::None
                          : info->choices[std::min<std::size_t>(word.index - 1, 2)];
    }
    return result;
}

bool Workspace::complete(int by)
{
    // As zsh does (decided October 2026): Tab completes the word as far as its matches agree,
    // showing them, and then goes round them, Shift-Tab back; one match is completed with a space
    // after it, unless it goes on (set theme=, a folder/).
    const auto replaceWord = [this](std::size_t start, const std::string& with)
    { line_.replace(start, line_.cursor(), decodeUtf8(with)); };
    if (completing_ && completing_->line == line_.text() && completing_->matches.size() > 1)
    {
        const auto count = completing_->matches.size();
        const auto& chosen = completing_->chosen;
        const auto to = !chosen  ? (by > 0 ? 0 : count - 1)
                        : by > 0 ? (*chosen + 1) % count
                                 : (*chosen + count - 1) % count;
        chooseIndex(to);
        return true;
    }
    completing_.reset();
    const auto word = completingWord();
    if (word.kind == Choices::None)
    {
        return true;
    }
    const auto found = matching(choices(word.kind, word.typed), word.typed);
    if (found.empty())
    {
        return true;
    }
    if (found.size() == 1)
    {
        const auto& only = found.front();
        // A setting's name goes on too, with =, ? or !.
        const bool goesOn = only.ends_with('=') || only.ends_with('/') ||
                            (word.kind == Choices::Setting && only.find('=') == std::string::npos);
        replaceWord(word.start, only + (goesOn ? "" : " "));
        return true;
    }
    // With several, the first Tab only shows them, none chosen, so typing can narrow them (: Tab
    // lists every command); the next goes round them.
    const auto shared = sharedStart(found);
    if (decodeUtf8(shared).size() > decodeUtf8(word.typed).size() &&
        matching(std::span{&shared, 1}, word.typed).size() == 1)
    {
        replaceWord(word.start, shared);
    }
    completing_ = Completing{found, std::nullopt, word.start, line_.text(), 0};
    return true;
}

bool Workspace::chooseMatch(std::ptrdiff_t by)
{
    // From the one chosen, or with none yet from before the first (or after the last).
    const auto& c = *completing_;
    const auto count = static_cast<std::ptrdiff_t>(c.matches.size());
    const auto from = c.chosen ? static_cast<std::ptrdiff_t>(*c.chosen) : (by > 0 ? -1 : count);
    chooseIndex(static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(from + by, 0, count - 1)));
    return true;
}

void Workspace::chooseIndex(std::size_t to)
{
    // The line takes the match, and the columns shown move to keep it in view.
    auto& c = *completing_;
    c.chosen = to;
    line_.replace(c.start, line_.cursor(), decodeUtf8(c.matches[to]));
    c.line = line_.text();
    const auto columns = matchColumns(c.matches);
    const auto column = to / static_cast<std::size_t>(columns.rows);
    if (column < c.scroll)
    {
        c.scroll = column;
    }
    while (column >= c.scroll + columnsShown(columns, c.scroll))
    {
        ++c.scroll;
    }
}

void Workspace::refilter()
{
    // The word's matches as it's typed, none chosen; gone when the word ends or none match.
    const auto& text = line_.text();
    const auto at = line_.cursor();
    const auto word = completingWord();
    const bool ended = at > 0 && (text[at - 1] == U' ' || text[at - 1] == U'\t');
    auto found = ended || word.kind == Choices::None
                     ? std::vector<std::string>{}
                     : matching(choices(word.kind, word.typed), word.typed);
    if (found.empty())
    {
        completing_.reset();
        return;
    }
    completing_ = Completing{std::move(found), std::nullopt, word.start, text, 0};
}

Workspace::MatchColumns Workspace::matchColumns(const std::vector<std::string>& matches) const
{
    // Six rows down and then across, as the hint menu is; more rows while they don't fit
    // across, up to the main area's height, and then they scroll.
    const int margin = BarSpacing::forCell(cell_).margin;
    MatchColumns columns;
    columns.room = std::max((width_ - 2 * margin) / cell_.width, 1);
    const auto count = static_cast<int>(matches.size());
    const int most = std::max(layout_.main.height / cell_.height - 1, 1);
    const auto lay = [&](int rows)
    {
        std::vector<int> widths;
        int total = 0;
        for (int i = 0; i < count; ++i)
        {
            if (i % rows == 0)
            {
                widths.push_back(0);
            }
            widths.back() =
                std::max(widths.back(),
                         static_cast<int>(decodeUtf8(matches[static_cast<std::size_t>(i)]).size()));
        }
        for (const auto w : widths)
        {
            total += w + 3;
        }
        return std::pair{widths, total};
    };
    columns.rows = std::max(std::min(count, std::min(6, most)), 1);
    auto [widths, total] = lay(columns.rows);
    while (total > columns.room && columns.rows < std::min(count, most))
    {
        std::tie(widths, total) = lay(++columns.rows);
    }
    columns.widths = std::move(widths);
    return columns;
}

std::size_t Workspace::columnsShown(const MatchColumns& columns, std::size_t first) const
{
    // As many as fit, leaving room for the markers of more either side; at least one.
    int used = first > 0 ? 2 : 0;
    std::size_t shown = 0;
    for (auto c = first; c < columns.widths.size(); ++c)
    {
        const bool last = c + 1 == columns.widths.size();
        if (used + columns.widths[c] + (last ? 0 : 2) > columns.room && shown > 0)
        {
            break;
        }
        used += columns.widths[c] + 3;
        ++shown;
    }
    return std::max<std::size_t>(shown, 1);
}

Region Workspace::completionRegion(const Completions& shown) const
{
    // Above the prompt, as the hint menu is: the matches down and then across, each column as
    // wide as what's in it, the one chosen in the cursor's colours; with more than fit across,
    // those in view, a ◀ or ▶ at the side with more.
    const int margin = BarSpacing::forCell(cell_).margin;
    const auto columns = matchColumns(shown.matches);
    const auto count = columnsShown(columns, shown.scroll);
    const int pad = cell_.height / 4;
    const int height = columns.rows * cell_.height + 2 * pad;
    const auto& status = layout_.bottom[0];
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    const Style plain{Role::Text, Role::Panel, kPlain};
    const Style chosen{Role::CursorText, Role::Cursor, kPlain};
    const Style more{Role::Dim, Role::Panel, kPlain};
    int left = 0;
    if (shown.scroll > 0)
    {
        region.text.push_back({margin, pad, U"◀", more});
        left = 2;
    }
    const auto end = std::min(shown.scroll + count, columns.widths.size());
    for (auto c = shown.scroll; c < end; ++c)
    {
        for (int row = 0; row < columns.rows; ++row)
        {
            const auto i =
                c * static_cast<std::size_t>(columns.rows) + static_cast<std::size_t>(row);
            if (i >= shown.matches.size())
            {
                break;
            }
            region.text.push_back({margin + left * cell_.width, pad + row * cell_.height,
                                   decodeUtf8(shown.matches[i]),
                                   shown.chosen == i ? chosen : plain});
        }
        left += columns.widths[c] + 3;
    }
    if (end < columns.widths.size())
    {
        region.text.push_back({margin + (columns.room - 1) * cell_.width, pad, U"▶", more});
    }
    return region;
}

bool Workspace::transport(std::string_view command)
{
    if (player_ == nullptr)
    {
        setMessage("Nothing to play to", MessageKind::Warning);
        return true;
    }
    if (command == "play-toggle")
    {
        player_->toggle();
    }
    else if (command == "play-stop-back")
    {
        returning_ = player_->playing();
        player_->toggle();
    }
    else if (command == "play-cursor" || command == "marker-set")
    {
        player_->setMarker(view().cursor().tick);
        if (command == "play-cursor")
        {
            player_->play();
            following_ = true;
        }
    }
    else
    {
        player_->panic();
    }
    showPlayhead();
    return true;
}

void Workspace::setPlayer(core::Player* player)
{
    player_ = player;
    if (player_ != nullptr)
    {
        player_->setRewind(rewind_);
    }
}

void Workspace::setRewind(bool rewind)
{
    rewind_ = rewind;
    if (player_ != nullptr)
    {
        player_->setRewind(rewind);
    }
}

void Workspace::showPlayhead()
{
    const bool playing = player_ != nullptr && player_->playing();
    if (playing && !wasPlaying_)
    {
        following_ = true;
        playedFrom_ = view().cursor();
    }
    if (playing)
    {
        // Until the engine starts, the playhead is at the play-from marker, where it will start.
        lastPlayhead_ = player_->position().value_or(player_->marker());
        view().showPlayhead(lastPlayhead_, !following_ ? Follow::None
                                           : rewind_   ? Follow::View
                                                       : Follow::Cursor);
    }
    else if (wasPlaying_)
    {
        // Stopped where it is (decided October 2026, after use), with rewind on or off, which
        // says only where the next play starts: the view staying and the cursor landing there,
        // without rewind where the engine says it stopped; but where the cursor was moved to
        // while playing, it stays; and with play-stop-back it goes back to where it was when
        // playback started.
        if (returning_ && playedFrom_)
        {
            view().stopPlayhead(std::nullopt);
            view().setCursor(*playedFrom_);
        }
        else
        {
            view().stopPlayhead(!following_ ? std::nullopt
                                : rewind_   ? std::optional{lastPlayhead_}
                                            : std::optional{player_->marker()});
            // Stopping in place is a jump, so '' goes back as Shift-Space does.
            if (following_ && playedFrom_ && view().cursor() != *playedFrom_)
            {
                jumpedFrom_ = playedFrom_;
            }
        }
        if (!player_->stopping())
        {
            returning_ = false;
        }
    }
    wasPlaying_ = playing || (player_ != nullptr && player_->stopping());
}

CommandResult Workspace::port(std::string_view name)
{
    const auto first = name.find_first_not_of(" \t");
    name = first == std::string_view::npos
               ? std::string_view{}
               : name.substr(first, name.find_last_not_of(" \t") - first + 1);
    if (!portFinder_)
    {
        return CommandResult::error("There are no MIDI ports here");
    }
    const auto id = view().cursor().track;
    if (!id)
    {
        return CommandResult::error("The conductor has no port; its tempo and meter aren't sent");
    }
    const auto found = portFinder_(name);
    const auto list = [](const std::vector<std::string>& names)
    {
        std::string text;
        for (const auto& n : names)
        {
            text += (text.empty() ? "" : ", ") + n;
        }
        return text;
    };
    const auto outputs = found.ports.empty() ? std::string{"there are no output ports"}
                                             : "outputs: " + list(found.ports);
    const auto tracks = document_.song().tracks();
    const auto at = std::ranges::find_if(tracks, [&](const auto& t) { return t->id() == *id; });
    const auto& track = **at;
    // The track's name, starting the message with a capital.
    auto who = track.name.empty() ? "track " + std::to_string(at - tracks.begin() + 1) : track.name;
    const auto trackName = who;
    who[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(who[0])));
    if (name.empty())
    {
        return {false,
                who + " plays to " +
                    (track.port.empty() ? std::string{"the default port"} : track.port) + "; " +
                    outputs,
                false};
    }
    if (!found.port)
    {
        return CommandResult::error(
            found.candidates.empty()
                ? "No output port matches \"" + std::string{name} + "\"; " + outputs
                : "\"" + std::string{name} + "\" could be " + list(found.candidates));
    }
    const auto& full = *found.port;
    document_.edit("Set port of " + trackName + " to " + full, view().place(), [&](core::Song& song)
                   { song.updateTrack(*id, [&](core::Track& t) { t.port = full; }); });
    view().refresh();
    return {true, who + " plays to " + full, false};
}

CommandResult Workspace::section(std::string_view name)
{
    const auto first = name.find_first_not_of(" \t");
    name = first == std::string_view::npos
               ? std::string_view{}
               : name.substr(first, name.find_last_not_of(" \t") - first + 1);
    auto& song = document_.song();
    const auto& meter = view().cursorMeter();
    const auto tick = meter.barStart(meter.toBarBeatTick(view().cursor().tick).bar);
    const auto at = positionText(meter, tick);
    const auto current = song.sectionAt(tick);
    if (tick >= song.end())
    {
        return CommandResult::error("There's no section after the song's end to split");
    }
    if (song.sectionStart(current) == tick)
    {
        return CommandResult::error(song.sections()[current]->name() + " starts at " + at +
                                    " already");
    }
    const std::string wanted = name.empty() ? song.newSectionName() : std::string{name};
    std::optional<core::Song::Split> split;
    document_.edit("Start section " + wanted + " at " + at, view().place(),
                   [&](core::Song& s) { split = s.splitSection(tick, wanted); });
    refresh();
    if (!split)
    {
        return CommandResult::error("Can't start a section at " + at);
    }
    // The name it got, numbered if another section had it; asking for one, with that to edit,
    // if none was given (decided October 2026, splitting first as i on the strip does).
    const auto& named = document_.song().sections()[split->index]->name();
    if (name.empty())
    {
        openPrompt("section-rename " + named);
    }
    std::string cut;
    if (split->conductor)
    {
        cut = "the conductor and the tracks following it";
    }
    for (const auto id : split->tracks)
    {
        const auto track = document_.song().track(id);
        cut += (cut.empty() ? "" : ", ") +
               (track->name.empty() ? std::string{"a track"} : track->name);
    }
    if (!cut.empty())
    {
        // A bar the new section starts inside ends short, as a remainder bar.
        return CommandResult{true, named + " starts at " + at + "; this cuts a bar short in " + cut,
                             true};
    }
    return {true, named + " starts at " + at, false};
}

namespace
{

std::string trackNameOf(const core::Song& song, core::TrackId id)
{
    const auto* track = song.track(id);
    return track == nullptr || track->name.empty() ? std::string{"A track"} : track->name;
}

} // namespace

CommandResult Workspace::label(std::string_view name)
{
    // :label NAME puts a label at the cursor's time, or renames the one there; alone, it asks
    // for the name, with the one there to edit, as :section-rename does.
    while (!name.empty() && name.front() == ' ')
    {
        name.remove_prefix(1);
    }
    while (!name.empty() && name.back() == ' ')
    {
        name.remove_suffix(1);
    }
    const auto tick = view().cursor().tick;
    const auto labels = document_.song().labels();
    const auto it = std::ranges::find(labels, tick, &core::Label::tick);
    if (name.empty())
    {
        openPrompt("label " + (it == labels.end() ? std::string{} : it->name));
        return {true};
    }
    const std::string text{name};
    if (it != labels.end() && it->name == text)
    {
        return {true, "The label here is already " + text, false};
    }
    const auto title =
        it == labels.end() ? "Label " + text : "Rename label " + it->name + " to " + text;
    document_.edit(title, view().place(), [&](core::Song& song) { song.setLabel(tick, text); });
    refresh();
    return {true, (it == labels.end() ? "Labelled " : "Renamed it ") + text, false};
}

CommandResult Workspace::deleteLabels()
{
    // The labels in the selection, or the one at the cursor's time; one undo step.
    const auto span = selection_ ? view().selectionSpan() : std::nullopt;
    const auto tick = view().cursor().tick;
    std::vector<core::Label> doomed;
    for (const auto& label : document_.song().labels())
    {
        if (span ? label.tick >= span->first && label.tick < span->second : label.tick == tick)
        {
            doomed.push_back(label);
        }
    }
    if (doomed.empty())
    {
        return CommandResult::error(
            span ? "There are no labels in " + view().selectionStatus().value_or("the selection")
                 : std::string{"There's no label here; ]l goes to the next"});
    }
    const auto title = doomed.size() == 1 ? "Delete label " + doomed.front().name
                                          : "Delete " + std::to_string(doomed.size()) + " labels";
    document_.edit(title, view().place(),
                   [&](core::Song& song)
                   {
                       for (const auto& label : doomed)
                       {
                           song.removeLabel(label.tick);
                       }
                   });
    refresh();
    return {true, "Deleted" + title.substr(6), false};
}

CommandResult Workspace::labelToSection()
{
    if (const auto span = selection_ ? view().selectionSpan() : std::nullopt)
    {
        return labelsToSections(span->first, span->second);
    }
    // The label at or before the cursor, its section starting at the bar line of its bar in
    // the conductor's bars, asking first unless the cursor is on it and it's on a bar line.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto tick = view().cursor().tick;
    const auto labels = song.labels();
    const auto after = std::ranges::upper_bound(labels, tick, {}, &core::Label::tick);
    if (after == labels.begin())
    {
        return CommandResult::error("There's no label at or before the cursor");
    }
    const auto label = *std::prev(after);
    const auto start = meter.barStart(meter.toBarBeatTick(label.tick).bar);
    const auto section = song.sectionAt(start);
    if (song.sectionStart(section) == start)
    {
        return CommandResult::error(label.name + " is at the start of " +
                                    song.sections()[section]->name() +
                                    ", so there's no section to start");
    }
    const auto at = positionText(meter, start);
    const auto make = [this, label, start, at]
    {
        std::optional<core::Song::Split> split;
        document_.edit("Start section " + label.name + " at " + at + ", from its label",
                       view().place(),
                       [&](core::Song& s)
                       {
                           split = s.splitSection(start, label.name);
                           s.removeLabel(label.tick);
                       });
        refresh();
        const auto& named = document_.song().sections()[split->index]->name();
        setMessage(named + " starts at " + at + ", from its label", MessageKind::Information);
    };
    if (label.tick == tick && label.tick == start)
    {
        make();
        return {true, {}, false};
    }
    ask(label.tick == start ? "Start a section from the label " + label.name + " at " + at + "?"
                            : "Start a section at " + at + " from the label " + label.name +
                                  " at " + positionText(meter, label.tick) + "?",
        make);
    return {true, {}, false};
}

CommandResult Workspace::labelsToSections(core::Tick from, core::Tick to)
{
    const auto labels = document_.song().labels();
    if (std::ranges::none_of(labels, [&](const core::Label& label)
                             { return label.tick >= from && label.tick < to; }))
    {
        return CommandResult::error("There are no labels in " +
                                    view().selectionStatus().value_or("the selection"));
    }
    auto run = std::make_shared<LabelRun>();
    run->from = view().cursor();
    run->first = from;
    run->last = to;
    runLabels(run);
    return {true, {}, false};
}

void Workspace::runLabels(std::shared_ptr<LabelRun> run)
{
    // On through the labels from where the run is: those on a bar line taken, those whose bar
    // already starts a section (or another label's) passed over, and for each of the others
    // the cursor going to it and a question, after which the run goes on.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto labels = song.labels();
    for (; run->next < labels.size(); ++run->next)
    {
        const auto label = labels[run->next];
        if (label.tick < run->first || label.tick >= run->last)
        {
            continue; // outside the selection
        }
        const auto start = meter.barStart(meter.toBarBeatTick(label.tick).bar);
        if (song.sectionStart(song.sectionAt(start)) == start)
        {
            ++run->atStarts;
            continue;
        }
        if (std::ranges::find(run->starts, start, &std::pair<core::Tick, std::string>::first) !=
            run->starts.end())
        {
            ++run->sharing;
            continue;
        }
        const auto take = [run, label, start]
        {
            run->starts.emplace_back(start, label.name);
            run->labels.push_back(label.tick);
        };
        if (label.tick == start)
        {
            take();
            continue;
        }
        auto cursor = view().cursor();
        cursor.tick = label.tick;
        view().setCursor(cursor);
        ++run->next;
        ask(
            "Start a section at " + positionText(meter, start) + " from the label " + label.name +
                " at " + positionText(meter, label.tick) + "?",
            [this, run, take]
            {
                take();
                runLabels(run);
            },
            [this, run]
            {
                ++run->declined;
                runLabels(run);
            },
            [this, run]
            {
                view().setCursor(run->from);
                setMessage("Nothing done", MessageKind::Information);
            });
        return;
    }
    finishLabels(*run);
}

void Workspace::finishLabels(const LabelRun& run)
{
    // Every section started at once, as one undo step, the cursor back where it was.
    view().setCursor(run.from);
    std::string left;
    const auto add = [&](int n, const std::string& one, const std::string& many)
    {
        if (n > 0)
        {
            left += "; " + (n == 1 ? "1 was " + one : std::to_string(n) + " were " + many);
        }
    };
    add(run.atStarts, "at a section's start", "at sections' starts");
    add(run.sharing, "in a bar with another", "in bars with others");
    add(run.declined, "left as it was", "left as they were");
    if (run.starts.empty())
    {
        setMessage("No labels turned into sections" + left, MessageKind::Information);
        return;
    }
    const auto turned = run.starts.size();
    document_.edit("Turn " + std::to_string(turned) + (turned == 1 ? " label" : " labels") +
                       " into sections",
                   view().place(),
                   [&](core::Song& s)
                   {
                       for (const auto& [start, name] : run.starts)
                       {
                           s.splitSection(start, name);
                       }
                       for (const auto tick : run.labels)
                       {
                           s.removeLabel(tick);
                       }
                   });
    refresh();
    setMessage("Turned " + std::to_string(turned) +
                   (turned == 1 ? " label into a section" : " labels into sections") + left,
               MessageKind::Information);
}

CommandResult Workspace::barDelete(int count)
{
    if (const auto span = selection_ ? view().selectionSpan() : std::nullopt)
    {
        return deleteBars(span->first, span->second);
    }
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto bar = meter.toBarBeatTick(view().cursor().tick).bar;
    const auto from = meter.barStart(bar);
    if (from >= song.end())
    {
        return CommandResult::error("There are no bars after the song's end to delete");
    }
    return deleteBars(from, std::min(meter.barStart(bar + std::max(count, 1)), song.end()));
}

CommandResult Workspace::deleteBars(core::Tick from, core::Tick to)
{
    // The conductor's bars from one bar line to another, on every track, done on a copy first,
    // which says whether it can be, as one undo step.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto bar = meter.toBarBeatTick(from).bar;
    const auto last = meter.toBarBeatTick(to).bar;
    if (meter.barStart(bar) != from || (to < song.end() && meter.barStart(last) != to))
    {
        return CommandResult::error(barsText(meter, from, to) +
                                    " aren't whole bars of the conductor's; take them out of "
                                    "each track instead");
    }
    auto n = last - bar;
    if (meter.barStart(last) != to)
    {
        ++n; // to the song's end, inside a bar
    }
    const auto bars = barsText(meter, from, to);
    auto edited = song;
    const auto edit = edited.removeBars(from, n);
    if (!edit.done)
    {
        if (edit.misaligned)
        {
            return CommandResult::error(trackNameOf(song, *edit.misaligned) +
                                        "'s bars don't line up with " + bars +
                                        "; delete them on each track instead");
        }
        return CommandResult::error("A song keeps at least one bar");
    }
    document_.edit("Delete " + bars + " on every track", view().place(),
                   [&](core::Song& s) { s = edited; });
    register_ = EventRegister{};
    register_.sections = edit.removed;
    refresh();
    return {true,
            "Deleted " + bars +
                " on every track; p on the section strip puts them back as a section",
            false};
}

CommandResult Workspace::barInsert(std::string_view arguments)
{
    // A number of bars and a meter, either or both, in either order.
    const auto& song = document_.song();
    std::int64_t n = 1;
    std::optional<core::TimeSignature> signature;
    std::size_t at = 0;
    while (true)
    {
        const auto start = arguments.find_first_not_of(" \t", at);
        if (start == std::string_view::npos)
        {
            break;
        }
        const auto stop = std::min(arguments.find_first_of(" \t", start), arguments.size());
        const auto word = arguments.substr(start, stop - start);
        at = stop;
        std::int64_t number = 0;
        const auto [ptr, ec] = std::from_chars(word.data(), word.data() + word.size(), number);
        if (ec == std::errc{} && ptr == word.data() + word.size() && number > 0)
        {
            n = number;
        }
        else if (const auto meter = signatureOf(word, song.ppqn()))
        {
            signature = meter;
        }
        else
        {
            return CommandResult::error(
                "bar-insert takes a number of bars and a meter, such as 2 3/4");
        }
    }
    const auto& meter = song.meter();
    const auto bar = meter.toBarBeatTick(view().cursor().tick).bar;
    const auto from = meter.barStart(bar);
    if (from >= song.end())
    {
        return CommandResult::error("Bars go in before a bar of the song");
    }
    const auto what = (n == 1 ? std::string{"a bar"} : std::to_string(n) + " bars") +
                      (signature ? " of " + std::to_string(signature->numerator) + "/" +
                                       std::to_string(signature->denominator)
                                 : std::string{});
    auto edited = song;
    const auto edit = edited.insertBars(from, n, signature);
    if (!edit.done)
    {
        if (edit.misaligned)
        {
            return CommandResult::error(trackNameOf(song, *edit.misaligned) +
                                        "'s bars don't line up with " + what +
                                        "; put them in on each track instead");
        }
        return CommandResult::error("Can't insert " + what + " here");
    }
    document_.edit("Insert " + what + " before bar " + std::to_string(bar) + " on every track",
                   view().place(), [&](core::Song& s) { s = edited; });
    refresh();
    if (signature)
    {
        chosenMeter(*signature);
    }
    return {true, {}, false};
}

void Workspace::chosenMeter(core::TimeSignature meter)
{
    // Offered from now on, unless it's built in.
    if (std::ranges::find(kOfferedMeters, meter) == kOfferedMeters.end())
    {
        remembered_.typedMeter(meter);
        rememberedChanged();
    }
}

CommandResult Workspace::renameSection(std::string_view name)
{
    const auto first = name.find_first_not_of(" \t");
    const auto index = document_.song().sectionAt(view().cursor().tick);
    const auto old = document_.song().sections()[index]->name();
    if (first == std::string_view::npos)
    {
        // Asking for one, with its name to edit, as c on the strip does.
        openPrompt("section-rename " + old);
        return {true};
    }
    name = name.substr(first, name.find_last_not_of(" \t") - first + 1);
    document_.edit("Rename " + old + " to " + std::string{name}, view().place(),
                   [&](core::Song& song) { song.renameSection(index, std::string{name}); });
    refresh();
    // As renamed, numbered if another section had the name.
    return {true, "Renamed " + old + " to " + document_.song().sections()[index]->name(), false};
}

void Workspace::openPrompt(std::string line)
{
    answer_ = {};
    mode_ = Mode::CommandLine;
    line_.open(decodeUtf8(line));
}

void Workspace::ask(std::string question, std::function<void()> yes, std::function<void()> no,
                    std::function<void()> stop)
{
    setMessage(std::move(question) + (stop ? " (y/n, Escape stops)" : " (y/n)"),
               MessageKind::Warning);
    question_ = Question{std::move(yes), std::move(no), std::move(stop)};
}

void Workspace::askFor(std::string label, std::function<void(const std::string&)> answer,
                       std::string defaultAnswer)
{
    mode_ = Mode::CommandLine;
    line_.open();
    answerLabel_ = std::move(label);
    answer_ = std::move(answer);
    answerDefault_ = std::move(defaultAnswer);
    answerCommand_.clear();
}

void Workspace::playFrom(core::Tick tick)
{
    if (player_ == nullptr)
    {
        setMessage("Nothing to play to", MessageKind::Warning);
        return;
    }
    player_->setMarker(tick);
    player_->play();
    following_ = true;
    showPlayhead();
}

Frame Workspace::frame() const
{
    Frame frame;
    view().addRegions(frame);
    if (keysPage_)
    {
        frame.push_back(keysRegion());
    }
    if (const auto shown = completions())
    {
        frame.push_back(completionRegion(*shown));
    }
    if (palette_)
    {
        frame.push_back(paletteRegion());
    }
    if (const auto shown = hints())
    {
        frame.push_back(hintRegion(*shown));
    }
    if (const auto lines = messageLines(); !lines.empty())
    {
        frame.push_back(messageRegion(lines));
    }
    if (!listing_.empty())
    {
        frame.push_back(listingRegion());
    }
    frame.push_back(mode_ == Mode::CommandLine && !view().typing() ? promptRegion()
                                                                   : statusRegion());
    return frame;
}

std::vector<PaletteItem> Workspace::paletteItems() const
{
    // The commands that can be run from Normal mode, the palette apart, with their keys.
    std::map<std::string, std::vector<std::string>, std::less<>> keys;
    for (const auto& [sequence, binding] : keymap_.bindings(Mode::Normal))
    {
        keys[binding.command].push_back(shownKeys(sequence));
    }
    std::vector<PaletteItem> items;
    const auto now = clock_();
    for (const auto& info : commands_.all())
    {
        const bool normal = info.modes == 0 || (info.modes & modeBit(Mode::Normal)) != 0;
        if (!normal || info.object || !info.palette || info.name == "palette")
        {
            continue;
        }
        items.push_back({.name = std::string{info.name},
                         .title = titleOf(info),
                         .description = std::string{info.description},
                         .aliases = std::string{info.aliases},
                         .offered = offered(info.name),
                         .keys = {},
                         .usage = std::string{info.usage},
                         .rank = remembered_.rank(info.name, now)});
        if (const auto bound = keys.find(info.name); bound != keys.end())
        {
            items.back().keys = bound->second;
        }
    }
    return items;
}

std::optional<char> Workspace::palettePrefix() const
{
    // A first character that searches something else than the commands.
    const auto& text = line_.text();
    if (!text.empty() &&
        std::u32string_view{U"@#'?"}.find(text.front()) != std::u32string_view::npos)
    {
        return static_cast<char>(text.front());
    }
    return std::nullopt;
}

std::vector<PaletteItem> Workspace::prefixItems(char prefix) const
{
    // @ sections, # tracks, ' labels and ? settings (decided October 2026), each with what
    // helps choose it: its bars and meter, its port and channel, where it is, its value.
    const auto& song = document_.song();
    std::vector<PaletteItem> items;
    const auto item = [&](std::string id, std::string title, std::string about)
    {
        items.push_back({.name = std::move(id),
                         .title = std::move(title),
                         .description = std::move(about),
                         .aliases = "",
                         .offered = true,
                         .keys = {},
                         .usage = {},
                         .rank = 0});
    };
    if (prefix == '@')
    {
        for (std::size_t i = 0; i < song.sections().size(); ++i)
        {
            const auto start = song.sectionStart(i);
            item(std::to_string(i), song.sections()[i]->name(),
                 barsText(song.meter(), start, song.sectionStart(i + 1)) + ", " +
                     std::to_string(song.meter().signatureAt(start).numerator) + "/" +
                     std::to_string(song.meter().signatureAt(start).denominator));
        }
    }
    else if (prefix == '#')
    {
        // The conductor first, as the event list's track 0 (decided November 2026).
        item("conductor", "Conductor", "track 0, the tempos, meters, sections and labels");
        const auto tracks = song.tracks();
        for (std::size_t i = 0; i < tracks.size(); ++i)
        {
            const auto& track = *tracks[i];
            item(std::to_string(i),
                 track.name.empty() ? "Track " + std::to_string(i + 1) : track.name,
                 "track " + std::to_string(i + 1) + ", " +
                     (track.port.empty() ? std::string{"the default port"} : track.port) +
                     ", channel " + std::to_string(track.channel));
        }
    }
    else if (prefix == '\'')
    {
        const auto labels = song.labels();
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            item(std::to_string(i), labels[i].name, positionText(song.meter(), labels[i].tick));
        }
        for (const auto& mark : song.marks())
        {
            item("mark " + std::string(1, mark.letter), "Mark " + std::string(1, mark.letter),
                 markAbout(mark));
        }
    }
    else if (prefix == '?' && settings_ != nullptr)
    {
        for (const auto& setting : settings_->all())
        {
            item(std::string{setting.name}, std::string{setting.name},
                 formatValue(setting.value) + "  " + std::string{setting.description});
        }
    }
    return items;
}

bool Workspace::inSongOrder() const
{
    // Sections, tracks and labels, listed from the top down as the song has them (decided
    // October 2026), rather than ranked from the line up as commands are.
    const auto prefix = palettePrefix();
    return prefix && *prefix != '?';
}

std::vector<PaletteItem> Workspace::paletteShown() const
{
    const auto prefix = palettePrefix();
    const auto items = prefix ? prefixItems(*prefix) : paletteItems();
    auto query = encodeUtf8(line_.text());
    if (prefix)
    {
        query.erase(0, 1);
    }
    std::vector<PaletteItem> shown;
    for (const auto i : paletteMatches(items, query, inSongOrder()))
    {
        shown.push_back(items[i]);
    }
    return shown;
}

bool Workspace::runPrefixItem(char prefix, const PaletteItem& item)
{
    // Going to a section, a track or a label, or a setting on the : prompt to change.
    const auto& song = document_.song();
    if (prefix == '?')
    {
        const auto* setting = settings_ != nullptr ? settings_->find(item.name) : nullptr;
        line_.open(decodeUtf8("set " + item.name + "=" +
                              (setting != nullptr ? formatValue(setting->value) : std::string{})));
        return true;
    }
    line_.clear();
    mode_ = selection_ ? Mode::Visual : Mode::Normal;
    if (item.name.starts_with("mark "))
    {
        const bool done = goToMark(static_cast<char32_t>(item.name.back()), 0);
        return done;
    }
    if (item.name == "conductor")
    {
        return run("view-conductor", 0).changed;
    }
    const auto index = static_cast<std::size_t>(std::stoul(item.name));
    auto cursor = view().cursor();
    if (!selection_)
    {
        jumpedFrom_ = cursor;
    }
    if (prefix == '@' && index < song.sections().size())
    {
        cursor.tick = song.sectionStart(index);
    }
    else if (prefix == '#' && index < song.tracks().size())
    {
        cursor.track = song.tracks()[index]->id();
    }
    else if (prefix == '\'' && index < song.labels().size())
    {
        cursor.tick = song.labels()[index].tick;
    }
    view().setCursor(cursor);
    return true;
}

void Workspace::setRemembered(Remembered remembered, std::function<void(const Remembered&)> changed)
{
    remembered_ = std::move(remembered);
    rememberedChanged_ = std::move(changed);
}

void Workspace::rememberedChanged()
{
    if (rememberedChanged_)
    {
        rememberedChanged_(remembered_);
    }
}

std::string Workspace::keysFor(std::string_view command) const
{
    auto keys = keysOf(command);
    return keys == command ? std::string{} : keys;
}

bool Workspace::runPalette(std::string_view command)
{
    // Enter runs the command chosen, or with an argument it must have, puts it on the : prompt,
    // its choices showing; Tab puts it there as it is to edit; Up and Down, and Ctrl-p and
    // Ctrl-n, go up and down the list; Escape closes it; the rest edit what's typed.
    const auto shown = paletteShown();
    const auto last = shown.empty() ? 0 : shown.size() - 1;
    if (command == "command-line-cancel" ||
        (command == "command-line-backspace" && line_.text().empty()))
    {
        // Back to the mode it was opened from, a selection kept.
        palette_.reset();
        line_.clear();
        mode_ = selection_ ? Mode::Visual : Mode::Normal;
        return true;
    }
    if (command == "command-line-older" || command == "command-line-newer" ||
        command == "command-line-page-up" || command == "command-line-page-down")
    {
        // Up and Down the way the list looks: for commands up is away from the line, to the
        // matches after the best; in song order up is earlier.
        const std::ptrdiff_t up = inSongOrder() ? -1 : 1;
        const std::ptrdiff_t by = command == "command-line-older"     ? up
                                  : command == "command-line-newer"   ? -up
                                  : command == "command-line-page-up" ? up * kPaletteRows
                                                                      : -up * kPaletteRows;
        // From none chosen, the best.
        palette_->chosen = !palette_->chosen
                               ? 0
                               : static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                                     static_cast<std::ptrdiff_t>(*palette_->chosen) + by, 0,
                                     static_cast<std::ptrdiff_t>(last)));
        return true;
    }
    if (command == "command-line-start" || command == "command-line-end")
    {
        // Home and End: the top and the bottom of the list as it looks.
        const bool top = command == "command-line-start";
        palette_->chosen = top == inSongOrder() ? 0 : last;
        return true;
    }
    if (command == "command-line-run" || command == "command-line-next")
    {
        if (shown.empty() || !palette_->chosen)
        {
            return false; // nothing chosen yet
        }
        const auto& item = shown[std::min(*palette_->chosen, last)];
        if (const auto prefix = palettePrefix())
        {
            palette_.reset();
            return runPrefixItem(*prefix, item);
        }
        const auto* info = commands_.find(item.name);
        palette_.reset();
        if (command == "command-line-run")
        {
            remembered_.ran(item.name, clock_());
            rememberedChanged();
        }
        // Taking arguments, even ones it can do without, it goes to the prompt, their hint
        // showing (decided October 2026, after use), so what it takes isn't missed; those it must
        // have with their choices showing too.
        const bool takes = info != nullptr && !info->usage.empty();
        const bool needs = takes && !info->usage.starts_with("[");
        if (command == "command-line-next" || takes)
        {
            line_.open(decodeUtf8(item.name + " "));
            if (needs)
            {
                complete(1);
            }
            return true;
        }
        // As its keys would run it in the mode the palette was opened from: in Visual mode on
        // the selection, as an operator or a command taking a range does.
        line_.clear();
        mode_ = selection_ ? Mode::Visual : Mode::Normal;
        const auto ran = run(item.name, 0);
        if (const auto keys = keysFor(item.name); message_.empty() && !keys.empty())
        {
            setMessage("Also: " + keys, MessageKind::Information); // teaching the binding
        }
        return ran.changed;
    }
    const auto edited = editLine(line_, command);
    palette_->chosen = line_.text().empty() ? std::nullopt : std::optional<std::size_t>{0};
    return edited.value_or(false);
}

Region Workspace::paletteRegion() const
{
    // Above the prompt, as completion's matches are: the best nearest the line, or in song order
    // from the top down, each with its keys and what it does; the one chosen in the cursor's
    // colours; those that do nothing here dimmed; a ▲ or ▼ at the side where there are more.
    const auto shown = paletteShown();
    // The row chosen, as an index, or past the matches if none is.
    const auto chosen = palette_->chosen
                            ? std::min(*palette_->chosen, shown.empty() ? 0 : shown.size() - 1)
                            : shown.size();
    const auto rows = std::min<std::size_t>(shown.size(), kPaletteRows);
    const auto first = chosen < shown.size() && chosen >= rows ? chosen - rows + 1 : 0;
    const int margin = BarSpacing::forCell(cell_).margin;
    const int pad = cell_.height / 4;
    const int height = std::max<int>(static_cast<int>(rows), 1) * cell_.height + 2 * pad;
    const auto& status = layout_.bottom[0];
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    if (shown.empty())
    {
        region.text.push_back(
            {margin, pad, U"No command matches", {Role::Dim, Role::Panel, kPlain}});
        return region;
    }
    std::size_t titleWidth = 0;
    std::size_t keysWidth = 0;
    std::vector<std::string> keys;
    for (auto i = first; i < first + rows; ++i)
    {
        keys.push_back(keysFor(shown[i].name));
        titleWidth = std::max(
            titleWidth, decodeUtf8(shown[i].title).size() +
                            (shown[i].usage.empty() ? 0 : 1 + decodeUtf8(shown[i].usage).size()));
        keysWidth = std::max(keysWidth, decodeUtf8(keys.back()).size());
    }
    const auto x = [&](std::size_t column)
    { return margin + static_cast<int>(column) * cell_.width; };
    for (std::size_t r = 0; r < rows; ++r)
    {
        const auto i = first + r;
        const auto& item = shown[i];
        const auto row = inSongOrder() ? r : rows - 1 - r; // the best at the bottom
        const int y = pad + static_cast<int>(row) * cell_.height;
        const bool isChosen = i == chosen;
        const auto background = isChosen ? Role::Cursor : Role::Panel;
        if (isChosen)
        {
            region.under.push_back({{0, y, region.bounds.width, cell_.height}, Role::Cursor, 255});
        }
        const auto role = [&](Role own)
        {
            return isChosen ? Role::CursorText : item.offered ? own : Role::Dim;
        };
        region.text.push_back(
            {x(0), y, decodeUtf8(item.title), {role(Role::Text), background, kPlain}});
        if (!item.usage.empty())
        {
            region.text.push_back({x(decodeUtf8(item.title).size() + 1),
                                   y,
                                   decodeUtf8(item.usage),
                                   {isChosen ? Role::CursorText : Role::Dim, background, kPlain}});
        }
        region.text.push_back(
            {x(titleWidth + 2), y, decodeUtf8(keys[r]), {role(Role::HintKey), background, kPlain}});
        region.text.push_back({x(titleWidth + keysWidth + 4),
                               y,
                               decodeUtf8(item.description),
                               {isChosen ? Role::CursorText : Role::Dim, background, kPlain}});
    }
    // More before the first shown, or after the last, at the side they'd be.
    const int side = region.bounds.width - margin - cell_.width;
    const Style more{Role::Dim, Role::Panel, kPlain};
    const bool before = first > 0;
    const bool after = first + rows < shown.size();
    const bool down = inSongOrder();
    const int top = pad;
    const int bottom = pad + static_cast<int>(rows - 1) * cell_.height;
    if (down ? before : after)
    {
        region.text.push_back({side, top, U"▲", more});
    }
    if (down ? after : before)
    {
        region.text.push_back({side, bottom, U"▼", more});
    }
    return region;
}

std::u32string Workspace::promptLabel() const
{
    // ":", or a question's label, for its answer; or from Visual mode, what's selected, which
    // the line acts on.
    if (answer_)
    {
        return decodeUtf8(answerLabel_);
    }
    if (searchPrompt_)
    {
        return U"/";
    }
    if (palette_)
    {
        return decodeUtf8(selection_ ? "Command on " +
                                           view().selectionStatus().value_or("selection") + ": "
                                     : std::string{"Command: "});
    }
    if (selection_)
    {
        return decodeUtf8(view().selectionStatus().value_or("selection") + ": ");
    }
    return U":";
}

Region Workspace::promptRegion() const
{
    // ":" and the line, from where the cursor still fits, and the cursor as a cell of its own.
    const int margin = BarSpacing::forCell(cell_).margin;
    const auto& text = line_.text();
    const auto cursor = line_.cursor();
    const auto label = promptLabel();
    const auto room = static_cast<std::size_t>(
        std::max((width_ - 2 * margin) / cell_.width - 1 - static_cast<int>(label.size()), 1));
    const std::size_t first = cursor >= room ? cursor - room + 1 : 0;
    Region region{
        .bounds = layout_.bottom[0], .background = Role::Bar, .text = {}, .under = {}, .over = {}};
    const auto x = [&](std::size_t column)
    { return margin + static_cast<int>(column) * cell_.width; };
    const auto lead = label.size();
    region.text.push_back({x(0), 0, label, kBarText});
    region.text.push_back({x(lead), 0, text.substr(first, cursor - first), kBarText});
    const char32_t under = cursor < text.size() ? text[cursor] : U' ';
    region.text.push_back({x(lead + cursor - first), 0, std::u32string(1, under), kCursor});
    if (cursor + 1 < text.size())
    {
        region.text.push_back({x(lead + 1 + cursor - first), 0, text.substr(cursor + 1), kBarText});
    }
    // The command's arguments not yet typed, dimmed after the line ([N] [METER]).
    if (const auto hint = argumentHint(); !hint.empty())
    {
        const auto end = std::max(text.size(), cursor + 1);
        const bool spaced = !text.empty() && text.back() == U' ';
        region.text.push_back({x(lead + end - first + (spaced ? 0 : 1)),
                               0,
                               decodeUtf8(hint),
                               {Role::Dim, Role::Bar, kPlain}});
    }
    return region;
}

std::string Workspace::argumentHint() const
{
    // At the : prompt, once the line holds a command and a space: its arguments from the one
    // being typed on, as its usage writes them, brackets for those it can do without (decided
    // October 2026, so :bar-insert says it takes [N] [METER]).
    if (answer_)
    {
        // A transform's question: the options not yet given, as they're written.
        const auto given = encodeUtf8(line_.text());
        std::string hint;
        for (const auto& option : transformOptions(answerCommand_))
        {
            if (given.find(std::string{option.name} + "=") == std::string::npos)
            {
                hint += (hint.empty() ? "" : " ") + std::string{option.name} + "=" +
                        std::string{option.form};
            }
        }
        return hint;
    }
    if (palette_ || view().typing())
    {
        return {};
    }
    const auto text = encodeUtf8(line_.text());
    const auto space = text.find(' ');
    if (space == std::string::npos)
    {
        return {};
    }
    const auto* info = commands_.find(std::string_view{text}.substr(0, space));
    if (info == nullptr || info->usage.empty())
    {
        return {};
    }
    // The arguments typed, or being typed: words after the name, the last only once a space
    // follows it.
    std::size_t typed = 0;
    for (std::size_t at = space; at < text.size();)
    {
        const auto start = text.find_first_not_of(' ', at);
        if (start == std::string::npos)
        {
            break;
        }
        const auto stop = std::min(text.find(' ', start), text.size());
        typed += stop < text.size() ? 1 : 0;
        at = stop;
    }
    std::vector<std::string_view> words;
    const auto usage = info->usage;
    for (std::size_t at = 0; at < usage.size();)
    {
        const auto start = usage.find_first_not_of(' ', at);
        if (start == std::string_view::npos)
        {
            break;
        }
        const auto stop = std::min(usage.find(' ', start), usage.size());
        words.push_back(usage.substr(start, stop - start));
        at = stop;
    }
    const bool typing = !text.ends_with(' ');
    std::string hint;
    for (auto i = typed + (typing ? 1 : 0); i < words.size(); ++i)
    {
        hint += (hint.empty() ? "" : " ") + std::string{words[i]};
    }
    return hint;
}

Region Workspace::statusRegion() const
{
    // A message too long for it is shown above it alone, not cut short here as well.
    auto region =
        layoutBar(statusBar(messageLines().empty()), width_, cell_, BarSpacing::forCell(cell_));
    region.bounds = layout_.bottom[0];
    return region;
}

std::vector<std::string> Workspace::messageLines() const
{
    // Whether the status line holds it, with what's beside it; if not, its words in lines as
    // wide as the window.
    if (message_.empty())
    {
        return {};
    }
    const auto bar = statusBar();
    const auto spacing = BarSpacing::forCell(cell_);
    const int columns = std::max((width_ - 2 * spacing.margin) / cell_.width, 1);
    int used = (spacing.gap + cell_.width - 1) / cell_.width;
    for (const auto* side : {&bar.left, &bar.right})
    {
        for (const auto& segment : *side)
        {
            used += static_cast<int>(decodeUtf8(segment.text).size());
        }
    }
    if (used <= columns)
    {
        return {};
    }
    std::vector<std::string> lines(1);
    std::size_t length = 0;
    for (std::size_t at = 0; at < message_.size();)
    {
        const auto space = message_.find(' ', at);
        const auto word =
            message_.substr(at, space == std::string::npos ? std::string::npos : space - at);
        const auto wide = decodeUtf8(word).size();
        if (length > 0 && length + 1 + wide > static_cast<std::size_t>(columns))
        {
            lines.emplace_back();
            length = 0;
        }
        lines.back() += (length > 0 ? " " : "") + word;
        length += (length > 0 ? 1 : 0) + wide;
        at = space == std::string::npos ? message_.size() : space + 1;
    }
    return lines;
}

Region Workspace::messageRegion(const std::vector<std::string>& lines) const
{
    const int margin = BarSpacing::forCell(cell_).margin;
    const int pad = cell_.height / 4;
    const int height = static_cast<int>(lines.size()) * cell_.height + 2 * pad;
    const auto& status = layout_.bottom[0];
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    const Style style{messageKind_ == MessageKind::Warning ? Role::Warning : Role::Message,
                      Role::Panel, kPlain};
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        region.text.push_back(
            {margin, pad + static_cast<int>(i) * cell_.height, decodeUtf8(lines[i]), style});
    }
    return region;
}

Bar Workspace::statusBar(bool withMessage) const
{
    const Style label{Role::Dim, Role::Bar, kPlain};
    Bar bottom;
    bottom.left = {{view().typing()         ? " EDIT "
                    : mode_ == Mode::Visual ? " VISUAL "
                                            : " NORMAL ",
                    {Role::ModeText, Role::Mode, kBold}}};
    if (recording_ != 0)
    {
        const auto ends = keysFor("macro-record");
        bottom.left.push_back({"  recording " + macroName(recording_) + ", " +
                                   (ends.empty() ? std::string{":macro-record"} : ends) +
                                   " ends it",
                               {Role::Accent, Role::Bar, kBold}});
    }
    if (withMessage && !message_.empty())
    {
        const auto role = messageKind_ == MessageKind::Warning ? Role::Warning : Role::Message;
        bottom.left.push_back({"  " + message_, {role, Role::Bar, kPlain}});
    }
    else if (const auto at = player_ != nullptr ? player_->position() : std::nullopt)
    {
        // While playing, where the playhead is.
        bottom.left.push_back({"  ▶ " + view().timeStatus(*at), {Role::Accent, Role::Bar, kBold}});
    }
    else if (const auto selected = mode_ == Mode::Visual ? view().selectionStatus() : std::nullopt)
    {
        bottom.left.push_back({"  " + *selected, kBarText});
        if (const auto warning = view().selectionWarning())
        {
            bottom.left.push_back({"; " + *warning, {Role::Warning, Role::Bar, kPlain}});
        }
        if (selection_ && selection_->cut)
        {
            bottom.left.push_back({"; cutting at the edges", {Role::Accent, Role::Bar, kPlain}});
        }
        if (selection_ && selection_->filter)
        {
            bottom.left.push_back({"; only the matches", {Role::Accent, Role::Bar, kPlain}});
        }
    }
    else
    {
        bottom.left.push_back({"  " + view().cursorStatus(), kBarText});
    }
    if (const auto keys = pending(); !keys.empty())
    {
        bottom.right.push_back({keys + "   ", {Role::Accent, Role::Bar, kBold}});
    }
    if (search_)
    {
        bottom.right.push_back({"/" + searchText_ + " ", label});
        bottom.right.push_back({view().matchStatus(*search_) + "   ", kBarText});
    }
    if (player_ != nullptr)
    {
        bottom.right.push_back({"From ", label});
        bottom.right.push_back({view().position(player_->marker()) + "   ", kBarText});
    }
    bottom.right.push_back({rewind_ ? "Rewind on   " : "Rewind off   ", kBarText});
    // The song's file, and [+] if it has changes not saved, as in Vim.
    const auto& file = document_.file();
    const auto& imported = document_.importedFrom();
    const auto name = file       ? utf8Text(file->filename())
                      : imported ? utf8Text(imported->filename()) + " (imported)"
                                 : std::string{"No file"};
    bottom.right.push_back({name + (document_.modified() ? " [+]" : ""), kBarText});
    return bottom;
}

} // namespace modal::ui
