#include "ui/Canvas.hpp"
#include "ui/Sound.hpp"

#include <cmath>
#include <string>
#include <vector>

#include <SDL_ttf.h>

#include "ui/Language.hpp"

namespace bitzkey::ui {

Rect Rect::clip(const Rect& other) const {
    const int left = std::max(x, other.x), top = std::max(y, other.y);
    const int right_edge = std::min(x + w, other.x + other.w), bottom_edge = std::min(y + h, other.y + other.h);
    if (right_edge <= left || bottom_edge <= top) return {x, y, 0, 0};
    return {left, top, right_edge - left, bottom_edge - top};
}

Rect Rect::clamp(const Rect& area) const {
    Rect moved = *this;
    if (w >= area.w) moved.x = area.x + area.w / 2 - w / 2;
    else moved.x = std::clamp(x, area.x, area.x + area.w - w);
    if (h >= area.h) moved.y = area.y + area.h / 2 - h / 2;
    else moved.y = std::clamp(y, area.y, area.y + area.h - h);
    return moved;
}

Canvas::Canvas(SDL_Surface* target) : target_(target), clip_{0, 0, target->w, target->h} {
    SDL_SetClipRect(target_, nullptr);
}

void Canvas::set_clip(const std::optional<Rect>& area) {
    const Rect whole{0, 0, target_->w, target_->h};
    clip_ = area ? area->clip(whole) : whole;
    if (area) {
        const SDL_Rect sdl{clip_.x, clip_.y, clip_.w, clip_.h};
        SDL_SetClipRect(target_, &sdl);
    } else {
        SDL_SetClipRect(target_, nullptr);
    }
}

Surface make_surface(int width, int height) {
    Surface surface(SDL_CreateRGBSurfaceWithFormat(0, std::max(1, width), std::max(1, height), 32, SDL_PIXELFORMAT_ARGB8888));
    SDL_FillRect(surface.get(), nullptr, 0);
    SDL_SetSurfaceBlendMode(surface.get(), SDL_BLENDMODE_BLEND);
    return surface;
}

std::uint32_t Canvas::map(Color color) const {
    return (static_cast<std::uint32_t>(color.a) << 24) | (static_cast<std::uint32_t>(color.r) << 16) |
           (static_cast<std::uint32_t>(color.g) << 8) | color.b;
}

void Canvas::span(int y, int x0, int x1, std::uint32_t pixel) {
    if (y < clip_.y || y >= clip_.bottom()) return;
    x0 = std::max(x0, clip_.x);
    x1 = std::min(x1, clip_.right() - 1);
    if (x0 > x1) return;
    auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(target_->pixels) + y * target_->pitch);
    std::fill(row + x0, row + x1 + 1, pixel);
}

void Canvas::fill(Color color) { SDL_FillRect(target_, nullptr, map(color)); }

