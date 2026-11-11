// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/choice_list.hpp>
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
    bool typing() const override { return editor_.active() || portChoice_.has_value(); }
    std::optional<std::string> typingHint() const override;
    void type(char32_t character) override;
    std::optional<bool> runTyping(std::string_view command) override;
    const InspectorEditor& inspectorEditor() const { return editor_; }
    bool choosingPort() const { return portChoice_.has_value(); }
    bool editingQuantize() const { return quantizeEditing_; }
    // Whole rows for shared batch operations, or the current track without a selection.
    std::vector<core::TrackId> selected() const;

private:
    std::size_t row() const;
    void go(std::size_t row);
    void ensureVisible();
    // A column's width in cells: Name grows with the longest name, up to a limit.
    int columnWidth(const TrackColumn& column) const;
    Inspection inspection() const;
    // The table's fields, as the editor works through them.
    Inspection editableFields() const;
    // Playback quantise's settings, edited in the inspector rather than the table.
    Inspection quantizeFields() const;
    TrackField fieldFor(std::string_view heading) const;
    std::optional<std::string> edit(TrackField field, std::string_view text, int steps = 0,
                                    bool fine = false);
    bool put(bool before, bool replace, int count);
    // Opens the cursor's column for editing, as a list of ports or in its cell; false if it can't.
    bool openField();
    // Goes on to the next or previous column that can be edited, and opens it.
    void nextField(bool forwards);

    std::optional<bool> runPortChoice(std::string_view command);
    // Why a track's output or input can't be used, as the Ports view has the machine's ports:
    // a port named that isn't found (as a song from elsewhere may name) or is disabled; Default
    // with no default set, or a default not found or disabled; or All with no input enabled,
    // or none of those enabled connected.
    enum class PortTrouble
    {
        None,
        Missing,
        Disabled,
        NoDefault,
        DefaultMissing,
        DefaultDisabled,
        NoInputs,
        NoInputsConnected,
    };
    PortTrouble portTrouble(const core::Track& track, bool input) const;
    // How the chooser shows a port: the default, every input, or by name.
    static std::string portLabel(const std::string& port);

    // Flips a button on the cursor's row, and any selected rows to match it.
    void toggle(TrackField field);

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
    bool quantizeEditing_ = false; // the editor on the inspector's quantise fields

    // Choosing a track's port from a list at its cell: "" is the default port, and a port the
    // track names that wasn't found stays on the list until something else is chosen.
    struct PortChoice
    {
        bool input = false;
        std::vector<std::string> ports;
        std::string current;
        ChoiceList list;
    };
    std::optional<PortChoice> portChoice_;
};

} // namespace modal::ui
