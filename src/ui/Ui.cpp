#include "ui/Ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include <SDL.h>
#include <SDL_ttf.h>

#include "core/PyFormat.hpp"

namespace bitzkey::ui {

Ui UI;

void Ui::fit(int width, int height) {
    scale_ = std::max(MIN_SCALE, std::min(static_cast<double>(width) / DESIGN_WIDTH, static_cast<double>(height) / DESIGN_HEIGHT));
}

int Ui::operator()(double length) const { return static_cast<int>(py_round(length * scale_)); }

namespace {
std::string font_path(Face face, bool bold) {
    // The Windows fonts the Python version asks pygame for: Segoe UI and Consolas.
    const char* windows = SDL_getenv("WINDIR");
    const std::string folder = std::string(windows ? windows : "C:\\Windows") + "\\Fonts\\";
    if (face == Face::Mono) return folder + (bold ? "consolab.ttf" : "consola.ttf");
    return folder + (bold ? "segoeuib.ttf" : "segoeui.ttf");
}
}  // namespace

TTF_Font* Ui::font(Face face, int size, bool bold) {
    const int pixels = std::max(6, static_cast<int>(py_round(size * scale_)));
    const auto key = std::make_tuple(face, pixels, bold);
    const auto found = fonts_.find(key);
    if (found != fonts_.end()) return found->second;
    TTF_Font* font = TTF_OpenFont(font_path(face, bold).c_str(), pixels);
    if (!font) throw std::runtime_error(std::string("Couldn't open a font: ") + TTF_GetError());
    fonts_.emplace(key, font);
    return font;
}

void Ui::close_fonts() {
    for (auto& [key, font] : fonts_) TTF_CloseFont(font);
    fonts_.clear();
}

}  // namespace bitzkey::ui
