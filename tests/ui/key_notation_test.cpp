// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/key_notation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace modal::ui;

namespace
{

std::vector<Key> keys(std::string_view text)
{
    auto parsed = parseKeys(text);
    if (const auto* error = std::get_if<std::string>(&parsed))
    {
        FAIL(*error);
    }
    return std::get<std::vector<Key>>(parsed);
}

std::string error(std::string_view text)
{
    const auto parsed = parseKeys(text);
    const auto* message = std::get_if<std::string>(&parsed);
    return message != nullptr ? *message : std::string{};
}

} // namespace

TEST_CASE("typed characters stand for themselves")
{
    CHECK(keys("j") == std::vector{Key::typed(U'j')});
    CHECK(keys("gg") == std::vector{Key::typed(U'g'), Key::typed(U'g')});
    CHECK(keys("$G") == std::vector{Key::typed(U'$'), Key::typed(U'G')});
    CHECK(keys("é") == std::vector{Key::typed(U'é')});
    CHECK(keys("<S-Space>") == std::vector{Key::typed(U' ', kShift)});
    CHECK(keys("<lt>") == std::vector{Key::typed(U'<')});
    CHECK(keys("<Space>") == std::vector{Key::typed(U' ')});
    CHECK(keys(">") == std::vector{Key::typed(U'>')});
    CHECK(keys("<>>") == std::vector{Key::typed(U'>')});
}

TEST_CASE("named and modified keys are written in angle brackets, in any case")
{
    CHECK(keys("<Esc>") == std::vector{Key::of(NamedKey::Escape)});
    CHECK(keys("<esc><ESCAPE>") ==
          std::vector{Key::of(NamedKey::Escape), Key::of(NamedKey::Escape)});
    CHECK(keys("<CR><Enter><Return>") == std::vector{Key::of(NamedKey::Enter),
                                                     Key::of(NamedKey::Enter),
                                                     Key::of(NamedKey::Enter)});
    CHECK(keys("<S-Tab>") == std::vector{Key::of(NamedKey::Tab, kShift)});
    CHECK(keys("<F5><f12>") == std::vector{Key::of(NamedKey::F5), Key::of(NamedKey::F12)});
    CHECK(keys("<PageDown>") == std::vector{Key::of(NamedKey::PageDown)});
    CHECK(keys("<C-A-Del>") == std::vector{Key::of(NamedKey::Delete, kCtrl | kAlt)});
    CHECK(keys("<C-kPlus><C-kminus><C-k0>") == std::vector{Key::of(NamedKey::KeypadPlus, kCtrl),
                                                           Key::of(NamedKey::KeypadMinus, kCtrl),
                                                           Key::of(NamedKey::Keypad0, kCtrl)});
}

TEST_CASE("with Ctrl or Alt a letter is the same in either case, and Shift is written")
{
    CHECK(keys("<C-d>") == std::vector{Key::typed(U'd', kCtrl)});
    CHECK(keys("<C-D>") == std::vector{Key::typed(U'd', kCtrl)});
    CHECK(keys("<c-d>") == std::vector{Key::typed(U'd', kCtrl)});
    CHECK(keys("<C-S-d>") == std::vector{Key::typed(U'd', kCtrl | kShift)});
    CHECK(keys("<A-x><M-x>") == std::vector{Key::typed(U'x', kAlt), Key::typed(U'x', kAlt)});
    CHECK(keys("<C-Space>") == std::vector{Key::typed(U' ', kCtrl)});
    CHECK(keys("<C-->") == std::vector{Key::typed(U'-', kCtrl)});
    CHECK(keys("<S-a>") == std::vector{Key::typed(U'A')}); // the character it types
}

TEST_CASE("notation that can't be read says why")
{
    CHECK(error("<Nope>") == "unknown key <Nope>");
    CHECK(error("<X-a>") == "unknown modifier in <X-a>");
    CHECK(error("a<b") == "no closing > after <; write < as <lt>");
    CHECK(error("<S-1>") == "Shift with 1 is written as the character it types, in <S-1>");
    CHECK(error("") == "no keys");
}

TEST_CASE("keys are written in notation that reads back the same")
{
    const std::vector<Key> all{
        Key::typed(U'j'),
        Key::typed(U'G'),
        Key::typed(U'<'),
        Key::typed(U' '),
        Key::typed(U'é'),
        Key::typed(U'd', kCtrl),
        Key::typed(U'd', kCtrl | kShift),
        Key::typed(U'x', kAlt),
        Key::typed(U' ', kCtrl),
        Key::typed(U'<', kCtrl),
        Key::of(NamedKey::Escape),
        Key::of(NamedKey::Tab, kShift),
        Key::of(NamedKey::F11),
        Key::of(NamedKey::Delete, kCtrl | kAlt | kShift),
        Key::of(NamedKey::KeypadMinus, kCtrl),
        Key::of(NamedKey::Keypad9, kAlt),
    };
    for (const auto& key : all)
    {
        CAPTURE(formatKey(key));
        CHECK(keys(formatKey(key)) == std::vector{key});
    }
    CHECK(formatKeys(all) == "jG<lt><Space>é<C-d><C-S-d><A-x><C-Space><C-lt><Esc><S-Tab><F11><C-A-"
                             "S-Del><C-kMinus><A-k9>");
}
