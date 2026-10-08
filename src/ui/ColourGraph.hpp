#pragma once
// The colour graph popup: every Bitzkey alive over time, stacked in its own colour.
//
// Each bar is a moment in the run; each Bitzkey alive then is a slice of it in its exact ring or
// dot colour, stacked by species and, within a species, sorted so similar colours flow together, or
// by a trait chosen from the SORT menu. Hovering over a slice shows that Bitzkey's details.
// Bitzkeys are picked by clicking their slice or dragging a box around slices, then copied or
// saved; the pick stays selected on the map when the graph closes. The graph also travels in time:
// drag the timeline beneath it, or right-click, to move the world to that turn. The mouse wheel
// zooms into a stretch of the run.

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

// What a species' Bitzkeys are sorted by: their colour (0), then each mind trait, then each body
// trait but species, then each climate gene; lowest at the bottom (for the home temperature,
// hottest at the bottom).
constexpr std::array<std::string_view, 3> CLIMATE_SORTS = {"temperature", "heat tolerance", "cold tolerance"};
constexpr int SORT_COUNT = 1 + MIND_TRAIT_COUNT + BODY_TRAIT_COUNT - 1 + 3;
std::string sort_name(int sort);   // "colour", or the trait's or climate gene's name
std::string sort_label(int sort);  // as the menu shows it
int sort_value(Dna dna, int sort);  // the inborn trait a sort orders by; 0 for colour

class ColourGraph {
public:
    // Map: the run is over, and the map is wanted. Catalog: catalog the Bitzkeys picked.
    enum class Action { None, Copy, Save, Seek, Map, Catalog };

    void switch_part() { dot_ = !dot_; }
    void reset();  // forget the selection and zoom, for a new world
    const std::set<int>& selection() const { return selection_; }
    void select_only(int identifier) { select({identifier}, Mode::Replace); }
    void close_menu() { menu_open_ = false; }
    int seek_turn() const { return seek_turn_; }

    // Handle a mouse event. Returns Copy or Save when a button asks for an export, and Seek when
    // the world should move to `seek_turn()`.
    Action handle(const SDL_Event& event);
    void draw(Canvas& canvas, const Simulation& simulation, const std::optional<std::string>& note);

private:
    friend struct Screens;  // the screenshot tool
    enum class Mode { Replace, Add, Remove, Toggle };
    struct Layout {
        Rect panel, chart, timeline, save, copy, clear, living, sort;
        std::optional<Rect> map;  // beneath the timeline, at the right, once the run is over
        Rect catalog;             // beneath the timeline, at the right, beside it
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

    bool dot_ = false;
    int sort_ = 0;
    bool menu_open_ = false;  // the sort menu
    std::set<int> selection_;
    std::optional<std::pair<int, int>> view_;  // the turns shown when zoomed in
    int seek_turn_ = 0;
    int selection_version_ = 0;
    int width_ = 0, height_ = 0;
    int mouse_x_ = 0, mouse_y_ = 0;
    int now_ = 0, frontier_ = 0;
    bool ended_ = false;  // whether the run is over, when last drawn
    std::optional<std::tuple<int, int, Uint16>> press_;  // where a press began, and its modifiers
    std::optional<int> scrub_;                           // the turn under the playhead while dragged
    std::vector<Bar> bars_;
    std::map<int, std::vector<std::tuple<int, int, double, double>>> places_;  // where each Bitzkey is drawn
    double peak_ = 1.0;
    Surface image_;
    std::vector<long long> key_;
    std::set<int> alive_;  // who is alive, as last drawn
};

}  // namespace bitzkey::ui
