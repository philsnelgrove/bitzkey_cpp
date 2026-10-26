#pragma once
// The colour graph popup: every Bitzkey alive over time, stacked in its own colour.
//
// Each bar is a moment in the run; each Bitzkey alive then is a slice of it in its exact ring or
// dot colour, stacked by species and, within a species, sorted so similar colours flow together, or
// by a trait chosen from the SORT menu. Hovering over a slice shows that Bitzkey's details.
// Bitzkeys are picked by clicking their slice or dragging a box around slices, then copied or
// saved; the pick stays selected on the map when the graph closes. The graph also travels in time:
// drag the timeline beneath it, or right-click, to move the world to that turn. The mouse wheel
// zooms into a stretch of the run and, as on the map, into the height around the pointer; dragging
// with the right or middle button pans, and Z shows it all again.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <SDL.h>

#include "core/Simulation.hpp"
#include "ui/Canvas.hpp"

namespace bitzkey::ui {

struct Screens;

struct Bar {
    int turn = 0;  // the last turn the bar covers; its Bitzkeys are those alive then
    int left = 0, right = 0;
    std::vector<std::tuple<int, double, double>> slices;  // (identifier, top, bottom)
    bool future = false;  // after the present, when the world has been rewound
};

// The smallest 1, 2 or 5 times a power of ten at or above the value.
double nice_ceiling(double value);

// What a species' Bitzkeys are sorted by: their colour (0), then each mind trait, each social trait,
// then each body trait but species, then each climate gene; lowest at the bottom (for the home
// temperature, hottest at the bottom).
constexpr std::array<std::string_view, 3> CLIMATE_SORTS = {"temperature", "heat tolerance", "cold tolerance"};
constexpr int SORT_COUNT = 1 + TEMPERAMENT_COUNT + BODY_TRAIT_COUNT - 1 + 3;
std::string sort_name(int sort);   // "colour", or the trait's or climate gene's name
std::string sort_label(int sort);  // as the menu shows it
int sort_value(Dna dna, int sort);  // the inborn trait a sort orders by; 0 for colour

class ColourGraph {
public:
    // Map: the run is over, and the map is wanted. Catalog: catalog the Bitzkeys picked.
    // Open: the catalog, to look through.
    enum class Action { None, Copy, Save, Map, Catalog, Renew, Open };

    void switch_part() { part_ = (part_ + 1) % 3; }
    void reset();  // forget the selection and zoom, for a new world
    void unzoom();  // the whole run, its whole height
    const std::set<int>& selection() const { return selection_; }
    void select_only(int identifier) { select({identifier}, Mode::Replace); }
    void select_all(const std::set<int>& picked) { select(picked, Mode::Replace); }  // exactly these
    void keep_only(const std::set<int>& alive);  // let go of any picked that are no longer alive
    void close_menu() { menu_open_ = false; }

    // Handle a mouse event. Returns Copy or Save when a button asks for an export.
    Action handle(const SDL_Event& event);
    void draw(Canvas& canvas, const Simulation& simulation, const std::optional<std::string>& note);

private:
    friend struct Screens;  // the screenshot tool
    enum class Mode { Replace, Add, Remove, Toggle };
    struct Layout {
        Rect panel, chart, timeline, save, copy, clear, sort;
        std::optional<Rect> map;  // beneath the timeline, at the right, once the run is over
        Rect catalog;             // beneath the timeline, at the right, beside it
        Rect renew;               // and beside that: a new world with the Bitzkeys picked
        Rect open;                // at the bottom right, on the line of help: the catalog, to look through
    };
    Layout layout() const;
    std::vector<std::pair<int, Rect>> menu_items() const;  // the sort menu's choices
    void draw_sort_button(Canvas& canvas, const Rect& button);
    void draw_menu(Canvas& canvas);
    void select(const std::set<int>& picked, Mode mode);
    std::pair<int, int> window() const;  // the first and last turn shown
    int turn_at(int x) const;
    int x_of(int turn) const;
    void zoom(int notches, int x);
    // Zoom into the height (positive notches) or out, as the map zooms, the population under `y` staying put.
    void zoom_height(int notches, int y);
    void set_floor(double floor);
    // Pan by dragging from (start_x, start_y) to (x, y), the view then `view` and the floor `floor`: the graph
    // follows the pointer, in time while zoomed into it and in height while zoomed into that.
    void drag(int start_x, int start_y, std::pair<int, int> view, double floor, int x, int y);
    void pan(int notches);
    std::optional<int> bitzkey_at(int x, int y) const;
    std::set<int> bitzkeys_in(const Rect& box) const;
    void build(const Simulation& simulation, const Rect& chart, int first, int last);
    void render(const Simulation& simulation, const Rect& chart);
    void draw_timeline(Canvas& canvas, const Rect& chart, const Rect& timeline, int first, int last);
    // Shade each winter quarter on the timeline, and mark where each year begins.
    void draw_seasons(Canvas& canvas, const Rect& chart, const Rect& timeline, int first, int last);
    void label_species(Canvas& canvas, const Simulation& simulation, const Rect& chart);
    std::string describe(const Simulation& simulation, std::optional<int> hovered) const;
    static Rect box_between(int x1, int y1, int x2, int y2);

    int part_ = 0;  // the colours shown: 0 the ring's, 1 the dot's, 2 the social background's
    int sort_ = 0;
    bool menu_open_ = false;  // the sort menu
    std::set<int> selection_;
    std::optional<std::pair<int, int>> view_;  // the turns shown when zoomed in
    bool pinned_ = false;  // the zoomed view reaches the latest turn, and keeps up with it as the run goes on
    double height_zoom_ = 1.0;  // how far zoomed into the height
    double floor_ = 0.0;        // the share of the chart's full height at its bottom, zoomed in
    // A pan with the right or middle button: where it began, the button, the view and floor then, and whether
    // it has moved far enough to be a pan rather than a right-click.
    struct Pan {
        int x, y, button;
        std::pair<int, int> view;
        double floor;
        bool moved;
    };
    std::optional<Pan> pan_;
    int selection_version_ = 0;
    int width_ = 0, height_ = 0;
    int mouse_x_ = 0, mouse_y_ = 0;
    int now_ = 0;
    bool ended_ = false;  // whether the run is over, when last drawn
    std::optional<std::tuple<int, int, Uint16>> press_;  // where a press began, and its modifiers
    std::vector<Bar> bars_;
    std::map<int, std::vector<std::tuple<int, int, double, double>>> places_;  // where each Bitzkey is drawn
    double peak_ = 1.0;
    Surface image_;
    std::vector<long long> key_;
    std::set<int> alive_;  // who is alive, as last drawn
};

}  // namespace bitzkey::ui
