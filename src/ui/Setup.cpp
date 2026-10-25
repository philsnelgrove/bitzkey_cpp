#include "ui/Setup.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "core/About.hpp"
#include "core/PyFormat.hpp"
#include "core/WorldFile.hpp"
#include "ui/Advanced.hpp"
#include "ui/CatalogScreen.hpp"
#include "ui/Designer.hpp"
#include "ui/Language.hpp"
#include "ui/Platform.hpp"
#include "ui/WorkersScreen.hpp"

namespace bitzkey::ui {

const std::array<Setting, 10> SETTINGS = {{
    {"width", "Grid width", "cells across the world", 10, MAX_GRID, 10},
    {"height", "Grid height", "cells down the world", 10, MAX_GRID, 10},
    {"population", "Bitzkeys", "spawned at the start, a random mix of species", 0, MAX_POPULATION, 10},
    {"grass_density", "Grass density", "percent of cells with grass at the start; regrowth scales too", 0, 100, 1},
    {"ocean", "Sea", "percent of the map under the sea, laid out by the seed; 0 for land alone", 0, 100, 1},
    {"seed", "Random seed", "the same seed replays the same world; blank for a new one", 0, 999'999'999, 1, true, "random"},
    {"population_cap", "Population cap", "most animals alive at once; blank lets crowding alone decide", 10, 99'999, 100, true,
     "no limit"},
    {"mind_mutation", "Mind mutation", "mind mutations a birth carries: 0 none, 50 about 1.5, 100 three", 0, 100, 5},
    {"body_mutation", "Body mutation", "percent chance a birth flips one body gene: 0 stable, 100 always", 0, 100, 5},
    {"year_length", "Year length", "turns in a year, of four seasons; a day is 8 turns, and lifespans are counted in years", 32, 28800, 80},
}};

namespace {
std::string upper(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// The form

SetupForm::SetupForm(const Defaults& defaults) {
    for (const Setting& setting : SETTINGS) {
        const auto found = defaults.find(setting.key);
        texts[setting.key] = (found == defaults.end() || !found->second) ? "" : std::to_string(*found->second);
        settle(setting);
    }
}

std::optional<int> SetupForm::value(const Setting& setting) const {
    const std::string& text = texts.at(setting.key);
    if (text.empty()) return setting.optional ? std::nullopt : std::optional<int>(setting.minimum);
    return std::max(setting.minimum, std::min(setting.maximum, std::stoi(text)));
}

const std::vector<Imported>& SetupForm::imports(std::string* error) const {
    if (parsed_text_ != import_text) {
        parsed_text_ = import_text;
        parse_error_.clear();
        try {
            parsed_ = import_bitzkeys(import_text);
        } catch (const std::exception& problem) {
            parsed_.clear();
            parse_error_ = problem.what();
        }
    }
    if (error) *error = parse_error_;
    return parsed_;
}

bool SetupForm::can_start() const {
    std::string error;
    imports(&error);
    return error.empty();
}

Simulation::Settings SetupForm::values() const {
    Simulation::Settings settings;
    const auto at = [this](std::string_view key) {
        return value(*std::find_if(SETTINGS.begin(), SETTINGS.end(), [key](const Setting& s) { return s.key == key; }));
    };
    settings.width = *at("width");
    settings.height = *at("height");
    settings.population = *at("population");
    settings.grass_density = *at("grass_density");
    settings.ocean = *at("ocean");
    if (const auto seed = at("seed")) settings.seed = static_cast<std::uint64_t>(*seed);
    settings.population_cap = at("population_cap");
    settings.mind_mutation = *at("mind_mutation");
    settings.body_mutation = *at("body_mutation");
    settings.year_length = *at("year_length");
    const auto& found = imports();
    settings.imported.assign(found.begin(), found.begin() + std::min<std::size_t>(found.size(), MAX_IMPORTED));
    return settings;
}

void SetupForm::select(int index) {
    if (const Setting* setting = focused()) settle(*setting);
    focus = py_mod(index, static_cast<int>(SETTINGS.size()) + 1);
    fresh = true;
}

void SetupForm::adjust(int steps) {
    const Setting* setting = focused();
    if (!setting) return;
    const auto current = value(*setting);
    const int start = current ? *current + steps * setting->step : setting->minimum;
    texts[setting->key] = std::to_string(std::max(setting->minimum, std::min(setting->maximum, start)));
    fresh = true;
}

void SetupForm::type_text(const std::string& text) {
    // Typed characters go into the import box when it has focus; digits into a setting.
    if (on_import()) {
        import_text = (import_text + text).substr(0, MAX_IMPORT_BYTES);
        return;
    }
    for (const char digit : text) {
        if (!std::isdigit(static_cast<unsigned char>(digit))) continue;
        std::string& current = texts[focused()->key];
        std::string typed = fresh ? std::string(1, digit) : current + digit;
        typed.erase(0, typed.find_first_not_of('0') == std::string::npos ? typed.size() : typed.find_first_not_of('0'));
        if (typed.empty()) typed = "0";
        current = typed.substr(0, MAX_DIGITS);
        fresh = false;
    }
}

void SetupForm::backspace() {
    if (on_import()) {
        pop_character(import_text);
        return;
    }
    std::string& current = texts[focused()->key];
    if (fresh) {
        current.clear();
    } else if (!current.empty()) {
        current.pop_back();
    }
    fresh = false;
}

void SetupForm::paste(const std::string& text) {
    select(static_cast<int>(SETTINGS.size()));
    const std::string separator = !import_text.empty() && import_text.back() != '\n' ? "\n" : "";
    type_text(separator + text);
}

void SetupForm::load(const std::string& text) {
    select(static_cast<int>(SETTINGS.size()));
    import_text = text.substr(0, MAX_IMPORT_BYTES);
}

void SetupForm::add_design(Dna dna) {
    paste(dumps(genome_record(dna)));  // all three words, social background and all
}

void SetupForm::settle(const Setting& setting) {
    const auto current = value(setting);
    texts[setting.key] = current ? std::to_string(*current) : "";
}

std::string import_summary(const std::vector<Imported>& bitzkeys) {
    const std::size_t kept = std::min<std::size_t>(bitzkeys.size(), MAX_IMPORTED);
    std::map<std::string, int> species;
    int brains = 0, minds = 0;
    for (std::size_t index = 0; index < kept; ++index) {
        species[std::string(SPECIES[static_cast<std::size_t>(decode_body(bitzkeys[index].dna)[SpeciesTrait])])] += 1;
        brains += bitzkeys[index].brain != nullptr;
        minds += bitzkeys[index].mind.has_value();
    }
    std::string parts;
    for (const auto& [name, count] : species) {
        if (!parts.empty()) parts += ", ";
        parts += std::to_string(count) + " " + name + (count != 1 ? "s" : "");
    }
    std::string summary = std::to_string(kept) + " Bitzkey" + (kept != 1 ? "s" : "") + " ready to import (" + parts + "; " +
                          std::to_string(brains) + " with saved brains, " + std::to_string(minds) +
                          " with saved temperaments), each spawning as a group of " + std::to_string(SPAWN_GROUP[0]) + "-" +
                          std::to_string(SPAWN_GROUP[1]);
    if (bitzkeys.size() > static_cast<std::size_t>(MAX_IMPORTED)) {
        summary += ". Only the first " + std::to_string(MAX_IMPORTED) + " of " + std::to_string(bitzkeys.size()) + " are used";
    }
    return summary;
}

// ---------------------------------------------------------------------------------------------
// The screen

SetupScreen::SetupScreen(Window& window, const Defaults& defaults, net::Pool& pool, net::Worker& worker)
    : window_(window), pool_(pool), worker_(worker), form_(defaults) {}

std::optional<Simulation::Settings> SetupScreen::start_with(const std::string& text) {
    form_.clear_import();
    form_.paste(text);
    if (form_.can_start()) return form_.values();
    return run();
}

std::optional<Simulation::Settings> SetupScreen::run() {
    became_worker_ = false;
    while (true) {
        const Uint32 frame_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window_.handle(event);
            if (event.type == SDL_QUIT) return std::nullopt;
            if (dialog_) {
                if (const auto choice = dialog_->handle(event, window_.width(), window_.height())) {
                    if (*choice == "quit") return std::nullopt;
                    dialog_.reset();
                }
            } else if (event.type == SDL_DROPFILE) {
                load(path_from_utf8(event.drop.file));
                SDL_free(event.drop.file);
            } else if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    ask_to_quit();
                } else if (key == SDLK_F11) {
                    window_.toggle_fullscreen();
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    if (ready()) return form_.values();
                } else if (key == SDLK_v && (event.key.keysym.mod & KMOD_CTRL)) {
                    paste_clipboard();
                } else {
                    this->key(event);
                }
            } else if (event.type == SDL_TEXTINPUT) {
                form_.type_text(event.text.text);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (!dialog_ && language_menu_.click(event.button.x, event.button.y)) continue;
                const Click action = click(event.button.x, event.button.y);
                if (action == Click::WindowClosed) return std::nullopt;
                if (action == Click::Worker) {
                    became_worker_ = true;
                    return std::nullopt;
                }
                if (action == Click::Quit) ask_to_quit();
                if (action == Click::Start && ready()) return form_.values();
                if (action == Click::World) return form_.values();  // a saved world, loaded in `loaded_`, to carry on
            }
        }
        Canvas canvas = window_.frame();
        draw(canvas);
        window_.present();
        const Uint32 spent = SDL_GetTicks() - frame_start;
        if (spent < 33) SDL_Delay(33 - spent);
    }
}

