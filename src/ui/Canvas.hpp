#pragma once
// Drawing in software onto an SDL surface, with pygame's shapes and conventions: rectangles that
// span [x, x + w), filled and outlined circles, rounded rectangles, lines, translucent overlays
// and anti-aliased text.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <initializer_list>
#include <optional>
#include <utility>
#include <string>
#include <string_view>

#include <SDL.h>

#include "ui/Ui.hpp"

namespace bitzkey::ui {

struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Color&) const = default;
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    int left() const { return x; }
    int top() const { return y; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    int centerx() const { return x + w / 2; }
    int centery() const { return y + h / 2; }
    bool contains(int px, int py) const { return w > 0 && h > 0 && x <= px && px < x + w && y <= py && py < y + h; }
    Rect& center_at(int cx, int cy) { x = cx - w / 2; y = cy - h / 2; return *this; }
    Rect& midright_at(int px, int py) { x = px - w; y = py - h / 2; return *this; }
    Rect& midleft_at(int px, int py) { x = px; y = py - h / 2; return *this; }
    Rect& midbottom_at(int px, int py) { x = px - w / 2; y = py - h; return *this; }
    Rect& midtop_at(int px, int py) { x = px - w / 2; y = py; return *this; }
    Rect& topright_at(int px, int py) { x = px - w; y = py; return *this; }
    Rect& bottomleft_at(int px, int py) { x = px; y = py - h; return *this; }
    Rect& bottomright_at(int px, int py) { x = px - w; y = py - h; return *this; }
    // The overlap with another rectangle; empty (at this one's corner) when they don't meet.
    Rect clip(const Rect& other) const;
    Rect inflate(int dx, int dy) const { return {x - dx / 2, y - dy / 2, w + dx, h + dy}; }
    // Moved, not resized, to lie inside `area` as far as it can.
    Rect clamp(const Rect& area) const;
};

struct SurfaceDeleter {
    void operator()(SDL_Surface* surface) const { SDL_FreeSurface(surface); }
};
using Surface = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

Surface make_surface(int width, int height);  // ARGB, cleared to transparent

class Canvas {
public:
    explicit Canvas(SDL_Surface* target);
    SDL_Surface* surface() const { return target_; }
    int width() const { return target_->w; }
    int height() const { return target_->h; }

    void fill(Color color);
    void fill_rect(const Rect& rect, Color color);   // blends when the colour is translucent
    void rect(const Rect& rect, Color color, int width = 0, int radius = 0);
    void circle(int cx, int cy, int radius, Color color, int width = 0);
    void line(int x1, int y1, int x2, int y2, Color color, int width = 1);
    void polygon(std::initializer_list<std::pair<int, int>> points, Color color);  // filled
    void blit(SDL_Surface* source, int x, int y, std::uint8_t alpha = 255);
    // Limit drawing to a rectangle; none draws anywhere again.
    void set_clip(const std::optional<Rect>& area);

    // Text, drawn with its top-left at (x, y); returns its width.
    int text(TTF_Font* font, std::string_view text, Color color, int x, int y);
    // Text placed by a rectangle anchor: the text's box is positioned with `place`.
    template <class Place>
    void text_at(TTF_Font* font, std::string_view words, Color color, Place place) {
        Rect box = text_box(font, words);
        place(box);
        text(font, words, color, box.x, box.y);
    }

    static Rect text_box(TTF_Font* font, std::string_view text);  // at (0, 0)
    static int text_width(TTF_Font* font, std::string_view text) { return text_box(font, text).w; }

private:
    void span(int y, int x0, int x1, std::uint32_t pixel);  // inclusive
    std::uint32_t map(Color color) const;

    SDL_Surface* target_;
    Rect clip_;
};

}  // namespace bitzkey::ui