void Canvas::fill_rect(const Rect& area, Color color) {
    const Rect box = area.clip(clip_);
    if (box.w <= 0 || box.h <= 0) return;
    if (color.a == 255) {
        const SDL_Rect sdl{box.x, box.y, box.w, box.h};
        SDL_FillRect(target_, &sdl, map(color));
        return;
    }
    const std::uint32_t alpha = color.a, keep = 255 - alpha;
    for (int y = box.y; y < box.bottom(); ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(target_->pixels) + y * target_->pitch);
        for (int x = box.x; x < box.right(); ++x) {
            const std::uint32_t old = row[x];
            const std::uint32_t r = (((old >> 16) & 0xFF) * keep + color.r * alpha) / 255;
            const std::uint32_t g = (((old >> 8) & 0xFF) * keep + color.g * alpha) / 255;
            const std::uint32_t b = ((old & 0xFF) * keep + color.b * alpha) / 255;
            const std::uint32_t a = std::max<std::uint32_t>((old >> 24) & 0xFF, alpha);
            row[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

namespace {
// How far a rounded rectangle's edge is inset on each row, for corners of `radius`.
std::vector<int> corner_insets(int height, int radius) {
    std::vector<int> insets(static_cast<std::size_t>(std::max(0, height)), 0);
    for (int row = 0; row < height; ++row) {
        const int from_edge = std::min(row, height - 1 - row);
        if (from_edge >= radius) continue;
        const double dy = radius - from_edge - 0.5;
        insets[static_cast<std::size_t>(row)] =
            static_cast<int>(std::lround(radius - std::sqrt(std::max(0.0, radius * radius - dy * dy))));
    }
    return insets;
}
}  // namespace

void Canvas::rect(const Rect& area, Color color, int width, int radius) {
    if (area.w <= 0 || area.h <= 0) return;
    radius = std::min({radius, area.w / 2, area.h / 2});
    if (radius <= 0 && width == 0) {
        fill_rect(area, color);
        return;
    }
    const std::uint32_t pixel = map(color);
    const std::vector<int> insets = corner_insets(area.h, std::max(0, radius));
    for (int row = 0; row < area.h; ++row) {
        const int inset = insets[static_cast<std::size_t>(row)];
        const int y = area.y + row;
        const int x0 = area.x + inset, x1 = area.right() - 1 - inset;
        if (width == 0 || row < width || row >= area.h - width) {
            span(y, x0, x1, pixel);
            continue;
        }
        // Outline: from this row's edge to the furthest inset of its neighbours, at least `width`.
        int reach = inset;
        if (row > 0) reach = std::max(reach, insets[static_cast<std::size_t>(row - 1)]);
        if (row + 1 < area.h) reach = std::max(reach, insets[static_cast<std::size_t>(row + 1)]);
        const int thickness = std::max(width, reach - inset + 1);
        span(y, x0, x0 + thickness - 1, pixel);
        span(y, x1 - thickness + 1, x1, pixel);
    }
}

void Canvas::circle(int cx, int cy, int radius, Color color, int width) {
    if (radius < 1) {
        if (radius == 0) span(cy, cx, cx, map(color));
        return;
    }
    const std::uint32_t pixel = map(color);
    const auto half = [](int r, int dy) {
        const double inside = static_cast<double>(r) * r + r - static_cast<double>(dy) * dy;
        return inside < 0 ? -1 : static_cast<int>(std::sqrt(inside));
    };
    const int inner = width > 0 && width < radius ? radius - width : -1;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int outer_half = half(radius, dy);
        if (outer_half < 0) continue;
        const int inner_half = inner >= 0 && std::abs(dy) <= inner ? half(inner, dy) : -1;
        if (inner_half < 0) {
            span(cy + dy, cx - outer_half, cx + outer_half, pixel);
        } else {
            span(cy + dy, cx - outer_half, cx - inner_half - 1, pixel);
            span(cy + dy, cx + inner_half + 1, cx + outer_half, pixel);
        }
    }
}

void Canvas::line(int x1, int y1, int x2, int y2, Color color, int width) {
    width = std::max(1, width);
    const std::uint32_t pixel = map(color);
    if (y1 == y2) {
        for (int offset = 0; offset < width; ++offset) span(y1 + offset - width / 2, std::min(x1, x2), std::max(x1, x2), pixel);
        return;
    }
    if (x1 == x2) {
        for (int y = std::min(y1, y2); y <= std::max(y1, y2); ++y) span(y, x1 - width / 2, x1 - width / 2 + width - 1, pixel);
        return;
    }
    const int dx = std::abs(x2 - x1), dy = -std::abs(y2 - y1);
    const int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int error = dx + dy, x = x1, y = y1;
    const bool steep = -dy > dx;
    while (true) {
        if (steep) {
            span(y, x - width / 2, x - width / 2 + width - 1, pixel);
        } else {
            for (int offset = 0; offset < width; ++offset) span(y + offset - width / 2, x, x, pixel);
        }
        if (x == x2 && y == y2) break;
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y += sy;
        }
    }
}

void Canvas::polygon(std::initializer_list<std::pair<int, int>> points, Color color) {
    // Scanline fill, sampling each row through its middle, with the outline drawn too, as pygame does.
    const std::vector<std::pair<int, int>> corners(points);
    if (corners.size() < 3) return;
    const std::uint32_t pixel = map(color);
    int top = corners[0].second, bottom = corners[0].second;
    for (const auto& [x, y] : corners) {
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    }
    std::vector<double> crossings;
    for (int y = top; y <= bottom; ++y) {
        crossings.clear();
        const double row = y + 0.5;
        for (std::size_t index = 0; index < corners.size(); ++index) {
            const auto [x1, y1] = corners[index];
            const auto [x2, y2] = corners[(index + 1) % corners.size()];
            if ((y1 <= row && row < y2) || (y2 <= row && row < y1)) {
                crossings.push_back(x1 + (row - y1) * (x2 - x1) / static_cast<double>(y2 - y1));
            }
        }
        std::sort(crossings.begin(), crossings.end());
        for (std::size_t index = 0; index + 1 < crossings.size(); index += 2) {
            span(y, static_cast<int>(std::lround(crossings[index])), static_cast<int>(std::lround(crossings[index + 1])), pixel);
        }
    }
    for (std::size_t index = 0; index < corners.size(); ++index) {
        const auto [x1, y1] = corners[index];
        const auto [x2, y2] = corners[(index + 1) % corners.size()];
        line(x1, y1, x2, y2, color);
    }
}

void Canvas::blit(SDL_Surface* source, int x, int y, std::uint8_t alpha) {
    if (!source) return;
    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_BLEND);
    SDL_SetSurfaceAlphaMod(source, alpha);
    SDL_Rect where{x, y, source->w, source->h};
    SDL_BlitSurface(source, nullptr, target_, &where);
    SDL_SetSurfaceAlphaMod(source, 255);
}

