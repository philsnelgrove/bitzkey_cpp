#include "ui/App.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>

#include "core/Exchange.hpp"
#include "core/Insights.hpp"
#include "core/PyFormat.hpp"
#include "core/Catalog.hpp"
#include "ui/Advanced.hpp"
#include "ui/CatalogScreen.hpp"
#include "ui/Inspector.hpp"
#include "ui/Look.hpp"
#include "ui/Platform.hpp"

namespace bitzkey::ui {

namespace {

// What an exemplar card can show: the living animal that stands out most by a statistic, highest, or
// lowest where `lowest`. A Mind statistic is an inborn temperament trait, a Body one a body trait, Home
// its home temperature, and a State one what it is now.
enum class StatKind { Mind, Body, Home, State };
enum StateStat { Age, Children, Generation, Hunger, Health, Lifespan, Burn, Stomach, Depth, Herd, Changed };
struct ExemplarStat {
    const char* title;
    StatKind kind;
    int index;  // the trait, or the StateStat
    bool lowest;
};
constexpr std::array<ExemplarStat, 50> EXEMPLAR_STATS = {{
    {"SMARTEST", StatKind::Mind, Intellect, false}, {"MOST AGGRESSIVE", StatKind::Mind, Aggression, false},
    {"MOST AMOROUS", StatKind::Mind, Libido, false}, {"MOST SOCIABLE", StatKind::Mind, Sociability, false},
    {"MOST CURIOUS", StatKind::Mind, Curiosity, false}, {"MOST FEARFUL", StatKind::Mind, Fear, false},
    {"GREATEST APPETITE", StatKind::Mind, Appetite, false}, {"MOST PATIENT", StatKind::Mind, Patience, false},
    {"MOST EMPATHETIC", StatKind::Mind, Empathy, false}, {"MOST TOLERANT", StatKind::Mind, Tolerance, false},
    {"BEST MEMORY", StatKind::Mind, Memory, false}, {"MOST RESILIENT", StatKind::Mind, Resilience, false},
    {"MOODIEST", StatKind::Mind, Moodiness, false}, {"MOST LOYAL", StatKind::Mind, Loyalty, false},
    {"MOST DOMINANT", StatKind::Mind, Dominance, false}, {"MOST TRUSTING", StatKind::Mind, Trust, false},
    {"MOST CLANNISH", StatKind::Mind, Kinship, false},
    {"SIMPLEST", StatKind::Mind, Intellect, true}, {"GENTLEST", StatKind::Mind, Aggression, true},
    {"BRAVEST", StatKind::Mind, Fear, true}, {"MOST SOLITARY", StatKind::Mind, Sociability, true},
    {"LARGEST", StatKind::Body, Size, false}, {"SMALLEST", StatKind::Body, Size, true},
    {"STRONGEST", StatKind::Body, Strength, false}, {"BEST ARMOURED", StatKind::Body, Armour, false},
    {"BEST ARMED", StatKind::Body, Weapons, false}, {"FASTEST", StatKind::Body, Speed, false},
    {"SLOWEST", StatKind::Body, Speed, true}, {"SHARPEST-EYED", StatKind::Body, Sight, false},
    {"MOST FERTILE", StatKind::Body, Fertility, false}, {"BIGGEST LITTERS", StatKind::Body, Litter, false},
    {"BEST SCAVENGER", StatKind::Body, Carrion, false},
    {"HOTTEST HOME", StatKind::Home, 0, true}, {"COLDEST HOME", StatKind::Home, 0, false},
    {"OLDEST", StatKind::State, Age, false}, {"YOUNGEST", StatKind::State, Age, true},
    {"MOST CHILDREN", StatKind::State, Children, false}, {"LATEST GENERATION", StatKind::State, Generation, false},
    {"BEST FED", StatKind::State, Hunger, true}, {"HUNGRIEST", StatKind::State, Hunger, false},
    {"HEALTHIEST", StatKind::State, Health, false}, {"MOST WOUNDED", StatKind::State, Health, true},
    {"LONGEST LIFESPAN", StatKind::State, Lifespan, false}, {"SHORTEST LIFESPAN", StatKind::State, Lifespan, true},
    {"FASTEST METABOLISM", StatKind::State, Burn, false}, {"SLOWEST METABOLISM", StatKind::State, Burn, true},
    {"BIGGEST STOMACH", StatKind::State, Stomach, false}, {"DEEPEST BRAIN", StatKind::State, Depth, false},
    {"MOST HERD-MINDED", StatKind::State, Herd, false}, {"MOST CHANGED BY EXPERIENCE", StatKind::State, Changed, false},
}};
// The statistic each card shows, left column then right, top to bottom; changed from a card's title,
// and kept for the rest of the session.
// SMARTEST, LARGEST, OLDEST, MOST CHILDREN, LATEST GENERATION; STRONGEST, FASTEST, BEST ARMOURED, MOST FERTILE, BEST FED
std::array<int, 10> exemplar_choices = {0, 21, 34, 36, 37, 23, 26, 24, 29, 38};
constexpr std::array<const char*, 4> HOMES = {"hot", "medium", "cold", "very cold"};
constexpr int MENU_ROW = 20, MENU_COLUMN = 200;  // an exemplar menu's rows and columns, in design pixels

double exemplar_value(const Bitzkey& agent, const ExemplarStat& stat) {
    switch (stat.kind) {
        case StatKind::Mind: return agent.inborn[stat.index];
        case StatKind::Body: return agent.body[stat.index];
        case StatKind::Home: return decode_climate(agent.dna).temperature;
        case StatKind::State: break;
    }
    switch (stat.index) {
        case Age: return agent.age;
        case Children: return agent.children;
        case Generation: return agent.generation;
        case Hunger: return agent.hunger;
        case Health: return agent.health / agent.max_health;
        case Lifespan: return agent.lifespan;
        case Burn: return agent.burn;
        case Stomach: return agent.stomach;
        case Depth: return agent.brain ? agent.brain->depth() : 0;
        case Herd: return agent.herd_instinct();
        default: {  // how far experience has moved its temperament from what it was born with
            std::vector<double> shifts;
            for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) shifts.push_back(std::abs(agent.mind[trait] - agent.inborn[trait]));
            return py_sum(shifts);
        }
    }
}

std::string decimals(const char* pattern, double value) {
    char text[48];
    std::snprintf(text, sizeof text, pattern, value);
    return text;
}

std::string exemplar_text(const Bitzkey& agent, const ExemplarStat& stat) {
    const double value = exemplar_value(agent, stat);
    const int whole = static_cast<int>(value);
    switch (stat.kind) {
        case StatKind::Mind: return std::string(TEMPERAMENT[static_cast<std::size_t>(stat.index)]) + " " + std::to_string(whole);
        case StatKind::Body: return std::string(BODY_TRAITS[static_cast<std::size_t>(stat.index)]) + " " + std::to_string(whole);
        case StatKind::Home: return std::string("home ") + HOMES[static_cast<std::size_t>(whole)];
        case StatKind::State: break;
    }
    switch (stat.index) {
        case Age: return "age " + std::to_string(whole);
        case Children: return std::to_string(whole) + " children";
        case Generation: return "generation " + std::to_string(whole);
        case Hunger: return "hunger " + std::to_string(whole) + "%";
        case Health: return "health " + std::to_string(static_cast<int>(value * 100)) + "%";
        case Lifespan: return "lifespan " + std::to_string(whole) + " turns";
        case Burn: return decimals("burn %.2f", value);
        case Stomach: return decimals("stomach %.2f", value);
        case Depth: return "brain " + std::to_string(whole) + " deep";
        case Herd: return decimals("herd instinct %.2f", value);
        default: return decimals("changed %.1f", value);
    }
}
constexpr int EXEMPLAR_WIDTH = 170;
constexpr int MAP_DRAG_THRESHOLD = 5;  // design pixels a press must move on the map to draw a marquee rather than click  // each column of exemplars, in design pixels

}  // namespace

