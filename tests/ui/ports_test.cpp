// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/ports.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using namespace modal::ui;

namespace
{

constexpr auto In = PortDirection::Input;
constexpr auto Out = PortDirection::Output;

std::vector<std::string> names(std::initializer_list<const char*> list)
{
    return {list.begin(), list.end()};
}

} // namespace

TEST_CASE("ports seen for the first time start disabled, and are marked present or not")
{
    PortTable table;
    CHECK(table.detected(In, names({"Xkey 1", "loopMIDI Port 1"})));
    CHECK(table.detected(Out, names({"loopMIDI Port 1", "Babyface Midi Port 1"})));
    REQUIRE(table.ports().size() == 4);
    for (const auto& port : table.ports())
    {
        CHECK_FALSE(port.enabled);
        CHECK(port.present);
    }
    // A device's two sides are two ports, inputs listed first.
    CHECK(table.ports()[0].direction == In);
    CHECK(table.ports()[1].direction == In);
    CHECK(table.find(In, "loopMIDI Port 1") != nullptr);
    CHECK(table.find(Out, "loopMIDI Port 1") != nullptr);
    CHECK(table.usable(In).empty());
    CHECK(table.offered(Out).empty());

    // Seeing them again adds nothing; one unplugged stays, not present, keeping what it had.
    CHECK(table.setEnabled(In, "Xkey 1", true));
    CHECK(table.setAlias(In, "Xkey 1", "Keys"));
    CHECK_FALSE(table.detected(In, names({"loopMIDI Port 1"})));
    const auto* xkey = table.find(In, "Xkey 1");
    REQUIRE(xkey != nullptr);
    CHECK_FALSE(xkey->present);
    CHECK(xkey->enabled);
    CHECK(xkey->alias == "Keys");
    CHECK(table.usable(In).empty());             // enabled but not there
    CHECK(table.offered(In) == names({"Keys"})); // still offered, to keep a track's choice
    CHECK_FALSE(table.detected(In, names({"Xkey 1", "loopMIDI Port 1"})));
    CHECK(table.usable(In) == names({"Xkey 1"}));
}

TEST_CASE("a track's port resolves by alias, then detected name, if enabled and present")
{
    PortTable table;
    table.detected(Out, names({"Babyface Midi Port 1", "loopMIDI Port 1", "loopMIDI Port 2"}));
    table.setEnabled(Out, "Babyface Midi Port 1", true);
    table.setAlias(Out, "Babyface Midi Port 1", "Babyface");
    table.setEnabled(Out, "loopMIDI Port 1", true);

    CHECK(table.resolve(Out, "Babyface").port == "Babyface Midi Port 1");
    CHECK(table.resolve(Out, "babyface").port == "Babyface Midi Port 1");
    // Songs saved with detected names still play.
    CHECK(table.resolve(Out, "Babyface Midi Port 1").port == "Babyface Midi Port 1");
    CHECK(table.resolve(Out, "loopMIDI Port 1").port == "loopMIDI Port 1");
    // A part of one, as :port has matched, if only one has it.
    CHECK(table.resolve(Out, "baby").port == "Babyface Midi Port 1");
    CHECK(table.resolve(Out, "loopMIDI").problem == PortProblem::Missing); // two of them

    // Disabled, missing and unknown ports resolve to none, saying why.
    CHECK(table.resolve(Out, "loopMIDI Port 2") ==
          PortResolution{std::nullopt, PortProblem::Disabled, "loopMIDI Port 2"});
    CHECK(table.resolve(Out, "Pro-800").problem == PortProblem::Missing);
    table.detected(Out, names({"loopMIDI Port 1"}));
    CHECK(table.resolve(Out, "Babyface") ==
          PortResolution{std::nullopt, PortProblem::Missing, "Babyface"});
    // An input of the same name isn't an output.
    CHECK(table.resolve(In, "loopMIDI Port 1").problem == PortProblem::Missing);
}

TEST_CASE("an empty name is the default output, or every enabled input")
{
    PortTable table;
    table.detected(Out, names({"Host"}));
    CHECK(table.resolve(Out, "").problem == PortProblem::Missing); // no default set
    table.setEnabled(Out, "Host", true);
    CHECK(table.setDefaultOutput("Host"));
    CHECK(table.resolve(Out, "").port == "Host");
    CHECK_FALSE(table.setDefaultOutput("Unknown"));
    CHECK(table.resolve(In, "") == PortResolution{});
    // Forgetting the default's port leaves no default.
    CHECK(table.forget(Out, "Host"));
    CHECK(table.defaultOutput().empty());
}

TEST_CASE("the ports file keeps the ports, their aliases, enabling and the default")
{
    PortTable table;
    table.detected(In, names({"Xkey 1", "A \"quoted\" name"}));
    table.detected(Out, names({"loopMIDI Port 1", "Microsoft GS Wavetable Synth"}));
    table.setEnabled(In, "Xkey 1", true);
    table.setAlias(In, "Xkey 1", "Keys");
    table.setEnabled(Out, "loopMIDI Port 1", true);
    table.setAlias(Out, "loopMIDI Port 1", "Plugin host");
    table.setDefaultOutput("loopMIDI Port 1");

    const auto text = table.text();
    CHECK(text.find("input \"Xkey 1\" on alias=\"Keys\"\n") != std::string::npos);
    CHECK(text.find("output \"loopMIDI Port 1\" on default alias=\"Plugin host\"\n") !=
          std::string::npos);
    CHECK(text.find("output \"Microsoft GS Wavetable Synth\" off\n") != std::string::npos);

    std::vector<std::string> errors;
    auto read = PortTable::read(text, &errors);
    CHECK(errors.empty());
    // Presence isn't saved: it's what the system lists when Modal starts.
    for (const auto& port : table.ports())
    {
        CHECK_FALSE(read.find(port.direction, port.name)->present);
    }
    read.detected(In, names({"Xkey 1", "A \"quoted\" name"}));
    read.detected(Out, names({"loopMIDI Port 1", "Microsoft GS Wavetable Synth"}));
    CHECK(read == table);
}

TEST_CASE("a ports file line it can't read is reported, and the rest still read")
{
    std::vector<std::string> errors;
    const auto table = PortTable::read("# a comment\n"
                                       "input \"Xkey 1\" on\r\n"
                                       "speaker \"Nothing\" on\n"
                                       "output \"Unclosed on\n"
                                       "output \"Synth\" sideways\n"
                                       "input \"Xkey 1\" off\n"
                                       "\n"
                                       "output \"Synth\" on\n",
                                       &errors);
    REQUIRE(errors.size() == 4);
    CHECK(errors[0].starts_with("ports:3: "));
    CHECK(errors[3] == "ports:6: listed twice: Xkey 1");
    REQUIRE(table.ports().size() == 2);
    CHECK(table.find(In, "Xkey 1")->enabled);
    CHECK(table.find(Out, "Synth")->enabled);
}
