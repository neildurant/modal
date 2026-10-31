// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/arrange_view.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/time_motions.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string>
#include <variant>

namespace modal::ui
{

namespace
{

constexpr std::array kCommands{
    CommandInfo{.name = "go-section-strip", .description = "Go to the section strip"},
    CommandInfo{.name = "register-put-before",
                .description = "Put the yanked or deleted sections before this one"},
    CommandInfo{.name = "move-left",
                .description = "Move this section or note earlier, {count} places or steps",
                .count = true},
    CommandInfo{.name = "move-right",
                .description = "Move this section or note later, {count} places or steps",
                .count = true},
    CommandInfo{.name = "join", .description = "Join this section with the next"},
    CommandInfo{.name = "insert-before",
                .description = "Insert a new section before this one, of {count} bars (4)",
                .count = true},
    CommandInfo{.name = "insert-after",
                .description = "Insert a new section after this one, of {count} bars (4)",
                .count = true},
    CommandInfo{.name = "change", .description = "Rename this section"},
    CommandInfo{.name = "zoom-in", .description = "Zoom in, by {count} steps", .count = true},
    CommandInfo{.name = "zoom-out", .description = "Zoom out, by {count} steps", .count = true},
    CommandInfo{.name = "zoom-reset", .description = "Go back to the zoom the view starts at"},
};

std::string signatureText(core::TimeSignature signature)
{
    return std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
}

} // namespace

std::span<const CommandInfo> ArrangeView::commands()
{
    return kCommands;
}

ArrangeView::ArrangeView(core::Document& document, ViewHost& host)
    : document_(document)
    , host_(host)
    , page_(document.song())
{
}

std::vector<int> ArrangeView::topHeights(CellSize cell) const
{
    return {cell.height, inspectorHeight(cell)};
}

void ArrangeView::resize(const WindowLayout& layout, int width, CellSize cell)
{
    layout_ = layout;
    width_ = width;
    cell_ = cell;
    page_.resize(layout.main, cell);
}

const core::MeterMap& ArrangeView::meter() const
{
    const auto id = trackId();
    return id ? document_.song().meter(*id) : document_.song().meter();
}

std::optional<core::TrackId> ArrangeView::trackId() const
{
    const auto tracks = document_.song().tracks();
    if (tracks.empty())
    {
        return std::nullopt;
    }
    return tracks[std::min(page_.track(), tracks.size() - 1)]->id();
}

std::optional<bool> ArrangeView::run(std::string_view command, int count)
{
    if (const auto section = page_.stripCursor())
    {
        return runOnStrip(*section, command, count);
    }
    if (command == "go-section-strip" ||
        ((command == "cursor-up" || command == "track-previous") && page_.track() == 0))
    {
        enterStrip();
        return true;
    }
    const int n = std::max(count, 1);
    const auto& song = document_.song();
    const auto moveTo = [&](core::Tick tick) { page_.setCursor(page_.track(), tick); };
    if (command == "cursor-left" || command == "cursor-right" || command == "cursor-bar-next" ||
        command == "cursor-bar-previous" || command == "cursor-bar-line-next" ||
        command == "cursor-bar-line-previous")
    {
        const bool back = command.ends_with("left") || command.ends_with("previous");
        page_.moveBars(back ? -n : n);
    }
    else if (command == "cursor-down" || command == "cursor-up" || command == "track-next" ||
             command == "track-previous")
    {
        const bool up = command == "cursor-up" || command == "track-previous";
        page_.moveTracks(up ? -n : n);
    }
    else if (command == "cursor-left-scroll" || command == "cursor-right-scroll" ||
             command == "cursor-bar-previous-scroll" || command == "cursor-bar-next-scroll")
    {
        const bool back =
            command == "cursor-left-scroll" || command == "cursor-bar-previous-scroll";
        page_.scrollBars(back ? -n : n);
    }
    else if (command == "scroll-page-left" || command == "scroll-page-right")
    {
        page_.scrollBars(page_.barsInView() * (command == "scroll-page-left" ? -n : n));
    }
    else if (command == "cursor-down-scroll" || command == "cursor-up-scroll")
    {
        page_.scrollTracks(command == "cursor-down-scroll" ? n : -n);
    }
    else if (command.starts_with("scroll-half-") || command.starts_with("scroll-page-"))
    {
        const std::int64_t rows = page_.viewTracks();
        const auto page = std::max(command.starts_with("scroll-half-") ? rows / 2 : rows - 2,
                                   std::int64_t{1}); // as in Vim
        page_.scrollTracks(n * page * (command.ends_with("down") ? 1 : -1));
    }
    else if (command == "cursor-first" || command == "go-first")
    {
        page_.goToBar(count > 0 && command == "go-first" ? count : 1);
    }
    else if (command == "cursor-last" || command == "go-last")
    {
        if (count > 0 && command == "go-last")
        {
            page_.goToBar(count);
        }
        else
        {
            page_.goToLastBar();
        }
    }
    else if (command == "scroll-centre")
    {
        page_.centre();
    }
    else if (command == "zoom-in" || command == "zoom-out")
    {
        page_.zoomBy(command == "zoom-in" ? -n : n);
    }
    else if (command == "zoom-reset")
    {
        page_.resetZoom();
    }
    else if (command.starts_with("cursor-section-") || command.starts_with("cursor-marker-"))
    {
        std::vector<core::Tick> ticks;
        if (command.starts_with("cursor-section-"))
        {
            for (std::size_t i = 0; i < song.sections().size(); ++i)
            {
                ticks.push_back(song.sectionStart(i));
            }
        }
        else
        {
            for (const auto& marker : song.markers())
            {
                ticks.push_back(marker.tick);
            }
        }
        const auto tick = page_.time();
        const auto to =
            command.ends_with("-next") ? nextOf(ticks, tick, n) : previousOf(ticks, tick, n);
        if (!to)
        {
            return false;
        }
        moveTo(*to);
    }
    else if (command == "cursor-note-next" || command == "cursor-note-previous")
    {
        return toBarWithEvents(command == "cursor-note-next" ? n : -n);
    }
    else if (command == "register-put" || command == "register-put-before")
    {
        return put(command == "register-put-before");
    }
    else if (command == "event-delete")
    {
        return operate("operator-delete", "object-note", count);
    }
    else if (command == "inspector-edit")
    {
        host_.setMessage("Nothing to edit in a bar here; e edits a note in the piano roll, or a "
                         "section on the section strip",
                         MessageKind::Information);
    }
    else if (command == "field-edit")
    {
        // Enter opens the bar in the piano roll, the cursor at its start.
        const auto& m = meter();
        page_.setCursor(page_.track(), m.barStart(m.toBarBeatTick(page_.time()).bar));
        host_.open(ViewKind::PianoRoll);
    }
    else
    {
        return std::nullopt;
    }
    return true;
}

bool ArrangeView::operate(std::string_view op, std::string_view motion, int count)
{
    if (const auto section = page_.stripCursor())
    {
        return operateOnStrip(*section, op, motion, count);
    }
    if (document_.song().tracks().empty())
    {
        return false;
    }
    if (motion == "object-note" || motion == "object-chord")
    {
        host_.setMessage(std::string{"There's no single "} +
                             (motion == "object-note" ? "note" : "chord") +
                             " here; dd takes this bar's events",
                         MessageKind::Information);
        return true;
    }
    if (motion == "visual-time")
    {
        return deleteTime();
    }
    const auto area = areaFor(motion, count);
    return area && operateOn(op, *area, motion == "object-bar", sectionWhat(*area, motion, count));
}

bool ArrangeView::deleteTime()
{
    // D on some tracks: their time out of each section the selection crosses, each part
    // closing up within its own section, silence gathering at each one's end, since the other
    // tracks still fill it (decided October 2026).
    const auto area = selectionArea();
    if (!area)
    {
        return false;
    }
    const auto what = *selectionStatus();
    EventRegister taken;
    taken.tracks.resize(area->bottom - area->top + 1);
    taken.span = area->to - area->from;
    taken.bars = true;
    edit("Delete " + what + ", closing the gap",
         [&](core::Song& s)
         {
             // Rippling stays in its section, so each part leaves the others' times alone.
             const auto last = s.sectionAt(std::max<core::Tick>(area->to - 1, 0));
             for (auto i = s.sectionAt(area->from); i <= last; ++i)
             {
                 const auto from = std::max(area->from, s.sectionStart(i));
                 const auto to = std::min(area->to, s.sectionStart(i + 1));
                 if (from >= to)
                 {
                     continue;
                 }
                 auto part = rippleDelete(s, area->top, area->bottom, from, to);
                 for (std::size_t k = 0; k < part.tracks.size() && k < taken.tracks.size(); ++k)
                 {
                     for (auto event : part.tracks[k])
                     {
                         event.tick += from - area->from;
                         taken.tracks[k].push_back(std::move(event));
                     }
                 }
             }
         });
    host_.eventRegister() = std::move(taken);
    page_.setCursor(area->top, area->from);
    return true;
}

std::optional<std::pair<core::Tick, core::Tick>> ArrangeView::selectionSpan() const
{
    const auto area = selectionArea();
    return area ? std::optional{std::pair{area->from, area->to}} : std::nullopt;
}

std::optional<OperatorTarget> ArrangeView::target(std::string_view motion, int count)
{
    // The events starting in the area, controllers too, on each of its tracks; on the strip,
    // is takes the section's on every track, and as is is to these operators.
    const auto& song = document_.song();
    if (song.tracks().empty())
    {
        return std::nullopt;
    }
    if (const auto section = page_.stripCursor())
    {
        if (motion == "visual")
        {
            const auto area = selectionArea();
            return area ? targetIn(*area, *selectionStatus() + " on every track") : std::nullopt;
        }
        if (!isSectionObject(motion))
        {
            host_.setMessage("On the section strip, operators take is, this section's events on "
                             "every track",
                             MessageKind::Information);
            return std::nullopt;
        }
        auto [area, what] = stripContents(*section, count);
        return targetIn(area, std::move(what));
    }
    if (motion == "object-note" || motion == "object-chord")
    {
        host_.setMessage(std::string{"There's no single "} +
                             (motion == "object-note" ? "note" : "chord") +
                             " here; ib takes this bar's events",
                         MessageKind::Information);
        return std::nullopt;
    }
    const auto area = areaFor(motion, count);
    if (!area)
    {
        return std::nullopt;
    }
    auto what = sectionWhat(*area, motion, count);
    return targetIn(*area, what.empty() ? describe(*area) : std::move(what));
}

std::optional<OperatorTarget> ArrangeView::targetIn(const Area& area, std::string what) const
{
    const auto& song = document_.song();
    OperatorTarget result{.parts = {}, .what = std::move(what), .notesOnly = false};
    bool any = false;
    for (auto i = area.top; i <= area.bottom; ++i)
    {
        const auto id = song.tracks()[i]->id();
        const auto& events = song.events(id);
        const auto inside = events.startingIn(area.from, area.to);
        auto& part = result.parts.emplace_back(OperatorTarget::Part{id, {}});
        const auto first = static_cast<std::size_t>(inside.data() - events.all().data());
        for (std::size_t k = 0; k < inside.size(); ++k)
        {
            part.events.push_back(first + k);
        }
        any = any || !inside.empty();
    }
    if (!any)
    {
        host_.setMessage("Nothing there in " + result.what, MessageKind::Information);
        return std::nullopt;
    }
    return result;
}

std::pair<ArrangeView::Area, std::string> ArrangeView::stripContents(std::size_t section,
                                                                     int count) const
{
    const auto& song = document_.song();
    const auto range = sectionsFrom(song, song.sectionStart(section), count);
    return {Area{0, song.tracks().size() - 1, range.from, range.to},
            sectionsText(song, range) + " on every track"};
}

std::string ArrangeView::sectionWhat(const Area& area, std::string_view motion, int count) const
{
    if (motion == "visual")
    {
        return selectionStatus().value_or("");
    }
    // is on a track, "Verse of Bass", and it, "all of Bass", rather than their bars.
    if (motion == "object-track")
    {
        return "all of " + tracksText(area);
    }
    if (motion == "object-song")
    {
        return "the whole song";
    }
    if (!isSectionObject(motion))
    {
        return {};
    }
    const auto& song = document_.song();
    return sectionsText(song, sectionsFrom(song, area.from, count)) + " of " + tracksText(area);
}

std::optional<ArrangeView::Area> ArrangeView::selectionArea() const
{
    // v: the bars from where it started to the cursor's, both in, in the cursor's track's
    // bars, on every track.
    const auto selection = host_.selection();
    const auto& song = document_.song();
    if (const auto sections = selectedSections(); sections && !song.tracks().empty())
    {
        return Area{0, song.tracks().size() - 1, song.sectionStart(sections->first),
                    song.sectionStart(sections->second + 1)};
    }
    if (!selection || selection->where != Where::Tracks || song.tracks().empty())
    {
        return std::nullopt;
    }
    if (selection->kind == SelectionKind::Rows)
    {
        // V: the tracks from where it started to the cursor's, both in, at every time, or
        // the time an object set.
        const auto tracks = song.tracks();
        const auto at = std::ranges::find(tracks, selection->anchor.track,
                                          [](const auto& track) { return track->id(); });
        const auto anchor =
            at == tracks.end() ? page_.track() : static_cast<std::size_t>(at - tracks.begin());
        const auto [from, to] = selection->span.value_or(std::pair{core::Tick{0}, song.end()});
        return Area{std::min(anchor, page_.track()), std::max(anchor, page_.track()), from, to};
    }
    const auto& m = meter();
    const auto first = m.toBarBeatTick(std::min(selection->anchor.tick, page_.time())).bar;
    const auto last = m.toBarBeatTick(std::max(selection->anchor.tick, page_.time())).bar;
    return Area{0, song.tracks().size() - 1, m.barStart(first),
                std::min(m.barStart(last + 1), song.end())};
}

std::optional<std::pair<std::size_t, std::size_t>> ArrangeView::selectedSections() const
{
    // v on the strip: the sections from where it started to the cursor's, both in.
    const auto selection = host_.selection();
    const auto here = page_.stripCursor();
    if (!selection || selection->where != Where::Strip || !here)
    {
        return std::nullopt;
    }
    const auto& song = document_.song();
    const auto anchor = std::min(selection->index.value_or(song.sectionAt(selection->anchor.tick)),
                                 song.sections().size() - 1);
    return std::pair{std::min(anchor, *here), std::max(anchor, *here)};
}

std::optional<std::string> ArrangeView::selectionStatus() const
{
    if (const auto sections = selectedSections())
    {
        const auto& song = document_.song();
        return sectionsText(song, SectionRange{.first = sections->first,
                                               .count = sections->second - sections->first + 1,
                                               .from = 0,
                                               .to = 0});
    }
    const auto area = selectionArea();
    if (!area)
    {
        return std::nullopt;
    }
    if (host_.selection()->kind == SelectionKind::Rows)
    {
        return describe(*area);
    }
    return barsText(meter(), area->from, area->to) + " on every track";
}

std::optional<std::pair<core::Tick, core::Tick>> ArrangeView::objectSpan(std::string_view object,
                                                                         int count)
{
    if (page_.stripCursor())
    {
        host_.setMessage("On the strip, h and l select sections", MessageKind::Information);
        return std::nullopt;
    }
    if (object == "object-note" || object == "object-chord")
    {
        host_.setMessage("There's no single note or chord here to select",
                         MessageKind::Information);
        return std::nullopt;
    }
    const auto area = areaFor(object, count);
    if (!area)
    {
        return std::nullopt;
    }
    return std::pair{area->from, area->to};
}

std::optional<ArrangeView::Area> ArrangeView::areaFor(std::string_view motion, int count)
{
    // The cursor's bar, and its end, cut short by the song's.
    const auto& m = meter();
    const auto track = page_.track();
    const auto time = page_.time();
    const auto bar = m.toBarBeatTick(time).bar;
    const auto end = [&](std::int64_t b)
    { return std::min(m.barStart(b + 1), document_.song().end()); };
    Area area{track, track, m.barStart(bar), end(bar)};
    if (motion == "visual")
    {
        return selectionArea();
    }
    if (motion == "object-song")
    {
        // ie: every track, from the start to the end.
        return Area{0, document_.song().tracks().size() - 1, 0, document_.song().end()};
    }
    if (motion == "object-track")
    {
        // it: the whole track, and as many after it as the count says less one.
        const auto last = document_.song().tracks().size() - 1;
        area.bottom = std::min(track + static_cast<std::size_t>(std::max(count, 1)) - 1, last);
        area.from = 0;
        area.to = document_.song().end();
        return area;
    }
    if (isSectionObject(motion))
    {
        // is: the cursor's section on its track, and as many after it as the count says less
        // one.
        const auto range = sectionsFrom(document_.song(), time, count);
        area.from = range.from;
        area.to = range.to;
        return area;
    }
    if (motion == "operator-line" || motion == "object-bar-contents" || motion == "object-bar")
    {
        // dd, ib, ab: the cursor's bar, and as many after it as the count says less one.
        area.to = end(bar + std::max(count, 1) - 1);
        return area;
    }

    // Where the motion goes, without moving the cursor or the view.
    const auto top = page_.topTrack();
    const auto left = page_.leftTick();
    const auto zoom = page_.zoom();
    run(motion, count);
    const auto landedTrack = page_.track();
    const auto landedTime = page_.time();
    page_.setCursor(track, time);
    page_.setView(top, left, zoom);

    if (motion == "cursor-down" || motion == "cursor-up")
    {
        // Across tracks: the cursor's bar on each, as Vim's line motions take whole lines.
        area.top = std::min(track, landedTrack);
        area.bottom = std::max(track, landedTrack);
        return area;
    }
    // Across bars: up to the bar landed on, as Vim's character motions go, or with G and gg
    // the bar landed on too, as its line motions do.
    const bool inclusive = motion == "go-first" || motion == "go-last";
    const auto landed = m.toBarBeatTick(landedTime).bar;
    if (landed > bar)
    {
        area.to = inclusive ? end(landed) : m.barStart(landed);
    }
    else if (landed < bar)
    {
        area.from = m.barStart(landed);
        area.to = inclusive ? end(bar) : m.barStart(bar);
    }
    else if (!inclusive)
    {
        return std::nullopt;
    }
    return area.from < area.to ? std::optional{area} : std::nullopt;
}

std::string ArrangeView::describe(const Area& area) const
{
    return barsText(meter(), area.from, area.to) + " of " + tracksText(area);
}

std::string ArrangeView::tracksText(const Area& area) const
{
    const auto tracks = document_.song().tracks();
    const auto name = [&](std::size_t i)
    { return tracks[i]->name.empty() ? "track " + std::to_string(i + 1) : tracks[i]->name; };
    return area.top == area.bottom       ? name(area.top)
           : area.bottom == area.top + 1 ? name(area.top) + " and " + name(area.bottom)
                                         : name(area.top) + " to " + name(area.bottom);
}

bool ArrangeView::operateOn(std::string_view op, const Area& given, bool time, std::string named)
{
    // The events starting in the area, as the register's, their ticks from its start, taken as
    // bars; deleting time, as far as the section's end, where ripple stops.
    const auto& song = document_.song();
    auto area = given;
    const bool ripple = time && op == "operator-delete";
    if (ripple)
    {
        area.to = std::min(area.to, sectionEndAt(song, area.from));
    }
    const auto tracks = song.tracks();
    auto taken = takeEvents(song, area.top, area.bottom, area.from, area.to);
    taken.bars = true;
    std::size_t total = 0;
    for (const auto& events : taken.tracks)
    {
        total += events.size();
    }
    host_.eventRegister() = taken;
    auto what = named.empty() ? describe(area) : std::move(named);
    if (ripple)
    {
        edit("Delete " + what + ", closing the gap",
             [&](core::Song& s) { rippleDelete(s, area.top, area.bottom, area.from, area.to); });
        page_.setCursor(area.top, area.from);
        return true;
    }
    if (op == "operator-yank")
    {
        what[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(what[0])));
        host_.setMessage(what + " yanked (" + eventCount(total) + ")", MessageKind::Information);
        return true;
    }
    if (total == 0)
    {
        host_.setMessage("Nothing to delete in " + what, MessageKind::Information);
        return true;
    }
    edit("Delete " + what,
         [&](core::Song& s)
         {
             for (auto i = area.top; i <= area.bottom; ++i)
             {
                 s.updateEvents(tracks[i]->id(),
                                [&](core::EventList& events)
                                {
                                    const auto inside = events.startingIn(area.from, area.to);
                                    const auto first = static_cast<std::size_t>(
                                        inside.data() - events.all().data());
                                    for (auto k = inside.size(); k > 0; --k)
                                    {
                                        events.erase(first + k - 1);
                                    }
                                });
             }
         });
    // The cursor at the area's start, on its top track.
    page_.setCursor(area.top, area.from);
    return true;
}

