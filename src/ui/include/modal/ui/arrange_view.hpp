// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/track.hpp>
#include <modal/ui/arrange_page.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/view.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// The arrangement (decided for milestone 5), the home view: a row for each track, its bars
// drawn as blocks shaded by how busy they are, under a ruler of sections, meters and bar
// numbers. The cursor is a bar of a track; h and l move by the track's bars, j and k between
// tracks keeping the time, and zi, zo and z0 zoom. Enter on a bar opens it to edit.
class ArrangeView final : public View
{
public:
    // The commands only the arrangement has; it shares the others' names, such as
    // cursor-down, with the event list.
    static std::span<const CommandInfo> commands();

    // The document and the host outlive the view.
    ArrangeView(core::Document& document, ViewHost& host);

    std::string_view name() const override { return "the arrangement"; }
    std::vector<int> topHeights(CellSize cell) const override;
    void resize(const WindowLayout& layout, int width, CellSize cell) override;
    std::optional<bool> run(std::string_view command, int count) override;
    bool operate(std::string_view op, std::string_view motion, int count) override;
    std::optional<std::string> describeRange(std::string_view op,
                                             std::string_view motion) const override;
    void addRegions(Frame& frame) const override;
    std::string cursorStatus() const override;
    std::string timeStatus(core::Tick tick) const override;
    std::string position(core::Tick tick) const override;
    SongCursor cursor() const override;
    const core::MeterMap& cursorMeter() const override { return meter(); }
    void setCursor(const SongCursor& cursor) override;
    core::EditPlace place() const override;
    void restore(const core::EditPlace& place) override;
    void refresh() override;
    void showSong() override;
    void showPlayhead(core::Tick playhead, Follow follow) override;
    void stopPlayhead(std::optional<core::Tick> cursorTo) override;
    // Editing the section's fields in the inspector, with e on the strip.
    bool typing() const override { return editor_.active(); }
    void type(char32_t character) override { editor_.type(character); }
    std::optional<bool> runTyping(std::string_view command) override;
    const InspectorEditor& inspectorEditor() const { return editor_; }

    const ArrangePage& page() const { return page_; }

private:
    // An area of the song: tracks (counting from 0), and a time range.
    struct Area
    {
        std::size_t top;
        std::size_t bottom;
        core::Tick from;
        core::Tick to;
    };

    // Deletes or yanks the events starting in an area, or with time, as ab does, deletes the
    // area's time too, closing the gap up to the section's end.
    bool operateOn(std::string_view op, const Area& area, bool time = false);
    // Puts the register at the start of the cursor's bar, over what's there, or with insert
    // moving what's after it in the section later (P), asking first if the section must grow.
    bool put(bool insert);

    // The section strip: going onto it and back, its commands and operators, and what the
    // inspector shows of the section under the cursor.
    void enterStrip();
    void leaveStrip();
    std::optional<bool> runOnStrip(std::size_t section, std::string_view command, int count);
    bool operateOnStrip(std::size_t section, std::string_view op, std::string_view motion,
                        int count);
    bool putSections(std::size_t section, bool before);
    Inspection sectionInspection(std::size_t section) const;

    // Changes the song as one undo step, made where the cursor is, and shows the change.
    void edit(std::string title, const std::function<void(core::Song&)>& change);
    // Inserts the register at a time onto a track and those after it, as P does.
    void insertRegister(const EventRegister& reg, std::size_t track, core::Tick at,
                        const std::string& title);

    // An area's bars, in the cursor's track's bars, and its tracks, as words: "bars 3–4 of Bass".
    std::string describe(const Area& area) const;

    // The cursor's track's meter, or the conductor's with no tracks.
    const core::MeterMap& meter() const;
    std::optional<core::TrackId> trackId() const;
    Inspection inspection() const;
    // Sets the section's name or length from the inspector, or says why it can't.
    std::optional<std::string> setSectionField(std::size_t section, const std::string& heading,
                                               const std::string& text);

    core::Document& document_;
    ViewHost& host_;
    ArrangePage page_;
    std::map<core::TrackId, std::uint8_t> pitches_; // the pitch last used on each track
    // Where the cursor was on the tracks before the strip, to go back to.
    std::size_t returnTrack_ = 0;
    core::Tick returnTime_ = 0;
    std::size_t returnSection_ = 0;
    int width_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
    InspectorEditor editor_;
};

} // namespace modal::ui
