#include "ui/Advanced.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "core/PyFormat.hpp"
#include "ui/Language.hpp"

namespace bitzkey::ui {

namespace {

constexpr std::size_t MAX_CHARACTERS = 12;

std::string upper(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

// Shorten text, with an ellipsis, to fit the width.
std::string fit(TTF_Font* font, std::string text, int width) {
    text = tr(text);  // shortened in the language it is shown in
    const AsShown measuring;
    if (Canvas::text_width(font, text) <= width) return as_shown(text);
    while (!text.empty() && Canvas::text_width(font, text + "...") > width) pop_character(text);
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return as_shown(text + "...");
}

int rule_count() { return static_cast<int>(rules::all().size()); }

}  // namespace

// ---------------------------------------------------------------------------------------------
// The form

AdvancedForm::AdvancedForm(const rules::Values& given, bool running_) : running(running_) {
    for (const rules::Rule& rule : rules::all()) {
        const auto found = given.find(rule.key);
        values[rule.key] = found != given.end() ? found->second : rules::defaults().at(rule.key);
    }
}

std::string AdvancedForm::text(const rules::Rule& rule) const {
    if (&rule == &focused() && typed) return *typed;
    return rules::shown(rule, values.at(rule.key));
}

void AdvancedForm::select(int index) {
    settle();
    focus = py_mod(index, rule_count());
}

void AdvancedForm::adjust(int steps) {
    if (!rules::locked(focused().key, running).empty()) return;  // not on this computer
    settle();
    const rules::Rule& rule = focused();
    double& value = values[rule.key];
    value = rule.clamp(py_round(value + steps * rule.step, 9));
}

void AdvancedForm::type_text(const std::string& text) {
    const rules::Rule& rule = focused();
    if (!rules::locked(rule.key, running).empty()) return;
    std::string entry = typed.value_or("");
    for (const char character : text) {
        if (std::isdigit(static_cast<unsigned char>(character)) ||
            (character == '.' && !rule.integer && entry.find('.') == std::string::npos) ||
            (character == '-' && rule.minimum < 0 && entry.empty())) {
            entry += character;
        }
    }
    if (!entry.empty()) typed = entry.substr(0, MAX_CHARACTERS);
}

void AdvancedForm::backspace() {
    if (!rules::locked(focused().key, running).empty()) return;
    std::string shown = text(focused());
    if (!shown.empty()) shown.pop_back();
    typed = shown;
}

void AdvancedForm::reset() {
    typed.reset();
    values[focused().key] = rules::defaults().at(focused().key);
}

void AdvancedForm::reset_all() {
    typed.reset();
    values = rules::defaults();
}

void AdvancedForm::settle() {
    // Replace whatever was typed with the in-range value it stands for; nothing usable leaves the
    // value as it was.
    if (!typed) return;
    try {
        values[focused().key] = focused().clamp(std::stod(*typed));
    } catch (const std::exception&) {
    }
    typed.reset();
}

// ---------------------------------------------------------------------------------------------
// The screen

AdvancedScreen::AdvancedScreen(Window& window, const rules::Values& values, bool running) : window_(window), form_(values, running) {
    const auto& all = rules::all();
    int top = 0;
    for (std::size_t index = 0; index < all.size(); ++index) {
        if (index == 0 || all[index].group != all[index - 1].group) {
            lines_.push_back({all[index].group, top});
            top += HEADING_HEIGHT;
        }
        lines_.push_back({static_cast<int>(index), top});
        top += ROW_HEIGHT;
    }
    list_height_ = top;
}

std::pair<AdvancedScreen::Outcome, std::optional<rules::Values>> AdvancedScreen::run() {
    while (true) {
        const Uint32 frame_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window_.handle(event);
            if (event.type == SDL_QUIT) return {Outcome::Quit, std::nullopt};
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) return {Outcome::Cancel, std::nullopt};
                if (key == SDLK_F11) {
                    window_.toggle_fullscreen();
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    if (ready()) return {Outcome::Apply, form_.changed()};
                } else {
                    this->key(event);
                }
            } else if (event.type == SDL_TEXTINPUT) {
                form_.type_text(event.text.text);
                show_focus();
            } else if (event.type == SDL_MOUSEWHEEL) {
                scroll_by(-event.wheel.y * UI(ROW_HEIGHT));
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                const auto outcome = click(event.button.x, event.button.y);
                if (outcome == Outcome::Cancel) return {Outcome::Cancel, std::nullopt};
                if (outcome == Outcome::Apply && ready()) return {Outcome::Apply, form_.changed()};
            }
        }
        Canvas canvas = window_.frame();
        draw(canvas);
        window_.present();
        const Uint32 spent = SDL_GetTicks() - frame_start;
        if (spent < 33) SDL_Delay(33 - spent);
    }
}

