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
        .name = "transform-velocity-phrase",
        .description =
            "Scale the phrase dynamics of the notes a range or object takes: the contour of "
            "their velocities' averages over a window (130% bigger swells, 70% flatter), each "
            "note keeping its place on it; asking how much and the window",
        .aliases = "vel-phrase",
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
    CommandInfo{.name = "transform-quantize",
                .description = "Quantise the notes a range or object takes, asking in turn for "
                               "the grid (the track's, at Enter), how far onto it, the swing and "
                               "what to move (starts, ends, both or lengths); Enter throughout "
                               "does what q does",
                .aliases = "quantize quantise",
                .usage = "[GRID] [strength=100%] [swing=50%] [move=starts]",
                .count = true,
                .range = true,
                .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{
        .name = "transform-stretch",
        .description =
            "Stretch the time of the events a range or object takes about the first, asking by "
            "how much: 2 for half time, 1/2 for double time, or a percentage "
            "(150%); the notes alone in the piano roll",
        .aliases = "stretch",
        .usage = "[RATIO]",
        .count = true,
        .range = true},
    CommandInfo{
        .name = "transform-length",
        .description =
            "Set the lengths of the notes a range or object takes, asking how: legato, each held "
            "until the next but for the last, a length (1/8) or a "
            "percentage of their own (80%)",
        .aliases = "length",
        .usage = "[LENGTH]",
        .count = true,
        .range = true},
    CommandInfo{
        .name = "transform-legato",
        .description =
            "Make the notes a range or object takes legato, each note or chord held until the "
            "next starts, but for the last, which keeps its length",
        .aliases = "legato",
        .count = true,
        .range = true},
    CommandInfo{
        .name = "transform-thin",
        .description =
            "Thin out dense controller, pitch bend and aftertouch data in a range or object, "
            "asking which, the most to keep a quarter note (16) and the "
            "smallest change kept (2); each gesture's first and last kept, "
            "and never the pedals or other switches",
        .aliases = "thin",
        .usage = "[all|ccN|bend|aftertouch] [per=16] [step=2]",
        .count = true,
        .range = true,
        .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{
        .name = "transform-filter",
        .description =
            "Delete the events a range or object takes that match a query, as / writes them "
            "(vel<20, cc7), asking which, the search's at Enter",
        .aliases = "filter",
        .usage = "[QUERY]",
        .count = true,
        .range = true,
        .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{
        .name = "transform-split",
        .description =
            "Split the notes a range or object takes at a note, moving it and those above, or "
            "those below, to another track, asking at which note, which side "
            "and which track; a new name makes a new track",
        .aliases = "split",
        .usage = "[NOTE] [side=above] [to=TRACK]",
        .count = true,
        .range = true,
        .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{.name = "transform-remap",
                .description =
                    "Remap a controller (cc1 to cc11) or a note (C1 to D1) in a range or object, "
                    "asking which and to what",
                .aliases = "remap",
                .usage = "[ccN|NOTE] [to=ccN|NOTE]",
                .count = true,
                .range = true,
                .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{
        .name = "transform-pedal-repedal",
        .description =
            "Re-pedal the range: its sustain replaced by a pedal down just after its first chord, "
            "lifted and pressed again around each chord change, and lifted as "
            "its last notes end; asking how long before and after",
        .aliases = "repedal",
        .usage = "[lift=10ms] [down=60ms]",
        .count = true,
        .range = true,
        .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{.name = "transform-pedal-each",
                .description = "Pedal the range afresh on each bar or beat, its sustain replaced; "
                               "asking which, and how "
                               "long before and after each line",
                .aliases = "pedal-each",
                .usage = "[bar|beat] [lift=10ms] [down=60ms]",
                .count = true,
                .range = true,
                .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{.name = "transform-pedal-retime",
                .description = "Re-time the range's performed sustain: each lift and press near a "
                               "chord change moved to "
                               "just before and after it, the rest left as played",
                .aliases = "retime",
                .usage = "[lift=10ms] [down=60ms] [near=200ms]",
                .count = true,
                .range = true,
                .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{
        .name = "transform-pedal-clean",
        .description =
            "Clean up the range's pedalling: exact repeats removed and flicker, a lift and a press "
            "again within a few milliseconds, merged; the first and last "
            "events kept",
        .aliases = "pedal-clean",
        .usage = "[flicker=30ms]",
        .count = true,
        .range = true,
        .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{.name = "transform-pedal-clear",
                .description = "Clear a pedal from the range, lifting it at the start if it's down "
                               "there and pressing it "
                               "again at the end if it was down there",
                .aliases = "pedal-clear",
                .usage = "[sustain|sostenuto|soft|all]",
                .count = true,
                .range = true,
                .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{
        .name = "transform-pedal-offset",
        .description =
            "Move every pedal change in the range later by some milliseconds, or earlier (-20ms)",
        .aliases = "pedal-offset",
        .usage = "[MS]",
        .count = true,
        .range = true,
        .choices = {Choices::Option, Choices::Option, Choices::Option}},
    CommandInfo{.name = "transform-pedal-bake",
                .description = "Bake the range's sustain into its notes: each note held by the "
                               "pedal lengthened to where it "
                               "lifts, and the sustain then cleared",
                .aliases = "pedal-bake",
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
        // Where the views' commands do something, kept here beside each other, since a view
        // runs commands others describe; a test runs each binding offered in each place.
        using enum Where;
        const std::array<std::pair<std::string_view, std::uint8_t>, 130> where{{
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
            {"transform-quantize", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-pedal-repedal", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-pedal-each", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-pedal-retime", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-pedal-clean", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-pedal-clear", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-pedal-offset", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-pedal-bake", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-thin", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-filter", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-split", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-remap", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-stretch", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-length", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-legato", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-transpose-key", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-invert", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-invert-key", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-retrograde", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-compress", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-expand", onlyIn({Tracks, Strip, PianoRoll, EventList})},
            {"transform-velocity-phrase", onlyIn({Tracks, Strip, PianoRoll, EventList})},
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

} // namespace modal::ui