bool ArrangeView::put(bool insert)
{
    const auto& reg = host_.eventRegister();
    if (reg.empty())
    {
        host_.setMessage("Nothing yanked or deleted to put", MessageKind::Warning);
        return true;
    }
    if (!reg.sections.empty())
    {
        host_.setMessage("The register holds sections: put them on the section strip",
                         MessageKind::Warning);
        return true;
    }
    if (document_.song().tracks().empty())
    {
        return false;
    }
    // At the start of the cursor's bar, onto its track and those after it.
    const auto& m = meter();
    const auto at = m.barStart(m.toBarBeatTick(page_.time()).bar);
    const auto track = page_.track();
    std::size_t total = 0;
    for (const auto& events : reg.tracks)
    {
        total += events.size();
    }
    const auto tracks = document_.song().tracks();
    const auto name =
        tracks[track]->name.empty() ? "track " + std::to_string(track + 1) : tracks[track]->name;
    const auto where = name + (reg.tracks.size() > 1 ? " and the tracks after it" : "");
    if (!insert)
    {
        edit("Put " + eventCount(total) + " in " + where,
             [&](core::Song& song) { putRegister(song, reg, track, at); });
        return true;
    }
    insertRegister(reg, track, at, "Insert " + eventCount(total) + " in " + where);
    return true;
}

