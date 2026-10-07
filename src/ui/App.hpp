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
#include "ui/Window.hpp"

namespace bitzkey::ui {

struct Screens;

class App {
public:
    enum class Result { Quit, Configure };

    App(Window& window, Simulation::Settings settings);
    // Run until the window closes or the user leaves for the configuration panel.
    Result run();

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
    void open_export_dialog();
    Bitzkey* selected() const;
    void toggle_export();
    std::pair<std::vector<nlohmann::ordered_json>, int> chosen_records() const;
    void export_chosen(bool to_file);
    void export_graph(bool to_file);
    void select_at(int x, int y);
    int sidebar_width(const Canvas& canvas) const;
    Board board_geometry(const Canvas& canvas) const;
    void save_report();
    void say(const std::string& message);
    void copy(const std::string& text, const std::string& what);
    void copy_selected();
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
    std::string dialog_kind_;
    std::optional<Rect> add_button_;
    Rect export_button_;
    std::string message_;
    Uint32 message_until_ = 0;
    bool playing_ = true;
    double elapsed_ = 0.0;
    int speed_ = 0;  // index into the turn intervals: 1x, 2x, 4x, 8x, as fast as possible
    int insights_scroll_ = 0;          // how far down the insights panel is scrolled
    int insights_height_ = 0;          // how tall it was when last drawn
    std::optional<int> scroll_grip_;   // while the scroll bar is dragged, where on the thumb it was taken
};

}  // namespace bitzkey::ui
