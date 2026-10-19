#include "ui/LanguageMenu.hpp"

#include <algorithm>
#include <string>

#include "ui/Language.hpp"
#include "ui/Ui.hpp"

namespace bitzkey::ui {

namespace {
constexpr int ROW = 26;  // design pixels per language in the open list

std::string name_of(const std::string& code) {
    for (const LanguageName& known : languages()) {
        if (known.code == code) return known.name;
    }
    return code;
}
}  // namespace

int LanguageMenu::draw_button(Canvas& canvas, int right, int middle) {
    TTF_Font* font = UI.font(Face::Sans, 12, false, language());
    const std::string name = name_of(language());
    const int width = Canvas::text_width(font, name) + UI(34);
    button_ = Rect{0, 0, width, UI(26)}.midright_at(right, middle);
    canvas.rect(button_, open_ ? Color{80, 99, 85} : Color{44, 57, 52}, 0, UI(4));
    canvas.text_at(font, name, {220, 229, 211}, [&](Rect& box) { box.midleft_at(button_.x + UI(10), button_.centery()); });
    const int cx = button_.right() - UI(13), cy = button_.centery();  // a small arrow: the list opens below
    canvas.polygon({{cx - UI(4), cy - UI(2)}, {cx + UI(4), cy - UI(2)}, {cx, cy + UI(3)}}, {187, 199, 181});
    return button_.x;
}

void LanguageMenu::draw_list(Canvas& canvas) {
    items_.clear();
    if (!open_) return;
    int width = button_.w;
    for (const LanguageName& known : languages()) {
        width = std::max(width, Canvas::text_width(UI.font(Face::Sans, 13, false, known.code), known.name) + UI(28));
    }
    const int count = static_cast<int>(languages().size());
    Rect panel{button_.right() - width, button_.bottom() + UI(4), width, count * UI(ROW) + UI(8)};
    canvas.rect(panel, {27, 37, 37}, 0, UI(6));
    canvas.rect(panel, {80, 99, 85}, 1, UI(6));
    for (int index = 0; index < count; ++index) {
        const LanguageName& known = languages()[static_cast<std::size_t>(index)];
        const Rect item{panel.x + UI(4), panel.y + UI(4) + index * UI(ROW), panel.w - UI(8), UI(ROW)};
        items_.push_back(item);
        const bool current = known.code == language();
        if (current) canvas.rect(item, {44, 57, 52}, 0, UI(4));
        canvas.text_at(UI.font(Face::Sans, 13, current, known.code), known.name, current ? Color{238, 183, 110} : Color{220, 229, 211},
                       [&](Rect& box) { box.midleft_at(item.x + UI(10), item.centery()); });
    }
}

bool LanguageMenu::click(int x, int y) {
    if (button_.contains(x, y)) {
        open_ = !open_;
        return true;
    }
    if (!open_) return false;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (items_[index].contains(x, y)) set_language(languages()[index].code);
    }
    open_ = false;  // a click anywhere else only closes the list
    return true;
}

}  // namespace bitzkey::ui
