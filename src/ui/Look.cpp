#include "ui/Look.hpp"

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <numbers>
#include <set>
#include <tuple>
#include <vector>

#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"
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

// A filled shape, its edge softened by sampling each pixel four by four.
void fill_shape(SDL_Surface* surface, const std::vector<std::pair<double, double>>& corners, Color colour) {
    if (corners.size() < 3) return;
    double left = corners[0].first, right = left, top = corners[0].second, bottom = top;
    for (const auto& [x, y] : corners) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    }
    const auto inside = [&](double px, double py) {
        bool in = false;
        for (std::size_t index = 0, last = corners.size() - 1; index < corners.size(); last = index++) {
            const auto [x1, y1] = corners[index];
            const auto [x2, y2] = corners[last];
            if ((y1 > py) != (y2 > py) && px < x1 + (py - y1) * (x2 - x1) / (y2 - y1)) in = !in;
        }
        return in;
    };
    for (int y = static_cast<int>(std::floor(top)); y <= static_cast<int>(std::ceil(bottom)); ++y) {
        for (int x = static_cast<int>(std::floor(left)); x <= static_cast<int>(std::ceil(right)); ++x) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) hits += inside(x + (sx + 0.5) / 4, y + (sy + 0.5) / 4);
            }
            paint(surface, x, y, colour, hits / 16.0);
        }
    }
}

