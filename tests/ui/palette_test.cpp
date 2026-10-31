// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/palette.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

std::vector<std::string> titles(const std::vector<PaletteItem>& items, std::string_view query)
{
    std::vector<std::string> found;
    for (const auto i : paletteMatches(items, query))
    {
        found.push_back(items[i].title);
    }
    return found;
}

} // namespace

TEST_CASE("the palette matches titles, then words, then letters in order, then descriptions")
{
    const std::vector<PaletteItem> items{
        {.name = "velocity-compress",
         .title = "Velocity compress",
         .description = "Bring velocities nearer their average",
         .aliases = "",
         .offered = true},
        {.name = "zoom-in",
         .title = "Zoom in",
         .description = "Fewer bars",
         .aliases = "",
         .offered = true},
        {.name = "velocity-ramp",
         .title = "Velocity ramp",
         .description = "Change velocity across a range",
         .aliases = "crescendo diminuendo",
         .offered = true},
        {.name = "grid",
         .title = "Grid",
         .description = "The track's grid",
         .aliases = "",
         .offered = false},
        {.name = "section-rename",
         .title = "Section rename",
         .description = "Rename the section",
         .aliases = "",
         .offered = true},
    };
    // Starting the title, ignoring case.
    CHECK(titles(items, "vel") == std::vector<std::string>{"Velocity compress", "Velocity ramp"});
    CHECK(titles(items, "ZOOM") == std::vector<std::string>{"Zoom in"});
    // A word of it, after the titles that start with it.
    CHECK(titles(items, "ren") == std::vector<std::string>{"Section rename"});
    // Its letters in order.
    CHECK(titles(items, "vcomp") == std::vector<std::string>{"Velocity compress"});
    // In the description or other names.
    CHECK(titles(items, "crescendo") == std::vector<std::string>{"Velocity ramp"});
    CHECK(titles(items, "bars") == std::vector<std::string>{"Zoom in"});
    // What does nothing here comes last; with nothing typed, everything.
    CHECK(titles(items, "") == std::vector<std::string>{"Section rename", "Velocity compress",
                                                        "Velocity ramp", "Zoom in", "Grid"});
    CHECK(titles(items, "qqq").empty());
}

TEST_CASE("the palette matches keys too, as typed: exactly first, then starting with it")
{
    const auto item = [](std::string name, std::string title, std::vector<std::string> keys)
    {
        return PaletteItem{.name = std::move(name),
                           .title = std::move(title),
                           .description = "",
                           .aliases = "",
                           .offered = true,
                           .keys = std::move(keys)};
    };
    const std::vector<PaletteItem> items{
        item("zoom-in", "Zoom in", {"zi"}), item("grid-finer", "Grid finer", {"zh"}),
        item("join", "Join", {"J"}),        item("cursor-down", "Cursor down", {"j", "<Down>"}),
        item("zebra", "Zebra", {}),
    };
    CHECK(titles(items, "zh").front() == "Grid finer");
    CHECK(titles(items, "J").front() == "Join");
    CHECK(titles(items, "j").front() == "Cursor down");
    CHECK(titles(items, "<Down>").front() == "Cursor down");
    // The title starting with it, then keys starting with it.
    CHECK(titles(items, "z") == std::vector<std::string>{"Zebra", "Zoom in", "Grid finer"});
}

namespace
{

struct Paletting
{
    Paletting()
    {
        registry.add(windowCommands());
        ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
        ws.show(ViewKind::Arrangement);
        ws.setLineHandler(
            [this](const std::string& line)
            {
                const auto result = registry.runLine(line);
                if (!result.message.empty())
                {
                    ws.setMessage(result.message,
                                  result.warning ? MessageKind::Warning : MessageKind::Information);
                }
            });
    }

    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            ws.handle(Key::typed(key), 0);
        }
    }
    void press(const Key& key) { ws.handle(key, 0); }
    void open() { press(Key::typed(U'p', kCtrl)); }

    // The palette's rows, best first, as text, and the one chosen.
    std::vector<std::u32string> rows() const
    {
        std::vector<std::pair<int, std::u32string>> found;
        const auto frame = ws.frame();
        const auto& prompt = frame.back().bounds;
        for (const auto& region : frame)
        {
            // The panel just above the prompt, its first run each row's title.
            if (region.background != Role::Panel || region.text.empty() ||
                region.bounds.y + region.bounds.height != prompt.y)
            {
                continue;
            }
            for (const auto& run : region.text)
            {
                if (run.x == region.text.front().x)
                {
                    found.emplace_back(run.y, run.text);
                }
            }
        }
        std::ranges::sort(found, std::greater{}); // from the bottom
        std::vector<std::u32string> result;
        for (const auto& [y, text] : found)
        {
            result.push_back(text);
        }
        return result;
    }

    std::u32string prompt() const
    {
        std::u32string text;
        const auto frame = ws.frame();
        for (const auto& run : frame.back().text)
        {
            text += run.text;
        }
        return text;
    }

    core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace ws{registry, document};
};

} // namespace

