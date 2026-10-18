#include "font.hpp"

#include <modal/ui/sdl/window.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

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
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer(options.title.c_str(), options.width, options.height, flags,
                                     &impl->window, &impl->renderer))
    {
        return std::string{"can't open a window: "} + SDL_GetError();
    }
    SDL_SetRenderVSync(impl->renderer, 1); // where the renderer can; otherwise unsynced

    if (!TTF_Init())
    {
        return std::string{"can't start SDL_ttf: "} + SDL_GetError();
    }
    impl->ttf = true;
    std::unique_ptr<Window> window{new Window{std::move(impl)}};
    if (auto error = window->setFont(options.font); !error.empty())
    {
        return error;
    }
    return window;
}

Window::Window(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

Window::~Window() = default;

void Window::drawFrame(Colour background, const std::function<void()>& draw)
{
    SDL_SetRenderDrawColor(impl_->renderer, background.r, background.g, background.b,
                           SDL_ALPHA_OPAQUE);
    SDL_RenderClear(impl_->renderer);
    if (draw)
    {
        draw();
    }
    if (impl_->offscreen)
    {
        SDL_DestroySurface(impl_->lastFrame);
        impl_->lastFrame = SDL_RenderReadPixels(impl_->renderer, nullptr);
    }
    SDL_RenderPresent(impl_->renderer);
}

void Window::drawText(int column, int row, std::string_view text, Colour colour)
{
    const auto cell = impl_->font->cell();
    const char* next = text.data();
    std::size_t left = text.size();
    for (int c = column; left > 0; ++c)
    {
        const auto character = static_cast<char32_t>(SDL_StepUTF8(&next, &left));
        impl_->font->draw(character, static_cast<float>(c * cell.width),
                          static_cast<float>(row * cell.height), colour);
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
    return {};
}

CellSize Window::cellSize() const
{
    return impl_->font->cell();
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

void Window::run(const std::function<void(Window&)>& redraw)
{
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
