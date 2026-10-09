#include "ui/Designer.hpp"

#include <cctype>
#include <cstdio>
#include <random>
#include <tuple>
#include <vector>

#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"
#include "ui/Look.hpp"

namespace bitzkey::ui {

namespace {
constexpr int PREVIEW_ZOOM = 16;  // the preview is the in-world look, this many times larger
constexpr Dna GENOME_MASK = ~0ULL;

constexpr std::array<std::string_view, 4> HARDINESS = {"none", "heat", "cold", "both"};

int shift_of(Field field) {
    if (field.kind == FieldKind::Climate) return field.index == 0 ? 30 : 62;
    return TRAIT_BITS * field.index + (field.kind == FieldKind::Mind ? 0 : 32);
}

int largest(Field field) { return field.kind == FieldKind::Climate ? 3 : TRAIT_MAX; }

std::string format(const char* pattern, auto... values) {
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, pattern, values...);
    return buffer;
}
}  // namespace

Field field_at(int position) {
    if (position < MIND_TRAIT_COUNT) return {FieldKind::Mind, position};
    if (position == MIND_TRAIT_COUNT) return {FieldKind::Climate, 0};
    if (position < COLUMN_ROWS + BODY_TRAIT_COUNT) return {FieldKind::Body, position - COLUMN_ROWS};
    if (position == COLUMN_ROWS + BODY_TRAIT_COUNT) return {FieldKind::Climate, 1};
    return {FieldKind::Hex, position - 2 * COLUMN_ROWS};
}

int field_position(Field field) {
    switch (field.kind) {
        case FieldKind::Mind: return field.index;
        case FieldKind::Body: return COLUMN_ROWS + field.index;
        case FieldKind::Climate: return field.index == 0 ? MIND_TRAIT_COUNT : COLUMN_ROWS + BODY_TRAIT_COUNT;
        default: return 2 * COLUMN_ROWS + field.index;
    }
}

int DesignerForm::value(Field field) const { return static_cast<int>((dna >> shift_of(field)) & static_cast<Dna>(largest(field))); }

void DesignerForm::set_value(Field field, int new_value) {
    const int shift = shift_of(field), most = largest(field);
    dna = (dna & ~(static_cast<Dna>(most) << shift) & GENOME_MASK) |
          (static_cast<Dna>(std::max(0, std::min(most, new_value))) << shift);
}

std::string DesignerForm::code(int part) const {
    if (focused() == Field{FieldKind::Hex, part} && typed) return *typed;
    return hex_word(word(part));
}

void DesignerForm::select(int position) {
    focus = py_mod(position, FIELD_COUNT);
    typed.reset();  // an unfinished hex code is dropped
}

void DesignerForm::adjust(int steps) {
    if (focused().kind != FieldKind::Hex) {
        set_value(focused(), value(focused()) + steps);
        typed.reset();
    }
}

void DesignerForm::type_text(const std::string& text) {
    const Field field = focused();
    if (field.kind == FieldKind::Hex) {
        for (const char raw : text) {
            const char character = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
            if (std::isxdigit(static_cast<unsigned char>(character)) && (typed ? typed->size() : 0) < 8) {
                typed = typed.value_or("") + character;
            }
        }
        if (typed && typed->size() == 8) {
            set_word(field.index, static_cast<std::uint32_t>(std::stoul(*typed, nullptr, 16)));
            typed.reset();
        }
        return;
    }
    for (const char character : text) {  // values are single digits, so each one replaces
        if (std::isdigit(static_cast<unsigned char>(character))) {
            set_value(field, character - '0');
            typed.reset();
        }
    }
}

void DesignerForm::backspace() {
    const Field field = focused();
    if (field.kind == FieldKind::Hex) {
        std::string current = code(field.index);
        if (!current.empty()) current.pop_back();
        typed = current;
    } else {
        set_value(field, 0);
        typed.reset();
    }
}

void DesignerForm::randomise(PyRandom& rng) {
    dna = rng.getrandbits(64);
    typed.reset();
}

void DesignerForm::set_word(int part, std::uint32_t new_word) {
    if (part == 0) {
        dna = (dna & WORD) | (static_cast<Dna>(new_word) << 32);
    } else {
        dna = (dna & ~static_cast<Dna>(WORD)) | new_word;
    }
}

namespace {
std::uint64_t fresh_seed() {
    std::random_device device;
    return (static_cast<std::uint64_t>(device()) << 32) | device();
}
}  // namespace