namespace {

// Seconds between turns at each speed; 0 runs as many turns as fit in a frame.
// Each speed twice the last; MAX is 128x, as fast as the turns can be simulated, 25 ms a frame at most.
constexpr std::array<double, 8> TURN_INTERVALS = {0.24, 0.12, 0.06, 0.03, 0.015, 0.0075, 0.00375, 0.001875};
constexpr std::array<const char*, 8> SPEED_LABELS = {"1x", "2x", "4x", "8x", "16x", "32x", "64x", "MAX"};
constexpr Uint32 FRAME_MS = 33;      // about 30 frames a second
constexpr Uint32 MAX_TURN_MS = 25;   // at full speed, the most of a frame spent simulating
constexpr int INSIGHTS_TOP = 290;    // the insights panel's top, in design pixels: below the population
constexpr double MAP_ZOOM_STEP = 1.25;  // each wheel notch zooms the map in (or out) by this much
constexpr double MAP_ZOOM_MAX = 12.0;   // the closest the map zooms in, times its whole-map size
constexpr double TRAIL_OPACITY = 0.5;   // how faintly a Bitzkey shows on each cell it passed through this turn

std::string format(const char* pattern, auto... values) {
    char buffer[512];
    std::snprintf(buffer, sizeof buffer, pattern, values...);
    return buffer;
}

std::string upper(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

std::string plural(std::size_t count, const char* word = "Bitzkey") {
    return std::to_string(count) + " " + word + (count != 1 ? "s" : "");
}

nlohmann::ordered_json genome_only(Dna dna) {
    nlohmann::ordered_json record = nlohmann::ordered_json::object();
    record["version"] = GENOME_VERSION;
    record["ring"] = hex_word(body_word(dna));
    record["dot"] = hex_word(mind_word(dna));
    return record;
}

}  // namespace

App::App(Window& window, Simulation::Settings settings) : window_(window), settings_(std::move(settings)) { new_world(); }

void App::new_world() { simulation_ = std::make_unique<Simulation>(settings_); }

App::Result App::run() {
    Uint32 last = SDL_GetTicks();
    while (true) {
        const Uint32 frame_start = SDL_GetTicks();
        const double seconds = (frame_start - last) / 1000.0;
        last = frame_start;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window_.handle(event);
            if (event.type == SDL_QUIT) return Result::Quit;
            if (report_) {
                if (report_->handle(event, window_.width(), window_.height())) report_.reset();
            } else if (dialog_) {
                if (const auto choice = dialog_->handle(event, window_.width(), window_.height())) {
                    Dialog current = *dialog_;
                    if (answer(*choice)) {
                        leave(std::move(current));
                        return Result::Configure;
                    }
                }
            } else if (!deploying_.empty() && !show_census_ && deploy_event(event)) {
                // deploying from the catalog
            } else if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE && exemplar_menu_) {
                    exemplar_menu_.reset();  // just closes the menu
                } else if (key == SDLK_ESCAPE) {
                    if (show_census_) {
                        if (leave_graph()) return Result::Configure;
                    } else {
                        ask_to_leave();
                    }
                } else if (key == SDLK_F11) {
                    window_.toggle_fullscreen();
                } else if (key == SDLK_SPACE) {
                    if (simulation_->ended()) say(end_note());
                    else playing_ = !playing_;
                } else if (key == SDLK_RIGHT) {
                    playing_ = false;
                    simulation_->advance();
                } else if (key == SDLK_LEFT) {
                    playing_ = false;
                    simulation_->rewind();
                } else if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS) {
                    speed_ = std::min(speed_ + 1, static_cast<int>(TURN_INTERVALS.size()) - 1);
                    say(std::string("Speed ") + SPEED_LABELS[static_cast<std::size_t>(speed_)] + ".");
                } else if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
                    speed_ = std::max(speed_ - 1, 0);
                    say(std::string("Speed ") + SPEED_LABELS[static_cast<std::size_t>(speed_)] + ".");
                } else if (key == SDLK_r) {
                    new_world();
                    selected_id_.reset();
                    graph_.reset();
                } else if (key == SDLK_i) {
                    show_insights_ = !show_insights_;
                } else if (key == SDLK_b) {
                    save_report();
                } else if (key == SDLK_g) {
                    if (!show_census_) show_census_ = true;
                    else if (leave_graph()) return Result::Configure;
                } else if (key == SDLK_TAB && show_census_) {
                    graph_.switch_part();
                } else if (key == SDLK_c) {
                    if (show_census_) export_graph(false);
                    else copy_selected();
                } else if (key == SDLK_s && show_census_) {
                    export_graph(true);
                } else if (key == SDLK_a && !show_census_) {
                    toggle_export();
                } else if (key == SDLK_e) {
                    open_export_dialog();
                } else if (key == SDLK_k) {
                    if (show_census_) catalog_picked();
                    else catalog_selected();
                } else if (key == SDLK_z && show_census_) {
                    graph_.unzoom();  // the whole run, its whole height
                } else if (key == SDLK_z) {
                    map_zoom_ = 1.0;  // the whole map again
                    map_centre_.reset();
                }
            } else if (show_insights_ && !show_census_ && scroll_event(event)) {
                // the insights panel scrolled
            } else if (!show_census_ && map_event(event)) {
                // the map zoomed or panned
            } else if (show_census_ && (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
                                        event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL)) {
                const ColourGraph::Action action = graph_.handle(event);
                if (action == ColourGraph::Action::Seek) seek(graph_.seek_turn());
                else if (action == ColourGraph::Action::Map) close_graph();  // at the run's end, to look over the map
                else if (action == ColourGraph::Action::Catalog) catalog_picked();
                else if (action == ColourGraph::Action::Open) browse_catalog();
                else if (action == ColourGraph::Action::Renew) {
                    if (renew()) return Result::Renew;
                }
                else if (action != ColourGraph::Action::None) export_graph(action == ColourGraph::Action::Save);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                Canvas canvas = window_.frame();
                if (!exemplar_menu_ && map_view(canvas).first.contains(event.button.x, event.button.y)) {
                    marquee_from_ = std::tuple{event.button.x, event.button.y, (SDL_GetModState() & KMOD_CTRL) != 0};
                } else {
                    click(event.button.x, event.button.y);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && marquee_from_) {
                const auto [start_x, start_y, adding] = *marquee_from_;
                marquee_from_.reset();
                pick_on_map(start_x, start_y, event.button.x, event.button.y, adding);
            }
        }
        tick(seconds);
        Canvas canvas = window_.frame();
        draw(canvas);
        window_.present();
        const Uint32 spent = SDL_GetTicks() - frame_start;
        if (spent < FRAME_MS) SDL_Delay(FRAME_MS - spent);
    }
}

void App::tick(double seconds) {
    // While playing, advance at the chosen speed, and stop to show the graph once the run has ended.
    elapsed_ = std::min(elapsed_ + seconds, TURN_INTERVALS.front() + 0.01);  // time paused isn't made up for
    if (!playing_ || dialog_ || report_) return;
    if (simulation_->ended()) {
        finish();
        return;
    }
    const double interval = TURN_INTERVALS[static_cast<std::size_t>(speed_)];
    if (interval > 0) {
        // As many turns as the time since the last calls for (32x and 64x need several a frame), but
        // never more than MAX_TURN_MS of them: a world too big to keep up runs as fast as it can.
        const Uint32 start = SDL_GetTicks();
        while (elapsed_ >= interval && !simulation_->ended()) {
            elapsed_ -= interval;
            simulation_->advance();
            if (SDL_GetTicks() - start >= MAX_TURN_MS) {
                elapsed_ = 0;  // behind: start afresh rather than racing to catch up
                break;
            }
        }
    } else {
        const Uint32 start = SDL_GetTicks();
        do {
            simulation_->advance();
        } while (!simulation_->ended() && SDL_GetTicks() - start < MAX_TURN_MS);
    }
    if (simulation_->ended()) finish();
}

std::string App::end_note() const {
    const std::string cause = simulation_->end_reason() == EndReason::Extinct ? "every animal has died"
                                                                              : "the world is full, with no cell left free";
    return format("The simulation ended at turn %d: %s.", simulation_->turn(), cause.c_str());
}

void App::finish() {
    playing_ = false;
    show_census_ = true;
    say(end_note());
}

void App::seek(int turn) {
    // Pause and move the world to a turn already simulated, as far back as the history reaches.
    playing_ = false;
    simulation_->seek(turn);
    if (turn < simulation_->turn()) {
        say(format("The history reaches back only to turn %d; moved there.", simulation_->turn()));
    } else {
        say(format("Moved to turn %d.", simulation_->turn()));
    }
}

