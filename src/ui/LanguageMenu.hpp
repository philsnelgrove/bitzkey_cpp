#pragma once
// The language menu: a button naming the language shown, which opens a list of every language,
// each named in its own script. Choosing one switches the whole app at once and is remembered.

#include <vector>

#include "ui/Canvas.hpp"

namespace bitzkey::ui {

class LanguageMenu {
public:
    // The button, its right edge at `right` and centred on `middle`; returns its left edge.
    int draw_button(Canvas& canvas, int right, int middle);
    void draw_list(Canvas& canvas);  // last, over everything else, when open
    bool click(int x, int y);        // true when the click was the menu's
    bool open() const { return open_; }
    const Rect& button() const { return button_; }

private:
    Rect button_{};
    std::vector<Rect> items_;
    bool open_ = false;
};

}  // namespace bitzkey::ui
