#pragma once
// How a Bitzkey is drawn: an inner dot in its mind word's colour, sized by its body, inside a
// ring in its body word's colour.

#include <array>
#include <utility>
#include <vector>

#include "core/Genome.hpp"
#include "ui/Canvas.hpp"

namespace bitzkey::ui {

constexpr int RING_WIDTH = 2;
constexpr Color CROSS{225, 55, 55};  // the red cross through a dead Bitzkey
constexpr Color OUTLINE{190, 190, 190};  // the thin light grey outline around a species icon
constexpr int DOT_RADIUS_SMALLEST = 1;
constexpr int DOT_RADIUS_LARGEST = 5;  // bigger cells raise the top
constexpr int ICON_MIN_RADIUS = 3;     // a dot this big or bigger carries its species' icon
// The world: light grey, each row tinted by its temperature now, from hot to very cold: reddish
// brown, green, white and light blue.
constexpr Color WORLD_GREY{196, 198, 196};
constexpr std::array<Color, 4> ZONE_TINTS = {{{150, 85, 55}, {110, 170, 90}, {255, 255, 255}, {170, 210, 250}}};
constexpr double ZONE_TINT = 0.45;  // how far toward its tint a zone's grey goes
constexpr Color GRASS{62, 122, 52};
// The sea: its row's temperature tint drawn this far toward deep blue, so the climate's bands show through, and its
// grass, sea grass, in kelp green.
constexpr Color SEA{38, 84, 140};
constexpr double SEA_TINT = 0.6;
constexpr Color SEA_GRASS{58, 150, 120};
constexpr Color NIGHT{10, 16, 40, 90};  // the shadow over the night's half of the map
constexpr Color GRID{140, 146, 142};
constexpr Color SELECTED{25, 32, 32};     // the ring round the selected Bitzkey and those picked on the colour graph
constexpr Color EXPORTING{205, 120, 30};  // the ring round Bitzkeys on the export list

// The world's grey tinted for a temperature.
Color zone_colour(double temperature);  // part way between two tints while the weather turns
Color sea_colour(double temperature);   // the same under the sea

inline Color to_color(const Rgb& rgb) {
    return {static_cast<std::uint8_t>(rgb.r), static_cast<std::uint8_t>(rgb.g), static_cast<std::uint8_t>(rgb.b)};
}

// The dot's radius shows the body's size: from the smallest at size 0 to `largest` at the biggest.
int dot_radius(int size, int largest = DOT_RADIUS_LARGEST, int smallest = DOT_RADIUS_SMALLEST);
// The smallest and largest dots on the map, zoomed with it: the largest body's dot, ring and all, fills
// a cell, and the smallest stays big enough for its icon, growing with the largest in proportion.
std::pair<int, int> map_dot_radii(double cell);

// Where a species icon is copied, in the outline colour, to outline it `width` pixels thick: a pixel
// each way, or around a circle when wider, so its corners are outlined too.
std::vector<std::pair<int, int>> outline_offsets(int width);

// The dot inside its ring. A dot big enough to hold its species' icon is black, with the icon drawn
// in the dot's colour inside a thin light grey outline; a smaller one is simply that colour. A dead Bitzkey is dimmed and crossed
// through in red. With `opacity` below 1, all of it is drawn that faint, as a decaying corpse
// fades away.
// The ring round a selected Bitzkey, like a target's sights: a white ring inside a black one, each two
// pixels, so it shows on light ground and dark, with a pip at north, east, south and west.
void draw_reticle(Canvas& canvas, int x, int y, int radius);

void draw_bitzkey(Canvas& canvas, Dna dna, int x, int y, int radius, int ring_width = RING_WIDTH,
                  bool alive = true, double opacity = 1.0);

// A Bitzkey drawn alone, centred on a clear square, to be blitted as faintly as wanted.
Surface bitzkey_layer(Dna dna, int radius, int ring_width = RING_WIDTH, bool alive = true);

// A Bitzkey is drawn as a poker chip: its coloured edge CHIP_EDGE of its radius (at least the ring's width), with
// CHIP_INSERTS lighter inserts round it; the looks drawn are kept, up to CHIPS_KEPT, then drawn afresh.
constexpr double CHIP_EDGE = 0.32;
constexpr int CHIP_INSERTS = 6;
constexpr std::size_t CHIPS_KEPT = 20000;

// A tuft of grass the size of a square, or of kelp in the sea, kept once drawn.
SDL_Surface* grass_tuft(int width, int height, bool sea);

// The ground's texture, laid faintly over a square's colour: by its zone's usual temperature, or the sea.
enum class Ground { Sand, Meadow, Snow, Ice, Sea };
constexpr int GROUND_VARIANTS = 4;
constexpr int TEXTURE_FROM = 6;  // squares this many pixels across or more are textured, and their grass drawn as tufts
SDL_Surface* ground_tile(Ground ground, int width, int height, int variant);

}  // namespace bitzkey::ui