void ArrangeView::insertRegister(const EventRegister& reg, std::size_t track, core::Tick at,
                                 const std::string& title)
{
    // Asking first, if the section must grow to make room, by whole bars on every track.
    const auto& song = document_.song();
    const auto bars = barsToInsert(song, reg, track, at);
    const auto section = song.sectionAt(at);
    const auto insert = [this, reg, track, at, bars, section, title]
    {
        edit(title,
             [&](core::Song& s)
             {
                 if (bars > 0)
                 {
                     s.resizeSection(section, bars);
                 }
                 rippleInsert(s, reg, track, at);
             });
    };
    if (bars == 0)
    {
        insert();
        return;
    }
    host_.ask("Lengthen " + song.sections()[section]->name() + " by " +
                  (bars == 1 ? std::string{"a bar"} : std::to_string(bars) + " bars") +
                  " to make room?",
              insert);
}

std::optional<bool> ArrangeView::runTyping(std::string_view command)
{
    const auto section = page_.stripCursor().value_or(0);
    InspectorEditor::Hooks hooks{
        .inspection = [this] { return inspection(); },
        .set = [&](const std::string& heading, const std::string& text)
        { return setSectionField(section, heading, text); },
        .step = [&](const std::string& heading, int steps) -> std::optional<std::string>
        {
            if (heading != "Length")
            {
                return std::nullopt;
            }
            return setSectionField(section, heading,
                                   (steps < 0 ? "-" : "+") + std::to_string(std::abs(steps)));
        }};
    return editor_.run(command, host_, hooks);
}