bool AdvancedScreen::ready() {
    form_.finish();
    const auto problems = form_.problems();
    if (!problems.empty()) tell(problems.front() + ".");
    return problems.empty();
}

void AdvancedScreen::tell(const std::string& note) {
    note_ = note;
    note_until_ = SDL_GetTicks() + 5000;
}

void AdvancedScreen::key(const SDL_Event& event) {
    const SDL_Keycode key = event.key.keysym.sym;
    const bool shift = event.key.keysym.mod & KMOD_SHIFT;
    const int steps = shift ? 10 : 1;
    if (key == SDLK_UP || (key == SDLK_TAB && shift)) {
        form_.move(-1);
    } else if (key == SDLK_DOWN || key == SDLK_TAB) {
        form_.move(1);
    } else if (key == SDLK_PAGEUP) {
        form_.select(std::max(0, form_.focus - 10));
    } else if (key == SDLK_PAGEDOWN) {
        form_.select(std::min(rule_count() - 1, form_.focus + 10));
    } else if (key == SDLK_RIGHT) {
        form_.adjust(steps);
    } else if (key == SDLK_LEFT) {
        form_.adjust(-steps);
    } else if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
        form_.backspace();
    } else {
        return;
    }
    show_focus();
}

namespace {
constexpr const char* NATURAL_LAW =
    "These are the settings for what might be called 'natural law'.  They have been carefully balanced to produce a world with fair chances for all.  Messing with any of them can produce unpredictable results.  Have fun!";
constexpr int LAW_LINE = 20;  // design pixels between the lines of the word on natural law

// The word on natural law, wrapped to fit the width.
std::vector<std::string> law_lines(int width) {
    TTF_Font* font = UI.font(Face::Sans, 15);
    std::vector<std::string> lines;
    std::string line, word;
    const std::string text = tr(NATURAL_LAW);
    const AsShown measuring;
    const bool spaced = language() != "zh" && language() != "ja";  // Chinese and Japanese break between any characters
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t space = std::min(text.find(' ', at), text.size());
        if (!spaced && at < text.size()) {
            space = at + 1;
            while (space < text.size() && (static_cast<unsigned char>(text[space]) & 0xC0) == 0x80) ++space;
        }
        word = text.substr(at, space - at);
        const std::string trial = line.empty() ? word : line + (spaced ? " " : "") + word;
        if (!line.empty() && Canvas::text_width(font, trial) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        at = spaced || space >= text.size() ? space + 1 : space;
    }
    lines.push_back(line);
    for (const std::string& each : lines) as_shown(each);
    return lines;
}
}  // namespace

AdvancedScreen::Layout AdvancedScreen::layout() const {
    Layout places;
    places.card = Rect{0, 0, UI(760), std::min(window_.height() - UI(24), UI(800))}.center_at(window_.width() / 2,
                                                                                             window_.height() / 2);
    const Rect& card = places.card;
    const int top = UI(100) + static_cast<int>(law_lines(card.w - UI(56)).size()) * UI(LAW_LINE);  // beneath the word on natural law
    places.view = {card.x + UI(20), card.y + top, card.w - UI(40), card.h - top - UI(118)};
    places.apply = Rect{0, 0, UI(180), UI(44)}.bottomleft_at(card.centerx() + UI(6), card.bottom() - UI(42));
    places.cancel = Rect{0, 0, UI(140), UI(44)}.bottomright_at(card.centerx() - UI(6), card.bottom() - UI(42));
    places.reset_all = Rect{0, 0, UI(100), UI(28)}.topright_at(card.right() - UI(20), card.y + UI(28));
    places.reset = Rect{0, 0, UI(70), UI(28)}.topright_at(places.reset_all.x - UI(8), places.reset_all.y);
    return places;
}

int AdvancedScreen::max_scroll(const Rect& view) const { return std::max(0, UI(list_height_) - view.h); }

void AdvancedScreen::scroll_by(int amount) {
    scroll_ = std::max(0, std::min(max_scroll(layout().view), scroll_ + amount));
}

