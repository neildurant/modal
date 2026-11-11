// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/view.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace modal::ui
{

// The Metronome page (, m s, decided November 2026): the song's metronome and what comes before
// recording, a labelled row for each setting, in two groups as the Ports view has them, so
// every setting can be seen and found. Enter edits the field under the cursor in place, Up and
// Down stepping it, and changing a click's note or velocity plays it. Its settings are the
// song's, saved with it, so each change is an undo step.
class MetronomeView final : public View
{
public:
    // The rows, top to bottom.
    enum class Setting
    {
        Plays,
        Port,
        Channel,
        Downbeat,
        Beat,
        Length,
        Mode,
        CountIn,
        PreRoll,
    };
    // The fields a row has: its value, and for a click its velocity too.
    enum class Field
    {
        Value,
        Velocity,
    };

    MetronomeView(core::Document& document, ViewHost& host);
    std::string_view name() const override { return "the metronome page"; }
    std::vector<int> topHeights(CellSize cell) const override;
    void resize(const WindowLayout& layout, int width, CellSize cell) override;
    std::optional<bool> run(std::string_view command, int count) override;
    bool operate(std::string_view op, std::string_view motion, int count) override;
    void addRegions(Frame& frame) const override;
    std::string position(core::Tick tick) const override;
    // The song's cursor passes through, as the page has no timeline or tracks of its own.
    SongCursor cursor() const override { return songCursor_; }
    void setCursor(const SongCursor& cursor) override { songCursor_ = cursor; }
    const core::MeterMap& cursorMeter() const override;
    core::EditPlace place() const override;
    void restore(const core::EditPlace& place) override;
    void refresh() override {}
    void showSong() override;
    void showPlayhead(core::Tick, Follow) override {}
    void stopPlayhead(std::optional<core::Tick>) override {}
    bool typing() const override { return editor_.active(); }
    std::optional<std::string> typingHint() const override;
    void type(char32_t character) override;
    std::optional<bool> runTyping(std::string_view command) override;

    Setting setting() const { return kSettings[row_]; }
    Field field() const { return field_; }
    // Puts the cursor on a row, as , m p does on Port.
    void go(Setting setting);

    static constexpr std::array<Setting, 9> kSettings{
        Setting::Plays,  Setting::Port, Setting::Channel, Setting::Downbeat, Setting::Beat,
        Setting::Length, Setting::Mode, Setting::CountIn, Setting::PreRoll};

private:
    // What a field shows, and what editing it starts with.
    std::string text(Setting setting, Field field) const;
    // Sets a field from what's typed, or steps it; why not, if it can't.
    std::optional<std::string> set(const std::string& typed);
    std::optional<std::string> step(int steps);
    // Puts a changed song in as an undo step, saying what changed.
    void change(const std::string& what, const core::Song& changed);
    Inspection inspection() const;
    std::vector<std::string> ports() const;

    core::Document& document_;
    ViewHost& host_;
    SongCursor songCursor_;
    std::size_t row_ = 0;
    Field field_ = Field::Value;
    WindowLayout layout_;
    CellSize cell_{1, 1};
    int width_ = 0;
    InspectorEditor editor_;
};

} // namespace modal::ui