void Canvas::label(TTF_Font* font, std::string_view words, Color color, const Rect& button) {
    button_drawn(target_, button);  // a press on it is heard
    TTF_Font* fitting = UI.fitting(font, words, button.w - UI(10));
    text_at(fitting, words, color, [&](Rect& box) { box.center_at(button.centerx(), button.centery()); });
}

Rect Canvas::text_box(TTF_Font* font, std::string_view text) {
    int width = 0, height = TTF_FontHeight(font);
    if (!text.empty()) TTF_SizeUTF8(font, tr(text).c_str(), &width, &height);
    return {0, 0, width, height};
}

int Canvas::text(TTF_Font* font, std::string_view words, Color color, int x, int y) {
    if (words.empty()) return 0;
    std::string shown = tr(words);  // in the chosen language
    if (has_right_to_left(shown)) shown = visual_order(shown);
    Surface rendered(TTF_RenderUTF8_Blended(font, shown.c_str(), SDL_Color{color.r, color.g, color.b, 255}));
    if (!rendered) return 0;
    blit(rendered.get(), x, y);
    return rendered->w;
}

std::vector<std::string> wrapped_lines(TTF_Font* font, std::string_view english, int width) {
    const std::string text = tr(english);
    const bool spaced = language() != "zh" && language() != "ja";
    const AsShown measuring;
    std::vector<std::string> lines;
    std::string line;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = at;
        if (spaced) {
            end = std::min(text.find(' ', at), text.size());
        } else {
            do ++end;
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80);
        }
        const std::string word = text.substr(at, end - at);
        const std::string trial = line.empty() ? word : line + (spaced ? " " : "") + word;
        if (!line.empty() && Canvas::text_width(font, trial) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        at = spaced ? end + 1 : end;
    }
    if (!line.empty()) lines.push_back(line);
    for (const std::string& each : lines) as_shown(each);
    return lines;
}

}  // namespace bitzkey::ui
