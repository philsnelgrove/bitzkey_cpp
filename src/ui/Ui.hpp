#pragma once
// Scaling for text and controls. Screens are laid out for a 1200x820 window; on any other screen
// every length is multiplied by one factor, and fonts are rendered at the scaled size so text stays
// crisp rather than being stretched.

#include <map>
#include <string>
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
    // A font at `size` design pixels, scaled; fonts are cached by their real size.
    TTF_Font* font(Face face, int size, bool bold = false);
    void close_fonts();

private:
    double scale_ = 1.0;
    std::map<std::tuple<Face, int, bool>, TTF_Font*> fonts_;
};

extern Ui UI;

}  // namespace bitzkey::ui
