#include "ui/Icons.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "core/PyFormat.hpp"

namespace bitzkey::ui {

namespace {

Color toward(Color from, Color to, double share) {  // `share` of the way from one colour to another
    const auto channel = [share](int a, int b) { return static_cast<std::uint8_t>(std::clamp(std::lround(a + (b - a) * share), 0L, 255L)); };
    return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b), from.a};
}

}  // namespace

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
        case Species::Grazer: {  // a leaf: a pointed oval along a slanting midrib, its veins darker, on a short stem
            constexpr double base_x = -0.6, base_y = 0.6, tip_x = 0.8, tip_y = -0.8;
            const double along_x = tip_x - base_x, along_y = tip_y - base_y, length = std::hypot(along_x, along_y);
            const double across_x = -along_y / length, across_y = along_x / length;
            const auto on = [&](double t, double side) {  // a point `t` of the way up the rib, `side` across it
                return at(base_x + along_x * t + across_x * side, base_y + along_y * t + across_y * side);
            };
            const auto width = [](double t) { return 0.46 * std::pow(std::sin(std::numbers::pi * t), 0.75) * (1 - 0.2 * t); };
            constexpr int STEPS = 18;
            std::vector<std::pair<int, int>> edge;
            for (int step = 0; step <= STEPS; ++step) edge.push_back(on(static_cast<double>(step) / STEPS, width(static_cast<double>(step) / STEPS)));
            for (int step = STEPS - 1; step > 0; --step) edge.push_back(on(static_cast<double>(step) / STEPS, -width(static_cast<double>(step) / STEPS)));
            canvas.polygon(edge, colour);
            const Color vein = toward(colour, {0, 0, 0, 255}, 0.38);
            const int fine = std::max(1, size(0.06));
            canvas.line(on(0.02, 0).first, on(0.02, 0).second, on(0.9, 0).first, on(0.9, 0).second, vein, fine);
            for (const double t : {0.25, 0.45, 0.65}) {  // veins up toward the edge, either side of the rib
                for (const double side : {1.0, -1.0}) {
                    const auto from = on(t, 0), to = on(t + 0.14, side * width(t + 0.14) * 0.75);
                    canvas.line(from.first, from.second, to.first, to.second, vein, fine);
                }
            }
            line(at(base_x, base_y), at(-0.95, 0.92), size(0.12));
            break;
        }
        case Species::Omnivore:  // an apple: two lobes, a stem and a leaf
            circle(at(-0.28, 0.22), size(0.58));
            circle(at(0.28, 0.22), size(0.58));
            line(at(0, -0.3), at(0.12, -0.9), size(0.16));
            canvas.polygon({at(0.12, -0.7), at(0.62, -0.95), at(0.4, -0.52)}, colour);
            break;
        case Species::Predator: {  // a drumstick: a plump teardrop of meat narrowing to its bone, and the bone's knob, paler
            constexpr double meat_x = 0.28, meat_y = -0.28, round = 0.56;
            const double to_bone = std::atan2(0.5, -0.5);  // down and left, to the bone
            std::vector<std::pair<int, int>> meat{at(-0.22, 0.22)};
            constexpr int STEPS = 20;
            for (int step = 0; step <= STEPS; ++step) {
                const double angle = to_bone + 0.9 + (2 * std::numbers::pi - 1.8) * step / STEPS;
                meat.push_back(at(meat_x + round * std::cos(angle), meat_y + round * std::sin(angle)));
            }
            const Color bone = toward(colour, {255, 255, 255, 255}, 0.6);
            canvas.line(at(-0.1, 0.1).first, at(-0.1, 0.1).second, at(-0.62, 0.62).first, at(-0.62, 0.62).second, bone, size(0.2));
            canvas.circle(at(-0.8, 0.58).first, at(-0.8, 0.58).second, size(0.17), bone);
            canvas.circle(at(-0.58, 0.8).first, at(-0.58, 0.8).second, size(0.17), bone);
            canvas.polygon(meat, colour);
            const auto shine = at(meat_x + 0.18, meat_y - 0.18);
            canvas.circle(shine.first, shine.second, size(0.13), toward(colour, {255, 255, 255, 255}, 0.35));
            break;
        }
    }
}

}  // namespace bitzkey::ui