std::optional<std::string> ArrangeView::setSectionField(std::size_t section,
                                                        const std::string& heading,
                                                        const std::string& text)
{
    // The name, made unique as renaming does; or the length, as a number of bars or a change of
    // one, at least a bar, shortening deleting what starts in the bars that go, as - does.
    const auto& song = document_.song();
    const auto name = song.sections()[section]->name();
    if (heading == "Section")
    {
        if (text.empty())
        {
            return "A section needs a name";
        }
        if (text != name)
        {
            edit("Rename " + name + " to " + text,
                 [&](core::Song& s) { s.renameSection(section, text); });
        }
        return std::nullopt;
    }
    const auto& m = song.meter();
    const auto bars = m.toBarBeatTick(song.sectionStart(section + 1) - 1).bar -
                      m.toBarBeatTick(song.sectionStart(section)).bar + 1;
    const bool change = !text.empty() && (text[0] == '+' || text[0] == '-');
    std::int64_t number = 0;
    const auto* first = text.data() + (change && text[0] == '+' ? 1 : 0);
    const auto [ptr, ec] = std::from_chars(first, text.data() + text.size(), number);
    if (text.empty() || ec != std::errc{} || ptr != text.data() + text.size())
    {
        return "A length is a number of bars, such as 8, or a change such as +2";
    }
    const auto wanted = change ? bars + number : number;
    if (wanted < 1)
    {
        return "A section is at least a bar long";
    }
    if (wanted != bars)
    {
        edit((wanted > bars ? "Lengthen " : "Shorten ") + name + " to " + std::to_string(wanted) +
                 (wanted == 1 ? " bar" : " bars"),
             [&](core::Song& s) { s.resizeSection(section, wanted - bars); });
    }
    return std::nullopt;
}