void App::click(int x, int y) {
    if (exemplar_menu_) {  // a click picks a statistic or closes the menu
        for (const auto& [item, stat] : menu_items_) {
            if (item.contains(x, y)) exemplar_choices[static_cast<std::size_t>(*exemplar_menu_)] = stat;
        }
        exemplar_menu_.reset();
        return;
    }
    for (std::size_t slot = 0; slot < exemplar_titles_.size(); ++slot) {
        if (exemplar_titles_[slot].contains(x, y)) {
            exemplar_menu_ = static_cast<int>(slot);
            return;
        }
    }
    if (deaths_button_ && deaths_button_->contains(x, y)) {
        open_death_report();
    } else if (brains_button_ && brains_button_->contains(x, y)) {
        inspect_brains();
    } else if (add_button_ && add_button_->contains(x, y)) {
        toggle_export();
    } else if (catalog_button_ && catalog_button_->contains(x, y)) {
        catalog_selected();
    } else if (export_button_.contains(x, y)) {
        open_export_dialog();
    } else if (open_button_.contains(x, y)) {
        browse_catalog();
    } else if (advanced_button_.contains(x, y)) {
        edit_rules();
    } else if (const auto card = std::find_if(exemplar_cards_.begin(), exemplar_cards_.end(),
                                              [&](const auto& entry) { return entry.first.contains(x, y); });
               card != exemplar_cards_.end()) {
        select(card->second);
    } else {
        Canvas canvas = window_.frame();
        if (map_view(canvas).first.contains(x, y)) {  // a click on the map, pressed and let go at once
            pick_on_map(x, y, x, y, (SDL_GetModState() & KMOD_CTRL) != 0);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Dialogs and the export list

void App::open(const std::string& kind, Dialog dialog) {
    dialog_kind_ = kind;
    dialog_.emplace(std::move(dialog));
}

bool App::answer(const std::string& choice) {
    const std::string kind = dialog_kind_;
    dialog_.reset();
    if (kind == "leave") return choice == "leave";
    if (choice == "save" || choice == "copy") {
        export_chosen(choice == "save");
    } else if (choice == "catalog") {
        catalog_export_list();
    } else if (choice == "clear") {
        const std::size_t count = export_list_.size();
        export_list_.clear();
        say("Cleared " + plural(count) + " from the export list.");
    }
    return false;
}

void App::leave(Dialog dialog) {
    // Say that it's leaving, then let the world go.
    dialog.start("leave", "Leaving, please be patient", "Cleaning up messy neural networks...");
    dialog_.emplace(std::move(dialog));
    playing_ = false;
    Canvas canvas = window_.frame();
    draw(canvas);
    window_.present();
    SDL_PumpEvents();
    simulation_.reset();
}

bool App::leave_graph() {
    if (!simulation_->ended()) {
        close_graph();
        return false;
    }
    if (!export_list_.empty()) {
        ask_to_leave();
        return false;
    }
    leave(Dialog("Leaving this simulation", {}, {{"leave", "LEAVE"}}, "leave"));
    return true;
}

void App::ask_to_leave() {
    std::vector<std::string> lines = {"You'll return to the configuration panel to set up a new world.", "This world won't be kept."};
    if (!export_list_.empty()) {
        lines.push_back(plural(export_list_.size()) + " added to export will be lost unless exported first.");
    }
    open("leave", Dialog("Leave this simulation?", lines, {{"stay", "STAY"}, {"leave", "LEAVE"}}, "stay"));
}

void App::open_export_dialog() {
    const std::size_t count = export_list_.size();
    if (!count) {
        open("export", Dialog("Export all selected",
                              {"Nothing has been added to export yet.", "Select a Bitzkey and press ADD TO EXPORT (or A)."},
                              {{"close", "CLOSE"}}, "close"));
        return;
    }
    const int gone = chosen_records().second;
    std::vector<std::string> lines = {plural(count) + " added to export.",
                                      "The living export as they are now, with what they have learned since."};
    if (gone) lines.push_back(std::to_string(gone) + (gone != 1 ? " have" : " has") + " died; they export as they were when added.");
    lines.emplace_back("Save them to a file in exports/, copy them to the clipboard, or add them to the catalog.");
    open("export", Dialog("Export all selected", lines,
                          {{"clear", "CLEAR LIST"}, {"cancel", "CANCEL"}, {"catalog", "CATALOG"}, {"copy", "COPY"}, {"save", "SAVE FILE"}},
                          "cancel"));
}

Bitzkey* App::selected() const {
    if (!selected_id_) return nullptr;
    for (const auto& agent : simulation_->agents()) {
        if (agent->identifier == *selected_id_) return agent.get();
    }
    return nullptr;
}

void App::toggle_export() {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    Bitzkey* agent = selected();
    if (!agent) {
        say("Select a Bitzkey first, then add it to export.");
        return;
    }
    const auto found = std::find_if(export_list_.begin(), export_list_.end(),
                                    [agent](const ExportEntry& entry) { return entry.identifier == agent->identifier; });
    if (found != export_list_.end()) {
        export_list_.erase(found);
        say(format("Removed #%03d from the export list (%zu on it).", agent->identifier, export_list_.size()));
    } else {
        export_list_.push_back({agent->identifier, agent->dna, bitzkey_record(*agent)});
        say(format("Added #%03d to the export list (%zu on it).", agent->identifier, export_list_.size()));
    }
}

std::pair<std::vector<nlohmann::ordered_json>, int> App::chosen_records() const {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    // The living as they are now, the dead as they were when added (their brain is gone).
    std::unordered_map<int, const Bitzkey*> agents;
    for (const auto& agent : simulation_->agents()) agents[agent->identifier] = agent.get();
    std::vector<nlohmann::ordered_json> records;
    int gone = 0;
    for (const ExportEntry& entry : export_list_) {
        const auto found = agents.find(entry.identifier);
        if (found != agents.end() && found->second->alive() && found->second->dna == entry.dna) {
            records.push_back(bitzkey_record(*found->second));
        } else {
            records.push_back(entry.saved);
            ++gone;
        }
    }
    return {records, gone};
}

void App::add_to_catalog(const std::vector<nlohmann::ordered_json>& entries, const std::string& what, const std::string& skipped) {
    int added = 0;
    try {
        added = catalog::add(entries, catalog_path());
    } catch (const std::exception& error) {
        say(std::string("Couldn't add to the catalog: ") + error.what());
        return;
    }
    const int already = static_cast<int>(entries.size()) - added;
    std::string note = added ? "Added " + what + " to the catalog"
                             : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(what[0])))) + what.substr(1) +
                                   " already in the catalog";
    if (added && already) note += " (" + std::to_string(already) + " already there)";
    say(note + "." + (skipped.empty() ? "" : " " + skipped));
}

void App::catalog_selected() {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    const Bitzkey* agent = selected();
    if (!agent || !agent->alive()) {
        say("Select a living Bitzkey first, then add it to the catalog.");
        return;
    }
    char name[16];
    std::snprintf(name, sizeof name, "#%03d", agent->identifier);
    add_to_catalog({catalog::entry_for(*agent, simulation_->turn())}, name);
}

void App::catalog_picked() {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    if (graph_.selection().empty()) {
        say("Pick Bitzkeys on the graph first, then add them to the catalog.");
        return;
    }
    std::unordered_map<int, const Bitzkey*> living;
    for (const Bitzkey* agent : simulation_->living()) living[agent->identifier] = agent;
    std::vector<nlohmann::ordered_json> entries;
    for (const int identifier : graph_.selection()) {  // in order of identifier, as the set keeps them
        const auto found = living.find(identifier);
        if (found != living.end()) entries.push_back(catalog::entry_for(*found->second, simulation_->turn()));
    }
    const int dead = static_cast<int>(graph_.selection().size() - entries.size());
    const std::string skipped = dead ? std::to_string(dead) + " dead left out: their brains are gone." : "";
    if (entries.empty()) {
        say("Only living Bitzkeys can be catalogued, with their brains; " + skipped);
        return;
    }
    add_to_catalog(entries, plural(entries.size()), skipped);
}

void App::edit_rules() {
    // Applied, they hold from this world's next turn, and for its restarts. Closing the window from there closes
    // the program.
    playing_ = false;
    const auto [outcome, changed] = AdvancedScreen(window_, rules::current(), true).run();
    if (outcome == AdvancedScreen::Outcome::Quit) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    } else if (outcome == AdvancedScreen::Outcome::Apply && changed) {
        rules::apply(*changed);
        simulation_->rules_changed();
        say(format("This world now plays by %zu rule%s changed from the defaults. Paused: SPACE plays on.", changed->size(),
                   changed->size() != 1 ? "s" : ""));
    }
}

void App::browse_catalog() {
    // The world waits meanwhile. DEPLOY closes it to paint the Bitzkeys chosen onto the map. Closing the window from
    // there closes the program.
    const auto [outcome, entries] = CatalogScreen(window_, true).run();
    if (outcome == CatalogScreen::Outcome::Quit) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    } else if (outcome == CatalogScreen::Outcome::Deploy && !entries.empty()) {
        std::vector<Imported> bitzkeys;
        for (const catalog::Entry& entry : entries) bitzkeys.push_back(catalog::imported(entry));
        start_deploying(std::move(bitzkeys));
    }
}

namespace {
SDL_Cursor* system_cursor(SDL_SystemCursor which) {
    static std::map<SDL_SystemCursor, SDL_Cursor*> made;  // kept for the program's life
    SDL_Cursor*& cursor = made[which];
    if (!cursor) cursor = SDL_CreateSystemCursor(which);
    return cursor;
}
}  // namespace

void App::start_deploying(std::vector<Imported> bitzkeys) {
    show_census_ = false;
    const std::size_t count = bitzkeys.size();
    deploying_ = std::move(bitzkeys);
    painted_.reset();
    SDL_SetCursor(system_cursor(SDL_SYSTEM_CURSOR_CROSSHAIR));
    say(format("DEPLOY: click or drag on the map to place %d of %s.  RIGHT-CLICK or ESC  stop", DEPLOY_GROUP,
               count == 1 ? "it" : format("each of the %zu", count).c_str()));
}

void App::stop_deploying() {
    deploying_.clear();
    painted_.reset();
    SDL_SetCursor(system_cursor(SDL_SYSTEM_CURSOR_ARROW));
    say("Deploying stopped.");
}

bool App::deploy_event(const SDL_Event& event) {
    // A left press on the map places a group, and holding it, the drag paints another every DEPLOY_GROUP cells; a
    // right-click or ESC stops.
    if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) ||
        (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT)) {
        stop_deploying();
        return true;
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        painted_.reset();
        return true;
    }
    const bool pressed = event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT;
    const bool painting = event.type == SDL_MOUSEMOTION && painted_;
    if (!pressed && !painting) return false;
    const auto cell = pressed ? cell_at(event.button.x, event.button.y) : cell_at(event.motion.x, event.motion.y);
    if (!cell) return !pressed;  // a press off the map works the rest of the screen
    if (painting && std::max(std::abs(cell->first - painted_->first), std::abs(cell->second - painted_->second)) < DEPLOY_GROUP) return true;
    painted_ = cell;
    const auto placed = simulation_->deploy(deploying_, cell->first, cell->second);
    say(format("Deployed %zu at (%d, %d).  RIGHT-CLICK or ESC  stop", placed.size(), cell->first, cell->second));
    return true;
}

