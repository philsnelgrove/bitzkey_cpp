#pragma once
// The running world: the board, the sidebar inspector and insights, the footer, the colour graph,
// the export list and dialogs.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Simulation.hpp"
#include "net/Pool.hpp"
#include "net/ProbeServer.hpp"
#include "net/Worker.hpp"
#include "ui/Canvas.hpp"
#include "ui/ColourGraph.hpp"
#include "ui/Dialog.hpp"
#include "ui/LanguageMenu.hpp"
#include "ui/Report.hpp"
#include "ui/Window.hpp"

namespace bitzkey::ui {

struct Screens;

class App {
public:
    enum class Result { Quit, Configure, Renew, Worker };  // Worker: the app became a worker, from WORKERS

    // Start a world by these settings, or carry on `loaded`, a world loaded; restarting starts afresh by the settings
    // either way. The world thinks through the pool while it lists workers.
    // With a probe, its looks at the world are answered between turns.
    App(Window& window, Simulation::Settings settings, net::Pool& pool, net::Worker& worker, std::unique_ptr<Simulation> loaded = nullptr,
        net::ProbeServer* probe = nullptr);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
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
    void click(int x, int y);
    void open(const std::string& kind, Dialog dialog);
    bool answer(const std::string& choice);  // true when the user leaves the simulation
    void leave(Dialog dialog);
    // Pause, and save the whole world to a file of the user's choosing, to load from the configuration panel and
    // carry on exactly as it would have. Returns whether it saved.
    bool save_world();
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
    // The square popup: moving the pointer over a map square lists the Bitzkeys stacked there, and the
    // list stays while the pointer is over it; clicking a name selects that Bitzkey. Returns whether the
    // event was used.
    bool square_event(const SDL_Event& event);
    void draw_square(Canvas& canvas);  // the popup listing the Bitzkeys in the square under the pointer
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
    bool open_workers();    // the workers it shares its thinking with; true when the app became a worker there
    // Deploy mode: the cursor becomes a target, and each click on the map, or each step of a drag, puts DEPLOY_GROUP
    // of each Bitzkey there; a right-click or ESC stops.
    void start_deploying(std::vector<Imported> bitzkeys);
    void stop_deploying();
    bool deploy_event(const SDL_Event& event);  // whether it used the event
    std::optional<std::pair<int, int>> cell_at(int x, int y);  // the world's cell under a point on the map
    void catalog_export_list();
    void new_world();
    std::string save_export(const std::string& name, const std::string& text);
    double corpse_opacity(const Bitzkey& agent) const;

    void draw(Canvas& canvas);
    void draw_header(Canvas& canvas);
    void draw_world(Canvas& canvas);
    void draw_ground(Canvas& canvas, const Board& board, const Rect& view);  // the squares, their grass and the night
    // The globe: the map wrapped round a sphere turned by globe_yaw_ about its axis and tipped by globe_tilt_, with
    // each square's Bitzkeys stacked on it, the largest at the bottom.
    void draw_globe(Canvas& canvas);
    bool globe_event(const SDL_Event& event, const Rect& view);  // a right or middle drag turns it, the wheel zooms
    void pick_on_globe(int x, int y);  // a left click selects the Bitzkey there, or none
    void draw_view_buttons(Canvas& canvas);  // MAP and GLOBE, above the map
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
    // Text in the small font in at most two lines of the width, broken between groups (three spaces apart) where it
    // has them, else between words; each line shortened with an ellipsis only if it still doesn't fit.
    std::vector<std::string> wrapped(const std::string& text, int width) const;
    // A footer note or the keys: one line if it fits, else two.
    void footer_lines(Canvas& canvas, const std::string& text, int left, int width, Color color) const;

    Window& window_;
    Simulation::Settings settings_;
    net::Pool& pool_;
    net::Worker& worker_;
    std::unique_ptr<Simulation> simulation_;
    net::ProbeServer* probe_ = nullptr;
    std::optional<int> selected_id_;
    bool show_insights_ = false;
    bool show_census_ = false;
    ColourGraph graph_;
    std::vector<ExportEntry> export_list_;
    std::optional<Dialog> dialog_;
    LanguageMenu language_menu_;
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
    std::optional<std::pair<int, int>> square_;         // the map square under the pointer, listed in a popup
    std::optional<Rect> square_panel_;
    std::vector<std::pair<Rect, int>> square_rows_;     // each name in the popup, and whom it is
    std::string renewal_;  // the Bitzkeys to start a new world with, on leaving for one
    Rect export_button_;
    Rect open_button_;  // the catalog, to look through
    Rect advanced_button_;  // this world's rules
    Rect save_button_;      // the whole world, to a file
    Rect workers_button_;   // the other Bitzkeys it shares its thinking with
    std::vector<Imported> deploying_;               // the Bitzkeys to paint onto the map; none when not deploying
    std::optional<std::pair<int, int>> painted_;    // the cell last painted while the button is held
    std::string message_;
    Uint32 message_until_ = 0;
    bool playing_ = true;
    double elapsed_ = 0.0;
    int speed_ = 0;  // index into the turn intervals: 1x, 2x, 4x, 8x, as fast as possible
    // The map's zoom (1 shows it all) and the cell at the middle of the view, none while it shows
    // the whole map; while it is dragged, where the drag began and the middle then.
    double map_zoom_ = 1.0;
    bool globe_ = false;       // the globe shows rather than the map
    bool hud_hidden_ = false;  // H: the map or globe alone, filling the window
    double globe_yaw_ = 0.0, globe_tilt_ = 0.35, globe_zoom_ = 1.0;
    std::optional<std::tuple<int, int, double, double>> turn_from_;  // a drag on the globe: where it began, and its turn then
    Rect map_button_, globe_button_, blind_button_;
    bool blind_ = false;  // neither map nor globe drawn, to spare the time
    std::vector<std::tuple<int, int, int, int>> globe_hits_;  // each living Bitzkey drawn on the globe: x, y, reach, identifier
    // Where each pixel of the globe's disc lies on the map, worked out again only as its size or tilt changes.
    struct GlobeLook {
        int size = 0;
        double tilt = 0.0;
        std::vector<float> longitude, row, light;  // a pixel each: the longitude before turning, the map's row (-1 off the disc), its light
    };
    GlobeLook globe_look_;
    Surface globe_ground_, globe_frame_;
    std::optional<std::pair<double, double>> map_centre_;
    std::optional<std::pair<std::pair<int, int>, std::pair<double, double>>> pan_from_;
    int insights_scroll_ = 0;          // how far down the insights panel is scrolled
    int insights_height_ = 0;          // how tall it was when last drawn
    std::optional<int> scroll_grip_;   // while the scroll bar is dragged, where on the thumb it was taken
};

}  // namespace bitzkey::ui
