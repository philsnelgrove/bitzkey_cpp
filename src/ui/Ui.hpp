#pragma once
// Scaling for text and controls. Screens are laid out for a 1200x820 window; on any other screen
// every length is multiplied by one factor, and fonts are rendered at the scaled size so text stays
// crisp rather than being stretched.

#include <map>
#include <string>
#include <string_view>
#include <tuple>

#include <SDL_ttf.h>

namespace bitzkey::ui {

constexpr int DESIGN_WIDTH = 1200;
constexpr int DESIGN_HEIGHT = 820;
constexpr double MIN_SCALE = 0.8;

enum class Face { Sans, Mono };

class Ui {
public:
    // Scale to the screen: as large as fits both its width and height, never below MIN_SCALE.
    void fit(int width, int height);
    // A length from the 1200x820 design, in pixels at the current scale.
    int operator()(double length) const;
    double scale() const { return scale_; }
    // Text size, on top of the scale, for those who need larger text: 1, 1.25, 1.5 or 2, the next each time.
    double text_size() const { return text_size_; }
    void next_text_size();
    // A font at `size` design pixels, scaled; fonts are cached by their real size.
    // In the chosen language's script, or in `language`'s when one is given.
    TTF_Font* font(Face face, int size, bool bold = false, const std::string& language = "");
    // The same font, smaller if it must be for the text (in the language shown) to fit `width`:
    // down to two thirds of its size. Longer words in other languages fit the places made for English.
    TTF_Font* fitting(TTF_Font* font, std::string_view text, int width);
    void close_fonts();

private:
    using Key = std::tuple<Face, int, bool, std::string>;  // face, pixels, bold, language
    TTF_Font* open(const Key& key);

    double scale_ = 1.0;
    double text_size_ = 1.0;
    std::map<Key, TTF_Font*> fonts_;
    std::map<TTF_Font*, Key> keys_;
};

extern Ui UI;

}  // namespace bitzkey::ui