std::optional<std::pair<int, int>> App::cell_at(int x, int y) {
    Canvas canvas = window_.frame();
    if (!map_view(canvas).first.contains(x, y)) return std::nullopt;
    const Board board = board_geometry(canvas);
    const int cell_x = static_cast<int>(std::floor((x - board.rect.x) / board.cell_width));
    const int cell_y = static_cast<int>(std::floor((y - board.rect.y) / board.cell_height));
    if (cell_x < 0 || cell_x >= simulation_->width() || cell_y < 0 || cell_y >= simulation_->height()) return std::nullopt;
    return std::pair{cell_x, cell_y};
}

void App::catalog_export_list() {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    std::unordered_map<int, const Bitzkey*> agents;
    for (const auto& agent : simulation_->agents()) agents[agent->identifier] = agent.get();
    std::vector<nlohmann::ordered_json> entries;
    for (const ExportEntry& entry : export_list_) {
        const auto found = agents.find(entry.identifier);
        if (found != agents.end() && found->second->alive() && found->second->dna == entry.dna) {
            entries.push_back(catalog::entry_for(*found->second, simulation_->turn()));
        } else {
            nlohmann::ordered_json saved = entry.saved;
            saved["catalog"] = {{"identifier", entry.identifier}};
            entries.push_back(std::move(saved));
        }
    }
    add_to_catalog(entries, plural(entries.size()) + " from the export list");
}

std::string App::save_export(const std::string& name, const std::string& text) {
    write_file(data_dir() / "exports" / name, text + "\n");
    return name;
}

void App::export_chosen(bool to_file) {
    const auto [records, gone] = chosen_records();
    std::string what = plural(records.size()) + " from the export list";
    if (gone) what += format(" (%d as they were when added)", gone);
    const std::string text = format_records(records);
    if (!to_file) {
        copy(text, what);
        return;
    }
    try {
        const std::string name = save_export(format("bitzkeys-turn-%05d-chosen-%zu.json", simulation_->turn(), records.size()), text);
        say("Saved " + what + " to exports/" + name);
    } catch (const std::exception& error) {
        say(std::string("Couldn't save: ") + error.what());
    }
}

void App::export_graph(bool to_file) {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    // The Bitzkeys selected in the colour graph, or everyone alive if none are. The living go with
    // brain and temperament; the dead keep no brain, and the long dead only their genome.
    const std::set<int>& chosen = graph_.selection();
    std::unordered_map<int, const Bitzkey*> agents;
    for (const auto& agent : simulation_->agents()) agents[agent->identifier] = agent.get();
    std::vector<nlohmann::ordered_json> records;
    int dead = 0;
    if (!chosen.empty()) {
        for (const int identifier : chosen) {
            const auto found = agents.find(identifier);
            if (found != agents.end()) {
                records.push_back(bitzkey_record(*found->second));
                dead += !found->second->alive();
            } else {
                records.push_back(genome_only(simulation_->lineage(identifier)));
                ++dead;
            }
        }
    } else {
        for (const Bitzkey* agent : simulation_->living()) records.push_back(bitzkey_record(*agent));
    }
    if (records.empty()) {
        say("Nobody is alive to export.");
        return;
    }
    std::string what = std::to_string(records.size()) + " " + (chosen.empty() ? "" : "selected ") + "Bitzkey" + (records.size() != 1 ? "s" : "");
    if (dead) what += format(" (%d dead, so without a brain)", dead);
    const std::string text = format_records(records);
    if (!to_file) {
        copy(text, what);
        return;
    }
    try {
        const std::string name = save_export(format("bitzkeys-turn-%05d-%s-%zu.json", simulation_->turn(),
                                                    chosen.empty() ? "alive" : "selected", records.size()), text);
        say("Saved " + what + " to exports/" + name);
    } catch (const std::exception& error) {
        say(std::string("Couldn't save: ") + error.what());
    }
}

void App::close_graph() {
    // The sidebar then shows no one Bitzkey, unless the pick is just the one it was showing.
    show_census_ = false;
    graph_.close_menu();
    const auto& picked = graph_.selection();
    if (!picked.empty() && !(selected_id_ && picked.size() == 1 && picked.contains(*selected_id_))) selected_id_.reset();
}

std::optional<int> App::living_at(int x, int y) {
    Canvas canvas = window_.frame();
    const Board board = board_geometry(canvas);
    const Bitzkey* nearest = nullptr;
    double nearest_distance = std::numeric_limits<double>::infinity();
    for (const Bitzkey* agent : simulation_->living()) {
        const double px = board.rect.x + (agent->x + 0.5) * board.cell_width;
        const double py = board.rect.y + (agent->y + 0.5) * board.cell_height;
        const double distance = std::hypot(x - px, y - py);
        if (distance < nearest_distance) {
            nearest = agent;
            nearest_distance = distance;
        }
    }
    if (nearest && board.rect.contains(x, y) &&
        nearest_distance < std::max<double>(UI(9), std::min(board.cell_width, board.cell_height) * 1.6)) {
        return nearest->identifier;
    }
    return std::nullopt;
}

void App::pick_on_map(int start_x, int start_y, int end_x, int end_y, bool adding) {
    std::set<int> picked;
    if (std::max(std::abs(end_x - start_x), std::abs(end_y - start_y)) < UI(MAP_DRAG_THRESHOLD)) {
        const std::optional<int> hit = living_at(start_x, start_y);
        if (adding) {
            picked = graph_.selection();
            if (hit && !picked.erase(*hit)) picked.insert(*hit);
        } else if (hit) {
            picked.insert(*hit);
        }
    } else {
        Canvas canvas = window_.frame();
        const Board board = board_geometry(canvas);
        const Rect box{std::min(start_x, end_x), std::min(start_y, end_y), std::abs(end_x - start_x) + 1, std::abs(end_y - start_y) + 1};
        if (adding) picked = graph_.selection();
        for (const Bitzkey* agent : simulation_->living()) {
            const int px = static_cast<int>(board.rect.x + (agent->x + 0.5) * board.cell_width);
            const int py = static_cast<int>(board.rect.y + (agent->y + 0.5) * board.cell_height);
            if (box.contains(px, py)) picked.insert(agent->identifier);
        }
    }
    graph_.select_all(picked);  // the map and the graph share one selection
    if (picked.size() == 1) {   // one picked shows in the inspector
        selected_id_ = *picked.begin();
        show_insights_ = false;
    } else {
        selected_id_.reset();
    }
}

void App::select(int identifier) {
    selected_id_ = identifier;
    graph_.select_only(identifier);  // the map and the graph share one selection
    show_insights_ = false;          // selecting shows the inspector
}

bool App::renew() {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    std::vector<const Bitzkey*> chosen;
    for (const Bitzkey* agent : simulation_->living()) {
        if (graph_.selection().contains(agent->identifier)) chosen.push_back(agent);
    }
    std::sort(chosen.begin(), chosen.end(), [](const Bitzkey* a, const Bitzkey* b) { return a->identifier < b->identifier; });
    if (chosen.empty()) {
        say("Pick living Bitzkeys on the graph first, then start a new world with them.");
        return false;
    }
    renewal_ = export_bitzkeys(chosen);
    copy(renewal_, plural(chosen.size()));
    leave(Dialog("Starting a new world", {}, {{"leave", "LEAVE"}}, "leave"));
    return true;
}

std::vector<App::Exemplar> App::exemplars() const {
    std::vector<const Bitzkey*> animals;
    for (const Bitzkey* agent : simulation_->living()) {
        if (agent->species != Species::Plant) animals.push_back(agent);
    }
    std::vector<Exemplar> found;
    for (const int choice : exemplar_choices) {  // ties go to the oldest, then the first born
        const ExemplarStat& stat = EXEMPLAR_STATS[static_cast<std::size_t>(choice)];
        const double sign = stat.lowest ? -1 : 1;
        const Bitzkey* best = nullptr;
        for (const Bitzkey* agent : animals) {
            if (!best || std::tuple{sign * exemplar_value(*agent, stat), agent->age, -agent->identifier} >
                             std::tuple{sign * exemplar_value(*best, stat), best->age, -best->identifier}) {
                best = agent;
            }
        }
        found.push_back({stat.title, best, best ? exemplar_text(*best, stat) : "none alive"});
    }
    return found;
}

Rect App::board_area(const Canvas& canvas) const {
    return {0, UI(62), canvas.width() - sidebar_width(canvas), canvas.height() - UI(140)};
}

std::pair<Rect, Rect> App::exemplar_columns(const Canvas& canvas) const {
    const Rect area = board_area(canvas);
    const int width = UI(EXEMPLAR_WIDTH);
    return {Rect{area.x + UI(16), area.y + UI(21), width, area.h - UI(42)},
            Rect{area.right() - UI(16) - width, area.y + UI(21), width, area.h - UI(42)}};
}