void SetupScreen::ask_to_quit() {
    dialog_.emplace("Quit Bitzkey?", std::vector<std::string>{"The application will close."},
                    std::vector<std::pair<std::string, std::string>>{{"stay", "CANCEL"}, {"quit", "QUIT"}}, "stay");
}

bool SetupScreen::ready() {
    if (!form_.can_start()) tell("Fix or clear the import text before starting.");
    return form_.can_start();
}

void SetupScreen::tell(const std::string& note) {
    note_ = note;
    note_until_ = SDL_GetTicks() + 5000;
}

void SetupScreen::key(const SDL_Event& event) {
    const SDL_Keycode key = event.key.keysym.sym;
    const bool shift = event.key.keysym.mod & KMOD_SHIFT;
    const int steps = shift ? 10 : 1;
    if (key == SDLK_UP || (key == SDLK_TAB && shift)) {
        form_.move(-1);
    } else if (key == SDLK_DOWN || key == SDLK_TAB) {
        form_.move(1);
    } else if (key == SDLK_RIGHT) {
        form_.adjust(steps);
    } else if (key == SDLK_LEFT) {
        form_.adjust(-steps);
    } else if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
        form_.backspace();
    }
}

bool SetupScreen::design() {
    const auto [outcome, dna] = Designer(window_, last_design_).run();
    if (outcome == Designer::Outcome::Quit) return false;
    if (dna) {
        last_design_ = dna;
        form_.add_design(*dna);
        tell("Added a designed " + std::string(SPECIES[static_cast<std::size_t>(decode_body(*dna)[SpeciesTrait])]) +
             " to the import list.");
    }
    return true;
}

