#include "ui/Look.hpp"

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
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

Color sea_colour(double temperature) {
    const Color zone = zone_colour(temperature);
    const auto blend = [](int from, int to) { return static_cast<std::uint8_t>(py_round(from + (to - from) * SEA_TINT)); };
    return {blend(zone.r, SEA.r), blend(zone.g, SEA.g), blend(zone.b, SEA.b)};
}

int dot_radius(int size, int largest, int smallest) {
    return static_cast<int>(py_round(smallest + static_cast<double>(largest - smallest) * size / TRAIT_MAX));
}

std::pair<int, int> map_dot_radii(double cell) {
    const int largest = std::max(DOT_RADIUS_LARGEST, static_cast<int>(py_round(cell / 2)) - RING_WIDTH);
    const int smallest = std::max(ICON_MIN_RADIUS, static_cast<int>(py_round(static_cast<double>(largest) * ICON_MIN_RADIUS / DOT_RADIUS_LARGEST)));
    return {smallest, largest};
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

void draw_reticle(Canvas& canvas, int x, int y, int radius) {
    canvas.circle(x, y, radius + 2, {0, 0, 0}, 2);
    canvas.circle(x, y, radius, {255, 255, 255}, 2);
    const int reach = radius + 2, length = std::max(3, radius / 2);
    for (const auto& [dx, dy] : {std::pair{0, -1}, std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}}) {
        canvas.line(x + dx * reach, y + dy * reach, x + dx * (reach + length), y + dy * (reach + length), {0, 0, 0}, 4);
        canvas.line(x + dx * reach, y + dy * reach, x + dx * (reach + length), y + dy * (reach + length), {255, 255, 255}, 2);
    }
}

namespace {

// Paint a colour over one pixel of an ARGB surface at a coverage of 0 to 1, as paint laid over what is there.
void paint(SDL_Surface* surface, int x, int y, Color colour, double coverage) {
    if (x < 0 || y < 0 || x >= surface->w || y >= surface->h || coverage <= 0) return;
    auto* pixel = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(surface->pixels) + y * surface->pitch) + x;
    const double alpha = std::min(1.0, coverage) * colour.a / 255.0;
    const double below = ((*pixel >> 24) & 0xFF) / 255.0;
    const double out = alpha + below * (1 - alpha);
    if (out <= 0) return;
    const auto mix = [&](int shift, int value) {
        const double under = (*pixel >> shift) & 0xFF;
        return static_cast<std::uint32_t>(std::lround((value * alpha + under * below * (1 - alpha)) / out)) & 0xFF;
    };
    *pixel = (static_cast<std::uint32_t>(std::lround(out * 255)) << 24) | (mix(16, colour.r) << 16) | (mix(8, colour.g) << 8) | mix(0, colour.b);
}

// A soft line about `width` pixels wide.
void stroke(SDL_Surface* surface, double x1, double y1, double x2, double y2, Color colour, double width = 1.0) {
    const int left = static_cast<int>(std::floor(std::min(x1, x2) - width - 1)), right = static_cast<int>(std::ceil(std::max(x1, x2) + width + 1));
    const int top = static_cast<int>(std::floor(std::min(y1, y2) - width - 1)), bottom = static_cast<int>(std::ceil(std::max(y1, y2) + width + 1));
    const double dx = x2 - x1, dy = y2 - y1, length = dx * dx + dy * dy;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {  // each pixel by its distance from the segment
            const double px = x + 0.5, py = y + 0.5;
            const double t = length > 0 ? std::clamp(((px - x1) * dx + (py - y1) * dy) / length, 0.0, 1.0) : 0.0;
            const double distance = std::hypot(px - (x1 + dx * t), py - (y1 + dy * t));
            paint(surface, x, y, colour, std::clamp(width / 2 + 0.5 - distance, 0.0, 1.0));
        }
    }
}

Color scaled(Color colour, double factor) {
    const auto channel = [factor](int value) { return static_cast<std::uint8_t>(std::clamp(std::lround(value * factor), 0L, 255L)); };
    return {channel(colour.r), channel(colour.g), channel(colour.b), colour.a};
}

Color mixed(Color from, Color to, double share) {
    const auto channel = [share](int a, int b) { return static_cast<std::uint8_t>(std::lround(a + (b - a) * share)); };
    return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b), from.a};
}

