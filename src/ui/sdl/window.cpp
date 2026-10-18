#include <modal/ui/sdl/window.hpp>

#include <SDL3/SDL.h>

#include <utility>

namespace modal::ui::sdl
{

struct Window::Impl
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Surface* lastFrame = nullptr; // offscreen only
    bool offscreen = false;

    ~Impl()
    {
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
    return std::unique_ptr<Window>{new Window{std::move(impl)}};
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

void Window::run(const std::function<void(Window&)>& redraw)
{
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
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
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
