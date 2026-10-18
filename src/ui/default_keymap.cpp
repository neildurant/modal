#include <modal/ui/default_keymap.hpp>

#include <array>
#include <cassert>
#include <string>

namespace modal::ui
{

namespace
{

constexpr auto N = Mode::Normal;

constexpr std::array kDefaults{
    DefaultBinding{N, "h", "cursor-left", "Left"},
    DefaultBinding{N, "<Left>", "cursor-left", "Left"},
    DefaultBinding{N, "j", "cursor-down", "Down"},
    DefaultBinding{N, "<Down>", "cursor-down", "Down"},
    DefaultBinding{N, "k", "cursor-up", "Up"},
    DefaultBinding{N, "<Up>", "cursor-up", "Up"},
    DefaultBinding{N, "l", "cursor-right", "Right"},
    DefaultBinding{N, "<Right>", "cursor-right", "Right"},
    DefaultBinding{N, "0", "cursor-first", "First column"},
    DefaultBinding{N, "<Home>", "cursor-first", "First column"},
    DefaultBinding{N, "$", "cursor-last", "Last column"},
    DefaultBinding{N, "<End>", "cursor-last", "Last column"},
    DefaultBinding{N, "gg", "go-first", "First row, or row {count}"},
    DefaultBinding{N, "G", "go-last", "Last row, or row {count}"},
    DefaultBinding{N, "<C-d>", "scroll-half-down", "Half a page down"},
    DefaultBinding{N, "<C-u>", "scroll-half-up", "Half a page up"},
    DefaultBinding{N, "<C-f>", "scroll-page-down", "A page down"},
    DefaultBinding{N, "<PageDown>", "scroll-page-down", "A page down"},
    DefaultBinding{N, "<C-b>", "scroll-page-up", "A page up"},
    DefaultBinding{N, "<PageUp>", "scroll-page-up", "A page up"},
    DefaultBinding{N, "z", "scroll-centre", "Centre the view on the cursor"},
    DefaultBinding{N, "zz", "zoom-reset", "Default zoom"},
    DefaultBinding{N, "+", "zoom-in", "Zoom in"},
    DefaultBinding{N, "=", "zoom-in", "Zoom in"},
    DefaultBinding{N, "-", "zoom-out", "Zoom out"},
    DefaultBinding{N, "<Tab>", "test-next-page", "Next test page"},
    DefaultBinding{N, "<Esc>", "cancel", "Cancel"},
};

} // namespace

std::span<const DefaultBinding> defaultBindings()
{
    return kDefaults;
}

Keymap defaultKeymap()
{
    Keymap keymap;
    for (const auto& binding : kDefaults)
    {
        [[maybe_unused]] const auto error = keymap.bind(
            binding.mode, binding.keys, {std::string{binding.command}, std::string{binding.hint}});
        assert(error.empty()); // the tests read every default binding
    }
    return keymap;
}

} // namespace modal::ui