// A chip's outer radius, the width of its coloured edge, its face's radius, and how far its shadow falls.
struct Chip {
    int outer, edge;
    double face, drop;
};
Chip chip_of(int radius, int ring_width) {
    const int outer = radius + ring_width;
    const int edge = std::max(ring_width, static_cast<int>(std::lround(outer * CHIP_EDGE)));
    return {outer, edge, static_cast<double>(outer - edge), std::max(1.0, outer * 0.14)};
}

}  // namespace

Surface bitzkey_layer(Dna dna, int radius, int ring_width, bool alive) {
    // A poker chip, lit from the top left: its edge in the ring's colour with lighter inserts, its face the social
    // background's (or the dot's, too small for an icon) carrying the species icon in the dot's, a shine on the top
    // left and a soft shadow below to the right.
    const Chip chip = chip_of(radius, ring_width);
    const int reach = chip.outer + static_cast<int>(std::ceil(chip.drop)) + 2;
    Surface layer = make_surface(2 * reach + 1, 2 * reach + 1);
    SDL_Surface* target = layer.get();
    Color dot = to_color(color_from_word(mind_word(dna))), ring = to_color(color_from_word(body_word(dna)));
    Color social = to_color(color_from_word(social_word(dna))), outline = OUTLINE;
    if (!alive) {
        for (Color* colour : {&dot, &ring, &outline, &social}) *colour = scaled(*colour, 1.0 / 3);
    }
    const bool icon = radius >= ICON_MIN_RADIUS;
    const Color face = icon ? social : dot, insert = mixed(ring, {255, 255, 255}, 0.55);
    const double centre = reach + 0.5, outer = chip.outer;
    SDL_LockSurface(target);
    for (int y = 0; y < target->h; ++y) {  // the shadow, softened at its edge
        for (int x = 0; x < target->w; ++x) {
            const double distance = std::hypot(x + 0.5 - centre - chip.drop, y + 0.5 - centre - chip.drop);
            paint(target, x, y, {0, 0, 0, 255}, 0.38 * std::clamp((outer + 1 - distance) / (outer * 0.35 + 1), 0.0, 1.0));
        }
    }
    for (int y = 0; y < target->h; ++y) {  // the chip
        for (int x = 0; x < target->w; ++x) {
            const double dx = x + 0.5 - centre, dy = y + 0.5 - centre, distance = std::hypot(dx, dy);
            const double coverage = std::clamp(outer + 0.5 - distance, 0.0, 1.0);
            if (coverage <= 0) continue;
            Color colour = face;
            if (distance > chip.face) {
                const double turn = (std::atan2(dy, dx) + std::numbers::pi) / (2 * std::numbers::pi) * CHIP_INSERTS;
                colour = turn - std::floor(turn) < 0.4 && outer >= 5 ? insert : ring;
            }
            if (std::abs(distance - chip.face) < 0.7 && outer >= 5) colour = scaled(colour, 0.7);  // the groove round the face
            const double light = (-dx - dy) / (outer * std::numbers::sqrt2);  // toward the top left
            const double rim = std::clamp((distance - (outer - 1.5)) / 1.5, 0.0, 1.0);
            paint(target, x, y, scaled(colour, (1 + 0.2 * light) * (1 - 0.25 * rim)), coverage);
        }
    }
    SDL_UnlockSurface(target);
    if (icon) {
        Canvas canvas(target);
        const auto species = static_cast<Species>(decode_body(dna)[SpeciesTrait]);
        const double half = chip.face * 0.82;
        for (const auto& [dx, dy] : outline_offsets(std::max(1, static_cast<int>(chip.face / 6)))) {
            draw_icon(canvas, species, reach + dx, reach + dy, half, outline);
        }
        draw_icon(canvas, species, reach, reach, half, dot);
    }
    SDL_LockSurface(target);
    const double shine_x = centre - outer * 0.35, shine_y = centre - outer * 0.42, shine = outer * 0.5;
    for (int y = 0; y < target->h; ++y) {  // the shine, only on the chip
        for (int x = 0; x < target->w; ++x) {
            const double distance = std::hypot(x + 0.5 - centre, y + 0.5 - centre);
            const double off = std::hypot((x + 0.5 - shine_x) / 1.25, y + 0.5 - shine_y) / shine;
            if (off < 1) {
                paint(target, x, y, {255, 255, 255, 255},
                      (alive ? 0.42 : 0.15) * (1 - off * off) * std::clamp(outer - 0.5 - distance, 0.0, 1.0));
            }
        }
    }
    SDL_UnlockSurface(target);
    if (!alive) {
        Canvas canvas(target);
        const int width = std::max(2, chip.outer / 3);
        canvas.line(reach - chip.outer, reach - chip.outer, reach + chip.outer, reach + chip.outer, CROSS, width);
        canvas.line(reach - chip.outer, reach + chip.outer, reach + chip.outer, reach - chip.outer, CROSS, width);
    }
    return layer;
}

