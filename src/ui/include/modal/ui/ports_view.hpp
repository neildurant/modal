// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/ports.hpp>
#include <modal/ui/view.hpp>

#include <optional>
#include <string>
#include <vector>

namespace modal::ui
{

// The Ports view (, v p, decided November 2026): this machine's MIDI ports, the inputs and the
// outputs in groups of their own, each with an E button for whether it's enabled, an alias edited
// in place, its detected name and whether it's there now, and for outputs a D button for the
// default output. Its changes are the machine's, not the song's, so they aren't undo steps.
class PortsView final : public View
{
public:
    // The columns, left to right.
    enum class Column
    {
        Enabled,
        Default,
        Alias,
        Name,
        Status,
    };

    PortsView(core::Document& document, ViewHost& host);
    std::string_view name() const override { return "the ports view"; }
    std::vector<int> topHeights(CellSize cell) const override;
    void resize(const WindowLayout& layout, int width, CellSize cell) override;
    std::optional<bool> run(std::string_view command, int count) override;
    bool operate(std::string_view op, std::string_view motion, int count) override;
    void addRegions(Frame& frame) const override;
    std::string position(core::Tick tick) const override;
    // The song's cursor passes through, as the view has no timeline or tracks of its own.
    SongCursor cursor() const override { return songCursor_; }
    void setCursor(const SongCursor& cursor) override { songCursor_ = cursor; }
    const core::MeterMap& cursorMeter() const override;
    core::EditPlace place() const override;
    void restore(const core::EditPlace& place) override;
    void refresh() override;
    void showSong() override;
    void showPlayhead(core::Tick, Follow) override {}
    void stopPlayhead(std::optional<core::Tick>) override {}
    bool typing() const override { return editor_.active(); }
    std::optional<std::string> typingHint() const override;
    void type(char32_t character) override;
    std::optional<bool> runTyping(std::string_view command) override;

    // The port the cursor is on, if there are any, and the column.
    const KnownPort* current() const;
    Column column() const { return kColumns[column_]; }

    static constexpr std::array<Column, 5> kColumns{Column::Enabled, Column::Default, Column::Alias,
                                                    Column::Name, Column::Status};

private:
    // The table's lines: a group's heading, or a port, by its index among the table's ports.
    struct Line
    {
        PortDirection direction;
        std::optional<std::size_t> port;
    };
    std::vector<Line> lines() const;
    // The cursor's line among them, always a port's when there is one.
    std::size_t line() const;
    void go(std::size_t line, int towards);
    void ensureVisible();
    int columnWidth(Column column) const;
    Inspection inspection() const;
    void change(const PortTable& table);
    std::optional<std::string> setAlias(const std::string& text);
    void act();

    core::Document& document_;
    ViewHost& host_;
    SongCursor songCursor_;
    // The cursor, by its port, so it stays on the port as others come and go.
    PortDirection direction_ = PortDirection::Input;
    std::string port_;
    std::size_t column_ = 0;
    std::size_t top_ = 0;
    WindowLayout layout_;
    CellSize cell_{1, 1};
    int width_ = 0;
    InspectorEditor editor_;
};

} // namespace modal::ui
