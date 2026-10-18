#include "font.hpp"

#include <modal/ui/sdl/window.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace modal::ui::sdl
{

struct Window::Impl
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Surface* lastFrame = nullptr; // offscreen only
    bool offscreen = false;
    bool ttf = false; // SDL_ttf initialised
    FontOptions fontOptions;
    std::unique_ptr<Font> font;
    int minimumColumns = 0;
    int minimumRows = 0;

    ~Impl()
    {
        font.reset();
        if (ttf)
        {
            TTF_Quit();
        }
        SDL_DestroySurface(lastFrame);
        if (renderer != nullptr)
        {
            SDL_DestroyRenderer(renderer);
        }
        if (window != nullptr)
        {
            SDL_DestroyWindow(window);
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

std::variant<std::unique_ptr<Window>, std::string> Window::open(const WindowOptions& options)
{
    if (options.offscreen)
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0"); // no GPU driver at all
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        return std::string{"can't start SDL's video: "} + SDL_GetError();
    }

    auto impl = std::make_unique<Impl>();
    impl->offscreen = options.offscreen;
    // Hidden until it has its font and size.
    const SDL_WindowFlags flags =
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    // Sized in cells, it starts at a placeholder size until it has its font.
    const bool inCells = options.columns > 0 && options.rows > 0;
    if (!SDL_CreateWindowAndRenderer(options.title.c_str(), inCells ? 640 : options.width,
                                     inCells ? 400 : options.height, flags, &impl->window,
                                     &impl->renderer))
    {
        return std::string{"can't open a window: "} + SDL_GetError();
    }
    SDL_SetRenderVSync(impl->renderer, 1); // where the renderer can; otherwise unsynced

    if (!TTF_Init())
    {
        return std::string{"can't start SDL_ttf: "} + SDL_GetError();
    }
    impl->ttf = true;
    impl->minimumColumns = options.minimumColumns;
    impl->minimumRows = options.minimumRows;
    std::unique_ptr<Window> window{new Window{std::move(impl)}};
    if (auto error = window->setFont(options.font); !error.empty())
    {
        return error;
    }
    if (inCells)
    {
        window->resizeToCells(options.columns, options.rows);
        SDL_SetWindowPosition(window->impl_->window, SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED);
    }
    SDL_ShowWindow(window->impl_->window);
    return window;
}

Window::Window(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

Window::~Window() = default;

namespace
{

void fill(SDL_Renderer* renderer, const Rect& rect, Colour colour, Uint8 alpha = SDL_ALPHA_OPAQUE)
{
    SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, alpha);
    const SDL_FRect area{static_cast<float>(rect.x), static_cast<float>(rect.y),
                         static_cast<float>(rect.width), static_cast<float>(rect.height)};
    SDL_RenderFillRect(renderer, &area);
}

Rect offset(Rect rect, const Rect& by)
{
    rect.x += by.x;
    rect.y += by.y;
    return rect;
}

} // namespace

void Window::draw(const Frame& frame, const Theme& theme)
{
    SDL_Renderer* renderer = impl_->renderer;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const auto background = theme[Role::Background];
    SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    for (const auto& region : frame)
    {
        drawRegion(region, theme);
    }
    SDL_SetRenderClipRect(renderer, nullptr);

    if (impl_->offscreen)
    {
        SDL_DestroySurface(impl_->lastFrame);
        impl_->lastFrame = SDL_RenderReadPixels(renderer, nullptr);
    }
    SDL_RenderPresent(renderer);
}

void Window::drawRegion(const Region& region, const Theme& theme)
{
    const Rect& bounds = region.bounds;
    if (bounds.width <= 0 || bounds.height <= 0)
    {
        return;
    }
    SDL_Renderer* renderer = impl_->renderer;
    const SDL_Rect clip{bounds.x, bounds.y, bounds.width, bounds.height};
    SDL_SetRenderClipRect(renderer, &clip);

    Font& font = *impl_->font;
    const auto cell = font.cell();
    const auto regionBackground = theme[region.background];
    fill(renderer, bounds, regionBackground);

    for (const auto& run : region.text)
    {
        const auto colour = theme.background(run.style);
        if (colour != regionBackground)
        {
            const auto width = static_cast<int>(run.text.size()) * cell.width;
            fill(renderer, offset({run.x, run.y, width, cell.height}, bounds), colour);
        }
    }
    for (const auto& shape : region.under)
    {
        fill(renderer, offset(shape.rect, bounds), theme[shape.role], shape.alpha);
    }
    for (const auto& run : region.text)
    {
        const auto colour = theme.foreground(run.style);
        const bool bold = (run.style.attributes & kBold) != 0;
        int x = bounds.x + run.x;
        const int y = bounds.y + run.y;
        for (const char32_t character : run.text)
        {
            font.draw(character, bold, static_cast<float>(x), static_cast<float>(y), colour);
            x += cell.width;
        }
        if ((run.style.attributes & kUnderline) != 0)
        {
            const auto width = static_cast<int>(run.text.size()) * cell.width;
            fill(renderer,
                 {bounds.x + run.x, y + font.underlineTop(), width, font.underlineThickness()},
                 colour);
        }
    }
    for (const auto& shape : region.over)
    {
        fill(renderer, offset(shape.rect, bounds), theme[shape.role], shape.alpha);
    }
}

std::string Window::setFont(const FontOptions& font)
{
    auto opened = Font::open(impl_->renderer, font, SDL_GetWindowDisplayScale(impl_->window));
    if (auto* error = std::get_if<std::string>(&opened))
    {
        return std::move(*error);
    }
    impl_->font = std::move(std::get<std::unique_ptr<Font>>(opened));
    impl_->fontOptions = font;
    applyMinimumSize();
    return {};
}

CellSize Window::cellSize() const
{
    return impl_->font->cell();
}

void Window::setMinimumCells(int columns, int rows)
{
    impl_->minimumColumns = std::max(columns, 0);
    impl_->minimumRows = std::max(rows, 0);
    applyMinimumSize();
}

namespace
{

// SDL takes window sizes in the display's units, which on some systems aren't pixels.
int toWindowUnits(SDL_Window* window, int pixels)
{
    const float density = std::max(SDL_GetWindowPixelDensity(window), 0.01f);
    return static_cast<int>(std::ceil(static_cast<float>(pixels) / density));
}

} // namespace

void Window::resizeToCells(int columns, int rows)
{
    const auto cell = impl_->font->cell();
    SDL_SetWindowSize(impl_->window, toWindowUnits(impl_->window, columns * cell.width),
                      toWindowUnits(impl_->window, rows * cell.height));
    SDL_SyncWindow(impl_->window);
}

void Window::applyMinimumSize()
{
    const auto cell = impl_->font->cell();
    const int width = toWindowUnits(impl_->window, impl_->minimumColumns * cell.width);
    const int height = toWindowUnits(impl_->window, impl_->minimumRows * cell.height);
    SDL_SetWindowMinimumSize(impl_->window, width, height);

    int current[2] = {};
    SDL_GetWindowSize(impl_->window, &current[0], &current[1]);
    if (current[0] < width || current[1] < height)
    {
        SDL_SetWindowSize(impl_->window, std::max(current[0], width), std::max(current[1], height));
        SDL_SyncWindow(impl_->window);
    }
}

namespace
{

// While the user drags a window's edge, Windows runs its own loop and the main loop waits, so
// SDL sends the expose events of a live resize to event watches instead, on the main thread,
// to be drawn at once. Otherwise the newly uncovered area stays unpainted until the drag ends.
struct LiveResize
{
    Window* window;
    const std::function<void(Window&)>* redraw;

    static bool watch(void* userdata, SDL_Event* event)
    {
        if (event->type == SDL_EVENT_WINDOW_EXPOSED && event->window.data1 == 1)
        {
            const auto* self = static_cast<LiveResize*>(userdata);
            (*self->redraw)(*self->window);
        }
        return true;
    }
};

} // namespace

namespace
{

NamedKey namedKey(SDL_Keycode code)
{
    switch (code)
    {
    case SDLK_ESCAPE:
        return NamedKey::Escape;
    case SDLK_TAB:
        return NamedKey::Tab;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return NamedKey::Enter;
    case SDLK_BACKSPACE:
        return NamedKey::Backspace;
    case SDLK_DELETE:
        return NamedKey::Delete;
    case SDLK_INSERT:
        return NamedKey::Insert;
    case SDLK_UP:
        return NamedKey::Up;
    case SDLK_DOWN:
        return NamedKey::Down;
    case SDLK_LEFT:
        return NamedKey::Left;
    case SDLK_RIGHT:
        return NamedKey::Right;
    case SDLK_HOME:
        return NamedKey::Home;
    case SDLK_END:
        return NamedKey::End;
    case SDLK_PAGEUP:
        return NamedKey::PageUp;
    case SDLK_PAGEDOWN:
        return NamedKey::PageDown;
    default:
        break;
    }
    if (code >= SDLK_F1 && code <= SDLK_F12)
    {
        return static_cast<NamedKey>(static_cast<int>(NamedKey::F1) +
                                     static_cast<int>(code - SDLK_F1));
    }
    return NamedKey::None;
}

Modifiers modifiers(SDL_Keymod mod)
{
    Modifiers result = kNoModifiers;
    result |= (mod & SDL_KMOD_CTRL) != 0 ? kCtrl : kNoModifiers;
    result |= (mod & SDL_KMOD_ALT) != 0 ? kAlt : kNoModifiers;
    result |= (mod & SDL_KMOD_SHIFT) != 0 ? kShift : kNoModifiers;
    return result;
}

// Turns SDL's key events into Keys. Named keys come from key presses; characters come from
// text input, which follows the keyboard's layout, except with Ctrl or Alt, which type no
// text, when they come from the key pressed. Ctrl and Alt together are taken as AltGr, which
// types text on many layouts.
class KeyReader
{
public:
    explicit KeyReader(const std::function<bool(const Key&)>& key)
        : key_(key)
    {
    }

    // Whether the event was a key the handler said changed what's shown.
    bool read(const SDL_Event& event)
    {
        if (!key_)
        {
            return false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            skipText_ = false;
            const auto mods = modifiers(event.key.mod);
            if (const auto named = namedKey(event.key.key); named != NamedKey::None)
            {
                return key_(Key::of(named, mods));
            }
            const bool ctrlOrAlt = (mods & (kCtrl | kAlt)) != 0;
            const bool altGr = (mods & kCtrl) != 0 && (mods & kAlt) != 0;
            if (ctrlOrAlt && !altGr && event.key.key >= 0x20 && event.key.key < 0x7f)
            {
                // Some systems type text for Alt and a key as well; it's the same key.
                skipText_ = true;
                return key_(Key::typed(static_cast<char32_t>(event.key.key), mods));
            }
            return false;
        }
        if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text != nullptr)
        {
            if (std::exchange(skipText_, false))
            {
                return false;
            }
            bool changed = false;
            for (const char32_t character : decodeUtf8(event.text.text))
            {
                changed = key_(Key::typed(character)) || changed;
            }
            return changed;
        }
        return false;
    }

private:
    const std::function<bool(const Key&)>& key_;
    bool skipText_ = false;
};

} // namespace

void Window::run(const std::function<void(Window&)>& redraw,
                 const std::function<bool(const Key&)>& key)
{
    KeyReader keys{key};
    SDL_StartTextInput(impl_->window);

    LiveResize liveResize{this, &redraw};
    SDL_AddEventWatch(&LiveResize::watch, &liveResize);
    struct Unwatch
    {
        LiveResize* liveResize;
        ~Unwatch() { SDL_RemoveEventWatch(&LiveResize::watch, liveResize); }
    } unwatch{&liveResize};

    bool needsFrame = true;
    for (;;)
    {
        if (needsFrame)
        {
            redraw(*this);
            needsFrame = false;
        }

        SDL_Event event;
        if (!SDL_WaitEvent(&event))
        {
            return;
        }
        do
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                return;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                setFont(impl_->fontOptions); // if it fails, the old size will do
                needsFrame = true;
                break;
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                needsFrame = true;
                break;
            default:
                needsFrame = keys.read(event) || needsFrame;
                break;
            }
        } while (SDL_PollEvent(&event)); // all that's waiting, then one frame for them
    }
}

std::optional<Colour> Window::pixel(int x, int y) const
{
    const auto* frame = impl_->lastFrame;
    if (frame == nullptr || x < 0 || y < 0 || x >= frame->w || y >= frame->h)
    {
        return std::nullopt;
    }
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 0;
    if (!SDL_ReadSurfacePixel(impl_->lastFrame, x, y, &r, &g, &b, &a))
    {
        return std::nullopt;
    }
    return Colour{r, g, b};
}

bool Window::saveFrame(const std::string& path) const
{
    return impl_->lastFrame != nullptr && SDL_SaveBMP(impl_->lastFrame, path.c_str());
}

int Window::pixelWidth() const
{
    int width = 0;
    SDL_GetWindowSizeInPixels(impl_->window, &width, nullptr);
    return width;
}

int Window::pixelHeight() const
{
    int height = 0;
    SDL_GetWindowSizeInPixels(impl_->window, nullptr, &height);
    return height;
}

void showError(const std::string& title, const std::string& message)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(), nullptr);
}

} // namespace modal::ui::sdl