void draw_bitzkey(Canvas& canvas, Dna dna, int x, int y, int radius, int ring_width, bool alive, double opacity) {
    if (opacity <= 0) return;
    // Each look drawn once and kept (see LOOKS_KEPT): far fewer than a world's genomes, which nearly all differ.
    using Key = std::tuple<std::uint64_t, int, int, bool>;
    struct Kept {
        Surface layer;
        std::list<Key>::iterator used;  // its place in the order last drawn
    };
    static std::map<Key, Kept> kept;
    static std::list<Key> order;  // the most recently drawn first
    std::uint64_t look = static_cast<std::uint64_t>(decode_body(dna)[SpeciesTrait]);
    for (const std::uint32_t word : {mind_word(dna), body_word(dna), social_word(dna)}) {
        const Color colour = to_color(color_from_word(word));
        for (const int channel : {colour.r, colour.g, colour.b}) look = look << 5 | static_cast<std::uint64_t>(channel / LOOK_SHADES);
    }
    const Key key{look, radius, ring_width, alive};
    auto found = kept.find(key);
    if (found != kept.end()) {
        order.splice(order.begin(), order, found->second.used);
    } else {
        if (kept.size() >= LOOKS_KEPT) {
            kept.erase(order.back());
            order.pop_back();
        }
        order.push_front(key);
        found = kept.emplace(key, Kept{bitzkey_layer(dna, radius, ring_width, alive), order.begin()}).first;
    }
    SDL_Surface* layer = found->second.layer.get();
    const int reach = layer->w / 2;
    canvas.blit(layer, x - reach, y - reach, static_cast<std::uint8_t>(py_round(255 * std::min(1.0, opacity))));
}

SDL_Surface* grass_tuft(int width, int height, bool sea) {
    static std::map<std::tuple<int, int, bool>, Surface> kept;
    auto [found, added] = kept.try_emplace(std::tuple{width, height, sea});
    if (!added) return found->second.get();
    Surface tuft = make_surface(width, height);
    SDL_Surface* target = tuft.get();
    SDL_LockSurface(target);
    const double base_x = width * 0.5, base_y = height * 0.8, tall = height * 0.55, thick = std::max(1.0, width / 12.0);
    if (sea) {  // kelp: a few swaying strands, darker ones behind a light one
        for (int strand = 0; strand < 3; ++strand) {
            const double x0 = base_x + (strand - 1) * width * 0.16;
            const Color colour = strand == 1 ? SEA_GRASS : scaled(SEA_GRASS, 0.7);
            double px = x0, py = base_y;
            for (int step = 1; step <= 8; ++step) {
                const double t = step / 8.0;
                const double nx = x0 + std::sin(t * 5.5 + strand) * width * 0.08, ny = base_y - t * tall * (strand == 1 ? 1.0 : 0.8);
                stroke(target, px, py, nx, ny, colour, thick);
                px = nx;
                py = ny;
            }
        }
    } else {  // blades fanning from a clump, dark beneath and light at their tips
        constexpr std::array<double, 5> lean = {-0.55, -0.25, 0.0, 0.3, 0.6}, height_share = {0.7, 0.95, 1.0, 0.85, 0.65};
        for (std::size_t blade = 0; blade < lean.size(); ++blade) {
            const double tip_x = base_x + lean[blade] * width * 0.42, tip_y = base_y - tall * height_share[blade];
            const double mid_x = base_x + lean[blade] * width * 0.12, mid_y = base_y - tall * height_share[blade] * 0.55;
            stroke(target, base_x + (static_cast<double>(blade) - 2.0) * thick * 0.6, base_y, mid_x, mid_y, scaled(GRASS, 0.75), thick * 1.3);
            stroke(target, mid_x, mid_y, tip_x, tip_y, mixed(GRASS, {170, 220, 120}, 0.45), thick);
        }
    }
    SDL_UnlockSurface(target);
    found->second = std::move(tuft);
    return found->second.get();
}