void ArrangeView::edit(std::string title, const std::function<void(core::Song&)>& change)
{
    document_.edit(std::move(title), place(), change);
    refresh();
}

void ArrangeView::enterStrip()
{
    returnTrack_ = page_.track();
    returnTime_ = page_.time();
    returnSection_ = document_.song().sectionAt(returnTime_);
    page_.setStripCursor(returnSection_);
}

void ArrangeView::leaveStrip()
{
    // Back to the track the cursor came from: where it was, if it's on the same section, or
    // else the start of the section it's on now.
    const auto section = page_.stripCursor().value_or(returnSection_);
    page_.setStripCursor(std::nullopt);
    page_.setCursor(returnTrack_, section == returnSection_
                                      ? returnTime_
                                      : document_.song().sectionStart(section));
}

std::optional<bool> ArrangeView::runOnStrip(std::size_t section, std::string_view command,
                                            int count)
{
    const int n = std::max(count, 1);
    auto& song = document_.song();
    const auto last = song.sections().size() - 1;
    const auto go = [&](std::int64_t to)
    {
        page_.setStripCursor(static_cast<std::size_t>(
            std::clamp<std::int64_t>(to, 0, static_cast<std::int64_t>(last))));
        return true;
    };
    const auto name = [&](std::size_t i) { return song.sections()[i]->name(); };
    if (command == "cursor-left" || command == "cursor-right")
    {
        return go(static_cast<std::int64_t>(section) + (command == "cursor-left" ? -n : n));
    }
    if (command == "cursor-first" || command == "go-first")
    {
        return go(count > 0 && command == "go-first" ? count - 1 : 0);
    }
    if (command == "cursor-last" || command == "go-last")
    {
        return go(count > 0 && command == "go-last" ? count - 1 : static_cast<std::int64_t>(last));
    }
    if (command == "cursor-down" || command == "track-next")
    {
        leaveStrip();
        return true;
    }
    if (command == "cursor-up" || command == "track-previous" || command == "go-section-strip")
    {
        return false; // on the strip already
    }
    if (command == "zoom-in" || command == "zoom-out")
    {
        page_.zoomBy(command == "zoom-in" ? -n : n);
        return true;
    }
    if (command == "zoom-reset")
    {
        page_.resetZoom();
        return true;
    }
    if (command == "inspector-edit")
    {
        if (editor_.open(inspection()))
        {
            host_.startTyping();
        }
        return true;
    }
    if (command == "event-delete" || command == "yank-this")
    {
        return operateOnStrip(section,
                              command == "event-delete" ? "operator-delete" : "operator-yank",
                              "this-section", count);
    }
    if (command == "register-put" || command == "register-put-before")
    {
        return putSections(section, command == "register-put-before");
    }
    if (command == "move-left" || command == "move-right")
    {
        const auto by = command == "move-left" ? -n : n;
        std::size_t to = section;
        edit("Move " + name(section) + (by < 0 ? " earlier" : " later"),
             [&](core::Song& s) { to = s.moveSection(section, by); });
        page_.setStripCursor(to);
        return true;
    }
    if (command == "join")
    {
        if (section == last)
        {
            host_.setMessage(name(section) + " is the last section, with none after to join",
                             MessageKind::Warning);
            return true;
        }
        // As Vim's J: a count joins that many sections into one.
        const int joins = std::min(std::max(n - 1, 1), static_cast<int>(last - section));
        edit("Join " + name(section) + " with " + name(section + 1) +
                 (joins > 1 ? " and the next " + std::to_string(joins - 1) : ""),
             [&](core::Song& s)
             {
                 for (int i = 0; i < joins; ++i)
                 {
                     s.joinSections(section);
                 }
             });
        page_.setStripCursor(section);
        return true;
    }
    if (command.starts_with("field-increase") || command.starts_with("field-decrease") ||
        command == "number-increase" || command == "number-decrease")
    {
        const auto bars = command.find("-increase") != std::string_view::npos ? n : -n;
        bool changed = false;
        edit((bars > 0 ? "Lengthen " : "Shorten ") + name(section) + " by " +
                 (n == 1 ? std::string{"a bar"} : std::to_string(n) + " bars"),
             [&](core::Song& s) { changed = s.resizeSection(section, bars); });
        if (!changed)
        {
            host_.setMessage(name(section) + " can't be shorter than a bar", MessageKind::Warning);
        }
        page_.setStripCursor(section);
        return true;
    }
    if (command == "insert-before" || command == "insert-after")
    {
        // A new section of 4 bars, or the count, named for now and then to be named.
        const auto at = command == "insert-before" ? section : section + 1;
        const auto newName = song.newSectionName();
        edit("Insert " + newName,
             [&](core::Song& s) { s.insertEmptySection(at, newName, count > 0 ? count : 4); });
        page_.setStripCursor(at);
        host_.openPrompt("section-rename " + newName);
        return true;
    }
    if (command == "change")
    {
        host_.openPrompt("section-rename " + name(section));
        return true;
    }
    if (command == "field-edit")
    {
        host_.playFrom(song.sectionStart(section));
        return true;
    }
    if (command == "scroll-page-left" || command == "scroll-page-right")
    {
        host_.setMessage("Paging is for the tracks' bars: j goes back to them",
                         MessageKind::Information);
        return true;
    }
    return std::nullopt;
}

