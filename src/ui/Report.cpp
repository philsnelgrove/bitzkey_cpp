#include "ui/Report.hpp"

#include <algorithm>

namespace bitzkey::ui {

namespace {
constexpr int LINE = 18;  // design pixels to a line of the report
}

Report::Layout Report::layout(int width, int height) const {
    Layout places;
    int widest = 0;
    for (const std::string& line : lines) widest = std::max(widest, Canvas::text_width(UI.font(Face::Mono, 13), line));
    places.panel = Rect{0, 0, std::min(width - UI(40), std::max(UI(560), widest + UI(48))),
                        std::min(height - UI(40), UI(160) + UI(LINE) * static_cast<int>(lines.size()))}
                       .center_at(width / 2, height / 2);
    const Rect& panel = places.panel;
    places.body = {panel.x + UI(24), panel.y + UI(84), panel.w - UI(48), panel.h - UI(84 + 70)};
    places.close = Rect{0, 0, UI(110), UI(40)}.bottomright_at(panel.right() - UI(24), panel.bottom() - UI(18));
    return places;
}

int Report::shown(int width, int height) const { return std::max(1, layout(width, height).body.h / UI(LINE)); }

void Report::scroll_by(int lines_down, int width, int height) {
    scroll = std::max(0, std::min(static_cast<int>(lines.size()) - shown(width, height), scroll + lines_down));
}

bool Report::handle(const SDL_Event& event, int width, int height) {
    if (event.type == SDL_KEYDOWN) {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) return true;
        if (key == SDLK_DOWN) scroll_by(1, width, height);
        else if (key == SDLK_UP) scroll_by(-1, width, height);
        else if (key == SDLK_PAGEDOWN) scroll_by(10, width, height);
        else if (key == SDLK_PAGEUP) scroll_by(-10, width, height);
    } else if (event.type == SDL_MOUSEWHEEL) {
        scroll_by(-3 * event.wheel.y, width, height);
    } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        return layout(width, height).close.contains(event.button.x, event.button.y);
    }
    return false;
}

void Report::draw(Canvas& canvas) {
    const Layout places = layout(canvas.width(), canvas.height());
    const Rect &panel = places.panel, &body = places.body;
    canvas.fill_rect({0, 0, canvas.width(), canvas.height()}, {8, 12, 12, 185});
    canvas.rect(panel, {30, 40, 40}, 0, UI(8));
    canvas.rect(panel, {80, 99, 85}, 1, UI(8));
    canvas.text(UI.font(Face::Sans, 22, true), title, {229, 239, 220}, panel.x + UI(24), panel.y + UI(20));
    canvas.text(UI.font(Face::Sans, 13), note, {144, 170, 151}, panel.x + UI(24), panel.y + UI(54));
    const int count = shown(canvas.width(), canvas.height());
    scroll = std::max(0, std::min(std::max(0, static_cast<int>(lines.size()) - count), scroll));
    canvas.set_clip(body);
    for (int row = 0; row < count && scroll + row < static_cast<int>(lines.size()); ++row) {
        const bool heading = scroll + row == 0;
        canvas.text(UI.font(Face::Mono, 13), lines[static_cast<std::size_t>(scroll + row)],
                    heading ? Color{220, 229, 211} : Color{187, 199, 181}, body.x, body.y + row * UI(LINE));
    }
    canvas.set_clip(std::nullopt);
    if (static_cast<int>(lines.size()) > count) {
        const std::string more = "lines " + std::to_string(scroll + 1) + "-" +
                                 std::to_string(std::min(static_cast<int>(lines.size()), scroll + count)) + " of " +
                                 std::to_string(lines.size()) + "   (wheel or UP/DOWN)";
        canvas.text(UI.font(Face::Sans, 12), more, {116, 139, 124}, panel.x + UI(24), panel.bottom() - UI(44));
    }
    canvas.rect(places.close, {131, 205, 143}, 0, UI(6));
    canvas.text_at(UI.font(Face::Sans, 13, true), "CLOSE", {19, 26, 27},
                   [&](Rect& box) { box.center_at(places.close.centerx(), places.close.centery()); });
}

}  // namespace bitzkey::ui
