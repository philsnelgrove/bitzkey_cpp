#pragma once
// A small modal dialog drawn over the screen: a title, a few lines of text and a row of buttons.
// Enter picks the default button (the last) and Esc the cancelling one.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

#include "ui/Canvas.hpp"

namespace bitzkey::ui {

class Dialog {
public:
    Dialog(std::string title, std::vector<std::string> lines, std::vector<std::pair<std::string, std::string>> buttons,
           std::string cancel)
        : title(std::move(title)), lines(std::move(lines)), buttons(std::move(buttons)), cancel(std::move(cancel)) {}

    // Show that a choice is under way: relabel and grey out its button, and add a caption.
    void start(const std::string& choice, const std::string& label, const std::string& caption);
    // The choice an event makes, if any.
    std::optional<std::string> handle(const SDL_Event& event, int width, int height) const;
    void draw(Canvas& canvas) const;

    std::string title;
    std::vector<std::string> lines;
    std::vector<std::pair<std::string, std::string>> buttons;  // (choice, label), left to right
    std::string cancel;                                         // the choice Esc makes
    std::optional<std::string> busy;                            // a choice under way
    std::string caption;                                        // shown beneath while busy

private:
    struct Layout {
        Rect panel;
        std::vector<std::pair<std::string, Rect>> buttons;
    };
    Layout layout(int width, int height) const;
};

}  // namespace bitzkey::ui