bool ArrangeView::operateOnStrip(std::size_t section, std::string_view op, std::string_view motion,
                                 int count)
{
    // Sections behave like lines (decided October 2026): x and Y this one, or as many as the
    // count; motions across sections up to where they land, or with G and gg including it. dd,
    // yy and the bars' and notes' objects aren't on the strip. as is this section, as x and Y
    // take it, and is its events on every track.
    if (motion == "object-section-contents")
    {
        if (document_.song().tracks().empty())
        {
            return false;
        }
        auto [area, what] = stripContents(section, count);
        const bool done = operateOn(op, area, false, std::move(what));
        page_.setStripCursor(section);
        return done;
    }
    if (motion == "object-section")
    {
        motion = "this-section";
    }
    else if (motion == "operator-line" || motion.starts_with("object-"))
    {
        host_.setMessage("The strip holds sections, not bars or notes: as takes this section, "
                         "and is its events on every track",
                         MessageKind::Information);
        return true;
    }
    const auto& song = document_.song();
    const auto last = song.sections().size() - 1;
    std::size_t first = section;
    std::size_t lastTaken = section;
    if (motion == "this-section")
    {
        lastTaken = std::min(section + static_cast<std::size_t>(std::max(count, 1)) - 1, last);
    }
    else if (motion == "visual" || motion == "visual-time")
    {
        const auto selected = selectedSections();
        if (!selected)
        {
            return false;
        }
        first = selected->first;
        lastTaken = selected->second;
    }
    else
    {
        run(motion, count);
        const auto landed = page_.stripCursor().value_or(section);
        page_.setStripCursor(section);
        const bool inclusive = motion == "go-first" || motion == "go-last";
        if (landed > section)
        {
            lastTaken = inclusive ? landed : landed - 1;
        }
        else if (landed < section)
        {
            first = landed;
            lastTaken = inclusive ? section : section - 1;
        }
        else if (!inclusive)
        {
            return false;
        }
    }
    const auto taking = lastTaken - first + 1;
    const auto what =
        taking == 1 ? song.sections()[first]->name()
                    : std::to_string(taking) + " sections from " + song.sections()[first]->name();
    auto& reg = host_.eventRegister();
    EventRegister taken;
    for (auto i = first; i <= lastTaken; ++i)
    {
        taken.sections.push_back(song.sections()[i]);
    }
    reg = std::move(taken);
    if (op == "operator-yank")
    {
        host_.setMessage(what + " yanked", MessageKind::Information);
        return true;
    }
    if (taking > last)
    {
        host_.setMessage("A song keeps at least one section", MessageKind::Warning);
        return true;
    }
    edit("Delete " + (taking == 1 ? "section " : std::string{}) + what,
         [&](core::Song& s) { s.removeSections(first, taking); });
    page_.setStripCursor(std::min(first, document_.song().sections().size() - 1));
    return true;
}

bool ArrangeView::putSections(std::size_t section, bool before)
{
    const auto& reg = host_.eventRegister();
    if (reg.empty())
    {
        host_.setMessage("Nothing yanked or deleted to put", MessageKind::Warning);
        return true;
    }
    if (reg.sections.empty())
    {
        host_.setMessage("The register holds bars of tracks: put them on the tracks",
                         MessageKind::Warning);
        return true;
    }
    const auto at = before ? section : section + 1;
    const auto what = reg.sections.size() == 1 ? reg.sections.front()->name()
                                               : std::to_string(reg.sections.size()) + " sections";
    const auto& song = document_.song();
    edit("Put " + what + (before ? " before " : " after ") + song.sections()[section]->name(),
         [&](core::Song& s) { s.insertSections(at, reg.sections); });
    page_.setStripCursor(at);
    return true;
}