TEST_CASE("Ctrl-p opens the palette at the prompt, and Enter runs the command chosen")
{
    Paletting p;
    p.open();
    CHECK(p.ws.mode() == Mode::CommandLine);
    CHECK(p.prompt().starts_with(U"Command: "));
    CHECK_FALSE(p.rows().empty());
    p.type(U"zoom in");
    REQUIRE_FALSE(p.rows().empty());
    CHECK(p.rows().front() == U"Zoom in");
    const auto zoom = p.ws.arrangement().page().zoom();
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.mode() == Mode::Normal);
    CHECK(p.ws.arrangement().page().zoom() == zoom - 1);
    CHECK(p.ws.message() == "Also: zi"); // its keys, to learn

    // By its keys, as remembered.
    p.open();
    p.type(U"zo");
    REQUIRE_FALSE(p.rows().empty());
    CHECK(p.rows().front() == U"Zoom out");
}

TEST_CASE("in the palette, Up and Down choose, Tab puts the command on the prompt")
{
    Paletting p;
    p.open();
    p.type(U"zoom");
    const auto rows = p.rows();
    REQUIRE(rows.size() >= 2);
    p.press(Key::of(NamedKey::Up)); // away from the line, to the next match
    p.press(Key::typed(U'p', kCtrl));
    p.press(Key::typed(U'n', kCtrl));
    p.press(Key::of(NamedKey::Tab));
    CHECK(p.ws.mode() == Mode::CommandLine);
    CHECK(p.ws.commandLine().text().ends_with(U" "));
    CHECK(p.prompt().starts_with(U":"));
    p.press(Key::of(NamedKey::Escape));

    // A command that must have an argument goes to the prompt, its choices showing.
    static constexpr std::array kNeeding{CommandInfo{
        .name = "needs-a-name", .description = "Takes a name", .usage = "NAME", .keys = false}};
    p.registry.add(kNeeding);
    p.open();
    p.type(U"needs");
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.mode() == Mode::CommandLine);
    CHECK(p.ws.commandLine().text() == U"needs-a-name ");
    p.press(Key::of(NamedKey::Escape));

    // Escape closes it.
    p.open();
    p.press(Key::of(NamedKey::Escape));
    CHECK(p.ws.mode() == Mode::Normal);
}

TEST_CASE("the palette from Visual mode runs commands as their keys would there")
{
    Paletting p;
    const auto statusOf = [&] { return p.prompt(); };
    p.type(U"vl");
    p.open();
    CHECK(p.prompt().starts_with(U"Command on bars 1–2 on every track: "));
    // Escape goes back to the selection.
    p.press(Key::of(NamedKey::Escape));
    CHECK(p.ws.mode() == Mode::Visual);

    // Cutting at the edges, on for the selection.
    p.open();
    p.type(U"cut at");
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.mode() == Mode::Visual);
    CHECK(p.ws.message() == "Also: |");
    p.type(U"oo"); // the message gone, the selection's said
    CHECK(statusOf().find(U"cutting at the edges") != std::u32string::npos);

    // An operator, on the selection.
    p.open();
    p.type(U"operator delete");
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.mode() == Mode::Normal);
    CHECK(p.document.modified());
}

TEST_CASE("the palette chooses nothing until something's typed or moved to")
{
    Paletting p;
    p.open();
    const auto zoom = p.ws.arrangement().page().zoom();
    p.press(Key::of(NamedKey::Enter)); // nothing chosen: nothing runs
    CHECK(p.ws.mode() == Mode::CommandLine);
    p.press(Key::of(NamedKey::Down)); // the best
    p.press(Key::of(NamedKey::Escape));
    CHECK(p.ws.arrangement().page().zoom() == zoom);
    // Typing chooses the best of what matches; deleting it all, none again.
    p.open();
    p.type(U"z");
    p.press(Key::of(NamedKey::Backspace));
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.mode() == Mode::CommandLine);
}

TEST_CASE("the palette leaves out the commands for config files")
{
    Paletting p;
    p.registry.add(configCommands());
    p.open();
    p.type(U"cgroup");
    CHECK((p.rows().empty() || p.rows().front() != U"Cgroup"));
    p.press(Key::of(NamedKey::Escape));
    p.open();
    p.type(U"set");
    REQUIRE_FALSE(p.rows().empty());
    CHECK(p.rows().front() == U"Set"); // set stays
}
