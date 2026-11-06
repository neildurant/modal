// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/track_fields.hpp>
#include <modal/ui/view.hpp>

namespace modal::ui
{

class TracksView final : public View
{
public:
    TracksView(core::Document& document, ViewHost& host);
    std::string_view name() const override { return "the tracks view"; }
    std::vector<int> topHeights(CellSize cell) const override;
    void resize(const WindowLayout& layout, int width, CellSize cell) override;
    std::optional<bool> run(std::string_view command, int count) override;
    bool operate(std::string_view op, std::string_view motion, int count) override;
    void addRegions(Frame& frame) const override;
    std::string cursorStatus() const override;
    std::string timeStatus(core::Tick tick) const override;
    std::string position(core::Tick tick) const override;
    SongCursor cursor() const override { return cursor_; }
    void setCursor(const SongCursor& cursor) override;
    const core::MeterMap& cursorMeter() const override;
    core::EditPlace place() const override;
    void restore(const core::EditPlace& place) override;
    void refresh() override;
    void showSong() override;
    void showPlayhead(core::Tick playhead, Follow follow) override;
    void stopPlayhead(std::optional<core::Tick> cursorTo) override;
    std::optional<std::string> selectionStatus() const override;
    std::optional<std::size_t> selectionIndex() const override { return row(); }
    bool typing() const override { return editor_.active(); }
    void type(char32_t character) override { editor_.type(character); }
    std::optional<bool> runTyping(std::string_view command) override;
    const InspectorEditor& inspectorEditor() const { return editor_; }

private:
    std::size_t row() const;
    void go(std::size_t row);
    void ensureVisible();
    std::vector<core::TrackId> selected() const;
    Inspection inspection() const;
    TrackField fieldFor(std::string_view heading) const;
    std::optional<std::string> edit(TrackField field, std::string_view text, int steps = 0,
                                    bool fine = false);
    bool put(bool before, bool replace, int count);

    core::Document& document_;
    ViewHost& host_;
    SongCursor cursor_;
    std::size_t column_ = 1;
    std::size_t top_ = 0;
    int left_ = 0;
    WindowLayout layout_;
    CellSize cell_{1, 1};
    int width_ = 0;
    InspectorEditor editor_;
};

} // namespace modal::ui
