#pragma once
// Small controls for the workers' screens: a box to type a line in, buttons, and the form for what a worker shares.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL.h>

#include "net/Brains.hpp"
#include "ui/Canvas.hpp"

namespace bitzkey::ui {

// One line of text to type, focused by a click on it. What is typed is shown as typed, untranslated; the
// placeholder, shown while it is empty, is translated.
struct TextField {
    std::string text;
    std::string placeholder;
    std::size_t max = 60;
    bool digits = false;  // whole numbers alone
    Rect rect{};          // where it was last drawn

    void draw(Canvas& canvas, bool focused) const;
    void type(std::string_view typed);
    void backspace();
    std::optional<int> number() const;  // none when empty or not a number
};

// A button, its label centred; the primary one green, a warning one red.
enum class Tone { Plain, Primary, Warning };
void draw_button(Canvas& canvas, const Rect& rect, std::string_view label, Tone tone = Tone::Plain);

// What a worker shares: cores and memory typed in, and each GPU found ticked or not.
class ShareForm {
public:
    ShareForm(const net::Machine& machine, const net::Resources& shared);
    // Laid out from (x, y), `width` wide; returns the height it took.
    int draw(Canvas& canvas, int x, int y, int width, const TextField* focused);
    // A click: focuses a box (returned) or ticks a GPU; none if it missed them.
    TextField* click(int x, int y);
    std::vector<TextField*> fields() { return {&cores_, &memory_}; }
    // What is chosen, or the problem with it.
    std::optional<net::Resources> read(std::string& problem) const;

private:
    net::Machine machine_;
    TextField cores_, memory_;
    std::vector<bool> gpus_;
    std::vector<Rect> ticks_;
};

}  // namespace bitzkey::ui
