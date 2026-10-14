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

// The climate fields: 0 the hardiness (the tolerance flags, the dot's top two bits, as one value:
// 1 heat, 2 cold, 3 both), 1 the home temperature (the ring's top two bits). The social fields: its
// traits, then its neurodivergence.
enum class FieldKind { Mind, Body, Climate, Social, Hex };
struct Field {
    FieldKind kind;
    int index;  // the trait, or 0 for the ring, 1 for the dot and 2 for the social background
    bool operator==(const Field&) const = default;
};
constexpr int COLUMN_ROWS = DOT_TRAIT_COUNT + 1;  // a column each for the dot and ring: its traits, then its climate field
constexpr int SOCIAL_FIELDS = SOCIAL_TRAIT_COUNT + 1;  // beneath them both, in two columns
constexpr int SOCIAL_ROWS = (SOCIAL_FIELDS + 1) / 2;
constexpr int ROW_COUNT = 2 * COLUMN_ROWS + SOCIAL_FIELDS;  // every field but the hex codes
constexpr int FIELD_COUNT = ROW_COUNT + 3;
Field field_at(int position);
int field_position(Field field);

// The genome being designed, the focused field, and any hex code being typed.
class DesignerForm {
public:
    explicit DesignerForm(Dna dna) : dna(dna) {}

    Field focused() const { return field_at(focus); }
    int value(Field field) const;  // 0 to 7; for species, the raw value that picks it (mod 4); neurodivergence to 15
    void set_value(Field field, int value);
    std::uint32_t word(int part) const { return part == 0 ? body_word(dna) : part == 1 ? mind_word(dna) : social_word(dna); }
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
        Rect card, preview, apply, cancel, random;
        std::array<Rect, 3> codes;  // the ring's, the dot's and the social background's
        std::array<Rect, ROW_COUNT> rows;
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