Designer::Designer(Window& window, std::optional<Dna> dna) : window_(window), rng_(fresh_seed()), form_(0) {
    if (!dna) dna = (rng_.getrandbits(64) & ~(static_cast<Dna>(0x3) << 32)) | (static_cast<Dna>(rng_.randrange(1, 4)) << 32);
    form_.dna = *dna;
}

std::pair<Designer::Outcome, std::optional<Dna>> Designer::run() {
    while (true) {
        const Uint32 frame_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window_.handle(event);
            if (event.type == SDL_QUIT) return {Outcome::Quit, std::nullopt};
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) return {Outcome::Cancel, std::nullopt};
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER) return {Outcome::Apply, form_.dna};
                if (key == SDLK_F11) {
                    window_.toggle_fullscreen();
                } else {
                    this->key(event);
                }
            } else if (event.type == SDL_TEXTINPUT) {
                form_.type_text(event.text.text);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (const auto outcome = click(event.button.x, event.button.y)) {
                    return {*outcome, *outcome == Outcome::Apply ? std::optional<Dna>(form_.dna) : std::nullopt};
                }
            }
        }
        Canvas canvas = window_.frame();
        draw(canvas);
        window_.present();
        const Uint32 spent = SDL_GetTicks() - frame_start;
        if (spent < 33) SDL_Delay(33 - spent);
    }
}

void Designer::key(const SDL_Event& event) {
    const SDL_Keycode key = event.key.keysym.sym;
    const bool shift = event.key.keysym.mod & KMOD_SHIFT;
    if (key == SDLK_UP || (key == SDLK_TAB && shift)) {
        form_.move(-1);
    } else if (key == SDLK_DOWN || key == SDLK_TAB) {
        form_.move(1);
    } else if (key == SDLK_RIGHT || key == SDLK_LEFT) {
        form_.adjust((shift ? 5 : 1) * (key == SDLK_RIGHT ? 1 : -1));
    } else if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
        form_.backspace();
    }
}

Designer::Layout Designer::layout() const {
    Layout places;
    places.card = Rect{0, 0, UI(1040), UI(800)}.center_at(window_.width() / 2, window_.height() / 2);
    const Rect& card = places.card;
    places.preview = {card.x + UI(24), card.y + UI(92), UI(320), UI(300)};
    places.ring_code = {places.preview.right() - UI(160), places.preview.bottom() + UI(16), UI(160), UI(34)};
    places.dot_code = {places.preview.right() - UI(160), places.preview.bottom() + UI(60), UI(160), UI(34)};
    const int column_width = (card.right() - UI(24) - (places.preview.right() + UI(32)) - UI(24)) / 2;
    for (int index = 0; index < 2 * COLUMN_ROWS; ++index) {
        const int column = index / COLUMN_ROWS, row = index % COLUMN_ROWS;
        const int left = places.preview.right() + UI(32) + column * (column_width + UI(24));
        places.rows[static_cast<std::size_t>(index)] = {left, card.y + UI(124 + row * 50), column_width, UI(42)};
    }
    places.apply = {card.right() - UI(24) - UI(120), card.bottom() - UI(68), UI(120), UI(44)};
    places.cancel = {card.right() - UI(24) - UI(132) - UI(120), card.bottom() - UI(68), UI(120), UI(44)};
    places.random = {card.x + UI(24), card.bottom() - UI(68), UI(120), UI(44)};
    return places;
}

std::array<Rect, 3> Designer::controls(const Rect& row) {
    const Rect plus{row.right() - UI(38), row.centery() - UI(15), UI(30), UI(30)};
    const Rect box{plus.x - UI(112), row.centery() - UI(16), UI(104), UI(32)};
    const Rect minus{box.x - UI(38), row.centery() - UI(15), UI(30), UI(30)};
    return {minus, box, plus};
}

std::optional<Designer::Outcome> Designer::click(int x, int y) {
    const Layout places = layout();
    if (places.apply.contains(x, y)) return Outcome::Apply;
    if (places.cancel.contains(x, y)) return Outcome::Cancel;
    if (places.random.contains(x, y)) form_.randomise(rng_);
    if (places.ring_code.contains(x, y)) form_.select(field_position({FieldKind::Hex, 0}));
    if (places.dot_code.contains(x, y)) form_.select(field_position({FieldKind::Hex, 1}));
    for (int index = 0; index < 2 * COLUMN_ROWS; ++index) {
        const Rect& row = places.rows[static_cast<std::size_t>(index)];
        if (!row.contains(x, y)) continue;
        if (index != form_.focus) form_.select(index);
        const auto [minus, box, plus] = controls(row);
        if (minus.contains(x, y)) form_.adjust(-1);
        else if (plus.contains(x, y)) form_.adjust(1);
    }
    return std::nullopt;
}

