#pragma once
// The app's window: fullscreen at the desktop's resolution by default; F11 switches to a resizable
// window and back. Each frame is drawn in software into `frame()` and shown by `present()`.

#include <SDL.h>

#include "ui/Canvas.hpp"

namespace bitzkey::ui {

constexpr int WINDOW_WIDTH = 1200;
constexpr int WINDOW_HEIGHT = 820;
constexpr int MIN_WIDTH = 860;
constexpr int MIN_HEIGHT = 760;

class Window {
public:
    explicit Window(bool fullscreen, const char* title = "Bitzkey | An artificial ecology");
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Canvas frame();  // the surface to draw this frame into, sized to the window
    void present();
    void toggle_fullscreen();
    bool fullscreen() const { return fullscreen_; }
    int width() const { return frame_->w; }
    int height() const { return frame_->h; }
    void handle(const SDL_Event& event);  // follow resizes

private:
    void fit_to_window();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    Surface frame_;
    bool fullscreen_;
};

}  // namespace bitzkey::ui
