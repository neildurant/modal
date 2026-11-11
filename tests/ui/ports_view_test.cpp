// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/demo_song.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr auto In = PortDirection::Input;
constexpr auto Out = PortDirection::Output;

// A workspace with a machine's ports: two inputs and three outputs, one of them unplugged.
struct Porting
{
    Porting()
    {
        registry.add(windowCommands());
        const std::vector<std::string> inputs{"Xkey 1", "loopMIDI Port 1"};
        const std::vector<std::string> outputs{"loopMIDI Port 1", "Babyface Midi Port 1"};
        table.detected(Out, std::vector<std::string>{"Gone Synth"});
        table.detected(In, inputs);
        table.detected(Out, outputs);
        ws.setPortControl({.table = [this] { return &table; },
                           .change =
                               [this](const PortTable& changed)
                           {
                               table = changed;
                               ++changes;
                           },
                           .look = [this] { ++looks; }});
        ws.resize(1200, 600, {10, 20});
        type(U",vm");
    }
    void type(std::u32string_view keys)
    {
        for (auto c : keys)
        {
            ws.handle(Key::typed(c), 0);
        }
    }
    void press(NamedKey key) { ws.handle(Key::of(key), 0); }
    const KnownPort& port() const
    {
        REQUIRE(ws.portsView().current() != nullptr);
        return *ws.portsView().current();
    }
    std::u32string text() const
    {
        std::u32string all;
        for (const auto& region : ws.frame())
            for (const auto& run : region.text)
                all += run.text + U"|";
        return all;
    }

    core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace ws{registry, document};
    PortTable table;
    int changes = 0;
    int looks = 0;
};

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

} // namespace

TEST_CASE("the leader, v and m, or gm, open the ports view, looking for ports, the inputs and "
          "outputs apart")
{
    Porting p;
    CHECK(p.ws.shown() == ViewKind::Ports);
    CHECK(p.ws.where() == Where::PortList);
    CHECK(p.looks == 1);
    const auto text = p.text();
    CHECK(contains(text, U"Inputs|"));
    CHECK(contains(text, U"Outputs|"));
    CHECK(contains(text, U"not connected"));
    CHECK(contains(text, U"0 of 2 enabled"));
    // The cursor on the first input, on its E button.
    CHECK(p.port().direction == In);
    CHECK(p.port().name == "Xkey 1");
    CHECK(p.ws.portsView().column() == PortsView::Column::Enabled);
    // :ports opens it too.
    p.type(U"ga");
    p.registry.runLine("ports");
    CHECK(p.ws.shown() == ViewKind::Ports);
    // And gm, as ga and gt open theirs.
    p.type(U"gagm");
    CHECK(p.ws.shown() == ViewKind::Ports);
    CHECK(p.looks == 3);
}

TEST_CASE("in the ports view, j and k go from port to port, past the groups' headings")
{
    Porting p;
    p.type(U"j");
    CHECK(p.port().name == "loopMIDI Port 1");
    CHECK(p.port().direction == In);
    p.type(U"j");
    CHECK(p.port().direction == Out); // the Outputs heading passed over
    CHECK(p.port().name == "Gone Synth");
    p.type(U"9j");
    CHECK(p.port().name == "Babyface Midi Port 1");
    p.type(U"gg");
    CHECK(p.port().name == "Xkey 1");
    p.type(U"G");
    CHECK(p.port().name == "Babyface Midi Port 1");
    p.type(U"k");
    CHECK(p.port().name == "loopMIDI Port 1");
    CHECK(p.port().direction == Out);
}

TEST_CASE("Enter on E enables a port, and again disables it, saying so")
{
    Porting p;
    p.press(NamedKey::Enter);
    CHECK(p.changes == 1);
    CHECK(p.table.find(In, "Xkey 1")->enabled);
    CHECK(p.ws.message() == "Enabled Xkey 1: listened to");
    CHECK(contains(p.text(), U"1 of 2 enabled"));
    p.press(NamedKey::Enter);
    CHECK_FALSE(p.table.find(In, "Xkey 1")->enabled);
    CHECK(p.ws.message() == "Disabled Xkey 1");
    // Not a song edit: nothing to undo.
    CHECK_FALSE(p.document.canUndo());
}