void Designer::draw(Canvas& canvas) {
    const Layout places = layout();
    const Rect& card = places.card;
    const Rect& preview = places.preview;
    TTF_Font* font = UI.font(Face::Sans, 15);
    TTF_Font* bold = UI.font(Face::Sans, 22, true);
    TTF_Font* small = UI.font(Face::Sans, 12);
    canvas.fill({19, 26, 27});
    canvas.rect(card, {27, 37, 37}, 0, UI(8));
    canvas.rect(card, {80, 99, 85}, 1, UI(8));
    const int title = canvas.text(bold, "BITZKEY DESIGNER", {229, 239, 220}, card.x + UI(24), card.y + UI(22));
    canvas.text(small, "NEW WORLD  /  DESIGN AN IMPORT", {144, 170, 151}, card.x + UI(48) + title, card.y + UI(31));
    canvas.text(font, "Every trait is part of the genome, so the dot and ring colours change with them.", {157, 177, 159},
                card.x + UI(24), card.y + UI(60));

    // The large preview, and the Bitzkey at the size it will have in the world.
    canvas.rect(preview, {22, 30, 30}, 0, UI(6));
    canvas.rect(preview, {54, 68, 62}, 1, UI(6));
    const int size = form_.value({FieldKind::Body, Size});
    draw_bitzkey(canvas, form_.dna, preview.centerx(), preview.y + UI(150), UI(PREVIEW_ZOOM * dot_radius(size)),
                 UI(PREVIEW_ZOOM * RING_WIDTH));
    canvas.text(small, "actual size", {116, 139, 124}, preview.x + UI(12), preview.bottom() - UI(26));
    draw_bitzkey(canvas, form_.dna, preview.x + UI(96), preview.bottom() - UI(18), dot_radius(size));

    for (int part = 0; part < 2; ++part) {
        const Rect& box = part == 0 ? places.ring_code : places.dot_code;
        const bool focused = form_.focused() == Field{FieldKind::Hex, part};
        canvas.text(font, part == 0 ? "RING / BODY" : "DOT / MIND", {149, 170, 150}, preview.x, box.y + UI(8));
        canvas.rect(box, {19, 26, 27}, 0, UI(4));
        canvas.rect(box, focused ? Color{131, 205, 143} : Color{54, 68, 62}, 1, UI(4));
        std::string code = form_.code(part);
        if (focused && form_.typed && SDL_GetTicks() / 500 % 2) code += "_";
        canvas.text_at(UI.font(Face::Mono, 18), code, {238, 238, 220}, [&](Rect& text_box) {
            text_box.midleft_at(box.x + UI(12), box.centery());
        });
    }

    const Bitzkey sample(0, form_.dna, 0, 0);
    std::vector<std::string> facts;
    if (sample.species == Species::Plant) {
        facts = {"A plant: it photosynthesises and never moves,", "thinks, breeds or eats.",
                 format("Lives about %.0f turns.", sample.lifespan)};
    } else {
        const double fertility = sample.physique(Fertility);
        const std::array<std::pair<int, int>, 2> parents = {std::pair{sample.body[Litter], sample.body[Size]},
                                                            std::pair{sample.body[Litter], sample.body[Size]}};
        const int litter = litter_size(parents);
        const std::string species(species_name(sample.species));
        facts = {
            format("A %s.  Health %.0f.  Metabolism %.2f.", species.c_str(), sample.max_health, sample.metabolism),
            format("Stomach %.2f.  Burns %.2f hunger a turn.", sample.stomach, sample.burn),
            format("Moves %d cell%s a step.  Sees %d cells.", sample.steps, sample.steps != 1 ? "s" : "", sample.sight),
            format("Breeding costs each parent %.0f hunger, then", 25 - 15 * fertility),
            format("%d turns to recover.  With a like partner, litters of %d.", static_cast<int>(py_round(20 - 15 * fertility)), litter),
            "Carrion gives it " + py_percent(sample.digestion, 0) + format(" of its meat.  Lives about %.0f turns.", sample.lifespan),
        };
    }
    const int home = form_.value({FieldKind::Climate, 1}), hardiness = form_.value({FieldKind::Climate, 0});
    const std::string temperature(TEMPERATURES[static_cast<std::size_t>(home)]);
    if (sample.species == Species::Plant) {
        facts.push_back("At home where it is " + temperature + "; elsewhere it withers.");
    } else {
        constexpr std::array<const char*, 4> hardy = {"", "  Tolerates heat.", "  Tolerates cold.", "  Tolerates heat and cold."};
        facts.push_back("At home where it is " + temperature + "." + hardy[static_cast<std::size_t>(hardiness)]);
    }
    for (std::size_t index = 0; index < facts.size(); ++index) {
        canvas.text(small, facts[index], {187, 199, 181}, preview.x, places.dot_code.bottom() + UI(20 + 22 * static_cast<int>(index)));
    }

    // The trait rows: mind on the left, body on the right.
    canvas.text(font, "MIND  /  INNER DOT", {220, 229, 211}, places.rows[0].x, places.rows[0].y - UI(28));
    canvas.text(font, "BODY  /  OUTER RING", {220, 229, 211}, places.rows[COLUMN_ROWS].x, places.rows[COLUMN_ROWS].y - UI(28));
    for (int index = 0; index < 2 * COLUMN_ROWS; ++index) {
        draw_row(canvas, field_at(index), places.rows[static_cast<std::size_t>(index)], index == form_.focus);
    }

    const std::array<std::tuple<Rect, const char*, bool>, 3> buttons = {
        std::tuple{places.random, "RANDOM", false}, std::tuple{places.cancel, "CANCEL", false}, std::tuple{places.apply, "APPLY", true}};
    for (const auto& [button, label, filled] : buttons) {
        canvas.rect(button, filled ? Color{131, 205, 143} : Color{44, 57, 52}, 0, UI(6));
        canvas.text_at(bold, label, filled ? Color{19, 26, 27} : Color{220, 229, 211},
                       [&](Rect& box) { box.center_at(button.centerx(), button.centery()); });
    }
    canvas.text_at(small,
                   "UP/DOWN  choose   LEFT/RIGHT  adjust (SHIFT x5)   type a value or hex code   ENTER  apply   "
                   "ESC  cancel   F11  window",
                   {130, 152, 136}, [&](Rect& box) { box.midbottom_at(card.centerx(), card.bottom() - UI(84)); });
}