SDL_Surface* ground_tile(Ground ground, int width, int height, int variant) {
    // A faint texture laid over a square's colour, so the season's tint still shows through: one of a few variants, so
    // the pattern doesn't repeat.
    static std::map<std::tuple<int, int, int, int>, Surface> kept;
    auto [found, added] = kept.try_emplace(std::tuple{static_cast<int>(ground), width, height, variant});
    if (!added) return found->second.get();
    Surface tile = make_surface(width, height);
    SDL_Surface* target = tile.get();
    std::uint32_t state = 2654435761U * static_cast<std::uint32_t>(variant * 7 + static_cast<int>(ground) * 131 + 1);
    const auto next = [&state] {  // the same texture every time
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (state & 0xFFFFFF) / static_cast<double>(0x1000000);
    };
    const auto speck = [&](Color colour, double coverage) {
        paint(target, static_cast<int>(next() * width), static_cast<int>(next() * height), colour, coverage);
    };
    SDL_LockSurface(target);
    const double w = width, h = height;
    switch (ground) {
        case Ground::Sand:  // ripples drifted by the wind, and grit
            for (int ripple = 0; ripple < 2; ++ripple) {
                const double y0 = h * (0.3 + 0.4 * ripple + 0.1 * next()), x0 = w * 0.1 * next();
                double px = x0, py = y0;
                for (int step = 1; step <= 6; ++step) {
                    const double nx = x0 + step * w * 0.13, ny = y0 + std::sin(step * 1.1 + ripple) * h * 0.05;
                    stroke(target, px, py, nx, ny, {255, 240, 205, 75});
                    px = nx;
                    py = ny;
                }
            }
            for (int grit = 0; grit < 4; ++grit) speck({110, 75, 40, 255}, 0.3);
            break;
        case Ground::Meadow:  // short darker strokes, and pale flecks
            for (int mark = 0; mark < 4; ++mark) {
                const double x0 = next() * w, y0 = next() * h;
                stroke(target, x0, y0, x0 + (next() - 0.5) * w * 0.12, y0 - h * 0.12, {45, 85, 35, 75});
            }
            for (int fleck = 0; fleck < 3; ++fleck) speck({215, 235, 160, 255}, 0.3);
            break;
        case Ground::Snow: {  // a drift's blue shadow, and sparkle
            const double x0 = w * (0.1 + 0.3 * next()), y0 = h * (0.4 + 0.4 * next());
            stroke(target, x0, y0, x0 + w * 0.5, y0 - h * 0.08, {150, 175, 210, 90}, std::max(1.0, w / 10));
            for (int flake = 0; flake < 6; ++flake) speck(flake % 2 ? Color{255, 255, 255, 255} : Color{160, 190, 225, 255}, 0.6);
            break;
        }
        case Ground::Ice: {  // cracks across the ice, and a glint
            double px = next() * w, py = next() * h;
            for (int step = 0; step < 3; ++step) {
                const double nx = px + (next() - 0.4) * w * 0.4, ny = py + (next() - 0.4) * h * 0.4;
                stroke(target, px, py, nx, ny, {245, 252, 255, 100});
                px = nx;
                py = ny;
            }
            const int gx = static_cast<int>(next() * w), gy = static_cast<int>(next() * h);
            for (const auto& [dx, dy] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                paint(target, gx + dx, gy + dy, {255, 255, 255, 255}, dx || dy ? 0.35 : 0.8);
            }
            break;
        }
        case Ground::Sea: {  // wave crests, light over the deep
            const int crests = 1 + static_cast<int>(next() * 2);
            for (int crest = 0; crest < crests; ++crest) {
                const double cx = w * (0.2 + 0.6 * next()), cy = h * (0.25 + 0.5 * next()), span = w * 0.22;
                double px = cx - span, py = cy;
                for (int step = 1; step <= 6; ++step) {
                    const double t = step / 6.0, nx = cx - span + 2 * span * t, ny = cy - std::sin(t * std::numbers::pi) * h * 0.08;
                    stroke(target, px, py, nx, ny, {225, 238, 255, 85});
                    px = nx;
                    py = ny;
                }
            }
            break;
        }
    }
    SDL_UnlockSurface(target);
    found->second = std::move(tile);
    return found->second.get();
}

}  // namespace bitzkey::ui