bool SetupScreen::catalog() {
    // The Bitzkeys chosen in the catalog join the bottom of the import text, brains and all.
    const auto [outcome, entries] = CatalogScreen(window_).run();
    if (outcome == CatalogScreen::Outcome::Quit) return false;
    if (!entries.empty()) {
        form_.paste(format_records(entries));
        tell(entries.size() == 1 ? std::string("Added a Bitzkey from the catalog to the import list, with its trained brain.")
                                 : "Added " + std::to_string(entries.size()) +
                                       " Bitzkeys from the catalog to the import list, with their trained brains.");
    }
    return true;
}

bool SetupScreen::advanced() {
    const auto [outcome, rules] = AdvancedScreen(window_, rules_).run();
    if (outcome == AdvancedScreen::Outcome::Quit) return false;
    if (rules) {
        rules_ = *rules;
        tell(std::to_string(rules_.size()) + " advanced rule" + (rules_.size() != 1 ? "s" : "") +
             " changed from the defaults.");
    }
    return true;
}

void SetupScreen::paste_clipboard() {
    if (const auto text = clipboard_text()) {
        form_.paste(*text);
    } else {
        tell("The clipboard has no text to paste.");
    }
}

void SetupScreen::choose_file() {
    if (const auto path = ui::choose_file(data_dir() / "exports")) load(*path);
}

void SetupScreen::load(const std::filesystem::path& path) {
    const std::string name = utf8_name(path);
    try {
        if (std::filesystem::file_size(path) > MAX_IMPORT_BYTES) {
            tell(name + " is too large to import.");
            return;
        }
        std::string text = read_file(path);
        std::erase(text, '\r');
        form_.load(text);
    } catch (const std::exception& error) {
        tell("Couldn't read " + name + ": " + error.what());
        return;
    }
    tell("Loaded " + name + ".");
}

