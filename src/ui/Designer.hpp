#pragma once
// The Bitzkey designer: every trait of a genome, editable, beside a large preview in its true
// colours. Opened from the configuration panel; applying a design adds it to the import list.

#include <array>
#include <optional>
#include <string>
#include <utility>

#include "core/Genome.hpp"
#include "core/PyRandom.hpp"
#include "ui/Canvas.hpp"
#include "ui/Window.hpp"

namespace bitzkey::ui {

struct Screens;

enum class FieldKind { Mind, Body, Hex };
struct Field {
    FieldKind kind;
    int index;  // the trait, or 0 for the ring and 1 for the dot
    bool operator==(const Field&) const = default;
};
constexpr int FIELD_COUNT = MIND_TRAIT_COUNT + BODY_TRAIT_COUNT + 2;
Field field_at(int position);
int field_position(Field field);

// The genome being designed, the focused field, and any hex code being typed.
class DesignerForm {
public:
    explicit DesignerForm(Dna dna) : dna(dna) {}

    Field focused() const { return field_at(focus); }
    int value(Field field) const;  // 0 to 7; for species, the raw value that picks it (mod 4)
    void set_value(Field field, int value);
    std::uint32_t word(int part) const { return part == 0 ? body_word(dna) : mind_word(dna); }
    std::string code(int part) const;  // what is being typed into a word, else its value
    void select(int position);
    void move(int offset) { select(focus + offset); }
    void adjust(int steps);
    void type_text(const std::string& text);
    void backspace();
    void randomise(PyRandom& rng);

    Dna dna;
    int focus = 0;
    std::optional<std::string> typed;  // partial input for the focused hex field

private:
    void set_word(int part, std::uint32_t word);
};

class Designer {
public:
    enum class Outcome { Apply, Cancel, Quit };

    Designer(Window& window, std::optional<Dna> dna);
    std::pair<Outcome, std::optional<Dna>> run();

private:
    friend struct Screens;  // the screenshot tool
    struct Layout {
        Rect card, preview, ring_code, dot_code, apply, cancel, random;
        std::array<Rect, MIND_TRAIT_COUNT + BODY_TRAIT_COUNT> rows;
    };
    Layout layout() const;
    static std::array<Rect, 3> controls(const Rect& row);  // minus, value box, plus
    std::optional<Outcome> click(int x, int y);
    void key(const SDL_Event& event);
    void draw(Canvas& canvas);
    void draw_row(Canvas& canvas, Field field, const Rect& row, bool focused);

    Window& window_;
    PyRandom rng_;
    DesignerForm form_;
};

}  // namespace bitzkey::ui