void App::draw_exemplar_menu(Canvas& canvas) {
    // The open card's statistics, beneath its title, in as many columns as the screen needs.
    menu_items_.clear();
    if (!exemplar_menu_ || *exemplar_menu_ >= static_cast<int>(exemplar_titles_.size())) return;
    const Rect& heading = exemplar_titles_[static_cast<std::size_t>(*exemplar_menu_)];
    const int row = UI(MENU_ROW), column = UI(MENU_COLUMN), count = static_cast<int>(EXEMPLAR_STATS.size());
    const int top = heading.bottom() + UI(2);
    int rows = std::max(1, std::min(count, (canvas.height() - top - UI(12)) / row));
    const int columns = (count + rows - 1) / rows;
    rows = (count + columns - 1) / columns;  // as even as the columns allow
    const int width = columns * column + UI(8);
    int left = *exemplar_menu_ < 5 ? heading.x : heading.right() - width;  // toward the map
    left = std::max(UI(4), std::min(canvas.width() - width - UI(4), left));
    const Rect panel{left, top, width, rows * row + UI(8)};
    canvas.rect(panel, {27, 37, 37}, 0, UI(5));
    canvas.rect(panel, {131, 205, 143}, 1, UI(5));
    int mouse_x = 0, mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    TTF_Font* small = UI.font(Face::Sans, 12);
    const int current = exemplar_choices[static_cast<std::size_t>(*exemplar_menu_)];
    for (int stat = 0; stat < count; ++stat) {
        const Rect item{panel.x + UI(4) + stat / rows * column, panel.y + UI(4) + stat % rows * row, column, row};
        if (item.contains(mouse_x, mouse_y)) canvas.rect(item, {44, 57, 52}, 0, UI(3));
        canvas.text(small, EXEMPLAR_STATS[static_cast<std::size_t>(stat)].title,
                    stat == current ? Color{131, 205, 143} : Color{220, 229, 211}, item.x + UI(6), item.y + UI(2));
        menu_items_.push_back({item, stat});
    }
}

void App::draw_exemplars(Canvas& canvas) {
    // Five cards a side, evenly spaced, each the living animal that stands out in its way; a click selects it.
    exemplar_cards_.clear();
    exemplar_titles_.clear();
    const std::vector<Exemplar> found = exemplars();
    TTF_Font* small = UI.font(Face::Sans, 12);
    const int gap = UI(8);
    const auto [left, right] = exemplar_columns(canvas);
    for (int side = 0; side < 2; ++side) {
        const Rect& column = side ? right : left;
        const int height = (column.h - 4 * gap) / 5;
        for (int index = 0; index < 5; ++index) {
            const Exemplar& exemplar = found[static_cast<std::size_t>(side * 5 + index)];
            const Rect card{column.x, column.y + index * (height + gap), column.w, height};
            const bool chosen = exemplar.agent && selected_id_ && exemplar.agent->identifier == *selected_id_;
            canvas.rect(card, {27, 37, 37}, 0, UI(5));
            canvas.rect(card, chosen ? Color{238, 183, 110} : Color{54, 68, 62}, 1, UI(5));  // the selected one lit
            const Rect heading{card.x + UI(5), card.y + UI(5), card.w - UI(10), UI(22)};  // a menu of statistics
            canvas.rect(heading, {44, 57, 52}, 0, UI(4));
            if (exemplar_menu_ == static_cast<int>(exemplar_titles_.size())) canvas.rect(heading, {131, 205, 143}, 1, UI(4));
            canvas.text(small, fit(exemplar.title, heading.w - UI(26)), {220, 229, 211}, heading.x + UI(5), heading.y + UI(3));
            const int tx = heading.right() - UI(10), ty = heading.centery();
            canvas.polygon({{tx - UI(4), ty - UI(2)}, {tx + UI(4), ty - UI(2)}, {tx, ty + UI(3)}}, {220, 229, 211});
            exemplar_titles_.push_back(heading);
            if (!exemplar.agent) {
                canvas.text(small, exemplar.said, {116, 139, 124}, card.x + UI(10), card.y + UI(28));
                continue;
            }
            const int radius = std::max(1, std::min(UI(16), height / 5));
            const int px = card.x + UI(10) + radius + RING_WIDTH, py = card.y + (height + UI(20)) / 2;
            draw_bitzkey(canvas, exemplar.agent->dna, px, py, radius);
            const int x = px + radius + UI(12);
            canvas.text(small, format("#%03d  %s", exemplar.agent->identifier,
                                      std::string(SPECIES[static_cast<std::size_t>(exemplar.agent->species)]).c_str()),
                        {231, 236, 222}, x, py - UI(16));
            canvas.text(small, exemplar.said, {149, 170, 150}, x, py + UI(2));
            exemplar_cards_.push_back({card, exemplar.agent->identifier});
        }
    }
}

void App::save_report() {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    const auto alive = simulation_->living();
    const std::vector<const Bitzkey*> animals(alive.begin(), alive.end());
    const std::string name = format("brain-report-turn-%05d.txt", simulation_->turn());
    try {
        write_file(data_dir() / "reports" / name, brain_report(simulation_->turn(), animals, simulation_->insights()));
        say("Brain report saved to reports/" + name);
    } catch (const std::exception& error) {
        say(std::string("Couldn't save the report: ") + error.what());
    }
}

void App::say(const std::string& message) {
    message_ = message;
    message_until_ = SDL_GetTicks() + 5000;
}

void App::copy(const std::string& text, const std::string& what) {
    std::string error;
    if (set_clipboard_text(text, error)) say("Copied " + what + " to the clipboard.");
    else say("Couldn't copy " + what + " to the clipboard: " + error);
}

void App::copy_selected() {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    const Bitzkey* agent = selected();
    if (!agent) {
        say("Click a Bitzkey first, then press C to copy its genome.");
        return;
    }
    const std::vector<const Bitzkey*> one = {agent};
    copy(export_bitzkeys(one), format("#%03d's %s", agent->identifier, agent->brain ? "genome and brain" : "genome"));
}

double App::corpse_opacity(const Bitzkey& agent) const {
    if (!agent.died_turn) return 1.0;
    return std::max(0.0, 1 - static_cast<double>(simulation_->turn() - *agent.died_turn) / CORPSE_TURNS);
}

// ---------------------------------------------------------------------------------------------
// Drawing

int App::sidebar_width(const Canvas& canvas) const { return std::min(UI(340), std::max(UI(290), canvas.width() / 3)); }

std::pair<Rect, double> App::map_view(const Canvas& canvas) const {
    const Rect view = board_area(canvas).inflate(-UI(42) - 2 * UI(EXEMPLAR_WIDTH + 16), -UI(42));  // between the exemplars
    return {view, std::min(static_cast<double>(view.w) / simulation_->width(), static_cast<double>(view.h) / simulation_->height())};
}

std::pair<double, double> App::clamped_centre(std::pair<double, double> centre, const Rect& view, double scale) const {
    const auto clamp = [scale](double middle, int cells, int pixels) {
        const double half = pixels / 2.0 / scale;  // half the view, in cells
        return cells <= 2 * half ? cells / 2.0 : std::min(cells - half, std::max(half, middle));
    };
    return {clamp(centre.first, simulation_->width(), view.w), clamp(centre.second, simulation_->height(), view.h)};
}

App::Board App::board_geometry(const Canvas& canvas) const {
    const auto [view, fit] = map_view(canvas);
    const double scale = fit * map_zoom_;
    const auto [centre_x, centre_y] = clamped_centre(
        map_centre_.value_or(std::pair{simulation_->width() / 2.0, simulation_->height() / 2.0}), view, scale);
    const Rect board{static_cast<int>(py_round(view.centerx() - centre_x * scale)), static_cast<int>(py_round(view.centery() - centre_y * scale)),
                     static_cast<int>(py_round(scale * simulation_->width())), static_cast<int>(py_round(scale * simulation_->height()))};
    return {board, scale, scale};
}

bool App::map_event(const SDL_Event& event) {
    Canvas canvas = window_.frame();
    const auto [view, fit] = map_view(canvas);
    const std::pair<double, double> middle{simulation_->width() / 2.0, simulation_->height() / 2.0};
    if (event.type == SDL_MOUSEWHEEL) {
        int x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        if (!view.contains(x, y)) return false;
        const Board board = board_geometry(canvas);
        // The cell under the pointer stays put.
        const double cell_x = (x - board.rect.x) / board.cell_width, cell_y = (y - board.rect.y) / board.cell_height;
        map_zoom_ = std::max(1.0, std::min(MAP_ZOOM_MAX, map_zoom_ * std::pow(MAP_ZOOM_STEP, event.wheel.y)));
        const double scale = fit * map_zoom_;
        map_centre_ = clamped_centre({cell_x - (x - view.centerx()) / scale, cell_y - (y - view.centery()) / scale}, view, scale);
        return true;
    }
    const bool panning_button = event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP
                                    ? event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE
                                    : false;
    if (event.type == SDL_MOUSEBUTTONDOWN && panning_button && view.contains(event.button.x, event.button.y)) {
        pan_from_ = std::pair{std::pair{event.button.x, event.button.y}, clamped_centre(map_centre_.value_or(middle), view, fit * map_zoom_)};
        return true;
    }
    if (event.type == SDL_MOUSEBUTTONUP && panning_button && pan_from_) {
        pan_from_.reset();
        return true;
    }
    if (event.type == SDL_MOUSEMOTION && pan_from_) {
        const auto [start, centre] = *pan_from_;
        const double scale = fit * map_zoom_;
        map_centre_ = clamped_centre({centre.first - (event.motion.x - start.first) / scale,
                                      centre.second - (event.motion.y - start.second) / scale}, view, scale);
        return true;
    }
    return false;
}

