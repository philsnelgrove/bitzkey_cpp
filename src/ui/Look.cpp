#include "ui/Look.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

#include "core/PyFormat.hpp"
#include "ui/Icons.hpp"

namespace bitzkey::ui {

Color zone_colour(double temperature) {
    const int top = static_cast<int>(ZONE_TINTS.size()) - 1;
    const int lower = std::min(static_cast<int>(temperature), top);
    const int upper = std::min(lower + 1, top);
    const double turned = temperature - lower;
    const Color& low = ZONE_TINTS[static_cast<std::size_t>(lower)];
    const Color& high = ZONE_TINTS[static_cast<std::size_t>(upper)];
    const auto blend = [turned](int grey, int from, int to) {
        const double tint = from + (to - from) * turned;
        return static_cast<std::uint8_t>(py_round(grey + (tint - grey) * ZONE_TINT));
    };
    return {blend(WORLD_GREY.r, low.r, high.r), blend(WORLD_GREY.g, low.g, high.g), blend(WORLD_GREY.b, low.b, high.b)};
}

int dot_radius(int size, int largest) {
    return static_cast<int>(py_round(DOT_RADIUS_SMALLEST + static_cast<double>(largest - DOT_RADIUS_SMALLEST) * size / TRAIT_MAX));
}

std::vector<std::pair<int, int>> outline_offsets(int width) {
    if (width <= 1) return {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    std::set<std::pair<int, int>> offsets;
    for (int step = 0; step < 16; ++step) {
        const double angle = step * std::numbers::pi / 8;
        offsets.emplace(static_cast<int>(py_round(width * std::cos(angle))), static_cast<int>(py_round(width * std::sin(angle))));
    }
    return {offsets.begin(), offsets.end()};
}

Surface bitzkey_layer(Dna dna, int radius, int ring_width, bool alive) {
    const int reach = radius + ring_width + 1;
    Surface layer = make_surface(2 * reach + 1, 2 * reach + 1);
    Canvas faint(layer.get());
    draw_bitzkey(faint, dna, reach, reach, radius, ring_width, alive);
    return layer;
}

void draw_bitzkey(Canvas& canvas, Dna dna, int x, int y, int radius, int ring_width, bool alive, double opacity) {
    if (opacity <= 0) return;
    if (opacity < 1) {
        const Surface layer = bitzkey_layer(dna, radius, ring_width, alive);
        const int reach = layer->w / 2;
        canvas.blit(layer.get(), x - reach, y - reach, static_cast<std::uint8_t>(py_round(255 * opacity)));
        return;
    }
    Color dot = to_color(color_from_word(mind_word(dna))), ring = to_color(color_from_word(body_word(dna))), outline = OUTLINE;
    if (!alive) {
        for (Color* colour : {&dot, &ring, &outline}) {
            *colour = {static_cast<std::uint8_t>(colour->r / 3), static_cast<std::uint8_t>(colour->g / 3),
                       static_cast<std::uint8_t>(colour->b / 3)};
        }
    }
    canvas.circle(x, y, radius + ring_width, ring);
    if (radius >= ICON_MIN_RADIUS) {
        canvas.circle(x, y, radius, CENTRE);
        const auto species = static_cast<Species>(decode_body(dna)[SpeciesTrait]);
        for (const auto& [dx, dy] : outline_offsets(std::max(1, ring_width / RING_WIDTH))) {  // as thick as the ring is enlarged
            draw_icon(canvas, species, x + dx, y + dy, radius * 0.8, outline);
        }
        draw_icon(canvas, species, x, y, radius * 0.8, dot);
    } else {
        canvas.circle(x, y, radius, dot);
    }
    if (!alive) {
        const int reach = radius + ring_width;
        const int width = std::max(2, reach / 3);
        canvas.line(x - reach, y - reach, x + reach, y + reach, CROSS, width);
        canvas.line(x - reach, y + reach, x + reach, y - reach, CROSS, width);
    }
}

}  // namespace bitzkey::ui