TEST_CASE("Enter on Default makes a port its direction's default, one at a time, and again none")
{
    Porting p;
    p.type(U"l");
    CHECK(p.ws.portsView().column() == PortsView::Column::Default);
    p.press(NamedKey::Enter);
    CHECK(p.table.defaultPort(In) == "Xkey 1");
    CHECK(p.ws.message() == "Default input: Xkey 1");
    p.type(U"G");
    p.press(NamedKey::Enter);
    CHECK(p.table.defaultPort(Out) == "Babyface Midi Port 1");
    CHECK(p.ws.message() == "Default output: Babyface Midi Port 1");
    CHECK(p.table.defaultPort(In) == "Xkey 1"); // each direction its own
    p.type(U"k");
    p.press(NamedKey::Enter);
    CHECK(p.table.defaultPort(Out) == "loopMIDI Port 1"); // the other gives way
    p.press(NamedKey::Enter);
    CHECK(p.table.defaultPort(Out).empty());
    CHECK(p.ws.message() == "No default output: tracks set to Default aren't played");
}

TEST_CASE("Enter on the alias edits it in place, unique among its direction's ports")
{
    Porting p;
    p.type(U"G0ll"); // the last output's alias
    CHECK(p.ws.portsView().column() == PortsView::Column::Alias);
    p.press(NamedKey::Enter);
    CHECK(p.ws.portsView().typing());
    p.type(U"Babyface Out");
    p.press(NamedKey::Enter);
    CHECK_FALSE(p.ws.portsView().typing());
    CHECK(p.table.find(Out, "Babyface Midi Port 1")->alias == "Babyface Out");
    CHECK(p.ws.message() == "Babyface Midi Port 1 is called Babyface Out");
    CHECK(contains(p.text(), U"Babyface Out"));

    // Another output can't take it, nor another's detected name; an input can.
    p.type(U"k");
    p.press(NamedKey::Enter);
    p.type(U"Babyface Out");
    p.press(NamedKey::Enter);
    CHECK(p.ws.message() == "Another output is called Babyface Out");
    p.press(NamedKey::Escape);
    CHECK(p.table.find(Out, "loopMIDI Port 1")->alias.empty());
    p.type(U"gg");
    p.press(NamedKey::Enter);
    p.type(U"Babyface Out");
    p.press(NamedKey::Enter);
    CHECK(p.table.find(In, "Xkey 1")->alias == "Babyface Out");

    // The choosers' own words aren't aliases.
    p.press(NamedKey::Enter);
    p.press(NamedKey::Backspace);
    p.type(U"default");
    p.press(NamedKey::Enter);
    CHECK(p.ws.message() == "Tracks' choosers use Default for themselves");
    p.press(NamedKey::Escape);

    // Nothing clears it, the detected name showing again.
    p.press(NamedKey::Enter);
    p.press(NamedKey::Backspace);
    p.press(NamedKey::Enter);
    CHECK(p.table.find(In, "Xkey 1")->alias.empty());
    CHECK(p.ws.message() == "Xkey 1 has no alias");
}

TEST_CASE("Enter on a port's detected name does nothing as it's read only")
{
    Porting p;
    p.type(U"G0lll");
    REQUIRE(p.ws.portsView().column() == PortsView::Column::Name);
    const auto before = p.ws.message();
    p.press(NamedKey::Enter);
    CHECK_FALSE(p.ws.portsView().typing());
    CHECK(p.ws.message() == before);
    p.type(U"k"); // the cursor still moves
    CHECK(p.ws.portsView().column() == PortsView::Column::Name);
    CHECK_FALSE(p.ws.portsView().typing());
}

TEST_CASE("the cursor stops at a port's name and doesn't go on to its status")
{
    Porting p;
    p.type(U"G0llll");
    CHECK(p.ws.portsView().column() == PortsView::Column::Name);
    p.type(U"0$");
    CHECK(p.ws.portsView().column() == PortsView::Column::Name);
}

TEST_CASE("x forgets a port that isn't there, not one that is")
{
    Porting p;
    p.type(U"x");
    CHECK(p.ws.message() == "Xkey 1 is there now, so it would be found again; disable it instead");
    CHECK(p.table.find(In, "Xkey 1") != nullptr);
    p.type(U"jj"); // Gone Synth, unplugged
    REQUIRE(p.port().name == "Gone Synth");
    p.type(U"x");
    CHECK(p.table.find(Out, "Gone Synth") == nullptr);
    CHECK(p.ws.message() == "Forgot Gone Synth");
    CHECK(p.port().name == "loopMIDI Port 1"); // on to the next
}

TEST_CASE("the ports view's inspector says what the column is, and why a port can't be used")
{
    Porting p;
    auto text = p.text();
    CHECK(contains(text, U"On: listened to, for Thru, recording and the In light"));
    CHECK(contains(text, U"not listened to"));
    p.type(U"jj");
    text = p.text();
    CHECK(contains(text, U"Not connected"));
    p.type(U"l");
    CHECK(contains(p.text(), U"Default: the output of tracks set to Default, one at a time"));
}