Inspection ArrangeView::sectionInspection(std::size_t section) const
{
    const auto& song = document_.song();
    const auto& m = song.meter();
    const auto start = song.sectionStart(section);
    const auto end = song.sectionStart(section + 1);
    const auto first = m.toBarBeatTick(start).bar;
    const auto last = m.toBarBeatTick(end - 1).bar;
    const auto bars = last - first + 1;
    Inspection result{.kind = "Section", .fields = {}};
    result.fields.push_back({"Position", positionText(m, start)});
    result.fields.push_back(
        {.heading = "Time", .value = songTime(song.tempo().toTime(start)), .aside = true});
    result.fields.push_back({.heading = "Section",
                             .value = song.sections()[section]->name(),
                             .editable = true,
                             .editText = song.sections()[section]->name()});
    result.fields.push_back({"Bars", std::to_string(first) + "–" + std::to_string(last)});
    result.fields.push_back(
        {.heading = "Length",
         .value = std::to_string(bars) + (bars == 1 ? " bar, " : " bars, ") +
                  songTime(song.tempo().toTime(end) - song.tempo().toTime(start)),
         .editable = true,
         .editText = std::to_string(bars)});
    result.fields.push_back({"Meter", signatureText(m.signatureAt(start))});
    return result;
}

bool ArrangeView::toBarWithEvents(int count)
{
    // Tab and Shift-Tab: past empty bars to the next or previous bar where an event of the
    // cursor's track starts, controllers too, as Sequencer Plus Gold's Tab went.
    const auto id = trackId();
    if (!id)
    {
        return false;
    }
    const auto all = document_.song().events(*id).all();
    const auto& m = meter();
    auto bar = m.toBarBeatTick(page_.time()).bar;
    for (int i = 0; i < std::abs(count); ++i)
    {
        std::optional<core::Tick> found;
        if (count > 0)
        {
            const auto at =
                std::ranges::lower_bound(all, m.barStart(bar + 1), {}, &core::Event::tick);
            if (at != all.end())
            {
                found = at->tick;
            }
        }
        else
        {
            const auto at = std::ranges::lower_bound(all, m.barStart(bar), {}, &core::Event::tick);
            if (at != all.begin())
            {
                found = std::prev(at)->tick;
            }
        }
        if (!found)
        {
            if (i == 0)
            {
                const auto& track = *document_.song().tracks()[page_.track()];
                host_.setMessage(std::string{"No bars with events "} +
                                     (count > 0 ? "after" : "before") + " this on " +
                                     (track.name.empty()
                                          ? "track " + std::to_string(page_.track() + 1)
                                          : track.name),
                                 MessageKind::Information);
                return true;
            }
            break;
        }
        bar = m.toBarBeatTick(*found).bar;
    }
    page_.setCursor(page_.track(), m.barStart(bar));
    return true;
}

std::optional<std::pair<std::string, std::string>>
ArrangeView::relabel(std::string_view command) const
{
    // On the tracks, Tab and Shift-Tab go by bars with events; on the strip, the step keys
    // lengthen and shorten the section.
    if (!page_.stripCursor())
    {
        if (command == "cursor-note-next")
        {
            return std::pair{std::string{"Moving the cursor"}, std::string{"Next bar with events"}};
        }
        if (command == "cursor-note-previous")
        {
            return std::pair{std::string{"Moving the cursor"},
                             std::string{"Bar with events before"}};
        }
        return std::nullopt;
    }
    if (command == "field-increase" || command == "field-increase-fine" ||
        command == "number-increase")
    {
        return std::pair{std::string{}, std::string{"A bar longer"}};
    }
    if (command == "field-decrease" || command == "field-decrease-fine" ||
        command == "number-decrease")
    {
        return std::pair{std::string{}, std::string{"A bar shorter"}};
    }
    return std::nullopt;
}

std::optional<std::string> ArrangeView::describeRange(std::string_view op,
                                                      std::string_view motion) const
{
    if (page_.stripCursor())
    {
        // Sections, as lines, for d and y, and the section's events on every track; dd, yy
        // and the bars' and notes' objects aren't on the strip.
        if (motion == "object-section-contents")
        {
            return "this section's events on every track";
        }
        if (op != "operator-delete" && op != "operator-yank")
        {
            return std::nullopt;
        }
        if (motion == "object-section")
        {
            return "this section";
        }
        if (motion == "cursor-right")
        {
            return "this section";
        }
        if (motion == "cursor-left")
        {
            return "the section before";
        }
        if (motion == "go-last")
        {
            return "to the last section";
        }
        if (motion == "go-first")
        {
            return "from the first section";
        }
        return std::nullopt;
    }
    if (motion == "operator-line" || motion == "object-bar-contents")
    {
        return "this bar's events";
    }
    if (motion == "object-bar")
    {
        // As time only for d and y; to the others it's ib.
        if (op != "operator-delete" && op != "operator-yank")
        {
            return std::nullopt;
        }
        return op == "operator-delete" ? "this bar, closing the gap" : "this bar as time";
    }
    if (motion == "cursor-right")
    {
        return "this bar";
    }
    if (motion == "cursor-left")
    {
        return "the bar before";
    }
    if (motion == "cursor-down")
    {
        return "this bar here and on the track below";
    }
    if (motion == "cursor-up")
    {
        return "this bar here and on the track above";
    }
    if (motion == "object-note" || motion == "object-chord")
    {
        return std::nullopt;
    }
    if (motion == "object-section-contents")
    {
        return "this section's events";
    }
    if (motion == "object-section")
    {
        return sectionDescription(op);
    }
    if (motion == "object-track")
    {
        return "this track's events";
    }
    if (motion == "object-song")
    {
        return "the whole song, every track";
    }
    return describeTimeRange(motion);
}

