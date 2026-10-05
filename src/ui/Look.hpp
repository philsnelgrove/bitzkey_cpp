#pragma once
// How a Bitzkey is drawn: an inner dot in its mind word's colour, sized by its body, inside a
// ring in its body word's colour.

#include <utility>
#include <vector>

#include "core/Genome.hpp"
#include "ui/Canvas.hpp"

namespace bitzkey::ui {

constexpr int RING_WIDTH = 2;
constexpr Color CROSS{225, 55, 55};  // the red cross through a dead Bitzkey
constexpr Color CENTRE{0, 0, 0};     // the black centre behind a species icon
constexpr Color OUTLINE{190, 190, 190};  // the thin light grey outline around a species icon
constexpr int DOT_RADIUS_SMALLEST = 1;
constexpr int DOT_RADIUS_LARGEST = 5;  // bigger cells raise the top
constexpr int ICON_MIN_RADIUS = 3;     // a dot this big or bigger carries its species' icon

inline Color to_color(const Rgb& rgb) {
    return {static_cast<std::uint8_t>(rgb.r), static_cast<std::uint8_t>(rgb.g), static_cast<std::uint8_t>(rgb.b)};
}

// The dot's radius shows the body's size: from the smallest at size 0 to `largest` at the biggest.
int dot_radius(int size, int largest = DOT_RADIUS_LARGEST);

// Where a species icon is copied, in the outline colour, to outline it `width` pixels thick: a pixel
// each way, or around a circle when wider, so its corners are outlined too.
std::vector<std::pair<int, int>> outline_offsets(int width);

// The dot inside its ring. A dot big enough to hold its species' icon is black, with the icon drawn
// in the dot's colour inside a thin light grey outline; a smaller one is simply that colour. A dead Bitzkey is dimmed and crossed
// through in red. With `opacity` below 1, all of it is drawn that faint, as a decaying corpse
// fades away.
void draw_bitzkey(Canvas& canvas, Dna dna, int x, int y, int radius, int ring_width = RING_WIDTH,
                  bool alive = true, double opacity = 1.0);

}  // namespace bitzkey::ui
