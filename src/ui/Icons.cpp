#include "ui/Icons.hpp"

#include <algorithm>

#include "core/PyFormat.hpp"

namespace bitzkey::ui {

Color icon_colour(Color background) {
    const double light = 0.299 * background.r + 0.587 * background.g + 0.114 * background.b;
    return light > 140 ? Color{20, 26, 27} : Color{238, 238, 220};
}

void draw_icon(Canvas& canvas, Species species, int cx, int cy, double half, Color colour) {
    const auto at = [&](double x, double y) {
        return std::pair{static_cast<int>(py_round(cx + x * half)), static_cast<int>(py_round(cy + y * half))};
    };
    const auto size = [&](double length) { return std::max(1, static_cast<int>(py_round(length * half))); };
    const auto line = [&](std::pair<int, int> a, std::pair<int, int> b, int width) {
        canvas.line(a.first, a.second, b.first, b.second, colour, width);
    };
    const auto circle = [&](std::pair<int, int> centre, int radius) { canvas.circle(centre.first, centre.second, radius, colour); };

    switch (species) {
        case Species::Plant: {  // a tree: a canopy over a trunk
            canvas.polygon({at(0, -1), at(0.85, 0.35), at(-0.85, 0.35)}, colour);
            const auto corner = at(-0.18, 0.3);
            canvas.fill_rect({corner.first, corner.second, size(0.36), size(0.7)}, colour);
            break;
        }
        case Species::Grazer:  // a leaf, pointing up and right, on a short stem
            canvas.polygon({at(-0.65, 0.65), at(-0.62, -0.05), at(-0.2, -0.58), at(0.8, -0.8), at(0.58, 0.2), at(0.05, 0.62)},
                           colour);
            line(at(-0.6, 0.6), at(-0.95, 0.95), size(0.18));
            break;
        case Species::Omnivore:  // an apple: two lobes, a stem and a leaf
            circle(at(-0.28, 0.22), size(0.58));
            circle(at(0.28, 0.22), size(0.58));
            line(at(0, -0.3), at(0.12, -0.9), size(0.16));
            canvas.polygon({at(0.12, -0.7), at(0.62, -0.95), at(0.4, -0.52)}, colour);
            break;
        case Species::Predator:  // a drumstick: the meat, a bone and its knuckle
            circle(at(0.25, -0.25), size(0.6));
            line(at(-0.05, 0.05), at(-0.62, 0.62), size(0.26));
            circle(at(-0.8, 0.6), size(0.2));
            circle(at(-0.6, 0.8), size(0.2));
            break;
    }
}

}  // namespace bitzkey::ui
