// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/track.hpp>
#include <modal/ui/arrange_page.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/inspector.hpp>
#include <modal/ui/view.hpp>

#include <cstdint>
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
    void addRegions(Frame& frame) const override;
    std::string cursorStatus() const override;
    std::string timeStatus(core::Tick tick) const override;
    std::string position(core::Tick tick) const override;
    SongCursor cursor() const override;
    void setCursor(const SongCursor& cursor) override;
    core::EditPlace place() const override;
    void restore(const core::EditPlace& place) override;
    void refresh() override;
    void showSong() override;
    void showPlayhead(core::Tick playhead, bool cursorFollows) override;
    void stopPlayhead(std::optional<core::Tick> cursorTo) override;

    const ArrangePage& page() const { return page_; }

private:
    // The cursor's track's meter, or the conductor's with no tracks.
    const core::MeterMap& meter() const;
    std::optional<core::TrackId> trackId() const;
    Inspection inspection() const;

    core::Document& document_;
    ViewHost& host_;
    ArrangePage page_;
    std::map<core::TrackId, std::uint8_t> pitches_; // the pitch last used on each track
    int width_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
};

} // namespace modal::ui
