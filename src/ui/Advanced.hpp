#pragma once
// The advanced panel: every tunable rule of the simulation, by group, opened from the
// configuration panel. Applying it sets the rules the next world plays by.

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/Rules.hpp"
#include "ui/Canvas.hpp"
#include "ui/Window.hpp"

namespace bitzkey::ui {

struct Screens;

// Every rule's value, the focused rule, and any number being typed into it.
class AdvancedForm {
public:
    explicit AdvancedForm(const rules::Values& values);

    const rules::Rule& focused() const { return rules::all()[static_cast<std::size_t>(focus)]; }
    std::string text(const rules::Rule& rule) const;
    rules::Values changed() const { return rules::changed(values); }
    std::vector<std::string> problems() const { return rules::problems(values); }
    void select(int index);
    void move(int offset) { select(focus + offset); }
    void adjust(int steps);
    // Digits, a decimal point for fractional rules, and a leading minus where values may be negative.
    void type_text(const std::string& text);
    void backspace();
    void reset();  // the focused rule back to its default
    void reset_all();
    void finish() { settle(); }

    rules::Values values;
    int focus = 0;
    std::optional<std::string> typed;  // partial input for the focused rule; none shows its value

private:
    void settle();
};

class AdvancedScreen {
public:
    enum class Outcome { Apply, Cancel, Quit };

    AdvancedScreen(Window& window, const rules::Values& values);
    // The changed rules when applied.
    std::pair<Outcome, std::optional<rules::Values>> run();

private:
    friend struct Screens;  // the screenshot tool
    static constexpr int ROW_HEIGHT = 44;
    static constexpr int HEADING_HEIGHT = 34;

    struct Layout {
        Rect card, view, apply, cancel, reset, reset_all;
    };
    // A line of the list: a group heading, or the index of a rule, with its top in the list.
    struct Line {
        std::variant<std::string, int> item;
        int top;
    };
    Layout layout() const;
    int max_scroll(const Rect& view) const;
    void scroll_by(int amount);
    void show_focus();
    std::vector<std::pair<const Line*, Rect>> rows(const Rect& view) const;
    static std::array<Rect, 3> buttons(const Rect& row);  // minus, value box, plus
    std::optional<Outcome> click(int x, int y);
    void key(const SDL_Event& event);
    bool ready();
    void tell(const std::string& note);
    void draw(Canvas& canvas);
    void draw_rule(Canvas& canvas, const rules::Rule& rule, const Rect& row, bool focused);
    void draw_scroll_bar(Canvas& canvas, const Rect& view);

    Window& window_;
    AdvancedForm form_;
    std::vector<Line> lines_;
    int list_height_ = 0;
    int scroll_ = 0;
    std::string note_;
    Uint32 note_until_ = 0;
};

}  // namespace bitzkey::ui