void AdvancedScreen::show_focus() {
    // Scroll just far enough to show the focused rule, and its heading when it opens a group.
    const Rect view = layout().view;
    std::size_t position = 0;
    while (!(std::holds_alternative<int>(lines_[position].item) && std::get<int>(lines_[position].item) == form_.focus)) {
        ++position;
    }
    const int top = std::holds_alternative<std::string>(lines_[position - 1].item) ? lines_[position - 1].top
                                                                                   : lines_[position].top;
    const int bottom = UI(lines_[position].top + ROW_HEIGHT);
    if (UI(top) < scroll_) {
        scroll_ = UI(top);
    } else if (bottom > scroll_ + view.h) {
        scroll_ = bottom - view.h;
    }
    scroll_ = std::max(0, std::min(max_scroll(view), scroll_));
}

std::vector<std::pair<const AdvancedScreen::Line*, Rect>> AdvancedScreen::rows(const Rect& view) const {
    std::vector<std::pair<const Line*, Rect>> shown;
    for (const Line& line : lines_) {
        const int height = std::holds_alternative<std::string>(line.item) ? HEADING_HEIGHT : ROW_HEIGHT;
        const Rect rect{view.x, view.y + UI(line.top) - scroll_, view.w - UI(14), UI(height - 4)};
        if (rect.bottom() > view.y && rect.y < view.bottom()) shown.emplace_back(&line, rect);
    }
    return shown;
}

std::array<Rect, 3> AdvancedScreen::buttons(const Rect& row) {
    const Rect plus{row.right() - UI(40), row.centery() - UI(13), UI(26), UI(26)};
    const Rect box{plus.x - UI(128), row.centery() - UI(15), UI(120), UI(30)};
    const Rect minus{box.x - UI(34), row.centery() - UI(13), UI(26), UI(26)};
    return {minus, box, plus};
}

std::optional<AdvancedScreen::Outcome> AdvancedScreen::click(int x, int y) {
    const Layout places = layout();
    if (places.apply.contains(x, y)) return Outcome::Apply;
    if (places.cancel.contains(x, y)) return Outcome::Cancel;
    if (places.reset.contains(x, y)) {
        form_.reset();
    } else if (places.reset_all.contains(x, y)) {
        form_.reset_all();
    } else if (places.view.contains(x, y)) {
        for (const auto& [line, row] : rows(places.view)) {
            if (!std::holds_alternative<int>(line->item) || !row.contains(x, y)) continue;
            const int index = std::get<int>(line->item);
            if (index != form_.focus) form_.select(index);
            const auto [minus, box, plus] = buttons(row);
            if (minus.contains(x, y)) form_.adjust(-1);
            else if (plus.contains(x, y)) form_.adjust(1);
        }
    }
    return std::nullopt;
}

void AdvancedScreen::draw(Canvas& canvas) {
    const Layout places = layout();
    const Rect& card = places.card;
    TTF_Font* bold = UI.font(Face::Sans, 22, true);
    TTF_Font* small = UI.font(Face::Sans, 12);
    TTF_Font* button_font = UI.font(Face::Sans, 13, true);
    canvas.fill({19, 26, 27});
    canvas.rect(card, {27, 37, 37}, 0, UI(8));
    canvas.rect(card, {80, 99, 85}, 1, UI(8));
    const int title = canvas.text(bold, "BITZKEY", {229, 239, 220}, card.x + UI(28), card.y + UI(24));
    canvas.text(small, form_.running ? "THIS WORLD  /  ADVANCED RULES" : "NEW WORLD  /  ADVANCED RULES", {144, 170, 151},
                card.x + UI(52) + title, card.y + UI(33));
    const std::vector<std::string> lines = law_lines(card.w - UI(56));
    for (std::size_t index = 0; index < lines.size(); ++index) {
        canvas.text(UI.font(Face::Sans, 15), lines[index], {229, 239, 220}, card.x + UI(28), card.y + UI(66) + static_cast<int>(index) * UI(LAW_LINE));
    }
    const std::string changed = std::string(form_.running ? "This world plays by these from its next turn. " : "The rules the world plays by. ") +
                                std::to_string(form_.changed().size()) + " changed from the defaults; changed values show in amber.";
    canvas.text(UI.fitting(UI.font(Face::Sans, 15), changed, card.w - UI(56)), changed, {157, 177, 159}, card.x + UI(28), card.y + UI(66) + static_cast<int>(lines.size()) * UI(LAW_LINE) + UI(6));
    for (const auto& [button, label] : {std::pair{places.reset, "RESET"}, std::pair{places.reset_all, "RESET ALL"}}) {
        canvas.rect(button, {44, 57, 52}, 0, UI(4));
        canvas.label(button_font, label, {220, 229, 211}, button);
    }

    const Rect& view = places.view;
    scroll_ = std::min(scroll_, max_scroll(view));
    canvas.set_clip(view);
    for (const auto& [line, row] : rows(view)) {
        if (const auto* group = std::get_if<std::string>(&line->item)) {
            canvas.text(button_font, upper(*group), {131, 205, 143}, row.x + UI(4), row.bottom() - UI(22));
        } else {
            const int index = std::get<int>(line->item);
            draw_rule(canvas, rules::all()[static_cast<std::size_t>(index)], row, index == form_.focus);
        }
    }
    canvas.set_clip(std::nullopt);
    draw_scroll_bar(canvas, view);

    canvas.rect(places.apply, {131, 205, 143}, 0, UI(6));
    canvas.label(bold, "APPLY", {19, 26, 27}, places.apply);
    canvas.rect(places.cancel, {44, 57, 52}, 0, UI(6));
    canvas.label(bold, "CANCEL", {220, 229, 211}, places.cancel);
    const bool noting = SDL_GetTicks() < note_until_;
    const std::string_view warning = rules::warning(form_.focused().key);  // the chosen rule's, in full
    canvas.text_at(small,
                   noting ? note_
                   : !warning.empty() ? fit(small, std::string(warning), card.w - UI(48))
                                      : "UP/DOWN  choose   LEFT/RIGHT  adjust (SHIFT x10)   PGUP/PGDN  jump   ENTER  apply   ESC  cancel",
                   noting || !warning.empty() ? Color{238, 183, 110} : Color{130, 152, 136},
                   [&](Rect& box) { box.midbottom_at(card.centerx(), card.bottom() - UI(14)); });
}

