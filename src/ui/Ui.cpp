#include "ui/Ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include <SDL.h>
#include <SDL_ttf.h>

#include "core/PyFormat.hpp"
#include "ui/Language.hpp"

namespace bitzkey::ui {

Ui UI;

void Ui::fit(int width, int height) {
    scale_ = std::max(MIN_SCALE, std::min(static_cast<double>(width) / DESIGN_WIDTH, static_cast<double>(height) / DESIGN_HEIGHT));
}

int Ui::operator()(double length) const { return static_cast<int>(py_round(length * scale_)); }

namespace {
// The Windows fonts the Python version asks pygame for, Segoe UI and Consolas, and for the scripts
// they lack: Microsoft YaHei for Chinese, Yu Gothic for Japanese, Nirmala UI for Hindi, and
// Courier New for monospaced Hebrew. Their bold faces are drawn by SDL_ttf where none is installed.
std::string font_path(Face face, bool bold, const std::string& language) {
    const char* windows = SDL_getenv("WINDIR");
    const std::string folder = std::string(windows ? windows : "C:\\Windows") + "\\Fonts\\";
    if (language == "zh") return folder + (bold ? "msyhbd.ttc" : "msyh.ttc");
    if (language == "ja") return folder + (bold ? "YuGothB.ttc" : "YuGothM.ttc");
    if (language == "hi") return folder + "Nirmala.ttc";
    if (language == "he" && face == Face::Mono) return folder + (bold ? "courbd.ttf" : "cour.ttf");
    if (face == Face::Mono) return folder + (bold ? "consolab.ttf" : "consola.ttf");
    return folder + (bold ? "segoeuib.ttf" : "segoeui.ttf");
}
}  // namespace

TTF_Font* Ui::font(Face face, int size, bool bold, const std::string& language) {
    const std::string& code = language.empty() ? bitzkey::ui::language() : language;
    const int pixels = std::max(6, static_cast<int>(py_round(size * scale_)));
    return open(std::make_tuple(face, pixels, bold, code));
}

TTF_Font* Ui::fitting(TTF_Font* font, std::string_view text, int width) {
    const auto found = keys_.find(font);
    if (found == keys_.end()) return font;
    const std::string shown = tr(text);
    const AsShown measuring;
    const auto [face, pixels, bold, code] = found->second;
    TTF_Font* chosen = font;
    for (int size = pixels; size >= std::max(6, pixels * 2 / 3); --size) {
        chosen = open(std::make_tuple(face, size, bold, code));
        int w = 0, h = 0;
        TTF_SizeUTF8(chosen, shown.c_str(), &w, &h);
        if (w <= width) break;
    }
    return chosen;
}

TTF_Font* Ui::open(const Key& key) {
    const auto found = fonts_.find(key);
    if (found != fonts_.end()) return found->second;
    const auto& [face, pixels, bold, code] = key;
    TTF_Font* font = TTF_OpenFont(font_path(face, bold, code).c_str(), pixels);
    if (!font) font = TTF_OpenFont(font_path(face, bold, "en").c_str(), pixels);  // the script's font missing: English's
    if (!font) throw std::runtime_error(std::string("Couldn't open a font: ") + TTF_GetError());
    if (code == "hi") {
        TTF_SetFontScriptName(font, "Deva");  // Devanagari, shaped by HarfBuzz
        if (bold) TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    }
    fonts_.emplace(key, font);
    keys_.emplace(font, key);
    return font;
}

void Ui::close_fonts() {
    for (auto& [key, font] : fonts_) TTF_CloseFont(font);
    fonts_.clear();
    keys_.clear();
}

}  // namespace bitzkey::ui
