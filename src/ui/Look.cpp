#include "ui/Look.hpp"

#include <algorithm>

#include "core/PyFormat.hpp"
#include "ui/Icons.hpp"

namespace bitzkey::ui {

int dot_radius(int size, int largest) {
    return static_cast<int>(py_round(DOT_RADIUS_SMALLEST + static_cast<double>(largest - DOT_RADIUS_SMALLEST) * size / TRAIT_MAX));
}

void draw_bitzkey(Canvas& canvas, Dna dna, int x, int y, int radius, int ring_width, bool alive, double opacity) {
    if (opacity <= 0) return;
    if (opacity < 1) {
        const int reach = radius + ring_width + 1;
        Surface layer = make_surface(2 * reach + 1, 2 * reach + 1);
        Canvas faint(layer.get());
        draw_bitzkey(faint, dna, reach, reach, radius, ring_width, alive);
        canvas.blit(layer.get(), x - reach, y - reach, static_cast<std::uint8_t>(py_round(255 * opacity)));
        return;
    }
    Color dot = to_color(color_from_word(mind_word(dna))), ring = to_color(color_from_word(body_word(dna)));
    if (!alive) {
        dot = {static_cast<std::uint8_t>(dot.r / 3), static_cast<std::uint8_t>(dot.g / 3), static_cast<std::uint8_t>(dot.b / 3)};
        ring = {static_cast<std::uint8_t>(ring.r / 3), static_cast<std::uint8_t>(ring.g / 3), static_cast<std::uint8_t>(ring.b / 3)};
    }
    canvas.circle(x, y, radius + ring_width, ring);
    canvas.circle(x, y, radius, dot);
    if (alive && radius >= ICON_MIN_RADIUS) {
        draw_icon(canvas, static_cast<Species>(decode_body(dna)[SpeciesTrait]), x, y, radius * 0.75, icon_colour(dot));
    }
    if (!alive) {
        const int reach = radius + ring_width;
        const int width = std::max(2, reach / 3);
        canvas.line(x - reach, y - reach, x + reach, y + reach, CROSS, width);
        canvas.line(x - reach, y + reach, x + reach, y - reach, CROSS, width);
    }
}

}  // namespace bitzkey::ui