void Designer::draw_row(Canvas& canvas, Field field, const Rect& row, bool focused) {
    TTF_Font* font = UI.font(Face::Sans, 15);
    const int value = form_.value(field);
    canvas.rect(row, focused ? Color{36, 49, 47} : Color{31, 42, 41}, 0, UI(5));
    if (focused) canvas.rect(row, {131, 205, 143}, 1, UI(5));
    const auto [minus, box, plus] = controls(row);
    const std::string_view name = field.kind == FieldKind::Climate ? (field.index == 0 ? "hardiness" : "temperature")
                                  : field.kind == FieldKind::Mind    ? MIND_TRAITS[field.index]
                                                                     : BODY_TRAITS[field.index];
    canvas.text(font, name, {232, 236, 219}, row.x + UI(12), row.y + UI(6));
    const Rect meter{row.x + UI(12), row.bottom() - UI(11), minus.x - row.x - UI(28), UI(4)};
    canvas.fill_rect(meter, {44, 57, 52});
    canvas.fill_rect({meter.x, meter.y, static_cast<int>(py_round(static_cast<double>(meter.w) * value / largest(field))), meter.h},
                     {131, 205, 143});
    for (const auto& [button, sign] : {std::pair{minus, "-"}, std::pair{plus, "+"}}) {
        canvas.rect(button, {44, 57, 52}, 0, UI(4));
        canvas.text_at(UI.font(Face::Sans, 22, true), sign, {220, 229, 211},
                       [&](Rect& text_box) { text_box.center_at(button.centerx(), button.centery()); });
    }
    canvas.rect(box, {19, 26, 27}, 0, UI(4));
    canvas.rect(box, focused ? Color{131, 205, 143} : Color{54, 68, 62}, 1, UI(4));
    std::string shown = std::to_string(value);
    if (field == Field{FieldKind::Body, SpeciesTrait}) shown = std::string(SPECIES[value % 4]) + " " + shown;
    if (field == Field{FieldKind::Climate, 1}) shown = std::string(TEMPERATURES[static_cast<std::size_t>(value)]);
    if (field == Field{FieldKind::Climate, 0}) shown = std::string(HARDINESS[static_cast<std::size_t>(value)]);
    canvas.text_at(font, shown, {238, 238, 220}, [&](Rect& text_box) { text_box.midright_at(box.right() - UI(10), box.centery()); });
}

}  // namespace bitzkey::ui