bool SetupScreen::load_world() {
    const auto path = choose_world_file(data_dir() / "worlds");
    if (!path) return false;
    const std::string name = utf8_name(*path);
    tell("Loading " + name + ", please be patient...");
    Canvas canvas = window_.frame();
    draw(canvas);
    window_.present();
    SDL_PumpEvents();
    try {
        loaded_ = WorldFile::load(*path);
    } catch (const std::exception& error) {
        tell("Couldn't load " + name + ": " + error.what());
        return false;
    }
    return true;
}

SetupScreen::Layout SetupScreen::layout() const {
    Layout places;
    const int settings = static_cast<int>(SETTINGS.size());
    places.card = Rect{0, 0, UI(640), UI(110 + settings * ROW_HEIGHT + IMPORT_HEIGHT + 110)}.center_at(window_.width() / 2,
                                                                                                         window_.height() / 2);
    const Rect& card = places.card;
    for (int index = 0; index < settings; ++index) {
        places.rows.push_back({card.x + UI(20), card.y + UI(104 + index * ROW_HEIGHT), card.w - UI(40), UI(ROW_HEIGHT - 6)});
    }
    const Rect section{card.x + UI(20), places.rows.back().bottom() + UI(14), card.w - UI(40), UI(IMPORT_HEIGHT - 14)};
    places.clear = Rect{0, 0, UI(64), UI(28)}.topright_at(section.right(), section.y);
    places.load = Rect{0, 0, UI(92), UI(28)}.topright_at(places.clear.x - UI(8), section.y);
    places.paste = Rect{0, 0, UI(70), UI(28)}.topright_at(places.load.x - UI(8), section.y);
    places.design = Rect{0, 0, UI(80), UI(28)}.topright_at(places.paste.x - UI(8), section.y);
    places.catalog = Rect{0, 0, UI(122), UI(28)}.topright_at(places.design.x - UI(8), section.y);
    places.box = {section.x, section.y + UI(36), section.w, UI(16 * TEXT_LINES + 12)};
    places.advanced = Rect{0, 0, UI(120), UI(28)}.topright_at(card.right() - UI(20), card.y + UI(28));
    places.world = Rect{0, 0, UI(140), UI(44)}.bottomleft_at(card.x + UI(20), card.bottom() - UI(42));
    places.worker = Rect{0, 0, UI(150), UI(44)}.bottomright_at(card.right() - UI(20), card.bottom() - UI(42));
    const int middle = (places.world.right() + places.worker.x) / 2;  // QUIT and START between them
    places.quit = Rect{0, 0, UI(110), UI(44)}.bottomright_at(middle - UI(5), card.bottom() - UI(42));
    places.start = Rect{0, 0, UI(140), UI(44)}.bottomleft_at(middle + UI(5), card.bottom() - UI(42));
    return places;
}

std::array<Rect, 3> SetupScreen::buttons(const Rect& row) {
    const Rect plus{row.right() - UI(44), row.centery() - UI(15), UI(30), UI(30)};
    const Rect box{plus.x - UI(150), row.centery() - UI(17), UI(142), UI(34)};
    const Rect minus{box.x - UI(38), row.centery() - UI(15), UI(30), UI(30)};
    return {minus, box, plus};
}

SetupScreen::Click SetupScreen::click(int x, int y) {
    const Layout places = layout();
    if (places.start.contains(x, y)) return Click::Start;
    if (places.quit.contains(x, y)) return Click::Quit;
    if (places.world.contains(x, y)) return load_world() ? Click::World : Click::None;
    if (places.worker.contains(x, y)) {
        const auto outcome = WorkersScreen(window_, pool_, worker_, nullptr).run();
        if (outcome == WorkersScreen::Outcome::Quit) return Click::WindowClosed;
        return outcome == WorkersScreen::Outcome::Worker ? Click::Worker : Click::None;
    }
    if (places.design.contains(x, y)) return design() ? Click::None : Click::WindowClosed;
    if (places.catalog.contains(x, y)) return catalog() ? Click::None : Click::WindowClosed;
    if (places.advanced.contains(x, y)) return advanced() ? Click::None : Click::WindowClosed;
    if (places.paste.contains(x, y)) {
        paste_clipboard();
    } else if (places.load.contains(x, y)) {
        choose_file();
    } else if (places.clear.contains(x, y)) {
        form_.clear_import();
    } else if (places.box.contains(x, y)) {
        form_.select(static_cast<int>(SETTINGS.size()));
    }
    for (std::size_t index = 0; index < places.rows.size(); ++index) {
        const Rect& row = places.rows[index];
        if (!row.contains(x, y)) continue;
        if (static_cast<int>(index) != form_.focus) form_.select(static_cast<int>(index));
        const auto [minus, box, plus] = buttons(row);
        if (minus.contains(x, y)) form_.adjust(-1);
        else if (plus.contains(x, y)) form_.adjust(1);
    }
    return Click::None;
}