void App::draw(Canvas& canvas) {
    // Let go of any Bitzkey selected or picked that has died, so the sidebar returns to its neutral
    // state rather than speaking of a selection no longer there.
    if (simulation_) {
        std::set<int> living;
        for (const Bitzkey* agent : simulation_->living()) living.insert(agent->identifier);
        if (selected_id_ && !living.contains(*selected_id_)) selected_id_.reset();
        graph_.keep_only(living);
    }
    canvas.fill({19, 26, 27});
    draw_header(canvas);
    draw_world(canvas);
    draw_exemplars(canvas);
    draw_sidebar(canvas);
    draw_footer(canvas);
    draw_exemplar_menu(canvas);  // over everything but the graph and dialogs
    if (show_census_) {
        const std::optional<std::string> note =
            SDL_GetTicks() < message_until_ ? std::optional<std::string>(message_) : std::nullopt;
        graph_.draw(canvas, *simulation_, note);
    }
    if (dialog_) dialog_->draw(canvas);
    if (report_) report_->draw(canvas);
}

void App::open_death_report() {
    report_.emplace("Death report",
                    format("Every death in this world up to turn %d: %d in all, by cause and species.", simulation_->turn(),
                           simulation_->deaths()),
                    death_report_lines(simulation_->death_causes()));
}

void App::inspect_brains() {
    simulation_->sync_brains();  // the brains as they now are, when they think on the GPUs
    const Bitzkey* agent = selected();
    if (!agent || !agent->brain) {
        say("Only a living animal has a brain to inspect.");
        return;
    }
    playing_ = false;
    report_.emplace(format("Brains of #%03d, %s", agent->identifier, std::string(species_name(agent->species)).c_str()),
                    "Its chance of choosing each action open to it, in each standard situation, with its temperament now. "
                    "* marks the likeliest; \u00b7 an action not open.",
                    brain_matrix_lines(*agent));
}

void App::draw_header(Canvas& canvas) {
    canvas.fill_rect({0, 0, canvas.width(), UI(62)}, {27, 37, 37});
    const int title = canvas.text(UI.font(Face::Sans, 22, true), "BITZKEY", {229, 239, 220}, UI(24), UI(11));
    canvas.text(UI.font(Face::Sans, 12), "ARTIFICIAL ECOLOGY  /  RESEARCH BUILD", {144, 170, 151}, UI(48) + title, UI(20));
    const std::string status = playing_ ? "RUNNING" : simulation_->ended() ? "ENDED" : "PAUSED";
    const Color colour = playing_ ? Color{131, 205, 143} : Color{238, 183, 110};
    TTF_Font* font = UI.font(Face::Sans, 15);
    Rect label = Canvas::text_box(font, status);
    label.x = canvas.width() - label.w - UI(24);
    canvas.text(font, status, colour, label.x, UI(20));
    const std::string speed = std::string("SPEED ") + SPEED_LABELS[static_cast<std::size_t>(speed_)];
    Rect speed_box{};
    canvas.text_at(UI.font(Face::Sans, 12), speed, {149, 170, 150}, [&](Rect& box) {
        box.x = label.x - box.w - UI(20);
        box.y = UI(23);
        speed_box = box;
    });
    if (!simulation_->gpu_status().empty()) {  // where the brains think, when the GPU was asked for
        const std::string where = upper(simulation_->gpu_status());
        canvas.text_at(UI.font(Face::Sans, 12), fit(where, UI(420)), simulation_->on_gpu() ? Color{131, 205, 143} : Color{238, 183, 110},
                       [&](Rect& box) {
                           box.x = speed_box.x - box.w - UI(24);
                           box.y = UI(23);
                       });
    }
}

void App::draw_world(Canvas& canvas) {
    const Board board = board_geometry(canvas);
    const Rect& rect = board.rect;
    const Rect view = map_view(canvas).first.inflate(2, 2);
    canvas.set_clip(view);  // zoomed in, only part of the map shows
    // What may show, near enough: anything further out is skipped.
    const Rect reach = view.inflate(static_cast<int>(2 * board.cell_width) + UI(20), static_cast<int>(2 * board.cell_height) + UI(20));
    for (int y = 0; y < simulation_->height(); ++y) {  // each row in its temperature's colour
        const int top = static_cast<int>(py_round(rect.y + y * board.cell_height));
        const int bottom = static_cast<int>(py_round(rect.y + (y + 1) * board.cell_height));
        canvas.fill_rect({rect.x, top, rect.w, bottom - top}, zone_colour(simulation_->temperature(y)));
    }
    const int grass_radius = std::max(1, static_cast<int>(py_round(std::min(board.cell_width, board.cell_height) * 0.18)));
    for (int y = 0; y < simulation_->height(); ++y) {
        for (int x = 0; x < simulation_->width(); ++x) {
            if (simulation_->grass_at(x, y) <= 0) continue;
            const int px = static_cast<int>(py_round(rect.x + (x + 0.5) * board.cell_width));
            const int py = static_cast<int>(py_round(rect.y + (y + 0.5) * board.cell_height));
            if (reach.contains(px, py)) canvas.circle(px, py, grass_radius, GRASS);
        }
    }
    const auto [smallest, largest] = map_dot_radii(std::min(board.cell_width, board.cell_height));  // zoomed with the map
    for (const auto& agent : simulation_->agents()) {  // the way each came this turn, faint, beneath them all
        Surface layer;
        for (const auto& [x, y] : agent->trail) {
            const int px = static_cast<int>(py_round(rect.x + (x + 0.5) * board.cell_width));
            const int py = static_cast<int>(py_round(rect.y + (y + 0.5) * board.cell_height));
            if ((x == agent->x && y == agent->y) || !reach.contains(px, py)) continue;
            if (!layer) layer = bitzkey_layer(agent->dna, dot_radius(agent->body[Size], largest, smallest), RING_WIDTH, agent->alive());
            const int half = layer->w / 2;
            canvas.blit(layer.get(), px - half, py - half, static_cast<std::uint8_t>(py_round(255 * TRAIL_OPACITY)));
        }
    }
    for (const auto& agent : simulation_->agents()) {
        const int px = static_cast<int>(py_round(rect.x + (agent->x + 0.5) * board.cell_width));
        const int py = static_cast<int>(py_round(rect.y + (agent->y + 0.5) * board.cell_height));
        if (!reach.contains(px, py)) continue;
        const int radius = dot_radius(agent->body[Size], largest, smallest);
        draw_bitzkey(canvas, agent->dna, px, py, radius, RING_WIDTH, agent->alive(), corpse_opacity(*agent));
        if ((selected_id_ && agent->identifier == *selected_id_) || (agent->alive() && graph_.selection().contains(agent->identifier))) {
            draw_reticle(canvas, px, py, radius + RING_WIDTH + UI(3));
        }
        if (std::any_of(export_list_.begin(), export_list_.end(),
                        [&](const ExportEntry& entry) { return entry.identifier == agent->identifier; })) {
            canvas.circle(px, py, radius + RING_WIDTH + UI(6), EXPORTING, std::max(1, UI(1)));
        }
    }
    if (marquee_from_) {  // the marquee being drawn
        const auto [start_x, start_y, adding] = *marquee_from_;
        int mouse_x = 0, mouse_y = 0;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        if (std::max(std::abs(mouse_x - start_x), std::abs(mouse_y - start_y)) >= UI(MAP_DRAG_THRESHOLD)) {
            const Rect box{std::min(start_x, mouse_x), std::min(start_y, mouse_y), std::abs(mouse_x - start_x) + 1,
                           std::abs(mouse_y - start_y) + 1};
            canvas.fill_rect(box, {245, 238, 206, 40});
            canvas.rect(box, {245, 238, 206}, 1);
        }
    }
    for (int x = 0; x <= simulation_->width(); x += 10) {
        const int px = static_cast<int>(py_round(rect.x + x * board.cell_width));
        canvas.line(px, rect.y, px, rect.bottom(), GRID);
    }
    for (int y = 0; y <= simulation_->height(); y += 10) {
        const int py = static_cast<int>(py_round(rect.y + y * board.cell_height));
        canvas.line(rect.x, py, rect.right(), py, GRID);
    }
    canvas.rect(rect, {80, 99, 85}, 1);
    canvas.set_clip(std::nullopt);
}

