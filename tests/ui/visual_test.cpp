// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// Eight bars of 4/4 in sections A (bars 1-2), B (3-5) and C (6-8), and two tracks, Bass and
// Lead, each with a note starting each bar.
core::Song sectioned(core::Ppqn ppqn)
{
    const auto bar = ppqn.whole();
    core::Song song{ppqn};
    for (const auto& [name, low] : {std::pair{"Bass", 40}, std::pair{"Lead", 60}})
    {
        const auto id = song.addTrack(name);
        song.updateEvents(id,
                          [&](core::EventList& events)
                          {
                              for (int i = 0; i < 8; ++i)
                              {
                                  events.insert(
                                      {i * bar, core::Note{static_cast<std::uint8_t>(low + i), 100,
                                                           ppqn.quarter()}});
                              }
                          });
    }
    song.renameSection(0, "A");
    song.splitSection(2 * bar, "B");
    song.splitSection(5 * bar, "C");
    return song;
}

// The song in a workspace of 120 by 30 cells, in the arrangement.
struct Selecting
{
    explicit Selecting(core::Ppqn ppqn = core::kPpqn)
        : bar{ppqn.whole()}
        , document{sectioned(ppqn)}
    {
        registry.add(windowCommands());
        ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
        ws.show(ViewKind::Arrangement);
    }

    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            ws.handle(Key::typed(key), 0);
        }
    }
    void press(NamedKey key) { ws.handle(Key::of(key), 0); }

    // The selection as the status line has it, or empty if there's none.
    std::string selected() const { return ws.arrangement().selectionStatus().value_or(""); }
    std::string bars(int first, int last) const
    {
        return barsText(document.song().meter(), (first - 1) * bar, last * bar) + " on every track";
    }

    core::Tick bar;
    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

} // namespace

TEST_CASE("v in the arrangement selects bars on every track, following the cursor")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"lv");
    CHECK(s.ws.mode() == Mode::Visual);
    CHECK(s.selected() == s.bars(2, 2));
    s.type(U"2l");
    CHECK(s.selected() == s.bars(2, 4));
    s.type(U"j"); // onto the lead: still every track
    CHECK(s.selected() == s.bars(2, 4));
    s.type(U"3h"); // back past where it started
    CHECK(s.selected() == s.bars(1, 2));

    // o goes to the other end.
    s.type(U"o");
    CHECK(s.ws.arrangement().page().bar() == 2);
    CHECK(s.selected() == s.bars(1, 2));

    // The status line says so.
    const auto frame = s.ws.frame();
    std::u32string status;
    for (const auto& run : frame.back().text)
    {
        status += run.text;
    }
    CHECK(status.find(U" VISUAL ") != std::u32string::npos);

    // Escape, or v again, ends it; gv selects it again, the cursor where it was.
    s.press(NamedKey::Escape);
    CHECK(s.ws.mode() == Mode::Normal);
    CHECK(s.selected().empty());
    s.type(U"G");
    s.type(U"gv");
    CHECK(s.ws.mode() == Mode::Visual);
    CHECK(s.selected() == s.bars(1, 2));
    CHECK(s.ws.arrangement().page().bar() == 2);
    s.type(U"v");
    CHECK(s.ws.mode() == Mode::Normal);
}

TEST_CASE("an object in Visual mode selects what it takes")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"3lv"); // bar 4, in B
    s.type(U"is");
    CHECK(s.selected() == s.bars(3, 5));
    s.type(U"l"); // and on from its end
    CHECK(s.selected() == s.bars(3, 6));
    s.type(U"ib");
    CHECK(s.selected() == s.bars(6, 6));
    s.type(U"in");
    CHECK(s.ws.message() == "There's no single note or chord here to select");
    CHECK(s.selected() == s.bars(6, 6));
}

TEST_CASE("the selection is drawn across every track, in place of the cursor's bar")
{
    Selecting s;
    const auto widest = [&]
    {
        int width = 0;
        int height = 0;
        for (const auto& region : s.ws.frame())
        {
            for (const auto& shape : region.under)
            {
                if (shape.role == Role::Selection && shape.rect.width > width)
                {
                    width = shape.rect.width;
                    height = shape.rect.height;
                }
            }
        }
        return std::pair{width, height};
    };
    const auto [barWidth, tracksHeight] = widest(); // the cursor's bar
    s.type(U"v2l");
    const auto [width, height] = widest();
    CHECK(width > 2 * barWidth);
    CHECK(height == tracksHeight);
}

TEST_CASE("keys bound to nothing in Visual mode say so, and it goes on")
{
    Selecting s;
    s.type(U"vQ");
    CHECK(s.ws.message() == "Q isn't a key here");
    CHECK(s.ws.mode() == Mode::Visual);
}