void SetupScreen::draw(Canvas& canvas) {
    const Layout places = layout();
    const Rect& card = places.card;
    TTF_Font* bold = UI.font(Face::Sans, 22, true);
    TTF_Font* small = UI.font(Face::Sans, 12);
    canvas.fill({19, 26, 27});
    canvas.rect(card, {27, 37, 37}, 0, UI(8));
    canvas.rect(card, {80, 99, 85}, 1, UI(8));
    const int title = canvas.text(bold, "BITZKEY", {229, 239, 220}, card.x + UI(28), card.y + UI(24));
    const int subtitle = canvas.text(small, "NEW WORLD  /  CONFIGURATION", {144, 170, 151}, card.x + UI(52) + title, card.y + UI(33));
    canvas.text(small, "/  " + about(), {144, 170, 151}, card.x + UI(60) + title + subtitle, card.y + UI(33));  // the version, and the cores
    canvas.text(UI.font(Face::Sans, 15), "Set up the world, then start the simulation.", {157, 177, 159}, card.x + UI(28),
                card.y + UI(66));
    const bool changed = !rules_.empty();
    canvas.rect(places.advanced, changed ? Color{238, 183, 110} : Color{44, 57, 52}, 0, UI(4));
    canvas.label(UI.font(Face::Sans, 13, true), "ADVANCED", changed ? Color{19, 26, 27} : Color{220, 229, 211}, places.advanced);
    if (changed) {
        canvas.text_at(small, std::to_string(rules_.size()) + " rule" + (rules_.size() != 1 ? "s" : "") + " changed",
                       {238, 183, 110}, [&](Rect& box) { box.midtop_at(places.advanced.centerx(), places.advanced.bottom() + UI(4)); });
    }
    for (std::size_t index = 0; index < SETTINGS.size(); ++index) {
        draw_setting(canvas, SETTINGS[index], places.rows[index], static_cast<int>(index) == form_.focus);
    }
    draw_import(canvas, places);

    canvas.rect(places.start, form_.can_start() ? Color{131, 205, 143} : Color{70, 88, 76}, 0, UI(6));
    canvas.label(bold, "START", {19, 26, 27}, places.start);
    canvas.rect(places.quit, {44, 57, 52}, 0, UI(6));
    canvas.label(bold, "QUIT", {220, 229, 211}, places.quit);
    canvas.rect(places.world, {44, 57, 52}, 0, UI(6));
    canvas.label(UI.font(Face::Sans, 13, true), "LOAD WORLD", {220, 229, 211}, places.world);
    canvas.rect(places.worker, {44, 57, 52}, 0, UI(6));
    canvas.label(UI.font(Face::Sans, 13, true), "BECOME A WORKER", {220, 229, 211}, places.worker);
    const bool noting = SDL_GetTicks() < note_until_;
    canvas.text_at(small,
                   noting ? note_
                          : "UP/DOWN  choose   LEFT/RIGHT  adjust (SHIFT x10)   CTRL+V  paste   ENTER  start   F11  window   "
                            "ESC  quit",
                   noting ? Color{238, 183, 110} : Color{130, 152, 136},
                   [&](Rect& box) { box.midbottom_at(card.centerx(), card.bottom() - UI(14)); });
    language_menu_.draw_button(canvas, canvas.width() - UI(24), UI(31));
    language_menu_.draw_list(canvas);
    if (dialog_) dialog_->draw(canvas);
}

