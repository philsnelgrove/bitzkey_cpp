#pragma once
// The running world: the board, the sidebar inspector and insights, the footer, the colour graph,
// the export list and dialogs.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Simulation.hpp"
#include "ui/Canvas.hpp"
#include "ui/ColourGraph.hpp"
#include "ui/Dialog.hpp"
#include "ui/Report.hpp"
#include "ui/Window.hpp"

namespace bitzkey::ui {

struct Screens;

class App {
public:
    enum class Result { Quit, Configure, Renew };

    App(Window& window, Simulation::Settings settings);
    // Run until the window closes or the user leaves for the configuration panel, or for a new world
    // with the Bitzkeys picked on the graph (Renew), which renewal() then holds.
    Result run();
    const std::string& renewal() const { return renewal_; }

private:
    friend struct Screens;  // the screenshot tool
    struct ExportEntry {
        int identifier;
        Dna dna;
        nlohmann::ordered_json saved;  // its record when added
    };
    struct Board {
        Rect rect;
        double cell_width, cell_height;
    };

    void tick(double seconds);
    std::string end_note() const;
    void finish();
    void close_graph();  // the pick on the graph stays selected on the map
    void seek(int turn);
    void click(int x, int y);
    void open(const std::string& kind, Dialog dialog);
    bool answer(const std::string& choice);  // true when the user leaves the simulation
    void leave(Dialog dialog);
    void ask_to_leave();
    // Close the graph with G or ESC. Before the run's end, that returns to the map; after it, it leaves
    // for the configuration panel, asking first only if Bitzkeys wait on the export list. Returns
    // whether to leave now.
    bool leave_graph();
    void open_export_dialog();
    Bitzkey* selected() const;
    void toggle_export();
    std::pair<std::vector<nlohmann::ordered_json>, int> chosen_records() const;
    void export_chosen(bool to_file);
    void export_graph(bool to_file);
    // Finish a left press on the map. A click picks the living Bitzkey under the pointer, or clears the
    // selection on empty ground; a drag picks every living Bitzkey in its marquee. With CTRL (`adding`),
    // a click adds the Bitzkey to the selection or takes it out, and a marquee adds to it.
    void pick_on_map(int start_x, int start_y, int end_x, int end_y, bool adding);
    std::optional<int> living_at(int x, int y);  // the living Bitzkey clicked at, if any
    void select(int identifier);  // on the map and the graph alike
    // Copy the living Bitzkeys picked on the graph to the clipboard and leave, to start a new world with
    // them alone imported. Returns whether it left.
    bool renew();
    struct Exemplar {
        const char* title;
        const Bitzkey* agent;  // the living animal that stands out so, if any
        std::string said;      // what its card says
    };
    std::vector<Exemplar> exemplars() const;
    std::pair<Rect, Rect> exemplar_columns(const Canvas& canvas) const;  // either side of the map
    Rect board_area(const Canvas& canvas) const;  // left of the sidebar, between the header and the footer
    void draw_exemplars(Canvas& canvas);
    void draw_exemplar_menu(Canvas& canvas);  // the open card's statistics, beneath its title
    int sidebar_width(const Canvas& canvas) const;
    // Where the map shows, and the scale at which the whole map fits it.
    std::pair<Rect, double> map_view(const Canvas& canvas) const;
    // The view's middle, in cells, kept so the map fills the view where it can, or centred where it can't.
    std::pair<double, double> clamped_centre(std::pair<double, double> centre, const Rect& view, double scale) const;
    // The whole map's rectangle on screen at the current zoom (larger than the view when zoomed in),
    // and the size of a cell.
    Board board_geometry(const Canvas& canvas) const;
    // Zoom the map with the wheel around the pointer, and pan it by dragging with the right or middle
    // button; the world carries on meanwhile. Returns whether the event was used.
    bool map_event(const SDL_Event& event);
    void save_report();
    void say(const std::string& message);
    void copy(const std::string& text, const std::string& what);
    void copy_selected();
    // The catalog: the Bitzkey selected on the map, the living picked on the graph (the dead have
    // lost their brains), or the export list (the living as they are now, the dead as when added).
    void add_to_catalog(const std::vector<nlohmann::ordered_json>& entries, const std::string& what,
                        const std::string& skipped = "");
    void catalog_selected();
    void catalog_picked();
    void open_death_report();  // every death so far, by cause and species
    void inspect_brains();     // pause, and show the selected Bitzkey's choices in each standard situation
    void browse_catalog();  // look through the catalog, importing and exporting nothing
    void edit_rules();      // pause, and change the rules this world plays by
    void catalog_export_list();
    void new_world();
    std::string save_export(const std::string& name, const std::string& text);
    double corpse_opacity(const Bitzkey& agent) const;

    void draw(Canvas& canvas);
    void draw_header(Canvas& canvas);
    void draw_world(Canvas& canvas);
    void draw_sidebar(Canvas& canvas);
    void draw_insights(Canvas& canvas, int x, int width);
    // The insights panel scrolls within the sidebar, below the population counts.
    Rect insights_view() const;
    Rect scroll_track() const;
    int max_scroll() const;
    std::optional<Rect> scroll_thumb() const;
    void scroll_to(int scroll);
    bool scroll_event(const SDL_Event& event);  // whether the event scrolled the panel
    void draw_footer(Canvas& canvas);
    std::string fit(const std::string& text, int width) const;

    Window& window_;
    Simulation::Settings settings_;
    std::unique_ptr<Simulation> simulation_;
    std::optional<int> selected_id_;
    bool show_insights_ = false;
    bool show_census_ = false;
    ColourGraph graph_;
    std::vector<ExportEntry> export_list_;
    std::optional<Dialog> dialog_;
    std::optional<Report> report_;        // the death report or a brain inspected, while open
    std::optional<Rect> deaths_button_;   // on the insights panel
    std::optional<Rect> brains_button_;   // beside the selected Bitzkey's heading
    std::string dialog_kind_;
    std::optional<Rect> add_button_;
    std::optional<Rect> catalog_button_;
    std::optional<std::tuple<int, int, bool>> marquee_from_;  // a left press on the map: where, and whether CTRL was down
    std::vector<std::pair<Rect, int>> exemplar_cards_;  // each card beside the map, and whom it shows
    std::vector<Rect> exemplar_titles_;                 // each card's title, a menu of statistics, card by card
    std::optional<int> exemplar_menu_;                  // the card whose menu is open
    std::vector<std::pair<Rect, int>> menu_items_;      // the open menu's choices, and which statistic each is
    std::string renewal_;  // the Bitzkeys to start a new world with, on leaving for one
    Rect export_button_;
    Rect open_button_;  // the catalog, to look through
    Rect advanced_button_;  // this world's rules
    std::string message_;
    Uint32 message_until_ = 0;
    bool playing_ = true;
    double elapsed_ = 0.0;
    int speed_ = 0;  // index into the turn intervals: 1x, 2x, 4x, 8x, as fast as possible
    // The map's zoom (1 shows it all) and the cell at the middle of the view, none while it shows
    // the whole map; while it is dragged, where the drag began and the middle then.
    double map_zoom_ = 1.0;
    std::optional<std::pair<double, double>> map_centre_;
    std::optional<std::pair<std::pair<int, int>, std::pair<double, double>>> pan_from_;
    int insights_scroll_ = 0;          // how far down the insights panel is scrolled
    int insights_height_ = 0;          // how tall it was when last drawn
    std::optional<int> scroll_grip_;   // while the scroll bar is dragged, where on the thumb it was taken
};

}  // namespace bitzkey::ui