// A filled oval, crisp at its edge, or with `soft` above 0 fading out over that share of its reach.
void fill_oval(SDL_Surface* surface, double cx, double cy, double rx, double ry, Color colour, double soft = 0.0) {
    if (rx <= 0 || ry <= 0) return;
    for (int y = static_cast<int>(std::floor(cy - ry - 1)); y <= static_cast<int>(std::ceil(cy + ry + 1)); ++y) {
        for (int x = static_cast<int>(std::floor(cx - rx - 1)); x <= static_cast<int>(std::ceil(cx + rx + 1)); ++x) {
            const double off = std::hypot((x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry);
            const double coverage = soft > 0 ? std::clamp((1 - off) / soft, 0.0, 1.0) : std::clamp((1 - off) * std::min(rx, ry) + 0.5, 0.0, 1.0);
            paint(surface, x, y, colour, coverage);
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
    // Its shadow, cast as if it floated a little above the ground: a soft oval below it, wider than tall.
    const double shadow_x = chip.outer * 0.12, shadow_y = chip.outer * 0.78, shadow_w = chip.outer * 1.05, shadow_h = chip.outer * 0.42;
    const int reach = static_cast<int>(std::ceil(std::max(chip.outer + chip.drop, std::max(shadow_y + shadow_h, shadow_x + shadow_w)))) + 2;
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
    for (int y = 0; y < target->h; ++y) {  // the shadow, darkest at its middle and fading out to its edge
        for (int x = 0; x < target->w; ++x) {
            const double off = std::hypot((x + 0.5 - centre - shadow_x) / shadow_w, (y + 0.5 - centre - shadow_y) / shadow_h);
            if (off < 1) paint(target, x, y, {0, 0, 0, 255}, (alive ? 0.42 : 0.2) * (1 - off * off) * (1 - off * off));
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

void draw_pile(Canvas& canvas, bool meat, int count, int cx, int cy, int half) {
    const int lump = std::max(1, half * 3 / 5);
    const Color dark = meat ? Color{120, 34, 38} : Color{176, 92, 24}, light = meat ? Color{190, 62, 62} : Color{230, 150, 46};
    canvas.circle(cx - lump, cy + lump / 2, lump, dark);
    canvas.circle(cx + lump, cy + lump / 2, lump, dark);
    canvas.circle(cx, cy - lump / 2, lump, light);
    if (meat) canvas.circle(cx, cy - lump / 2, std::max(1, lump / 2), {236, 226, 205});  // the bone
    else canvas.circle(cx + lump / 2, cy - lump - lump / 2, std::max(1, lump / 3), {74, 140, 60});  // a leaf
    if (half < 5) return;
    TTF_Font* font = UI.font(Face::Sans, 11);
    const std::string number = std::to_string(count);
    canvas.text(font, number, {0, 0, 0}, cx + 1, cy + 1);
    canvas.text(font, number, {255, 255, 255}, cx, cy);
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

namespace bitzkey::ui {

void draw_gear(Canvas& canvas, int wields, int wears, int x, int y, int radius) {
    const int size = std::max(2, radius * 3 / 5);
    if (wears) {  // a shield: flat across the top, narrowing to a point below
        const int cx = x - radius, cy = y + radius * 3 / 5;
        const Color colour = wears == 1 ? Color{236, 226, 205} : Color{122, 80, 44};
        canvas.polygon({{cx - size, cy - size}, {cx + size, cy - size}, {cx + size, cy}, {cx, cy + size}, {cx - size, cy}}, colour);
    }
    if (!wields) return;
    const int cx = x + radius, cy = y + radius * 3 / 5;  // at its lower right, the upper right its herd's flag's
    const Color colour = wields == 2 ? Color{122, 80, 44} : wields == 1 ? Color{236, 226, 205} : Color{136, 138, 142};
    // A point along the tool's handle, from its foot (-1) to its tip (1), and across it.
    const auto at = [&](double along, double across) {
        return std::pair<int, int>{cx + static_cast<int>(std::lround((along + across) * size)), cy + static_cast<int>(std::lround((across - along) * size))};
    };
    if (wields == 4) {  // a stone carried: a pebble
        canvas.circle(cx, cy, std::max(2, size * 2 / 3), colour);
    } else if (wields == 2) {  // a club: thin at the grip, heavy at the head
        canvas.polygon({at(-1, 0.12), at(1, 0.4), at(1, -0.4), at(-1, -0.12)}, colour);
        const auto head = at(1, 0);
        canvas.circle(head.first, head.second, std::max(2, size * 3 / 5), colour);
    } else {  // an axe, of bone or stone: a handle, and a blade at its tip
        const auto foot = at(-1, 0), neck = at(0.9, 0);
        canvas.line(foot.first, foot.second, neck.first, neck.second, colour, std::max(1, size / 2));
        canvas.polygon({at(0.45, 0), at(1, 0), at(1.15, -0.75), at(0.3, -0.75)}, colour);  // the blade flares from the haft
    }
}

void draw_hide(Canvas& canvas, int hide, int x, int y, int radius) {
    if (hide <= 0) return;
    // A stretched pelt below it: four legs and a body, in the colour of the animal it came from, darker at its edge.
    static constexpr std::array<Color, 4> PELTS = {{{0, 0, 0}, {201, 176, 128}, {139, 98, 62}, {118, 120, 128}}};
    const Color pelt = PELTS[static_cast<std::size_t>(std::clamp(hide, 1, 3))], edge = scaled(pelt, 0.6);
    const int w = std::max(3, radius / 2), h = std::max(2, radius * 3 / 8), cy = y + radius + h / 3;
    const auto shape = [&](int grow) {
        return std::vector<std::pair<int, int>>{{x - w / 2 - grow, cy - h - grow}, {x - w / 4, cy - h / 2}, {x + w / 4, cy - h / 2},
                                                {x + w / 2 + grow, cy - h - grow}, {x + w / 2 - w / 6, cy}, {x + w / 2 + grow, cy + h + grow},
                                                {x + w / 4, cy + h / 2}, {x - w / 4, cy + h / 2}, {x - w / 2 - grow, cy + h + grow},
                                                {x - w / 2 + w / 6, cy}};
    };
    canvas.polygon(shape(1), edge);
    canvas.polygon(shape(0), pelt);
    if (hide == 3) {  // a predator's, striped
        for (const int dx : {-w / 4, 0, w / 4}) canvas.line(x + dx, cy - h / 2, x + dx, cy + h / 2, edge, std::max(1, w / 10));
    }
}

Rect flag_box(int x, int y, int radius) {
    const int size = std::max(4, radius * 3 / 5);
    return Rect{x + radius - size / 4, y - radius - size, size + size / 2, size + size / 2};
}

void draw_flag(Canvas& canvas, Color colour, int x, int y, int radius) {
    // A pole at its upper right and a pennant flying from it, outlined to stand out on any ground.
    const Rect box = flag_box(x, y, radius);
    const int pole_x = box.x + 1, top = box.y, foot = box.bottom(), size = box.h * 2 / 3;
    canvas.line(pole_x, top, pole_x, foot, Color{24, 30, 30}, std::max(2, size / 4));
    canvas.line(pole_x, top, pole_x, foot, {222, 214, 196}, std::max(1, size / 8));
    const std::vector<std::pair<int, int>> pennant{{pole_x, top}, {pole_x + size, top + size / 3}, {pole_x, top + size * 2 / 3}};
    canvas.polygon({{pole_x - 1, top - 1}, {pole_x + size + 2, top + size / 3}, {pole_x - 1, top + size * 2 / 3 + 1}}, Color{24, 30, 30});
    canvas.polygon(pennant, colour);
    canvas.polygon({{pole_x, top}, {pole_x + size, top + size / 3}, {pole_x, top + size / 3}}, mixed(colour, {255, 255, 255}, 0.3));  // lit above
}

void draw_wagon(Canvas& canvas, int x, int y, int size) {
    // A box on two wheels, its planks marked, as it stands beside one who drags it.
    const Color wood{150, 104, 62}, dark{96, 64, 38}, wheel{70, 54, 40};
    const int w = std::max(8, size), h = std::max(4, size * 2 / 5), left = x - w / 2, top = y - h / 2 - size / 8;
    canvas.rect({left - 1, top - 1, w + 2, h + 2}, Color{24, 30, 30});
    canvas.rect({left, top, w, h}, wood);
    for (int plank = 1; plank < 3; ++plank) canvas.line(left + 1, top + plank * h / 3, left + w - 2, top + plank * h / 3, dark);
    const int r = std::max(2, size / 5);
    for (const int wx : {left + w / 4, left + w * 3 / 4}) {
        canvas.circle(wx, top + h + r / 2, r + 1, Color{24, 30, 30});
        canvas.circle(wx, top + h + r / 2, r, wheel);
        canvas.circle(wx, top + h + r / 2, std::max(1, r / 3), {196, 176, 140});
    }
    canvas.line(left + w, top + h / 2, left + w + size / 3, top + h / 2 - size / 6, dark, std::max(1, size / 10));  // its shaft
}

void draw_crown(Canvas& canvas, int x, int y, int radius, int rank) {
    const int half = std::max(3, radius * 3 / 4), high = std::max(3, radius * 3 / 4);
    const int foot = y - radius - RING_WIDTH - 1;  // its band just clear of the ring
    // Shaded gold: an outline, the body in a deeper gold, its left lit, a bright band along its foot with a jewel.
    const auto points = [&](int grow) {
        return std::vector<std::pair<int, int>>{{x - half - grow, foot + grow}, {x + half + grow, foot + grow}, {x + half + grow, foot - high - grow},
                                                {x + half / 2, foot - high / 2 - grow}, {x, foot - high - grow}, {x - half / 2, foot - high / 2 - grow},
                                                {x - half - grow, foot - high - grow}};
    };
    canvas.polygon(points(1), Color{92, 64, 14});
    canvas.polygon(points(0), Color{206, 152, 36});
    canvas.polygon({{x - half, foot}, {x, foot}, {x, foot - high}, {x - half / 2, foot - high / 2}, {x - half, foot - high}},
                   Color{246, 204, 82});  // the lit side
    const int band = std::max(1, high / 4);
    canvas.fill_rect({x - half, foot - band, 2 * half + 1, band + 1}, Color{252, 226, 128});
    for (const int tip : {x - half, x, x + half}) canvas.circle(tip, foot - high, std::max(1, half / 5), Color{255, 240, 170});  // its points' pearls
    canvas.circle(x, foot - band / 2, std::max(1, half / 4), Color{196, 40, 52});  // a jewel
    // A small star for each rank, in a row above it: a chief one, a king six.
    const int stars = std::clamp(rank, 0, MAX_RANK), outer = std::max(2, radius / 3), inner = std::max(1, outer * 2 / 5);
    const int cy = foot - high - outer - 2, step = 2 * outer + std::max(1, outer / 2);
    for (int star = 0; star < stars; ++star) {
        const int cx = x + (2 * star - (stars - 1)) * step / 2;
        const auto at = [&](int point) {  // its ten points, out and in by turns, the first straight up
            const double angle = -std::numbers::pi / 2 + point * std::numbers::pi / 5;
            const int reach = point % 2 ? inner : outer;
            return std::pair<int, int>{cx + static_cast<int>(std::lround(reach * std::cos(angle))), cy + static_cast<int>(std::lround(reach * std::sin(angle)))};
        };
        canvas.polygon({at(0), at(1), at(2), at(3), at(4), at(5), at(6), at(7), at(8), at(9)}, Color{250, 228, 140});
    }
}

void draw_materials(Canvas& canvas, int stone, int bone, int wood, int cx, int cy, int half) {
    const int size = std::max(2, half / 2);
    const std::array<std::pair<int, Color>, 3> marks{{{stone, Color{136, 138, 142}}, {bone, Color{236, 226, 205}}, {wood, Color{122, 80, 44}}}};
    for (int index = 0; index < 3; ++index) {
        if (marks[index].first) canvas.fill_rect({cx + (index - 1) * (size + half / 2) - size / 2, cy + half, size, size}, marks[index].second);
    }
}

namespace {

constexpr Color BARK{104, 66, 36}, HEARTWOOD{214, 172, 112}, RINGS{150, 104, 58};

// A log lying from (x1, y1) forward to its cut end at (x2, y2), `thick` across: bark with a lit top, and the end's rings.
void log_at(SDL_Surface* target, double x1, double y1, double x2, double y2, double thick) {
    stroke(target, x1, y1, x2, y2, BARK, thick);
    stroke(target, x1, y1 - thick * 0.22, x2, y2 - thick * 0.22, mixed(BARK, {200, 150, 96}, 0.35), thick * 0.3);
    fill_oval(target, x2, y2, thick * 0.38, thick * 0.5, HEARTWOOD);
    fill_oval(target, x2, y2, thick * 0.2, thick * 0.27, RINGS);
    fill_oval(target, x2, y2, thick * 0.07, thick * 0.09, HEARTWOOD);
}

// Logs in a pyramid, rows of `widest` up to one, their cut ends to the front.
void log_stack(SDL_Surface* target, double s, int widest, double scale) {
    const double thick = s * 0.16 * scale, back = s * 0.22 * scale;
    for (int row = 0; row < widest; ++row) {
        const int count = widest - row;
        for (int index = 0; index < count; ++index) {
            const double x = s * 0.5 + (index - (count - 1) / 2.0) * thick * 0.95, y = s * 0.78 - row * thick * 0.82;
            log_at(target, x - back, y - back * 0.4, x, y, thick);
        }
    }
}

// A bone from one knobbed end to the other.
void bone_at(SDL_Surface* target, double x1, double y1, double x2, double y2, double thick) {
    constexpr Color BONE{238, 230, 210}, EDGE{150, 138, 116};
    const double dx = x2 - x1, dy = y2 - y1, length = std::hypot(dx, dy), nx = -dy / length, ny = dx / length;
    for (const auto& [colour, grow] : {std::pair{EDGE, 1.0}, std::pair{BONE, 0.0}}) {
        stroke(target, x1, y1, x2, y2, colour, thick + grow * 1.2);
        for (const auto& [ex, ey] : {std::pair{x1, y1}, std::pair{x2, y2}}) {
            for (const double side : {1.0, -1.0}) {
                fill_oval(target, ex + nx * thick * 0.55 * side, ey + ny * thick * 0.55 * side, thick * 0.62 + grow * 0.6,
                          thick * 0.62 + grow * 0.6, colour);
            }
        }
    }
}

// A rounded stone, darker beneath and lit from the top left.
void stone_at(SDL_Surface* target, double cx, double cy, double rx, double ry) {
    fill_oval(target, cx, cy, rx, ry, {92, 94, 98});
    fill_oval(target, cx - rx * 0.12, cy - ry * 0.16, rx * 0.84, ry * 0.76, {142, 144, 148});
    fill_oval(target, cx - rx * 0.35, cy - ry * 0.4, rx * 0.25, ry * 0.2, {206, 208, 212});
}

// A tongue of flame rising `tall` from a base `wide` across, its tip swayed `sway` to the side.
void flame_at(SDL_Surface* target, double cx, double base, double wide, double tall, double sway, Color colour) {
    constexpr int STEPS = 16;
    std::vector<std::pair<double, double>> edge;
    const auto half = [&](double u) { return wide / 2 * std::sqrt(std::max(0.0, std::sin(std::numbers::pi * u))) * (1 - 0.3 * u); };
    for (int step = 0; step <= STEPS; ++step) {
        const double u = static_cast<double>(step) / STEPS;
        edge.emplace_back(cx + sway * u * u + half(u), base - tall * u);
    }
    for (int step = STEPS - 1; step > 0; --step) {
        const double u = static_cast<double>(step) / STEPS;
        edge.emplace_back(cx + sway * u * u - half(u), base - tall * u);
    }
    fill_shape(target, edge, colour);
}

Surface draw_prop_surface(Prop prop, int size, int frame) {
    Surface sprite = make_surface(size, size);
    SDL_Surface* target = sprite.get();
    const double s = size;
    const auto at = [s](double x, double y) { return std::pair{x * s, y * s}; };
    SDL_LockSurface(target);
    switch (prop) {
        case Prop::Cabin: {  // a log cabin seen from the front and a little to the side, its roof pitched over a gable
            constexpr Color FRONT{170, 118, 68}, SIDE{124, 82, 46}, ROOF{136, 64, 44}, ROOF_EDGE{90, 40, 28};
            fill_oval(target, s * 0.52, s * 0.87, s * 0.44, s * 0.1, {0, 0, 0, 255}, 1.0);
            fill_shape(target, {at(0.56, 0.56), at(0.86, 0.46), at(0.86, 0.77), at(0.56, 0.88)}, SIDE);
            fill_shape(target, {at(0.14, 0.57), at(0.35, 0.34), at(0.56, 0.57), at(0.56, 0.88), at(0.14, 0.88)}, FRONT);
            const double line = std::max(0.8, s * 0.014);
            for (int course = 0; course < 4; ++course) {  // the logs of its walls
                const double y = 0.63 + course * 0.065;
                stroke(target, s * 0.14, s * y, s * 0.56, s * y, scaled(FRONT, 0.72), line);
                stroke(target, s * 0.56, s * y, s * 0.86, s * (y - 0.1), scaled(SIDE, 0.72), line);
            }
            fill_shape(target, {at(0.285, 0.7), at(0.4, 0.7), at(0.4, 0.88), at(0.285, 0.88)}, {58, 36, 22});  // the door
            fill_shape(target, {at(0.67, 0.6), at(0.77, 0.565), at(0.77, 0.65), at(0.67, 0.685)}, {244, 206, 112});  // a lit window
            fill_shape(target, {at(0.35, 0.33), at(0.66, 0.21), at(0.89, 0.46), at(0.57, 0.58)}, ROOF);
            stroke(target, s * 0.57, s * 0.58, s * 0.89, s * 0.46, ROOF_EDGE, s * 0.03);  // the eave
            stroke(target, s * 0.11, s * 0.59, s * 0.35, s * 0.32, ROOF_EDGE, s * 0.035);  // the gable's ends
            stroke(target, s * 0.35, s * 0.32, s * 0.58, s * 0.59, ROOF_EDGE, s * 0.035);
            stroke(target, s * 0.36, s * 0.32, s * 0.66, s * 0.21, mixed(ROOF, {255, 220, 180}, 0.3), s * 0.02);  // the ridge
            fill_shape(target, {at(0.7, 0.355), at(0.76, 0.335), at(0.76, 0.2), at(0.7, 0.22)}, {120, 116, 110});  // the chimney, standing on the roof
            break;
        }
        case Prop::Site:  // a stack of logs waiting to be built, between two stakes
            fill_oval(target, s * 0.5, s * 0.86, s * 0.42, s * 0.09, {0, 0, 0, 255}, 1.0);
            for (const double x : {0.14, 0.86}) stroke(target, s * x, s * 0.42, s * x, s * 0.86, HEARTWOOD, std::max(1.0, s * 0.035));
            log_stack(target, s, 3, 1.0);
            break;
        case Prop::Bones:
            bone_at(target, s * 0.18, s * 0.72, s * 0.78, s * 0.36, s * 0.11);
            bone_at(target, s * 0.22, s * 0.36, s * 0.8, s * 0.7, s * 0.11);
            break;
        case Prop::Logs:
            log_stack(target, s, 2, 1.45);
            break;
        case Prop::Stones:
            stone_at(target, s * 0.34, s * 0.66, s * 0.22, s * 0.16);
            stone_at(target, s * 0.66, s * 0.7, s * 0.19, s * 0.14);
            stone_at(target, s * 0.52, s * 0.44, s * 0.17, s * 0.14);
            break;
        case Prop::Fire: {  // flames over two crossed logs, in a glow, flickering frame by frame
            fill_oval(target, s * 0.5, s * 0.62, s * 0.48, s * 0.4, {255, 150, 40, 120}, 1.0);
            stroke(target, s * 0.22, s * 0.86, s * 0.78, s * 0.72, BARK, s * 0.09);
            stroke(target, s * 0.22, s * 0.72, s * 0.78, s * 0.86, scaled(BARK, 0.8), s * 0.09);
            const auto flicker = [frame](double phase) { return std::sin(frame * 1.9 + phase); };
            const double base = s * 0.82;
            flame_at(target, s * 0.36, base, s * 0.2, s * (0.34 + 0.05 * flicker(1)), s * 0.05 * flicker(2), {214, 52, 18});
            flame_at(target, s * 0.64, base, s * 0.2, s * (0.32 + 0.05 * flicker(3)), s * 0.05 * flicker(4), {214, 52, 18});
            flame_at(target, s * 0.5, base, s * 0.48, s * (0.66 + 0.06 * flicker(0)), s * 0.07 * flicker(5), {232, 74, 24});
            flame_at(target, s * 0.5, base, s * 0.32, s * (0.48 + 0.05 * flicker(6)), s * 0.06 * flicker(7), {250, 146, 32});
            flame_at(target, s * 0.5, base, s * 0.16, s * (0.28 + 0.04 * flicker(8)), s * 0.04 * flicker(9), {255, 230, 120});
            break;
        }
    }
    SDL_UnlockSurface(target);
    return sprite;
}

}  // namespace

SDL_Surface* prop_sprite(Prop prop, int size, int frame) {
    static std::map<std::tuple<int, int, int>, Surface> kept;
    size = std::max(4, size);
    frame = prop == Prop::Fire ? frame % FLAME_FRAMES : 0;
    auto [found, added] = kept.try_emplace(std::tuple{static_cast<int>(prop), size, frame});
    if (added) found->second = draw_prop_surface(prop, size, frame);
    return found->second.get();
}

void draw_prop(Canvas& canvas, Prop prop, int cx, int cy, int size, int frame) {
    const int side = std::max(4, size);
    canvas.blit(prop_sprite(prop, side, frame), cx - side / 2, cy - side / 2);
}

}  // namespace bitzkey::ui
