#include "ui/Dialog.hpp"

#include <algorithm>

namespace bitzkey::ui {

void Dialog::start(const std::string& choice, const std::string& label, const std::string& text) {
    for (auto& [name, shown] : buttons) {
        if (name == choice) shown = label;
    }
    busy = choice;
    caption = text;
}

Dialog::Layout Dialog::layout(int width, int height) const {
    Layout result;
    // As wide as its widest line or its row of buttons need, within the window.
    int row = -UI(10), widest = Canvas::text_width(UI.font(Face::Sans, 22, true), title);
    for (const auto& [choice, label] : buttons) row += std::max(UI(110), Canvas::text_width(UI.font(Face::Sans, 13, true), label) + UI(32)) + UI(10);
    for (const std::string& line : lines) widest = std::max(widest, Canvas::text_width(UI.font(Face::Sans, 15), line));
    const int panel_width = std::min(width - UI(40), std::max(UI(560), std::max(row, widest) + UI(48)));
    result.panel = Rect{0, 0, panel_width, UI(150 + 24 * static_cast<int>(lines.size()))}.center_at(width / 2, height / 2);
    int right = result.panel.right() - UI(24);
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
        const int button_width = std::max(UI(110), Canvas::text_width(UI.font(Face::Sans, 13, true), it->second) + UI(32));
        result.buttons.emplace_back(it->first, Rect{right - button_width, result.panel.bottom() - UI(64), button_width, UI(40)});
        right -= button_width + UI(10);
    }
    return result;
}

std::optional<std::string> Dialog::handle(const SDL_Event& event, int width, int height) const {
    if (busy) return std::nullopt;
    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_ESCAPE) return cancel;
        if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) return buttons.back().first;
    } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        for (const auto& [choice, rect] : layout(width, height).buttons) {
            if (rect.contains(event.button.x, event.button.y)) return choice;
        }
    }
    return std::nullopt;
}

void Dialog::draw(Canvas& canvas) const {
    const Layout places = layout(canvas.width(), canvas.height());
    const Rect& panel = places.panel;
    canvas.fill_rect({0, 0, canvas.width(), canvas.height()}, {8, 12, 12, 185});
    canvas.rect(panel, {30, 40, 40}, 0, UI(8));
    canvas.rect(panel, {80, 99, 85}, 1, UI(8));
    canvas.text(UI.font(Face::Sans, 22, true), title, {229, 239, 220}, panel.x + UI(24), panel.y + UI(20));
    for (std::size_t index = 0; index < lines.size(); ++index) {
        canvas.text(UI.font(Face::Sans, 15), lines[index], {187, 199, 181}, panel.x + UI(24),
                    panel.y + UI(64 + 24 * static_cast<int>(index)));
    }
    for (const auto& [choice, rect] : places.buttons) {
        const bool is_default = choice == buttons.back().first;
        Color fill, ink;
        if (busy) {
            fill = {52, 58, 56};
            ink = choice == *busy ? Color{128, 138, 132} : Color{92, 102, 96};
        } else {
            fill = is_default ? Color{131, 205, 143} : Color{44, 57, 52};
            ink = is_default ? Color{19, 26, 27} : Color{220, 229, 211};
        }
        canvas.rect(rect, fill, 0, UI(6));
        std::string label;
        for (const auto& [name, shown] : buttons) {
            if (name == choice) label = shown;
        }
        canvas.text_at(UI.font(Face::Sans, 13, true), label, ink, [&](Rect& box) { box.center_at(rect.centerx(), rect.centery()); });
    }
    if (busy && !caption.empty()) {
        canvas.text_at(UI.font(Face::Sans, 16), caption, {144, 170, 151},
                       [&](Rect& box) { box.midtop_at(panel.centerx(), panel.bottom() + UI(22)); });
    }
}

}  // namespace bitzkey::ui
