#include "ui/Window.hpp"
#include "ui/Sound.hpp"

#include <stdexcept>
#include <string>

namespace bitzkey::ui {

Window::Window(bool fullscreen) : fullscreen_(fullscreen) {
    const Uint32 flags = SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    window_ = SDL_CreateWindow("Bitzkey | An artificial ecology", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               WINDOW_WIDTH, WINDOW_HEIGHT, flags);
    if (!window_) throw std::runtime_error(std::string("Couldn't open a window: ") + SDL_GetError());
    SDL_SetWindowMinimumSize(window_, MIN_WIDTH, MIN_HEIGHT);
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) throw std::runtime_error(std::string("Couldn't draw to the window: ") + SDL_GetError());
    fit_to_window();
}

Window::~Window() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

void Window::fit_to_window() {
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(renderer_, &width, &height);
    if (frame_ && frame_->w == width && frame_->h == height) return;
    frame_ = make_surface(width, height);
    SDL_SetSurfaceBlendMode(frame_.get(), SDL_BLENDMODE_NONE);
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    UI.fit(width, height);
}

Canvas Window::frame() {
    fit_to_window();
    UI.fit(frame_->w, frame_->h);
    window_frame(frame_.get());
    return Canvas(frame_.get());
}

void Window::present() {
    SDL_UpdateTexture(texture_, nullptr, frame_->pixels, frame_->pitch);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
    frame_shown();
}

void Window::toggle_fullscreen() {
    fullscreen_ = !fullscreen_;
    if (fullscreen_) {
        SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(window_, 0);
        SDL_SetWindowSize(window_, WINDOW_WIDTH, WINDOW_HEIGHT);
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    fit_to_window();
}

void Window::handle(const SDL_Event& event) {
    if (event.type == SDL_WINDOWEVENT &&
        (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event.window.event == SDL_WINDOWEVENT_RESIZED)) {
        fit_to_window();
    }
}

}  // namespace bitzkey::ui
