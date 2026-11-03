// SPDX-License-Identifier: GPL-3.0-or-later

// The transforms, each defined once (decided November 2026, after a code review): its command,
// how it asks and what it asks for, and the function that does it. The menu's keys are the
// modalrc's; the commands are made from this table (see Workspace::commands), in every view.

#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <vector>

namespace modal::ui
{

std::span<const Workspace::TransformSpec> Workspace::transformSpecs()
{
    // Made once, the settings they share with it (found allocating on every lookup in a code
    // review, November 2026).
    static const std::vector<TransformSpec> specs = []
    {
        using Asks = TransformSpec::Asks;
        const TransformSetting window{.name = "window",
                                      .question = "over a window of",
                                      .answer = "1bar",
                                      .form = "1bar|2bars|1/2",
                                      .values = {"1bar", "2bars", "4bars"},
                                      .lengths = true};
        const TransformSetting threshold{.name = "threshold",
                                         .question = "only those",
                                         .answer = "all",
                                         .form = "all|>N|<N",
                                         .values = {"all"}};
        // The pedalling tools' times, in milliseconds, the lift and press from their settings.
        const TransformSetting lift{.name = "lift",
                                    .question = "lifting it before each change by",
                                    .answer = "{lift}",
                                    .form = "ms"};
        const TransformSetting press{.name = "down",
                                     .question = "and pressing it after by",
                                     .answer = "{press}",
                                     .form = "ms"};
        return std::vector<TransformSpec>{
            {.name = "transform-transpose",
             .aliases = "transpose",
             .usage = "[SEMITONES]",
             .description = "Transpose the notes a range or object takes by a number of "
                            "semitones, asking how many (+12, -3); :transpose N those of the "
                            "selection, or else of the cursor's bar",
             .asks = Asks::Once,
             .label = "Transpose {} by how many semitones",
             .examples = "+12, -3",
             .answer = "",
             .apply = &Workspace::applyTranspose},
            {.name = "transform-transpose-key",
             .aliases = "transpose-key",
             .usage = "[DEGREES] [key=KEY]",
             .description = "Transpose the notes a range or object takes by degrees of a key's "
                            "scale, asking for the "
                            "key (the last used, at Enter) and how many (+2, -3); notes "
                            "outside the scale keep their distance above its note below",
             .asks = Asks::Key,
             .label = "",
             .examples = "",
             .answer = "",
             .apply = &Workspace::applyKeyLine},
            {.name = "transform-invert",
             .aliases = "invert",
             .usage = "[PIVOT]",
             .description =
                 "Invert the notes a range or object takes, mirroring their pitches about a pivot, "
                 "chromatically, asking which (the first note, at Enter)",
             .asks = Asks::Once,
             .label = "Invert {} about",
             .examples = "",
             .answer = "{first}",
             .apply = &Workspace::applyInvert},
            {.name = "transform-invert-key",
             .aliases = "invert-key",
             .usage = "[PIVOT] [key=KEY]",
             .description = "Invert the notes a range or object takes within a key's scale, "
                            "mirroring them by its "
                            "degrees about a pivot, asking for the key and the pivot",
             .asks = Asks::Key,
             .label = "",
             .examples = "",
             .answer = "",
             .apply = &Workspace::applyKeyLine},
            {.name = "transform-retrograde",
             .aliases = "retrograde",
             .usage = "",
             .description =
                 "Reverse the order in time of the notes a range or object takes, mirroring their "
                 "rhythm",
             .asks = Asks::Nothing,
             .label = "",
             .examples = "",
             .answer = "",
             .apply = &Workspace::applyRetrograde},
            {.name = "transform-velocity-lift",
             .aliases = "vel-lift",
             .usage = "[PERCENT]",
             .description = "Lift the velocities of the notes a range or object takes a "
                            "percentage of the way towards 127, keeping their order and "
                            "proportions, asking how far (25%); :vel-lift N% those of the "
                            "selection, or else of the cursor's bar",
             .asks = Asks::Once,
             .label = "Lift the velocities of {} towards 127 by",
             .examples = "",
             .answer = "25%",
             .apply = &Workspace::applyLift},
            {.name = "transform-velocity-lower",
             .aliases = "vel-lower",
             .usage = "[PERCENT]",
             .description = "Lower the velocities of the notes a range or object takes a "
                            "percentage of the way towards the floor (velfloor), keeping "
                            "their order and proportions, asking how far (25%)",
             .asks = Asks::Once,
             .label = "Lower the velocities of {} towards the floor by",
             .examples = "",
             .answer = "25%",
             .apply = &Workspace::applyLift},
            {.name = "transform-velocity-limit",
             .aliases = "vel-limit",
             .usage = "[LOW-HIGH]",
             .description = "Keep the velocities of the notes a range or object takes "
                            "between two values, asking which (20-110)",
             .asks = Asks::Once,
             .label = "Keep the velocities of {} between",
             .examples = "20-110",
             .answer = "",
             .apply = &Workspace::applyLimit},
            {.name = "transform-velocity-ramp",
             .aliases = "vel-ramp",
             .usage = "[FROM-TO]",
             .description = "A crescendo or diminuendo over the notes a range or object "
                            "takes, by time: velocities set from one value to another "
                            "(60-100), or their own scaled from one percentage to another "
                            "(80%-120%)",
             .asks = Asks::Once,
             .label = "Ramp the velocities of {}",
             .examples = "60-100, 80%-120%",
             .answer = "",
             .apply = &Workspace::applyRamp},
            {.name = "transform-velocity-compress",
             .aliases = "vel-compress",
             .usage = "[PERCENT] [window=1bar] [centre=local] [threshold=>N]",
             .description =
                 "Compress the velocities of the notes a range or object takes, pulling each a "
                 "percentage of the way towards the average of those around it "
                 "(50%): evens out the playing, keeping its swells; window= how "
                 "far around (1bar, 2bars, 1/2), centre= local, average or a "
                 "velocity, threshold= only those above (>90) or below (<30)",
             .asks = Asks::InTurn,
             .label = "Compress the velocities of {}",
             .examples = "",
             .answer = "",
             .settings = {{.name = "", .question = "by how much", .answer = "50%"},
                          window,
                          {.name = "centre",
                           .question = "towards",
                           .answer = "local",
                           .form = "local|average|N",
                           .values = {"local", "average"}},
                          threshold},
             .apply = &Workspace::velocityOverTime},
            {.name = "transform-velocity-expand",
             .aliases = "vel-expand",
             .usage = "[PERCENT] [window=1bar] [centre=local] [threshold=>N]",
             .description =
                 "Expand the velocities of the notes a range or object takes, pushing each a "
                 "percentage further from the average of those around it (50%): "
                 "sharpens accents; takes compress's options",
             .asks = Asks::InTurn,
             .label = "Expand the velocities of {}",
             .examples = "",
             .answer = "",
             .settings = {{.name = "", .question = "by how much", .answer = "50%"},
                          window,
                          {.name = "centre",
                           .question = "away from",
                           .answer = "local",
                           .form = "local|average|N",
                           .values = {"local", "average"}},
                          threshold},
             .apply = &Workspace::velocityOverTime},
            {.name = "transform-velocity-phrase",
             .aliases = "vel-phrase",
             .usage = "[PERCENT] [window=1bar]",
             .description =
                 "Scale the phrase dynamics of the notes a range or object takes: the contour of "
                 "their velocities' averages over a window (130% bigger swells, 70% flatter), each "
                 "note keeping its place on it; asking how much and the window",
             .asks = Asks::InTurn,
             .label = "Scale the phrase dynamics of {}",
             .examples = "",
             .answer = "",
             .settings = {{.name = "", .question = "by how much", .answer = "130%"}, window},
             .apply = &Workspace::velocityOverTime},
            {.name = "transform-chord-accent",
             .aliases = "chord-accent",
             .usage = "[AMOUNT]",
             .description = "Accent the top note of each chord a range or object takes, up by an "
                            "amount and the "
                            "others down by it (10), to bring out a melody",
             .asks = Asks::Once,
             .label = "Accent the top notes of the chords in {} by",
             .examples = "",
             .answer = "10",
             .apply = &Workspace::velocityInChords},
            {.name = "transform-chord-balance",
             .aliases = "chord-balance",
             .usage = "[BOTTOM-TOP]",
             .description =
                 "Balance each chord a range or object takes by pitch: a straight slope of "
                 "velocities from its lowest note to its highest (70-95), or "
                 "reversed to bring out the bass (95-70)",
             .asks = Asks::Once,
             .label = "Balance the chords in {} from bottom to top",
             .examples = "",
             .answer = "70-95",
             .apply = &Workspace::velocityInChords},
            {.name = "transform-chord-compress",
             .aliases = "chord-compress",
             .usage = "[PERCENT]",
             .description = "Compress each chord a range or object takes, pulling its velocities a "
                            "percentage of "
                            "the way together about their average (50%)",
             .asks = Asks::Once,
             .label = "Compress the chords in {}",
             .examples = "",
             .answer = "50%",
             .apply = &Workspace::velocityInChords},
            {.name = "transform-chord-expand",
             .aliases = "chord-expand",
             .usage = "[PERCENT]",
             .description =
                 "Expand each chord a range or object takes, pushing its velocities a percentage "
                 "further apart about their average (50%)",
             .asks = Asks::Once,
             .label = "Expand the chords in {}",
             .examples = "",
             .answer = "50%",
             .apply = &Workspace::velocityInChords},
            {.name = "transform-quantize",
             .aliases = "quantize quantise",
             .usage = "[GRID] [strength=100%] [swing=50%] [move=starts]",
             .description = "Quantise the notes a range or object takes, asking in turn for "
                            "the grid (the track's, at Enter), how far onto it, the swing and "
                            "what to move (starts, ends, both or lengths); Enter throughout "
                            "does what q does",
             .asks = Asks::InTurn,
             .label = "Quantise {}",
             .examples = "",
             .answer = "",
             .settings = {{.name = "",
                           .question = "to a grid of",
                           .answer = "{grid}",
                           .form = "1/16|1/8t|grid",
                           .values = {"grid"},
                           .lengths = true},
                          {.name = "strength",
                           .question = "how far onto it",
                           .answer = "100%",
                           .form = "0%-100%",
                           .values = {"100%", "75%", "50%", "25%"}},
                          {.name = "swing",
                           .question = "with a swing of",
                           .answer = "50%",
                           .form = "25%-75%, 50% straight",
                           .values = {"50%", "55%", "60%", "66%"}},
                          {.name = "move",
                           .question = "moving",
                           .answer = "starts",
                           .form = "starts|ends|both|lengths",
                           .values = {"starts", "ends", "both", "lengths"}}},
             .apply = &Workspace::quantizeAsked},
            {.name = "transform-stretch",
             .aliases = "stretch",
             .usage = "[RATIO]",
             .description = "Stretch the time of the events a range or object takes about the "
                            "first, asking by "
                            "how much: 2 for half time, 1/2 for double time, or a percentage "
                            "(150%); the notes alone in the piano roll",
             .asks = Asks::Once,
             .label = "Stretch {} by",
             .examples = "2 half time, 1/2 double time, 150%",
             .answer = "",
             .apply = &Workspace::applyStretch},
            {.name = "transform-length",
             .aliases = "length",
             .usage = "[LENGTH]",
             .description = "Set the lengths of the notes a range or object takes, asking how: "
                            "legato, each held "
                            "until the next but for the last, a length (1/8) or a "
                            "percentage of their own (80%)",
             .asks = Asks::Once,
             .label = "Set the lengths of {} to",
             .examples = "legato, 1/8, 80%",
             .answer = "",
             .apply = &Workspace::applyLength},
            {.name = "transform-legato",
             .aliases = "legato",
             .usage = "",
             .description =
                 "Make the notes a range or object takes legato, each note or chord held until the "
                 "next starts, but for the last, which keeps its length",
             .asks = Asks::Nothing,
             .label = "",
             .examples = "",
             .answer = "",
             .apply = &Workspace::applyLength},
            {.name = "transform-thin",
             .aliases = "thin",
             .usage = "[all|ccN|bend|aftertouch] [per=16] [step=2]",
             .description =
                 "Thin out dense controller, pitch bend and aftertouch data in a range or object, "
                 "asking which, the most to keep a quarter note (16) and the "
                 "smallest change kept (2); each gesture's first and last kept, "
                 "and never the pedals or other switches",
             .asks = Asks::InTurn,
             .label = "Thin the controllers, bends and aftertouch of {}",
             .examples = "",
             .answer = "",
             .settings = {{.name = "",
                           .question = "which",
                           .answer = "all",
                           .form = "all|cc1|bend|aftertouch",
                           .values = {"all", "bend", "aftertouch", "cc1", "cc2", "cc7", "cc10",
                                      "cc11"}},
                          {.name = "per",
                           .question = "keeping at most this many a quarter note",
                           .answer = "16",
                           .form = "1-96"},
                          {.name = "step",
                           .question = "and changes of at least",
                           .answer = "2",
                           .form = "0-64, in value"}},
             .apply = &Workspace::thinAsked},
            {.name = "transform-filter",
             .aliases = "filter",
             .usage = "[QUERY]",
             .description =
                 "Delete the events a range or object takes that match a query, as / writes them "
                 "(vel<20, cc7), asking which, the search's at Enter",
             .asks = Asks::Once,
             .label = "Delete the events of {} matching",
             .examples = "vel<20, cc7",
             .answer = "{search}",
             .apply = &Workspace::applyFilter},
            {.name = "transform-split",
             .aliases = "split",
             .usage = "[NOTE] [side=above] [to=TRACK]",
             .description =
                 "Split the notes a range or object takes at a note, moving it and those above, or "
                 "those below, to another track, asking at which note, which side "
                 "and which track; a new name makes a new track",
             .asks = Asks::InTurn,
             .label = "Split {}",
             .examples = "",
             .answer = "",
             .settings = {{.name = "", .question = "at", .answer = "C4", .form = "a note"},
                          {.name = "side",
                           .question = "moving",
                           .answer = "above",
                           .form = "above|below",
                           .values = {"above", "below"}},
                          {.name = "to",
                           .question = "to the track",
                           .answer = "{track}",
                           .form = "a track, or a new one's name",
                           .tracks = true}},
             .apply = &Workspace::splitAsked},
            {.name = "transform-remap",
             .aliases = "remap",
             .usage = "[ccN|NOTE] [to=ccN|NOTE]",
             .description =
                 "Remap a controller (cc1 to cc11) or a note (C1 to D1) in a range or object, "
                 "asking which and to what",
             .asks = Asks::InTurn,
             .label = "Remap {}",
             .examples = "",
             .answer = "",
             .settings = {{.name = "", .question = "changing", .answer = "", .form = "cc1|C1"},
                          {.name = "to", .question = "to", .answer = "", .form = "cc11|D1"}},
             .apply = &Workspace::remapAsked},
            {.name = "transform-pedal-repedal",
             .aliases = "repedal",
             .usage = "[lift=10ms] [down=60ms]",
             .description = "Re-pedal the range: its sustain replaced by a pedal down just after "
                            "its first chord, "
                            "lifted and pressed again around each chord change, and lifted as "
                            "its last notes end; asking how long before and after",
             .asks = Asks::InTurn,
             .label = "Re-pedal {} at its chord changes",
             .examples = "",
             .answer = "",
             .settings = {lift, press},
             .apply = &Workspace::pedalAsked},
            {.name = "transform-pedal-each",
             .aliases = "pedal-each",
             .usage = "[bar|beat] [lift=10ms] [down=60ms]",
             .description = "Pedal the range afresh on each bar or beat, its sustain replaced; "
                            "asking which, and how "
                            "long before and after each line",
             .asks = Asks::InTurn,
             .label = "Pedal {} afresh",
             .examples = "",
             .answer = "",
             .settings = {{.name = "",
                           .question = "on each",
                           .answer = "bar",
                           .form = "bar|beat",
                           .values = {"bar", "beat"}},
                          lift,
                          press},
             .apply = &Workspace::pedalAsked},
            {.name = "transform-pedal-retime",
             .aliases = "retime",
             .usage = "[lift=10ms] [down=60ms] [near=200ms]",
             .description = "Re-time the range's performed sustain: each lift and press near a "
                            "chord change moved to "
                            "just before and after it, the rest left as played",
             .asks = Asks::InTurn,
             .label = "Re-time the pedalling of {}",
             .examples = "",
             .answer = "",
             .settings = {lift,
                          press,
                          {.name = "near",
                           .question = "moving those within",
                           .answer = "200ms",
                           .form = "ms of a change"}},
             .apply = &Workspace::pedalAsked},
            {.name = "transform-pedal-clean",
             .aliases = "pedal-clean",
             .usage = "[flicker=30ms]",
             .description =
                 "Clean up the range's pedalling: exact repeats removed and flicker, a lift "
                 "and a press "
                 "again within a few milliseconds, merged; the first and last "
                 "events kept",
             .asks = Asks::InTurn,
             .label = "Clean up the pedalling of {}",
             .examples = "",
             .answer = "",
             .settings = {{.name = "flicker",
                           .question = "merging flicker within",
                           .answer = "30ms",
                           .form = "ms between a lift and a press"}},
             .apply = &Workspace::pedalAsked},
            {.name = "transform-pedal-clear",
             .aliases = "pedal-clear",
             .usage = "[sustain|sostenuto|soft|all]",
             .description = "Clear a pedal from the range, lifting it at the start if it's down "
                            "there and pressing it "
                            "again at the end if it was down there",
             .asks = Asks::InTurn,
             .label = "Clear the pedalling of {}",
             .examples = "",
             .answer = "",
             .settings = {{.name = "",
                           .question = "which pedal",
                           .answer = "sustain",
                           .form = "sustain|sostenuto|soft|all",
                           .values = {"sustain", "sostenuto", "soft", "all"}}},
             .apply = &Workspace::pedalAsked},
            {.name = "transform-pedal-offset",
             .aliases = "pedal-offset",
             .usage = "[MS]",
             .description = "Move every pedal change in the range later by some milliseconds, or "
                            "earlier (-20ms)",
             .asks = Asks::InTurn,
             .label = "Offset the pedalling of {}",
             .examples = "",
             .answer = "",
             .settings =
                 {{.name = "", .question = "by", .answer = "20ms", .form = "ms, -20ms earlier"}},
             .apply = &Workspace::pedalAsked},
            {.name = "transform-pedal-bake",
             .aliases = "pedal-bake",
             .usage = "",
             .description = "Bake the range's sustain into its notes: each note held by the "
                            "pedal lengthened to where it "
                            "lifts, and the sustain then cleared",
             .asks = Asks::Nothing,
             .label = "",
             .examples = "",
             .answer = "",
             .apply = &Workspace::pedalAsked},
        };
    }();
    return specs;
}

const Workspace::TransformSpec* Workspace::findTransform(std::string_view name)
{
    const auto specs = transformSpecs();
    const auto found = std::ranges::find(specs, name, &TransformSpec::name);
    return found != specs.end() ? &*found : nullptr;
}

std::span<const TransformSetting> transformSettings(std::string_view command)
{
    const auto* spec = Workspace::findTransform(command);
    return spec != nullptr ? std::span<const TransformSetting>{spec->settings}
                           : std::span<const TransformSetting>{};
}

} // namespace modal::ui