void AdvancedScreen::draw_rule(Canvas& canvas, const rules::Rule& rule, const Rect& row, bool focused) {
    canvas.rect(row, focused ? Color{36, 49, 47} : Color{31, 42, 41}, 0, UI(5));
    if (focused) canvas.rect(row, {131, 205, 143}, 1, UI(5));
    const auto [minus, box, plus] = buttons(row);
    TTF_Font* small = UI.font(Face::Sans, 12);
    canvas.text(UI.font(Face::Sans, 15), upper(rule.label), rules::locked(rule.key, form_.running).empty() ? Color{232, 236, 219} : Color{120, 134, 124},
                row.x + UI(14), row.y + UI(3));
    const std::string lock = rules::locked(rule.key, form_.running);  // not to be changed on this computer: greyed, saying why
    const bool warned = lock.empty() && !rules::warning(rule.key).empty();  // a cost to heed: its hint in amber, the warning in full when chosen
    canvas.text(small, fit(small, !lock.empty() ? lock : (warned ? "WARNING  " : "") + rule.hint, minus.x - row.x - UI(28)),
                !lock.empty() ? Color{96, 110, 100} : warned ? Color{238, 183, 110} : Color{130, 152, 136}, row.x + UI(14), row.y + UI(21));
    for (const auto& [button, sign] : {std::pair{minus, "-"}, std::pair{plus, "+"}}) {
        canvas.rect(button, lock.empty() ? Color{44, 57, 52} : Color{34, 44, 42}, 0, UI(4));
        canvas.label(UI.font(Face::Sans, 22, true), sign, lock.empty() ? Color{220, 229, 211} : Color{96, 110, 100}, button);
    }
    canvas.rect(box, {19, 26, 27}, 0, UI(4));
    canvas.rect(box, focused ? Color{131, 205, 143} : Color{54, 68, 62}, 1, UI(4));
    std::string shown = form_.text(rule);
    const bool changed = form_.values.at(rule.key) != rules::defaults().at(rule.key);
    if (focused && form_.typed && SDL_GetTicks() / 500 % 2) shown += "_";
    canvas.text_at(UI.font(Face::Mono, 16), shown, !lock.empty() ? Color{96, 110, 100} : changed ? Color{238, 183, 110} : Color{238, 238, 220},
                   [&](Rect& text_box) { text_box.midright_at(box.right() - UI(10), box.centery()); });
}

void AdvancedScreen::draw_scroll_bar(Canvas& canvas, const Rect& view) {
    const Rect track{view.right() - UI(8), view.y, UI(6), view.h};
    canvas.rect(track, {36, 49, 47}, 0, UI(3));
    const int total = UI(list_height_);
    if (total <= view.h) return;
    const int length = std::max(UI(24), track.h * view.h / total);
    const int top = track.y + (track.h - length) * scroll_ / max_scroll(view);
    canvas.rect({track.x, top, track.w, length}, {80, 99, 85}, 0, UI(3));
}

}  // namespace bitzkey::ui