void App::draw_sidebar(Canvas& canvas) {
    TTF_Font* font = UI.font(Face::Sans, 15);
    TTF_Font* bold = UI.font(Face::Sans, 22, true);
    TTF_Font* small = UI.font(Face::Sans, 12);
    const int sidebar_x = canvas.width() - sidebar_width(canvas);
    canvas.line(sidebar_x, UI(62), sidebar_x, canvas.height() - UI(58), {54, 68, 62});
    const int x = sidebar_x + UI(20);
    const int width = canvas.width() - x - UI(20);
    const auto alive = simulation_->living();
    std::array<int, 4> counts{};
    for (const Bitzkey* agent : alive) counts[static_cast<std::size_t>(agent->species)] += 1;
    canvas.text(bold, format("TURN  %05d", simulation_->turn()), {232, 236, 219}, x, UI(78));
    canvas.text(small, format("%zu ALIVE    %d LOST", alive.size(), simulation_->deaths()), {157, 177, 159}, x, UI(108));
    const int turn = simulation_->turn();
    canvas.text_at(font, format("YEAR %d", year(turn)), {232, 236, 219}, [&](Rect& box) { box.topright_at(x + width, UI(84)); });
    canvas.text_at(small, upper(SEASONS[static_cast<std::size_t>(season(turn))]),
                   season(turn) % 2 ? Color{140, 185, 230} : Color{157, 177, 159},
                   [&](Rect& box) { box.topright_at(x + width, UI(108)); });
    canvas.line(x, UI(138), x + width, UI(138), {54, 68, 62});
    canvas.text(font, "POPULATION", {220, 229, 211}, x, UI(153));
    for (std::size_t index = 0; index < SPECIES.size(); ++index) {
        const int y = UI(184 + static_cast<int>(index) * 23);
        canvas.text(small, upper(SPECIES[index]), {149, 170, 150}, x, y);
        canvas.text(font, std::to_string(counts[index]), {231, 236, 222}, x + width - UI(32), y);
    }
    canvas.line(x, UI(286), x + width, UI(286), {54, 68, 62});
    add_button_.reset();
    catalog_button_.reset();
    deaths_button_.reset();
    brains_button_.reset();
    if (show_insights_) {
        draw_insights(canvas, x, width);
        return;
    }
    canvas.text(font, "SELECTED BITZKEY", {220, 229, 211}, x, UI(301));
    const Bitzkey* agent = selected();
    if (!agent) {
        canvas.text(small, "Click a dot to inspect its genome.", {146, 165, 149}, x, UI(332));
        canvas.text(small, "Living: click its current position.", {116, 139, 124}, x, UI(352));
        if (!graph_.selection().empty()) {
            canvas.text(small, format("%zu picked on the colour graph, ringed on the map.", graph_.selection().size()),
                        {238, 183, 110}, x, UI(380));
        }
        return;
    }

    draw_details(canvas, *agent, x, [](int offset) { return UI(332 + offset); }, width, true, simulation_->temperature(agent->y));
    if (agent->brain) {  // a living animal: its brain can be inspected, beside the heading
        brains_button_ = Rect{x + width - UI(130), UI(299), UI(130), UI(26)};
        canvas.rect(*brains_button_, {44, 57, 52}, 0, UI(5));
        canvas.text_at(UI.font(Face::Sans, 13, true), "INSPECT BRAINS", {220, 229, 211},
                       [&](Rect& box) { box.center_at(brains_button_->centerx(), brains_button_->centery()); });
    }
    canvas.text(small, "C  copy as JSON      A  export      K  catalog", {116, 139, 124}, x, UI(719));
    const bool chosen = std::any_of(export_list_.begin(), export_list_.end(),
                                    [agent](const ExportEntry& entry) { return entry.identifier == agent->identifier; });
    const int half = (width - UI(8)) / 2;
    add_button_ = Rect{x, UI(737), half, UI(28)};
    catalog_button_ = Rect{x + half + UI(8), UI(737), width - half - UI(8), UI(28)};
    canvas.rect(*catalog_button_, {44, 57, 52}, 0, UI(5));
    canvas.text_at(UI.font(Face::Sans, 13, true), "ADD TO CATALOG", {220, 229, 211},
                   [&](Rect& box) { box.center_at(catalog_button_->centerx(), catalog_button_->centery()); });
    canvas.rect(*add_button_, {44, 57, 52}, 0, UI(5));
    if (chosen) canvas.rect(*add_button_, {238, 183, 110}, 1, UI(5));
    canvas.text_at(UI.font(Face::Sans, 13, true), chosen ? "REMOVE FROM EXPORT" : "ADD TO EXPORT",
                   chosen ? Color{238, 183, 110} : Color{220, 229, 211},
                   [&](Rect& box) { box.center_at(add_button_->centerx(), add_button_->centery()); });
}

Rect App::insights_view() const {
    const int sidebar = std::min(UI(340), std::max(UI(290), window_.width() / 3));
    const int top = UI(INSIGHTS_TOP);
    return {window_.width() - sidebar + 1, top, sidebar - 1, window_.height() - UI(58) - top};
}

Rect App::scroll_track() const {
    const Rect view = insights_view();
    return {window_.width() - UI(11), view.y + UI(4), UI(6), view.h - UI(8)};
}

int App::max_scroll() const { return std::max(0, insights_height_ - insights_view().h); }

std::optional<Rect> App::scroll_thumb() const {
    // The scroll bar's thumb, sized to the share of the panel in view; none when it all fits.
    const Rect track = scroll_track();
    const int most = max_scroll();
    if (!most) return std::nullopt;
    const int height = std::max(UI(24), track.h * insights_view().h / insights_height_);
    return Rect{track.x, track.y + (track.h - height) * insights_scroll_ / most, track.w, height};
}

void App::scroll_to(int scroll) { insights_scroll_ = std::max(0, std::min(max_scroll(), scroll)); }

bool App::scroll_event(const SDL_Event& event) {
    // Scroll the insights panel with the wheel over the sidebar, or by dragging its thumb.
    const Rect view = insights_view();
    if (event.type == SDL_MOUSEWHEEL) {
        int mouse_x = 0, mouse_y = 0;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        if (!view.contains(mouse_x, mouse_y)) return false;
        scroll_to(insights_scroll_ - event.wheel.y * UI(48));
        return true;
    }
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        const Rect track = scroll_track();
        auto thumb = scroll_thumb();
        if (!thumb || !track.inflate(UI(8), 0).contains(event.button.x, event.button.y)) return false;
        if (!thumb->contains(event.button.x, event.button.y)) {  // a click on the track jumps the thumb there
            scroll_to((event.button.y - track.y - thumb->h / 2) * max_scroll() / std::max(1, track.h - thumb->h));
            thumb = scroll_thumb();
        }
        scroll_grip_ = event.button.y - thumb->y;
        return true;
    }
    if (event.type == SDL_MOUSEMOTION && scroll_grip_) {
        const Rect track = scroll_track();
        if (const auto thumb = scroll_thumb()) {
            scroll_to((event.motion.y - *scroll_grip_ - track.y) * max_scroll() / std::max(1, track.h - thumb->h));
        }
        return true;
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && scroll_grip_) {
        scroll_grip_.reset();
        return true;
    }
    return false;
}

