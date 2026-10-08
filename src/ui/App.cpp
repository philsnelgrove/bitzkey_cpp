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
#include "ui/CatalogScreen.hpp"
#include "ui/Inspector.hpp"
#include "ui/Look.hpp"
#include "ui/Platform.hpp"

namespace bitzkey::ui {

namespace {

// Seconds between turns at each speed; 0 runs as many turns as fit in a frame.
constexpr std::array<double, 5> TURN_INTERVALS = {0.24, 0.12, 0.06, 0.03, 0.0};
constexpr std::array<const char*, 5> SPEED_LABELS = {"1x", "2x", "4x", "8x", "MAX"};
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
            if (dialog_) {
                if (const auto choice = dialog_->handle(event, window_.width(), window_.height())) {
                    Dialog current = *dialog_;
                    if (answer(*choice)) {
                        leave(std::move(current));
                        return Result::Configure;
                    }
                }
            } else if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
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
                } else if (key == SDLK_z && !show_census_) {
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
                else if (action != ColourGraph::Action::None) export_graph(action == ColourGraph::Action::Save);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                click(event.button.x, event.button.y);
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
    elapsed_ += seconds;
    if (!playing_ || dialog_) return;
    if (simulation_->ended()) {
        finish();
        return;
    }
    const double interval = TURN_INTERVALS[static_cast<std::size_t>(speed_)];
    if (interval > 0) {
        if (elapsed_ < interval) return;
        elapsed_ = 0;
        simulation_->advance();
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
    if (add_button_ && add_button_->contains(x, y)) {
        toggle_export();
    } else if (catalog_button_ && catalog_button_->contains(x, y)) {
        catalog_selected();
    } else if (export_button_.contains(x, y)) {
        open_export_dialog();
    } else {
        select_at(x, y);
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

void App::catalog_export_list() {
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

void App::select_at(int x, int y) {
    Canvas canvas = window_.frame();
    const Board board = board_geometry(canvas);
    const Bitzkey* nearest = nullptr;
    double nearest_distance = std::numeric_limits<double>::infinity();
    for (const auto& agent : simulation_->agents()) {
        const double px = board.rect.x + (agent->x + 0.5) * board.cell_width;
        const double py = board.rect.y + (agent->y + 0.5) * board.cell_height;
        const double distance = std::hypot(x - px, y - py);
        if (distance < nearest_distance) {
            nearest = agent.get();
            nearest_distance = distance;
        }
    }
    const bool on_view = map_view(canvas).first.contains(x, y);  // zoomed in, the board reaches under the sidebar
    if (nearest && on_view && board.rect.contains(x, y) &&
        nearest_distance < std::max<double>(UI(9), std::min(board.cell_width, board.cell_height) * 1.6)) {
        selected_id_ = nearest->identifier;
        graph_.select_only(nearest->identifier);  // the map and the graph share one selection
        show_insights_ = false;                   // selecting shows the inspector
    }
}

void App::save_report() {
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
    const Rect area{0, UI(62), canvas.width() - sidebar_width(canvas), canvas.height() - UI(140)};
    const Rect view = area.inflate(-UI(42), -UI(42));
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
    canvas.fill({19, 26, 27});
    draw_header(canvas);
    draw_world(canvas);
    draw_sidebar(canvas);
    draw_footer(canvas);
    if (show_census_) {
        const std::optional<std::string> note =
            SDL_GetTicks() < message_until_ ? std::optional<std::string>(message_) : std::nullopt;
        graph_.draw(canvas, *simulation_, note);
    }
    if (dialog_) dialog_->draw(canvas);
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
    canvas.text_at(UI.font(Face::Sans, 12), speed, {149, 170, 150}, [&](Rect& box) {
        box.x = label.x - box.w - UI(20);
        box.y = UI(23);
    });
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
    const int largest = std::max(DOT_RADIUS_LARGEST, static_cast<int>(py_round(std::min(board.cell_width, board.cell_height) * 0.45)));
    for (const auto& agent : simulation_->agents()) {  // the way each came this turn, faint, beneath them all
        Surface layer;
        for (const auto& [x, y] : agent->trail) {
            const int px = static_cast<int>(py_round(rect.x + (x + 0.5) * board.cell_width));
            const int py = static_cast<int>(py_round(rect.y + (y + 0.5) * board.cell_height));
            if ((x == agent->x && y == agent->y) || !reach.contains(px, py)) continue;
            if (!layer) layer = bitzkey_layer(agent->dna, dot_radius(agent->body[Size], largest), RING_WIDTH, agent->alive());
            const int half = layer->w / 2;
            canvas.blit(layer.get(), px - half, py - half, static_cast<std::uint8_t>(py_round(255 * TRAIL_OPACITY)));
        }
    }
    for (const auto& agent : simulation_->agents()) {
        const int px = static_cast<int>(py_round(rect.x + (agent->x + 0.5) * board.cell_width));
        const int py = static_cast<int>(py_round(rect.y + (agent->y + 0.5) * board.cell_height));
        if (!reach.contains(px, py)) continue;
        const int radius = dot_radius(agent->body[Size], largest);
        draw_bitzkey(canvas, agent->dna, px, py, radius, RING_WIDTH, agent->alive(), corpse_opacity(*agent));
        if ((selected_id_ && agent->identifier == *selected_id_) || (agent->alive() && graph_.selection().contains(agent->identifier))) {
            canvas.circle(px, py, radius + RING_WIDTH + UI(3), SELECTED, std::max(1, UI(1)));
        }
        if (std::any_of(export_list_.begin(), export_list_.end(),
                        [&](const ExportEntry& entry) { return entry.identifier == agent->identifier; })) {
            canvas.circle(px, py, radius + RING_WIDTH + UI(6), EXPORTING, std::max(1, UI(1)));
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
    for (int trait = 0; trait < MIND_TRAIT_COUNT; ++trait) {
        canvas.text(small, upper(MIND_TRAITS[static_cast<std::size_t>(trait)]), {149, 170, 150}, x, at(y));
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
    const int room = export_button_.x - UI(185) - UI(16);
    if (SDL_GetTicks() < message_until_) {
        canvas.text(small, fit(message_, room), {238, 183, 110}, UI(185), height - UI(36));
        return;
    }
    canvas.text(small,
                fit("SPACE  pause   -/+  speed   LEFT / RIGHT  rewind / step   WHEEL / Z  zoom   I  insights   B  report   "
                    "G  graph   E  export   R  restart   F11  window   ESC  menu",
                    room),
                {187, 199, 181}, UI(185), height - UI(36));
}

}  // namespace bitzkey::ui