Inspection ArrangeView::inspection() const
{
    if (const auto section = page_.stripCursor())
    {
        return sectionInspection(*section);
    }
    // The cursor's bar: where it is, its meter, how many notes start in it, and its section.
    const auto& song = document_.song();
    const auto& m = meter();
    const auto bar = m.toBarBeatTick(page_.time()).bar;
    const auto start = m.barStart(bar);
    const auto end = std::min(m.barStart(bar + 1), song.end());
    const auto last = m.toBarBeatTick(std::max<core::Tick>(song.end() - 1, 0)).bar;
    Inspection result{.kind = "Bar", .fields = {}};
    result.fields.push_back({"Position", positionText(m, start)});
    result.fields.push_back(
        {.heading = "Time", .value = songTime(song.tempo().toTime(start)), .aside = true});
    result.fields.push_back({"Bar", std::to_string(bar) + " of " + std::to_string(last)});
    result.fields.push_back({"Meter", signatureText(m.signatureAt(start))});
    if (const auto id = trackId())
    {
        std::size_t notes = 0;
        for (const auto& event : song.events(*id).startingIn(start, end))
        {
            notes += std::holds_alternative<core::Note>(event.data) ? 1 : 0;
        }
        result.fields.push_back({"Notes", std::to_string(notes)});
    }
    result.fields.push_back({"Section", song.sections()[song.sectionAt(start)]->name()});
    return result;
}

void ArrangeView::addRegions(Frame& frame) const
{
    const auto* player = host_.player();
    std::optional<ArrangePage::Selected> selected;
    if (const auto area = selectionArea())
    {
        selected = ArrangePage::Selected{area->top, area->bottom, area->from, area->to,
                                         selectedSections().has_value()};
    }
    page_.addRegions(frame, player != nullptr ? std::optional{player->marker()} : std::nullopt,
                     selected);
    const auto track = trackId() ? page_.track() + 1 : 0;
    frame.push_back(trackBarRegion(document_.song(), track, page_.time(), meter(), layout_.top[0],
                                   width_, cell_));
    const auto last = std::max<core::Tick>(document_.song().end() - 1, 0);
    frame.push_back(inspectorRegion(inspection(), layout_.top[1], width_, cell_,
                                    static_cast<int>(positionText(meter(), last).size()),
                                    &editor_));
}

std::string ArrangeView::position(core::Tick tick) const
{
    return positionText(meter(), tick);
}

std::string ArrangeView::timeStatus(core::Tick tick) const
{
    const auto& song = document_.song();
    return positionText(meter(), tick) + "  " + signatureText(meter().signatureAt(tick)) + "  " +
           song.sections()[song.sectionAt(tick)]->name();
}

std::string ArrangeView::cursorStatus() const
{
    if (const auto section = page_.stripCursor())
    {
        return timeStatus(document_.song().sectionStart(*section));
    }
    return timeStatus(meter().barStart(meter().toBarBeatTick(page_.time()).bar));
}

SongCursor ArrangeView::cursor() const
{
    SongCursor cursor{.track = trackId(), .tick = page_.time(), .pitch = std::nullopt};
    if (const auto section = page_.stripCursor())
    {
        // On the strip, the section's start, on the track the cursor came from.
        const auto tracks = document_.song().tracks();
        cursor.track =
            returnTrack_ < tracks.size() ? std::optional{tracks[returnTrack_]->id()} : cursor.track;
        cursor.tick = document_.song().sectionStart(*section);
    }
    if (cursor.track)
    {
        if (const auto it = pitches_.find(*cursor.track); it != pitches_.end())
        {
            cursor.pitch = it->second;
        }
    }
    return cursor;
}

void ArrangeView::setCursor(const SongCursor& cursor)
{
    // From the conductor's list, the track the arrangement was last on.
    page_.setStripCursor(std::nullopt);
    auto track = page_.track();
    if (cursor.track)
    {
        const auto tracks = document_.song().tracks();
        const auto it =
            std::ranges::find_if(tracks, [&](const auto& t) { return t->id() == *cursor.track; });
        if (it != tracks.end())
        {
            track = static_cast<std::size_t>(it - tracks.begin());
            if (cursor.pitch)
            {
                pitches_[*cursor.track] = *cursor.pitch;
            }
        }
    }
    page_.setCursor(track, cursor.tick);
}

core::EditPlace ArrangeView::place() const
{
    const auto strip = page_.stripCursor();
    return {trackId(),
            page_.time(),
            {static_cast<std::int64_t>(page_.topTrack()), page_.leftTick(),
             static_cast<std::int64_t>(page_.zoom()),
             strip ? static_cast<std::int64_t>(*strip) : -1}};
}

void ArrangeView::restore(const core::EditPlace& place)
{
    refresh();
    setCursor({.track = place.track, .tick = place.tick, .pitch = cursor().pitch});
    // The view as it was, when the change was made in the arrangement, on the strip if it was.
    if (place.view.size() == 4)
    {
        page_.setView(static_cast<std::size_t>(std::max<std::int64_t>(place.view[0], 0)),
                      place.view[1],
                      static_cast<std::size_t>(std::max<std::int64_t>(place.view[2], 0)));
        if (place.view[3] >= 0)
        {
            enterStrip();
            page_.setStripCursor(static_cast<std::size_t>(place.view[3]));
        }
    }
}

void ArrangeView::refresh()
{
    page_.setSong(document_.song());
}

void ArrangeView::showSong()
{
    page_ = ArrangePage{document_.song()};
    page_.resize(layout_.main, cell_);
    pitches_.clear();
}

void ArrangeView::showPlayhead(core::Tick playhead, Follow follow)
{
    page_.setPlayhead(playhead);
    if (follow == Follow::Cursor && page_.stripCursor())
    {
        page_.setStripCursor(document_.song().sectionAt(playhead));
    }
    else if (follow == Follow::Cursor)
    {
        page_.setCursor(page_.track(), playhead);
    }
    if (follow != Follow::None)
    {
        page_.showTime(playhead, true);
    }
}

void ArrangeView::stopPlayhead(std::optional<core::Tick> cursorTo)
{
    page_.setPlayhead(std::nullopt);
    if (page_.stripCursor())
    {
        if (cursorTo)
        {
            page_.setStripCursor(document_.song().sectionAt(*cursorTo));
        }
        return;
    }
    page_.setCursor(page_.track(), cursorTo.value_or(page_.time()));
}

} // namespace modal::ui
