// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/note_fraction.hpp>
#include <modal/core/pitch.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/metronome_view.hpp>
#include <modal/ui/ports.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <string>
#include <utility>

namespace modal::ui
{

namespace
{

using Setting = MetronomeView::Setting;
using Field = MetronomeView::Field;
using When = core::MetronomeSettings::When;
using CountInMode = core::CountInSettings::Mode;

constexpr int kLabelWidth = 14;
constexpr int kValueWidth = 22; // room for a port's alias, and "While recording"
constexpr int kNoteWidth = 6;   // a click's note, then its drum name, then its velocity
constexpr int kDrumWidth = 19;
constexpr int kVelocityWidth = 4;

// The General MIDI percussion names, which most drum sounds follow, from 35 to 81.
std::string_view drumName(std::uint8_t note)
{
    static constexpr std::array<std::string_view, 47> kNames{"Acoustic Bass Drum",
                                                             "Bass Drum 1",
                                                             "Side Stick",
                                                             "Acoustic Snare",
                                                             "Hand Clap",
                                                             "Electric Snare",
                                                             "Low Floor Tom",
                                                             "Closed Hi-Hat",
                                                             "High Floor Tom",
                                                             "Pedal Hi-Hat",
                                                             "Low Tom",
                                                             "Open Hi-Hat",
                                                             "Low-Mid Tom",
                                                             "Hi-Mid Tom",
                                                             "Crash Cymbal 1",
                                                             "High Tom",
                                                             "Ride Cymbal 1",
                                                             "Chinese Cymbal",
                                                             "Ride Bell",
                                                             "Tambourine",
                                                             "Splash Cymbal",
                                                             "Cowbell",
                                                             "Crash Cymbal 2",
                                                             "Vibraslap",
                                                             "Ride Cymbal 2",
                                                             "Hi Bongo",
                                                             "Low Bongo",
                                                             "Mute Hi Conga",
                                                             "Open Hi Conga",
                                                             "Low Conga",
                                                             "High Timbale",
                                                             "Low Timbale",
                                                             "High Agogo",
                                                             "Low Agogo",
                                                             "Cabasa",
                                                             "Maracas",
                                                             "Short Whistle",
                                                             "Long Whistle",
                                                             "Short Guiro",
                                                             "Long Guiro",
                                                             "Claves",
                                                             "Hi Wood Block",
                                                             "Low Wood Block",
                                                             "Mute Cuica",
                                                             "Open Cuica",
                                                             "Mute Triangle",
                                                             "Open Triangle"};
    return note >= 35 && note <= 81 ? kNames[note - 35] : std::string_view{};
}

std::string_view label(Setting setting)
{
    switch (setting)
    {
    case Setting::Plays:
        return "Plays";
    case Setting::Port:
        return "Port";
    case Setting::Channel:
        return "Channel";
    case Setting::Downbeat:
        return "Downbeat";
    case Setting::Beat:
        return "Beat";
    case Setting::Length:
        return "Length";
    case Setting::Mode:
        return "Before";
    case Setting::CountIn:
        return "Count-in";
    case Setting::PreRoll:
        return "Pre-roll";
    }
    return {};
}

std::string_view about(Setting setting, Field field)
{
    switch (setting)
    {
    case Setting::Plays:
        return "When the metronome clicks: off, while recording, or always";
    case Setting::Port:
        return "Where the click goes: Default for the default output, or an output by name";
    case Setting::Channel:
        return "The click's MIDI channel, 10 for General MIDI drums";
    case Setting::Downbeat:
        return field == Field::Value ? "The note clicked on each bar's first beat"
                                     : "How hard the first beat is clicked, 1 to 127";
    case Setting::Beat:
        return field == Field::Value ? "The note clicked on the other beats"
                                     : "How hard the other beats are clicked, 1 to 127";
    case Setting::Length:
        return "How long each click lasts, cut short where the next comes sooner";
    case Setting::Mode:
        return "Before recording: nothing, a count-in (clicks, the song silent), or a pre-roll "
               "(the song)";
    case Setting::CountIn:
        return "How long the count-in is: bars, or beats (3 beats)";
    case Setting::PreRoll:
        return "How many bars of the song a pre-roll plays";
    }
    return {};
}

bool isClick(Setting setting)
{
    return setting == Setting::Downbeat || setting == Setting::Beat;
}

std::string lower(std::string text)
{
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string trimmed(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t");
    return first == std::string::npos
               ? std::string{}
               : text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::optional<int> number(const std::string& text)
{
    int value = 0;
    const auto [end, problem] = std::from_chars(text.data(), text.data() + text.size(), value);
    return problem == std::errc{} && end == text.data() + text.size() ? std::optional{value}
                                                                      : std::nullopt;
}

std::string whenWord(When when)
{
    auto text = whenText(when);
    text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

std::string modeWord(CountInMode mode)
{
    return mode == CountInMode::None ? "Nothing" : mode == CountInMode::CountIn ? "Count-in" : "Pre-roll";
}

std::string count(int n, std::string_view one)
{
    return std::to_string(n) + " " + std::string{one} + (n == 1 ? "" : "s");
}

// The lengths a click steps through.
constexpr std::array<core::NoteFraction, 5> kLengths{
    core::NoteFraction{1, 64}, core::NoteFraction{1, 32}, core::NoteFraction{1, 16},
    core::NoteFraction{1, 8}, core::NoteFraction{1, 4}};

} // namespace

MetronomeView::MetronomeView(core::Document& document, ViewHost& host)
    : document_(document)
    , host_(host)
{
}

std::vector<int> MetronomeView::topHeights(CellSize cell) const
{
    return {cell.height, inspectorHeight(cell)};
}

void MetronomeView::resize(const WindowLayout& layout, int width, CellSize cell)
{
    layout_ = layout;
    width_ = width;
    cell_ = cell;
}

void MetronomeView::showSong()
{
    songCursor_ = {};
    editor_.close();
}

const core::MeterMap& MetronomeView::cursorMeter() const
{
    return songCursor_.track ? document_.song().meter(*songCursor_.track)
                             : document_.song().meter();
}

std::string MetronomeView::position(core::Tick tick) const
{
    return positionText(cursorMeter(), tick);
}

core::EditPlace MetronomeView::place() const
{
    return {.track = songCursor_.track, .tick = songCursor_.tick};
}

void MetronomeView::restore(const core::EditPlace& place)
{
    songCursor_.track = place.track;
    songCursor_.tick = place.tick;
}

void MetronomeView::go(Setting setting)
{
    row_ = static_cast<std::size_t>(std::ranges::find(kSettings, setting) - kSettings.begin());
    field_ = Field::Value;
}

std::vector<std::string> MetronomeView::ports() const
{
    std::vector<std::string> result{""}; // the default
    if (const auto* table = host_.portTable())
    {
        for (auto& name : table->offered(PortDirection::Output))
            result.push_back(std::move(name));
    }
    else
    {
        for (auto& name : host_.outputPorts())
            result.push_back(std::move(name));
    }
    return result;
}

std::string MetronomeView::text(Setting setting, Field field) const
{
    const auto& song = document_.song();
    const auto& metronome = song.metronome;
    const auto& countIn = song.countIn;
    switch (setting)
    {
    case Setting::Plays:
        return whenWord(metronome.when);
    case Setting::Port:
        return metronome.port.empty() ? "Default" : metronome.port;
    case Setting::Channel:
        return std::to_string(metronome.channel);
    case Setting::Downbeat:
    case Setting::Beat:
    {
        const bool down = setting == Setting::Downbeat;
        return field == Field::Value
                   ? core::pitchName(down ? metronome.downbeatNote : metronome.beatNote)
                   : std::to_string(down ? metronome.downbeatVelocity : metronome.beatVelocity);
    }
    case Setting::Length:
        return core::toString(metronome.length);
    case Setting::Mode:
        return modeWord(countIn.mode);
    case Setting::CountIn:
        return countIn.beats > 0 ? count(countIn.beats, "beat") : count(countIn.bars, "bar");
    case Setting::PreRoll:
        return count(countIn.preRollBars, "bar");
    }
    return {};
}

void MetronomeView::change(const std::string& what, const core::Song& changed)
{
    const auto& now = document_.song();
    if (changed.metronome == now.metronome && changed.countIn == now.countIn)
    {
        host_.setMessage(what, MessageKind::Information);
        return;
    }
    document_.edit(what, place(),
                   [&](core::Song& song)
                   {
                       song.metronome = changed.metronome;
                       song.countIn = changed.countIn;
                   });
    host_.setMessage(what, MessageKind::Information);
    // Changing a click's sound plays it, to hear what it's become.
    if (isClick(setting()))
        host_.auditionClick(setting() == Setting::Downbeat);
}

std::optional<std::string> MetronomeView::set(const std::string& typed)
{
    auto song = document_.song();
    auto& metronome = song.metronome;
    auto& countIn = song.countIn;
    const auto text = trimmed(typed);
    const auto word = lower(text);
    const auto startsWith = [&](std::string_view whole)
    { return !word.empty() && lower(std::string{whole}).starts_with(word); };
    switch (setting())
    {
    case Setting::Plays:
        if (startsWith("off"))
            metronome.when = When::Off;
        else if (startsWith("while recording") || startsWith("recording"))
            metronome.when = When::Recording;
        else if (startsWith("always"))
            metronome.when = When::Always;
        else
            return "The metronome plays off, while recording, or always";
        change("Metronome " + whenText(metronome.when), song);
        return std::nullopt;
    case Setting::Port:
    {
        if (word.empty() || startsWith("default"))
        {
            metronome.port.clear();
        }
        else
        {
            // An enabled output by name, or the start of one.
            const auto all = ports();
            const auto match =
                std::ranges::find_if(all, [&](const std::string& name)
                                     { return !name.empty() && lower(name).starts_with(word); });
            if (match == all.end())
                return "No enabled output is called " + text;
            metronome.port = *match;
        }
        change("Click port: " + (metronome.port.empty() ? "Default" : metronome.port), song);
        return std::nullopt;
    }
    case Setting::Channel:
    {
        const auto channel = number(text);
        if (!channel || *channel < 1 || *channel > 16)
            return "The channel is from 1 to 16";
        metronome.channel = *channel;
        change("Click channel " + text, song);
        return std::nullopt;
    }
    case Setting::Downbeat:
    case Setting::Beat:
    {
        const bool down = setting() == Setting::Downbeat;
        if (field_ == Field::Value)
        {
            // A note by name, or its number.
            auto note = core::parsePitch(text);
            if (const auto n = number(text); !note && n && *n >= 0 && *n <= 127)
                note = static_cast<std::uint8_t>(*n);
            if (!note)
                return "A note by name (E5) or number (76)";
            (down ? metronome.downbeatNote : metronome.beatNote) = *note;
            change(std::string{down ? "Downbeat " : "Beat "} + core::pitchName(*note), song);
        }
        else
        {
            const auto velocity = number(text);
            if (!velocity || *velocity < 1 || *velocity > 127)
                return "The velocity is from 1 to 127";
            (down ? metronome.downbeatVelocity : metronome.beatVelocity) =
                static_cast<std::uint8_t>(*velocity);
            change(std::string{down ? "Downbeat velocity " : "Beat velocity "} + text, song);
        }
        return std::nullopt;
    }
    case Setting::Length:
    {
        const auto length = core::parseNoteFraction(text);
        if (!length)
            return "A note length, as 1/32";
        metronome.length = *length;
        change("Click length " + core::toString(*length), song);
        return std::nullopt;
    }
    case Setting::Mode:
        if (startsWith("nothing") || startsWith("none") || startsWith("off"))
            countIn.mode = CountInMode::None;
        else if (startsWith("count-in") || startsWith("countin"))
            countIn.mode = CountInMode::CountIn;
        else if (startsWith("pre-roll") || startsWith("preroll"))
            countIn.mode = CountInMode::PreRoll;
        else
            return "Before recording comes nothing, a count-in or a pre-roll";
        change(countInText(countIn), song);
        return std::nullopt;
    case Setting::CountIn:
    case Setting::PreRoll:
    {
        // A number of bars, or for the count-in "3 beats".
        const auto space = text.find(' ');
        const auto n = number(text.substr(0, space));
        const auto unit =
            space == std::string::npos ? std::string{} : lower(trimmed(text.substr(space)));
        const bool beats = unit == "beats" || unit == "beat";
        const bool bars = unit.empty() || unit == "bars" || unit == "bar";
        const bool preRoll = setting() == Setting::PreRoll;
        if (!n || (!bars && (!beats || preRoll)))
            return preRoll ? "A number of bars" : "A number of bars, or of beats (3 beats)";
        if (*n < 1 || *n > 99)
            return "From 1 to 99";
        if (preRoll)
            countIn.preRollBars = *n;
        else
        {
            countIn.beats = beats ? *n : 0;
            countIn.bars = beats ? countIn.bars : *n;
        }
        change(std::string{preRoll ? "Pre-roll " : "Count-in "} +
                   (beats ? count(*n, "beat") : count(*n, "bar")),
               song);
        return std::nullopt;
    }
    }
    return std::nullopt;
}

std::optional<std::string> MetronomeView::step(int steps)
{
    auto song = document_.song();
    auto& metronome = song.metronome;
    auto& countIn = song.countIn;
    const auto cycle = [&](int at, int size) { return ((at + steps) % size + size) % size; };
    const auto clamp = [&](int value, int low, int high)
    { return std::clamp(value + steps, low, high); };
    switch (setting())
    {
    case Setting::Plays:
        metronome.when = static_cast<When>(cycle(static_cast<int>(metronome.when), 3));
        change("Metronome " + whenText(metronome.when), song);
        break;
    case Setting::Port:
    {
        const auto all = ports();
        const auto at = std::ranges::find(all, metronome.port);
        const int index = at == all.end() ? 0 : static_cast<int>(at - all.begin());
        metronome.port = all[static_cast<std::size_t>(cycle(index, static_cast<int>(all.size())))];
        change("Click port: " + (metronome.port.empty() ? "Default" : metronome.port), song);
        break;
    }
    case Setting::Channel:
        metronome.channel = clamp(metronome.channel, 1, 16);
        change("Click channel " + std::to_string(metronome.channel), song);
        break;
    case Setting::Downbeat:
    case Setting::Beat:
    {
        const bool down = setting() == Setting::Downbeat;
        if (field_ == Field::Value)
        {
            auto& note = down ? metronome.downbeatNote : metronome.beatNote;
            note = static_cast<std::uint8_t>(clamp(note, 0, 127));
            change(std::string{down ? "Downbeat " : "Beat "} + core::pitchName(note), song);
        }
        else
        {
            auto& velocity = down ? metronome.downbeatVelocity : metronome.beatVelocity;
            velocity = static_cast<std::uint8_t>(clamp(velocity, 1, 127));
            change(std::string{down ? "Downbeat velocity " : "Beat velocity "} +
                       std::to_string(velocity),
                   song);
        }
        break;
    }
    case Setting::Length:
    {
        const auto at = std::ranges::find(kLengths, metronome.length);
        const int index = at == kLengths.end() ? 1 : static_cast<int>(at - kLengths.begin());
        // Up makes it longer.
        metronome.length = kLengths[static_cast<std::size_t>(std::clamp(index + steps, 0, 4))];
        change("Click length " + core::toString(metronome.length), song);
        break;
    }
    case Setting::Mode:
        countIn.mode = static_cast<CountInMode>(cycle(static_cast<int>(countIn.mode), 3));
        change(countInText(countIn), song);
        break;
    case Setting::CountIn:
        if (countIn.beats > 0)
            countIn.beats = clamp(countIn.beats, 1, 99);
        else
            countIn.bars = clamp(countIn.bars, 1, 99);
        change("Count-in " + text(Setting::CountIn, Field::Value), song);
        break;
    case Setting::PreRoll:
        countIn.preRollBars = clamp(countIn.preRollBars, 1, 99);
        change("Pre-roll " + count(countIn.preRollBars, "bar"), song);
        break;
    }
    return std::nullopt;
}

std::optional<bool> MetronomeView::run(std::string_view command, int count)
{
    const auto n = static_cast<std::size_t>(std::max(count, 1));
    const auto last = kSettings.size() - 1;
    const auto settle = [&]
    {
        if (!isClick(setting()))
            field_ = Field::Value;
    };
    if (command == "cursor-down")
    {
        row_ = std::min(row_ + n, last);
        settle();
    }
    else if (command == "cursor-up")
    {
        row_ -= std::min(row_, n);
        settle();
    }
    else if (command == "cursor-left" || command == "cursor-first")
    {
        field_ = Field::Value;
    }
    else if (command == "cursor-right" || command == "cursor-last")
    {
        field_ = isClick(setting()) ? Field::Velocity : Field::Value;
    }
    else if (command == "go-first")
    {
        row_ = 0;
        settle();
    }
    else if (command == "go-last")
    {
        row_ = last;
        settle();
    }
    else if (command == "field-edit" || command == "inspector-edit")
    {
        const auto shown = text(setting(), field_);
        if (editor_.open({.kind = "Metronome",
                          .fields = {{.heading = std::string{label(setting())},
                                      .value = shown,
                                      .editable = true,
                                      .editText = shown}}}))
        {
            host_.startTyping();
        }
    }
    else
    {
        return std::nullopt;
    }
    return true;
}

bool MetronomeView::operate(std::string_view, std::string_view, int)
{
    host_.refuse("There's nothing on the metronome page to operate on");
    return false;
}

std::optional<std::string> MetronomeView::typingHint() const
{
    if (!editor_.active())
        return std::nullopt;
    return std::string{about(setting(), field_)} + "; Up and Down step it, Enter sets it";
}

void MetronomeView::type(char32_t character)
{
    editor_.type(character);
}

std::optional<bool> MetronomeView::runTyping(std::string_view command)
{
    return editor_.run(
        command, host_,
        {
            .inspection =
                [this]
            {
                const auto shown = text(setting(), field_);
                return Inspection{.kind = "Metronome",
                                  .fields = {{.heading = std::string{label(setting())},
                                              .value = shown,
                                              .editable = true,
                                              .editText = shown}}};
            },
            .set = [this](const std::string&, const std::string& typed) { return set(typed); },
            .step = [this](const std::string&, int steps) { return step(steps); },
        });
}

Inspection MetronomeView::inspection() const
{
    Inspection result{.kind = setting() >= Setting::Mode ? "Count-in" : "Metronome", .fields = {}};
    result.fields.push_back({"About", std::string{about(setting(), field_)}});
    if (setting() == Setting::Port)
    {
        const auto* table = host_.portTable();
        const auto& port = document_.song().metronome.port;
        if (table != nullptr && !table->resolve(PortDirection::Output, port).port)
        {
            InspectorField nowhere{port.empty() ? "No default output" : "Port not usable",
                                   port.empty() ? "Choose one in the Ports view (gm)"
                                                : "Enable it in the Ports view (gm)"};
            nowhere.warning = true;
            result.fields.push_back(std::move(nowhere));
        }
    }
    return result;
}

void MetronomeView::addRegions(Frame& frame) const
{
    Region page{.bounds = layout_.main,
                .background = Role::Background,
                .text = {},
                .under = {},
                .over = {}};
    const Style plain{Role::Text, Role::Background, kPlain};
    const Style dim{Role::Dim, Role::Background, kPlain};
    const Style cursor{Role::CursorText, Role::Cursor, kPlain};
    int y = 0;
    const auto heading = [&](std::u32string text)
    {
        page.text.push_back(
            {cell_.width, y, std::move(text), {Role::Accent, Role::Background, kBold}});
        y += 2 * cell_.height;
    };
    // A field, padded to its width, the cursor's highlighted, or being edited in place.
    const auto field = [&](Setting setting, Field which, int x, int width, bool numeric)
    {
        const bool here = setting == this->setting() && which == field_;
        if (here && editor_.active())
        {
            inspectorValue(page, {std::string{label(setting)}, {}}, {}, x, y, width, cell_,
                           &editor_);
            return;
        }
        auto shown = decodeUtf8(text(setting, which));
        if (shown.size() > static_cast<std::size_t>(width))
        {
            shown.resize(static_cast<std::size_t>(width - 1));
            shown += U'…';
        }
        const auto padding = static_cast<std::size_t>(width) - shown.size();
        if (numeric)
            shown.insert(0, padding, U' ');
        else
            shown.append(padding, U' ');
        // Typed into, so shown as fields; the choosers are stepped.
        const bool typed = setting != Setting::Plays && setting != Setting::Mode;
        if (typed && !here)
            addField(page, x, y, width, cell_);
        Style style = plain;
        if (setting == Setting::Port)
        {
            const auto* table = host_.portTable();
            if (table != nullptr &&
                !table->resolve(PortDirection::Output, document_.song().metronome.port).port)
                style.foreground = Role::Warning;
        }
        page.text.push_back({x, y, std::move(shown), here ? cursor : style});
    };
    const auto& song = document_.song();
    const int valueX = (1 + kLabelWidth) * cell_.width;
    for (std::size_t i = 0; i < kSettings.size(); ++i)
    {
        const auto setting = kSettings[i];
        if (setting == Setting::Plays)
            heading(U"Metronome");
        if (setting == Setting::Mode)
        {
            y += cell_.height; // two blank lines between the groups
            heading(U"Before recording");
        }
        page.text.push_back({2 * cell_.width, y, decodeUtf8(label(setting)), dim});
        if (isClick(setting))
        {
            const bool down = setting == Setting::Downbeat;
            field(setting, Field::Value, valueX, kNoteWidth - 1, false);
            const auto note = down ? song.metronome.downbeatNote : song.metronome.beatNote;
            page.text.push_back(
                {valueX + kNoteWidth * cell_.width, y, decodeUtf8(drumName(note)), dim});
            field(setting, Field::Velocity, valueX + (kNoteWidth + kDrumWidth) * cell_.width,
                  kVelocityWidth - 1, true);
        }
        else
        {
            const int width = setting == Setting::Channel   ? 2
                              : setting == Setting::Length  ? 5
                              : setting == Setting::CountIn ? 9
                              : setting == Setting::PreRoll ? 9
                                                            : kValueWidth - 1;
            field(setting, Field::Value, valueX, width, setting == Setting::Channel);
        }
        y += cell_.height;
        if (setting == Setting::Length)
            y += cell_.height;
    }
    frame.push_back(std::move(page));
    if (layout_.top.size() < 2)
    {
        return;
    }
    // The bar: what plays, and what comes before recording.
    std::vector<BarPair> left{{"Metronome", {}}};
    std::vector<BarPair> right{{"Plays:", whenText(song.metronome.when)},
                               {"Before recording:", countInText(song.countIn)}};
    frame.push_back(labelledBar(left, right, layout_.top[0], width_, cell_));
    frame.push_back(inspectorRegion(inspection(), layout_.top[1], width_, cell_, 9, nullptr));
}

} // namespace modal::ui