void App::draw_insights(Canvas& canvas, int x, int width) {
    // What the animals are doing and what they are like, by species, scrolled within the sidebar.
    // Lengths are in design pixels from the top of the sidebar.
    const Insights& insights = simulation_->insights();
    TTF_Font* font = UI.font(Face::Sans, 15);
    TTF_Font* small = UI.font(Face::Sans, 12);
    TTF_Font* mono = UI.font(Face::Mono, 14);
    scroll_to(insights_scroll_);
    const int offset = insights_scroll_;
    canvas.set_clip(insights_view());
    const auto at = [offset](double y) { return UI(y) - offset; };

    std::array<int, 3> columns{};
    for (std::size_t index = 0; index < ANIMALS.size(); ++index) {
        columns[index] = x + UI(96) + static_cast<int>(index) * (width - UI(96)) / static_cast<int>(ANIMALS.size());
    }
    const auto heading = [&](const char* title, const char* note, double y) {
        canvas.text(font, title, {220, 229, 211}, x, at(y));
        canvas.text(small, note, {116, 139, 124}, x, at(y + 22));
        for (std::size_t index = 0; index < ANIMALS.size(); ++index) {
            canvas.text(small, upper(species_name(ANIMALS[index]).substr(0, 4)), {149, 170, 150}, columns[index], at(y + 46));
        }
        return y + 66;
    };

    {  // every death so far, opened from the top of the panel
        const Rect button{x + width - UI(130), at(INSIGHTS_TOP + 9), UI(130), UI(26)};
        const Rect view = insights_view();
        if (button.bottom() > view.y && button.y < view.bottom()) {
            deaths_button_ = button.clip(view);
            canvas.rect(button, {44, 57, 52}, 0, UI(5));
            canvas.text_at(UI.font(Face::Sans, 13, true), "DEATH REPORT", {220, 229, 211},
                           [&](Rect& box) { box.center_at(button.centerx(), button.centery()); });
        }
    }
    double y = heading("BEHAVIOUR", "% of chances taken, recent turns", INSIGHTS_TOP + 11);
    for (int action = 0; action < ACTION_COUNT; ++action) {
        canvas.text(small, upper(ACTIONS[static_cast<std::size_t>(action)]), {149, 170, 150}, x, at(y));
        for (std::size_t index = 0; index < ANIMALS.size(); ++index) {
            const auto rate = insights.rate(action, ANIMALS[index]);
            canvas.text(mono, rate.share ? py_percent(*rate.share, 4) : "  -", {231, 236, 222}, columns[index], at(y));
        }
        y += 17;
    }

    const std::array<std::pair<const char*, std::vector<Insights::Extreme>>, 3> blocks = {
        std::pair{"LEAST KEEN TO BREED", insights.extremes(Breed, 3, true)},
        std::pair{"KEENEST TO BREED", insights.extremes(Breed, 2, false)},
        std::pair{"LEAST KEEN TO FIGHT", insights.extremes(Attack, 2, true)}};
    y += 6;
    for (const auto& [title, groups] : blocks) {
        canvas.text(small, title, {220, 229, 211}, x, at(y));
        y += 19;
        if (groups.empty()) {
            canvas.text(small, "not enough chances yet", {116, 139, 124}, x, at(y));
            y += 18;
        }
        for (const auto& group : groups) {
            canvas.text(small, group.label, {175, 192, 172}, x, at(y));
            canvas.text(mono, py_percent(group.share, 4), {231, 236, 222}, x + width - UI(40), at(y));
            y += 18;
        }
        y += 8;
    }

    std::array<std::vector<const Bitzkey*>, 3> members;
    for (const Bitzkey* agent : simulation_->living()) {
        for (std::size_t index = 0; index < ANIMALS.size(); ++index) {
            if (agent->species == ANIMALS[index]) members[index].push_back(agent);
        }
    }
    const auto average = [&](std::size_t index, auto value) -> std::optional<double> {
        if (members[index].empty()) return std::nullopt;
        double total = 0;
        for (const Bitzkey* agent : members[index]) total += value(*agent);
        return total / static_cast<double>(members[index].size());
    };
    const auto shown = [](std::optional<double> value, const char* pattern) {
        return value ? format(pattern, *value) : std::string("-");
    };
    const auto percent = [](std::optional<double> value) { return value ? py_percent(*value, 0) : std::string("-"); };
    const auto right4 = [](const std::string& text) { return text.size() >= 4 ? text : std::string(4 - text.size(), ' ') + text; };

    y = heading("LIFE", "averages of the living", y + 10);
    using Row = std::pair<const char*, std::function<std::string(std::size_t)>>;
    const std::vector<Row> rows = {
        {"ALIVE", [&](std::size_t i) { return std::to_string(members[i].size()); }},
        {"SIZE", [&](std::size_t i) { return shown(average(i, [](const Bitzkey& a) { return static_cast<double>(a.body[Size]); }), "%.1f"); }},
        {"AGE", [&](std::size_t i) { return shown(average(i, [](const Bitzkey& a) { return static_cast<double>(a.age); }), "%.0f"); }},
        {"GENERATION", [&](std::size_t i) { return shown(average(i, [](const Bitzkey& a) { return static_cast<double>(a.generation); }), "%.1f"); }},
        {"HUNGER", [&](std::size_t i) { return percent(average(i, [](const Bitzkey& a) { return std::min(100.0, a.hunger) / 100; })); }},
        {"HEALTH", [&](std::size_t i) { return percent(average(i, [](const Bitzkey& a) { return std::max(0.0, a.health) / a.max_health; })); }},
        {"CHILDREN", [&](std::size_t i) { return shown(average(i, [](const Bitzkey& a) { return static_cast<double>(a.children); }), "%.1f"); }},
        {"HERDING", [&](std::size_t i) { return percent(average(i, [](const Bitzkey& a) { return a.herd_instinct(); })); }},
        {"RIVALRY", [&](std::size_t i) {
             return ANIMALS[i] == Species::Grazer ? std::string("-") : percent(average(i, [](const Bitzkey& a) { return a.rivalry; }));
         }},
    };
    for (const auto& [label, value] : rows) {
        canvas.text(small, label, {149, 170, 150}, x, at(y));
        for (std::size_t index = 0; index < ANIMALS.size(); ++index) {
            canvas.text(mono, right4(value(index)), {231, 236, 222}, columns[index], at(y));
        }
        y += 17;
    }

    y = heading("TEMPERAMENT", "average now; green above inborn, amber below", y + 14);
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) {
        canvas.text(small, upper(TEMPERAMENT[static_cast<std::size_t>(trait)]), {149, 170, 150}, x, at(y));
        for (std::size_t index = 0; index < ANIMALS.size(); ++index) {
            const auto now = average(index, [trait](const Bitzkey& a) { return a.mind[static_cast<std::size_t>(trait)]; });
            if (!now) {
                canvas.text(mono, "   -", {231, 236, 222}, columns[index], at(y));
                continue;
            }
            const double shift = *now - *average(index, [trait](const Bitzkey& a) {
                return static_cast<double>(a.inborn[static_cast<std::size_t>(trait)]);
            });
            const Color colour = std::abs(shift) < 0.25 ? Color{231, 236, 222} : shift > 0 ? Color{131, 205, 143} : Color{238, 183, 110};
            canvas.text(mono, format("%4.1f", *now), colour, columns[index], at(y));
        }
        y += 17;
    }

    canvas.text(font, "KEENEST BY ACTION", {220, 229, 211}, x, at(y + 14));
    canvas.text(small, "the group most likely to take each chance", {116, 139, 124}, x, at(y + 36));
    y += 60;
    bool any = false;
    for (int action = 0; action < ACTION_COUNT; ++action) {
        const auto groups = insights.extremes(action, 1, false);
        if (groups.empty()) continue;
        canvas.text(small, upper(ACTIONS[static_cast<std::size_t>(action)]), {149, 170, 150}, x, at(y));
        canvas.text(small, fit(groups[0].label, width - UI(124)), {175, 192, 172}, x + UI(76), at(y));
        canvas.text(mono, py_percent(groups[0].share, 4), {231, 236, 222}, x + width - UI(40), at(y));
        y += 18;
        any = true;
    }
    if (!any) {
        canvas.text(small, "not enough chances yet", {116, 139, 124}, x, at(y));
        y += 18;
    }

    insights_height_ = UI(y + 12) - UI(INSIGHTS_TOP);
    canvas.set_clip(std::nullopt);
    if (const auto thumb = scroll_thumb()) {
        canvas.rect(scroll_track(), {36, 49, 47}, 0, UI(3));
        canvas.rect(*thumb, scroll_grip_ ? Color{238, 183, 110} : Color{96, 120, 104}, 0, UI(3));
    }
}

std::string App::fit(const std::string& text, int width) const {
    // Shorten text in the small font, with an ellipsis, to fit the width: footer notes, and group
    // names in the insights panel.
    TTF_Font* small = UI.font(Face::Sans, 12);
    if (Canvas::text_width(small, text) <= width) return text;
    std::string shortened = text;
    while (!shortened.empty() && Canvas::text_width(small, shortened + "...") > width) shortened.pop_back();
    while (!shortened.empty() && shortened.back() == ' ') shortened.pop_back();
    return shortened + "...";
}

void App::draw_footer(Canvas& canvas) {
    TTF_Font* small = UI.font(Face::Sans, 12);
    const int height = canvas.height();
    canvas.fill_rect({0, height - UI(52), canvas.width(), UI(52)}, {27, 37, 37});
    std::string history = format("HISTORY  %d/%d", simulation_->turn(), simulation_->frontier());
    if (simulation_->earliest()) history += format("  FROM %d", simulation_->earliest());  // the oldest turns no longer fit the disk budget
    canvas.text(small, history, {158, 181, 160}, UI(22), height - UI(36));
    TTF_Font* button_font = UI.font(Face::Sans, 13, true);
    const std::string label = format("EXPORT ALL SELECTED (%zu)", export_list_.size());
    const bool any = !export_list_.empty();
    export_button_ = Rect{0, 0, Canvas::text_width(button_font, label) + UI(32), UI(34)}.midright_at(canvas.width() - UI(20), height - UI(26));
    canvas.rect(export_button_, any ? Color{238, 183, 110} : Color{44, 57, 52}, 0, UI(6));
    canvas.text_at(button_font, label, any ? Color{19, 26, 27} : Color{220, 229, 211},
                   [&](Rect& box) { box.center_at(export_button_.centerx(), export_button_.centery()); });
    open_button_ = Rect{0, 0, Canvas::text_width(button_font, "OPEN CATALOG") + UI(32), UI(34)}.midright_at(export_button_.x - UI(10),
                                                                                                         height - UI(26));
    canvas.rect(open_button_, {44, 57, 52}, 0, UI(6));
    canvas.text_at(button_font, "OPEN CATALOG", {220, 229, 211},
                   [&](Rect& box) { box.center_at(open_button_.centerx(), open_button_.centery()); });
    const bool changed = !rules::changed(rules::current()).empty();  // amber while this world plays by changed rules
    advanced_button_ = Rect{0, 0, Canvas::text_width(button_font, "ADVANCED") + UI(32), UI(34)}.midright_at(open_button_.x - UI(10),
                                                                                                        height - UI(26));
    canvas.rect(advanced_button_, changed ? Color{238, 183, 110} : Color{44, 57, 52}, 0, UI(6));
    canvas.text_at(button_font, "ADVANCED", changed ? Color{19, 26, 27} : Color{220, 229, 211},
                   [&](Rect& box) { box.center_at(advanced_button_.centerx(), advanced_button_.centery()); });
    const int left = std::max(UI(185), UI(22) + Canvas::text_width(small, history) + UI(24));  // clear of the history, however long
    const int room = advanced_button_.x - left - UI(16);
    if (SDL_GetTicks() < message_until_) {
        canvas.text(small, fit(message_, room), {238, 183, 110}, left, height - UI(36));
        return;
    }
    canvas.text(small,
                fit("SPACE  pause   -/+  speed   LEFT / RIGHT  rewind / step   WHEEL / Z  zoom   I  insights   B  report   "
                    "G  graph   E  export   R  restart   F11  window   ESC  menu",
                    room),
                {187, 199, 181}, left, height - UI(36));
}

}  // namespace bitzkey::ui