void SetupScreen::draw_setting(Canvas& canvas, const Setting& setting, const Rect& row, bool focused) {
    canvas.rect(row, focused ? Color{36, 49, 47} : Color{31, 42, 41}, 0, UI(5));
    if (focused) canvas.rect(row, {131, 205, 143}, 1, UI(5));
    const auto [minus, box, plus] = buttons(row);
    const int room = minus.x - row.x - UI(24);  // the label and hint stay clear of the buttons
    canvas.text(UI.fitting(UI.font(Face::Sans, 15), upper(setting.label), room), upper(setting.label), {232, 236, 219}, row.x + UI(16),
                row.y + UI(3));
    canvas.text(UI.fitting(UI.font(Face::Sans, 12), setting.hint, room), setting.hint, {130, 152, 136}, row.x + UI(16), row.y + UI(22));
    for (const auto& [button, sign] : {std::pair{minus, "-"}, std::pair{plus, "+"}}) {
        canvas.rect(button, {44, 57, 52}, 0, UI(4));
        canvas.label(UI.font(Face::Sans, 22, true), sign, {220, 229, 211}, button);
    }
    canvas.rect(box, {19, 26, 27}, 0, UI(4));
    canvas.rect(box, focused ? Color{131, 205, 143} : Color{54, 68, 62}, 1, UI(4));
    const std::string& text = form_.texts.at(setting.key);
    std::string shown = text.empty() ? std::string(setting.blank) : text;
    const Color color = text.empty() ? Color{116, 139, 124} : Color{238, 238, 220};
    if (focused && !form_.fresh && SDL_GetTicks() / 500 % 2) shown += "_";
    canvas.text_at(UI.font(Face::Mono, 18), shown, color, [&](Rect& text_box) { text_box.midright_at(box.right() - UI(12), box.centery()); });
}

void SetupScreen::draw_import(Canvas& canvas, const Layout& places) {
    const Rect& box = places.box;
    const bool focused = form_.on_import();
    TTF_Font* mono_small = UI.font(Face::Mono, 12);
    canvas.text(UI.fitting(UI.font(Face::Sans, 15), "IMPORT BITZKEYS", places.catalog.x - box.x - UI(8)), "IMPORT BITZKEYS",
                {232, 236, 219}, box.x, places.paste.y + UI(5));
    for (const auto& [button, label] : {std::pair{places.catalog, "FROM CATALOG"}, std::pair{places.design, "DESIGN"},
                                        std::pair{places.paste, "PASTE"},
                                        std::pair{places.load, "LOAD FILE"}, std::pair{places.clear, "CLEAR"}}) {
        canvas.rect(button, {44, 57, 52}, 0, UI(4));
        canvas.label(UI.font(Face::Sans, 13, true), label, {220, 229, 211}, button);
    }
    canvas.rect(box, {19, 26, 27}, 0, UI(4));
    canvas.rect(box, focused ? Color{131, 205, 143} : Color{54, 68, 62}, 1, UI(4));

    std::vector<std::string> lines;
    Color color{187, 199, 181};
    const bool placeholder = form_.import_text.empty() && !focused;  // wrapped to the box already
    if (placeholder) {
        lines = wrapped_lines(mono_small,
                              "Paste exported genomes here (CTRL+V), drop a file onto the window, load one, pick one FROM CATALOG, "
                              "or DESIGN your own. Imported Bitzkeys join the ones spawned above.",
                              box.w - UI(20));
        color = {116, 139, 124};
    } else {
        std::size_t start = 0;
        while (true) {
            const std::size_t end = form_.import_text.find('\n', start);
            lines.push_back(form_.import_text.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (lines.size() > TEXT_LINES) lines.erase(lines.begin(), lines.end() - TEXT_LINES);
        if (focused && SDL_GetTicks() / 500 % 2) lines.back() += "_";
    }
    const int columns = (box.w - UI(20)) / std::max(1, Canvas::text_width(mono_small, "M"));
    for (std::size_t index = 0; index < lines.size(); ++index) {
        std::string shown = lines[index];
        if (!placeholder && static_cast<int>(shown.size()) > columns) shown = shown.substr(0, static_cast<std::size_t>(std::max(0, columns - 1))) + "~";
        canvas.text(mono_small, as_shown(shown), color, box.x + UI(10), box.y + UI(7 + 16 * static_cast<int>(index)));  // as typed
    }

    std::string error;
    const auto& found = form_.imports(&error);
    std::string status;
    Color status_color;
    if (!error.empty()) {
        status = "Can't import: " + error;
        status_color = {238, 183, 110};
    } else if (!found.empty()) {
        status = import_summary(found);
        status_color = {131, 205, 143};
    } else {
        status = "Nothing to import.";
        status_color = {116, 139, 124};
    }
    canvas.text(UI.font(Face::Sans, 12), status, status_color, box.x, box.bottom() + UI(8));
}

}  // namespace bitzkey::ui
