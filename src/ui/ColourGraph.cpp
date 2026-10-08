#include "ui/ColourGraph.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

#include "core/PyFormat.hpp"
#include "ui/Icons.hpp"
#include "ui/Inspector.hpp"
#include "ui/Look.hpp"

namespace bitzkey::ui {

namespace {
constexpr int BAR_PIXELS = 5;          // target width of one bar
constexpr int DRAG_THRESHOLD = 4;      // a press that moves less than this (design pixels) is a click
constexpr double DIM = 0.75;           // how far unselected Bitzkeys fade toward the background
constexpr double FUTURE_DIM = 0.55;    // how far turns after the present fade, once rewound
constexpr int MIN_VIEW = 10;           // the fewest turns the graph zooms in to
constexpr double ZOOM_STEP = 0.8;      // each wheel notch shows this share of the turns before it
constexpr Color BACKGROUND{30, 40, 40};
constexpr int MENU_ROW = 22;           // design pixels per item of the sort menu
constexpr int YEAR_LABEL_SPACING = 56; // design pixels the year labels on the timeline keep apart, at least
constexpr int FIRST_CLIMATE_SORT = SORT_COUNT - static_cast<int>(CLIMATE_SORTS.size());

// The text, cut short with an ellipsis if it is wider than `width`.
std::string fit_text(const std::string& text, TTF_Font* font, int width) {
    if (Canvas::text_width(font, text) <= width) return text;
    std::string shortened = text;
    while (!shortened.empty() && Canvas::text_width(font, shortened + "...") > width) shortened.pop_back();
    while (!shortened.empty() && shortened.back() == ' ') shortened.pop_back();
    return shortened + "...";
}

Color colour_of(Dna dna, bool dot) { return to_color(color_from_word(dot ? mind_word(dna) : body_word(dna))); }

Color faded(Color colour, double amount) {
    const auto channel = [amount](int value, int back) {
        return static_cast<std::uint8_t>(py_round(value + (back - value) * amount));
    };
    return {channel(colour.r, BACKGROUND.r), channel(colour.g, BACKGROUND.g), channel(colour.b, BACKGROUND.b)};
}

std::string format(const char* pattern, auto... values) {
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, pattern, values...);
    return buffer;
}

// Python's format(value, "g"), for the chart's axis labels.
std::string general(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}
}  // namespace

double nice_ceiling(double value) {
    double scale = 1;
    while (true) {
        for (const int step : {1, 2, 5}) {
            if (step * scale >= value) return step * scale;
        }
        scale *= 10;
    }
}

std::string sort_name(int sort) {
    if (sort == 0) return "colour";
    if (sort >= FIRST_CLIMATE_SORT) return std::string(CLIMATE_SORTS[static_cast<std::size_t>(sort - FIRST_CLIMATE_SORT)]);
    if (sort <= MIND_TRAIT_COUNT) return std::string(MIND_TRAITS[static_cast<std::size_t>(sort - 1)]);
    return std::string(BODY_TRAITS[static_cast<std::size_t>(sort - MIND_TRAIT_COUNT)]);
}

std::string sort_label(int sort) {
    if (sort == 0) return "Colour value";
    std::string name = sort_name(sort);
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

int sort_value(Dna dna, int sort) {
    if (sort == 0) return 0;
    if (sort >= FIRST_CLIMATE_SORT) {
        const Climate climate = decode_climate(dna);
        return sort == FIRST_CLIMATE_SORT ? climate.temperature : sort == FIRST_CLIMATE_SORT + 1 ? climate.heat : climate.cold;
    }
    if (sort <= MIND_TRAIT_COUNT) return decode_mind(dna)[static_cast<std::size_t>(sort - 1)];
    return decode_body(dna)[static_cast<std::size_t>(sort - MIND_TRAIT_COUNT)];
}

void ColourGraph::reset() {
    select({}, Mode::Replace);
    view_.reset();
}

void ColourGraph::select(const std::set<int>& picked, Mode mode) {
    std::set<int> result;
    switch (mode) {
        case Mode::Add:
            result = selection_;
            result.insert(picked.begin(), picked.end());
            break;
        case Mode::Remove:
            std::set_difference(selection_.begin(), selection_.end(), picked.begin(), picked.end(), std::inserter(result, result.end()));
            break;
        case Mode::Toggle:
            std::set_symmetric_difference(selection_.begin(), selection_.end(), picked.begin(), picked.end(),
                                          std::inserter(result, result.end()));
            break;
        default:
            result = picked;
            break;
    }
    selection_ = std::move(result);
    ++selection_version_;
}

std::pair<int, int> ColourGraph::window() const {
    if (!view_) return {0, frontier_};
    return {std::max(0, view_->first), std::min(frontier_, view_->second)};
}

int ColourGraph::turn_at(int x) const {
    const Rect chart = layout().chart;
    const auto [first, last] = window();
    const int turns = last - first + 1;
    const long long offset = static_cast<long long>(x - chart.x) * turns;
    const int width = std::max(1, chart.w);
    const long long step = offset >= 0 ? offset / width : -((-offset + width - 1) / width);  // floor division
    return std::max(first, std::min(last, first + static_cast<int>(step)));
}

int ColourGraph::x_of(int turn) const {
    const Rect chart = layout().chart;
    const auto [first, last] = window();
    return static_cast<int>(py_round(chart.x + (turn - first + 0.5) * chart.w / (last - first + 1)));
}

void ColourGraph::zoom(int notches, int x) {
    const auto [first, last] = window();
    const int span = last - first + 1;
    const int whole = frontier_ + 1;
    const int new_span = std::max(MIN_VIEW, static_cast<int>(py_round(span * std::pow(ZOOM_STEP, notches))));
    if (new_span >= whole) {
        view_.reset();
        return;
    }
    const int centre = turn_at(x);
    int new_first = static_cast<int>(py_round(centre - static_cast<double>(centre - first) * new_span / span));
    new_first = std::max(0, std::min(whole - new_span, new_first));
    view_ = std::pair{new_first, new_first + new_span - 1};
}

void ColourGraph::pan(int notches) {
    if (!view_) return;
    const auto [first, last] = window();
    const int span = last - first + 1;
    const int new_first = std::max(0, std::min(frontier_ + 1 - span, first + notches * std::max(1, span / 10)));
    view_ = std::pair{new_first, new_first + span - 1};
}

ColourGraph::Layout ColourGraph::layout() const {
    Layout places;
    places.panel = Rect{0, 0, std::min(UI(940), width_ - UI(60)), std::min(UI(620), height_ - UI(90))}.center_at(width_ / 2, height_ / 2);
    const Rect& panel = places.panel;
    places.chart = {panel.x + UI(64), panel.y + UI(92), panel.w - UI(92), panel.h - UI(196)};
    places.timeline = {places.chart.x, places.chart.bottom() + UI(4), places.chart.w, UI(24)};
    int right = panel.right() - UI(24);
    for (const auto& [button, width] : {std::pair{&places.save, 76}, std::pair{&places.copy, 76},
                                        std::pair{&places.clear, 76}, std::pair{&places.sort, 170}}) {
        *button = {right - UI(width), panel.y + UI(18), UI(width), UI(30)};
        right -= UI(width + 8);
    }
    int bottom_right = panel.right() - UI(24);  // beneath the timeline, at the right
    if (ended_) {
        places.map = Rect{bottom_right - UI(140), places.timeline.bottom() + UI(6), UI(140), UI(28)};
        bottom_right = places.map->x - UI(8);
    }
    places.catalog = Rect{bottom_right - UI(150), places.timeline.bottom() + UI(6), UI(150), UI(28)};
    places.renew = Rect{places.catalog.x - UI(8) - UI(310), places.timeline.bottom() + UI(6), UI(310), UI(28)};
    return places;
}

std::vector<std::pair<int, Rect>> ColourGraph::menu_items() const {
    // Under the sort button, a small gap before the mind traits, the body traits and the climate genes.
    const Rect button = layout().sort;
    std::vector<std::pair<int, Rect>> items;
    int top = button.bottom() + UI(2);
    for (int sort = 0; sort < SORT_COUNT; ++sort) {
        if (sort == 1 || sort == 1 + MIND_TRAIT_COUNT || sort == FIRST_CLIMATE_SORT) top += UI(6);
        items.emplace_back(sort, Rect{button.x, top, button.w, UI(MENU_ROW)});
        top += UI(MENU_ROW);
    }
    return items;
}

Rect ColourGraph::box_between(int x1, int y1, int x2, int y2) {
    return {std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1) + 1, std::abs(y2 - y1) + 1};
}

std::optional<int> ColourGraph::bitzkey_at(int x, int y) const {
    for (const Bar& bar : bars_) {
        if (bar.left <= x && x < bar.right) {
            for (const auto& [identifier, top, bottom] : bar.slices) {
                if (top <= y && y < bottom) return identifier;
            }
        }
    }
    return std::nullopt;
}

std::set<int> ColourGraph::bitzkeys_in(const Rect& box) const {
    std::set<int> found;
    for (const Bar& bar : bars_) {
        if (bar.right > box.x && bar.left < box.right()) {
            for (const auto& [identifier, top, bottom] : bar.slices) {
                if (bottom > box.y && top < box.bottom()) found.insert(identifier);
            }
        }
    }
    return found;
}

ColourGraph::Action ColourGraph::handle(const SDL_Event& event) {
    const Layout places = layout();
    const Rect &chart = places.chart, &timeline = places.timeline;
    if (menu_open_ && event.type == SDL_MOUSEBUTTONDOWN) {  // a click picks a choice or closes the menu
        menu_open_ = false;
        if (event.button.button == SDL_BUTTON_LEFT) {
            for (const auto& [sort, item] : menu_items()) {
                if (item.contains(event.button.x, event.button.y)) sort_ = sort;
            }
        }
        return Action::None;
    }
    if (event.type == SDL_MOUSEMOTION) {
        mouse_x_ = event.motion.x;
        mouse_y_ = event.motion.y;
        if (scrub_) scrub_ = turn_at(event.motion.x);
    } else if (event.type == SDL_MOUSEWHEEL) {
        if (chart.contains(mouse_x_, mouse_y_) || timeline.contains(mouse_x_, mouse_y_)) {
            if (event.wheel.x) {
                pan(event.wheel.x);
            } else if (SDL_GetModState() & KMOD_SHIFT) {
                pan(-event.wheel.y);
            } else {
                zoom(event.wheel.y, mouse_x_);
            }
        }
    } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
        if (chart.contains(event.button.x, event.button.y) || timeline.contains(event.button.x, event.button.y)) {
            seek_turn_ = turn_at(event.button.x);
            return Action::Seek;
        }
    } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        const int x = event.button.x, y = event.button.y;
        if (places.copy.contains(x, y)) return Action::Copy;
        if (places.save.contains(x, y)) return Action::Save;
        if (places.map && places.map->contains(x, y)) return Action::Map;
        if (places.catalog.contains(x, y)) return Action::Catalog;
        if (places.renew.contains(x, y)) return Action::Renew;
        if (places.sort.contains(x, y)) {
            menu_open_ = true;
        } else if (places.clear.contains(x, y)) {
            select({}, Mode::Replace);
        } else if (timeline.contains(x, y)) {
            scrub_ = turn_at(x);
        } else if (chart.contains(x, y)) {
            press_ = std::tuple{x, y, static_cast<Uint16>(SDL_GetModState())};
        }
    } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        if (scrub_) {
            seek_turn_ = turn_at(event.button.x);
            scrub_.reset();
            return Action::Seek;
        }
        if (press_) {
            const auto [start_x, start_y, modifiers] = *press_;
            press_.reset();
            const bool shift = modifiers & KMOD_SHIFT, ctrl = modifiers & KMOD_CTRL;
            if (std::max(std::abs(event.button.x - start_x), std::abs(event.button.y - start_y)) < UI(DRAG_THRESHOLD)) {
                const auto hit = bitzkey_at(start_x, start_y);
                std::set<int> picked;
                if (hit && alive_.contains(*hit)) picked.insert(*hit);  // only the living can be picked
                select(picked, ctrl ? Mode::Remove : shift ? Mode::Toggle : Mode::Replace);
            } else {
                const Rect box = box_between(start_x, start_y, event.button.x, event.button.y).clip(chart);
                std::set<int> picked;
                for (const int identifier : bitzkeys_in(box)) {
                    if (alive_.contains(identifier)) picked.insert(identifier);
                }
                select(picked, ctrl ? Mode::Remove : shift ? Mode::Add : Mode::Replace);
            }
        }
    }
    return Action::None;
}

void ColourGraph::build(const Simulation& simulation, const Rect& chart, int first, int last) {
    // One bar per stretch of turns, each stacking the Bitzkeys alive at its end; the bar holding
    // the present shows it exactly.
    const int turns = last - first + 1;
    const int count = std::max(1, std::min(chart.w / BAR_PIXELS, turns));
    const int now = simulation.turn();
    std::vector<int> present;
    for (const Bitzkey* agent : simulation.living()) present.push_back(agent->identifier);
    const std::unordered_set<int> alive(present.begin(), present.end());
    struct Moment {
        int end;
        std::span<const int> members;
        bool future;
    };
    std::vector<Moment> moments;
    std::size_t most = 0;
    for (int index = 0; index < count; ++index) {
        const int start = first + static_cast<int>(static_cast<long long>(index) * turns / count);
        const int end = first + static_cast<int>(static_cast<long long>(index + 1) * turns / count) - 1;
        const std::span<const int> members = (start <= now && now <= end) ? std::span<const int>(present) : simulation.census().at(end);
        moments.push_back({end, members, start > now});
        most = std::max(most, members.size());
    }
    peak_ = nice_ceiling(static_cast<double>(most));
    const double unit = chart.h / peak_;

    // Stacked by species, plants at the bottom; within each, by the sort's trait if there is one, then
    // similar colours together: greys by lightness, then hues round the wheel. A trait sorts only the
    // living, the dead being remembered by their colour alone, beneath them.
    using Key = std::tuple<int, int, int, double, double, std::uint32_t, int>;
    std::unordered_map<int, Key> order;
    const auto key_of = [&](int identifier) -> const Key& {
        auto found = order.find(identifier);
        if (found == order.end()) {
            const Dna dna = simulation.lineage(identifier);
            const auto [group, hue, value] = colour_order(color_from_word(dot_ ? mind_word(dna) : body_word(dna)));
            const int trait = sort_ == 0 ? 0 : alive.contains(identifier) ? sort_value(dna, sort_) : -1;
            found = order.emplace(identifier, Key{decode_body(dna)[SpeciesTrait], trait, group, hue, value,
                                                  dot_ ? mind_word(dna) : body_word(dna), identifier}).first;
        }
        return found->second;
    };
    bars_.clear();
    std::vector<int> members;
    for (int index = 0; index < count; ++index) {
        const Moment& moment = moments[static_cast<std::size_t>(index)];
        Bar bar{moment.end, chart.x + index * chart.w / count, chart.x + (index + 1) * chart.w / count, {}, moment.future};
        members.assign(moment.members.begin(), moment.members.end());
        std::sort(members.begin(), members.end(), [&](int a, int b) { return key_of(a) < key_of(b); });
        double bottom = chart.bottom();
        for (const int identifier : members) {
            bar.slices.emplace_back(identifier, bottom - unit, bottom);
            bottom -= unit;
        }
        bars_.push_back(std::move(bar));
    }
}

void ColourGraph::render(const Simulation& simulation, const Rect& chart) {
    // Draw the bars once into an image; slices too thin to see merge with their neighbours.
    // Turns after the present, when rewound, are faded.
    image_ = make_surface(chart.w, chart.h);
    Canvas image(image_.get());
    image.fill(BACKGROUND);
    for (const double fraction : {0.5, 1.0}) {
        const int y = static_cast<int>(py_round(chart.h - fraction * chart.h));
        image.line(0, y, chart.w, y, {54, 68, 62});
    }
    image.line(0, chart.h - 1, chart.w, chart.h - 1, {54, 68, 62});
    const bool choosing = !selection_.empty();
    const auto fill = [&](Color colour, int left, int width, double top, double bottom) {
        const int y0 = static_cast<int>(py_round(top)) - chart.y, y1 = static_cast<int>(py_round(bottom)) - chart.y;
        if (y1 > y0) image.fill_rect({left, y0, width, y1 - y0}, colour);
    };
    for (const Bar& bar : bars_) {
        const int left = bar.left - chart.x;
        const int width = std::max(1, bar.right - bar.left - (bar.right - bar.left > 3 ? 1 : 0));
        std::optional<Color> run_colour;
        double run_top = 0, run_bottom = 0;
        for (const auto& [identifier, top, bottom] : bar.slices) {
            Color colour = colour_of(simulation.lineage(identifier), dot_);
            if (choosing && !selection_.contains(identifier)) colour = faded(colour, DIM);
            if (bar.future) colour = faded(colour, FUTURE_DIM);
            if (!run_colour || !(colour == *run_colour)) {
                if (run_colour) fill(*run_colour, left, width, run_top, run_bottom);
                run_colour = colour;
                run_bottom = bottom;
            }
            run_top = top;
        }
        if (run_colour) fill(*run_colour, left, width, run_top, run_bottom);
    }
}

void ColourGraph::draw(Canvas& canvas, const Simulation& simulation, const std::optional<std::string>& note) {
    width_ = canvas.width();
    height_ = canvas.height();
    alive_.clear();
    for (const Bitzkey* agent : simulation.living()) alive_.insert(agent->identifier);
    if (std::any_of(selection_.begin(), selection_.end(), [&](int identifier) { return !alive_.contains(identifier); })) {
        std::set<int> living;  // the dead are forgotten but for their colour
        std::set_intersection(selection_.begin(), selection_.end(), alive_.begin(), alive_.end(),
                              std::inserter(living, living.end()));
        select(living, Mode::Replace);
    }
    now_ = simulation.turn();
    frontier_ = simulation.frontier();
    ended_ = simulation.ended();
    const Layout places = layout();
    const Rect &panel = places.panel, &chart = places.chart, &timeline = places.timeline;
    TTF_Font* font = UI.font(Face::Sans, 15);
    TTF_Font* small = UI.font(Face::Sans, 12);
    canvas.fill_rect({0, 0, width_, height_}, {8, 12, 12, 175});
    canvas.rect(panel, BACKGROUND, 0, UI(6));
    canvas.rect(panel, {80, 99, 85}, 1, UI(6));
    canvas.text(UI.font(Face::Sans, 22, true), std::string("COLOUR GRAPH  /  ") + (dot_ ? "DOT / MIND" : "RING / BODY"),
                {229, 239, 220}, panel.x + UI(24), panel.y + UI(18));
    canvas.text(small,
                std::string("Every Bitzkey alive over time, in its own colour.    TAB  ring / dot    C  copy    S  save    K  catalog    "
                            "G or ESC  ") + (ended_ ? "new world" : "close"),
                {144, 170, 151}, panel.x + UI(24), panel.y + UI(54));
    for (const auto& [button, name] : {std::pair{places.clear, "CLEAR"}, std::pair{places.copy, "COPY"},
                                       std::pair{places.save, "SAVE"}}) {
        canvas.rect(button, {44, 57, 52}, 0, UI(4));
        canvas.text_at(font, name, {220, 229, 211}, [&](Rect& box) { box.center_at(button.centerx(), button.centery()); });
    }
    draw_sort_button(canvas, places.sort);
    if (note) canvas.text_at(small, *note, {238, 183, 110}, [&](Rect& box) { box.topright_at(panel.right() - UI(24), panel.y + UI(58)); });

    const auto [first, last] = window();
    const std::vector<long long> key = {simulation.turn(), simulation.frontier(), static_cast<long long>(simulation.census().snapshots().size()),
                                        static_cast<long long>(simulation.agents().size()), simulation.deaths(), dot_, chart.w, chart.h,
                                        chart.x, chart.y, selection_version_, first, last,
                                        static_cast<long long>(reinterpret_cast<std::uintptr_t>(&simulation)), sort_};
    if (key != key_) {
        key_ = key;
        build(simulation, chart, first, last);
        places_.clear();
        for (const Bar& bar : bars_) {
            for (const auto& [identifier, top, bottom] : bar.slices) places_[identifier].emplace_back(bar.left, bar.right, top, bottom);
        }
        render(simulation, chart);
    }
    canvas.blit(image_.get(), chart.x, chart.y);
    label_species(canvas, simulation, chart);

    for (const double fraction : {0.0, 0.5, 1.0}) {
        const int y = static_cast<int>(py_round(chart.bottom() - fraction * chart.h));
        canvas.text_at(small, general(peak_ * fraction), {149, 170, 150},
                       [&](Rect& box) { box.x = chart.x - box.w - UI(8); box.y = y - box.h / 2; });
    }

    draw_timeline(canvas, chart, timeline, first, last);

    const std::optional<int> hovered =
        chart.contains(mouse_x_, mouse_y_) && !menu_open_ ? bitzkey_at(mouse_x_, mouse_y_) : std::nullopt;
    if (hovered) {
        for (const auto& [left, right, top, bottom] : places_[*hovered]) {
            canvas.rect({left, static_cast<int>(py_round(top)) - 1, right - left, std::max(1, static_cast<int>(py_round(bottom - top))) + 2},
                        {245, 238, 206}, 1);
        }
    }
    if (press_) {
        const Rect box = box_between(std::get<0>(*press_), std::get<1>(*press_), mouse_x_, mouse_y_).clip(chart);
        canvas.fill_rect(box, {245, 238, 206, 40});
        canvas.rect(box, {245, 238, 206}, 1);
    }
    const int room = places.renew.x - panel.x - UI(40);
    canvas.text(font, fit_text(describe(simulation, hovered), font, room), {220, 229, 211}, panel.x + UI(24), timeline.bottom() + UI(10));
    if (places.map) {
        canvas.rect(*places.map, {238, 183, 110}, 0, UI(4));
        canvas.text_at(font, "RETURN TO MAP", {19, 26, 27}, [&](Rect& box) { box.center_at(places.map->centerx(), places.map->centery()); });
    }
    canvas.rect(places.catalog, {44, 57, 52}, 0, UI(4));
    canvas.text_at(font, "ADD TO CATALOG", {220, 229, 211},
                   [&](Rect& box) { box.center_at(places.catalog.centerx(), places.catalog.centery()); });
    canvas.rect(places.renew, {44, 57, 52}, 0, UI(4));
    canvas.text_at(font, "START A NEW WORLD WITH THE SELECTED", {220, 229, 211},
                   [&](Rect& box) { box.center_at(places.renew.centerx(), places.renew.centery()); });
    canvas.text(small,
                "Select: click, or drag a box (SHIFT adds, CTRL removes)    Time: drag the timeline, or right-click    "
                "Zoom: wheel, SHIFT+wheel pans",
                {144, 170, 151}, panel.x + UI(24), panel.bottom() - UI(28));
    if (menu_open_) {
        draw_menu(canvas);
    } else if (hovered && alive_.contains(*hovered) && !press_) {  // the dead show only their colour, below
        const auto& agents = simulation.agents();
        const auto found = std::find_if(agents.begin(), agents.end(), [&](const auto& agent) { return agent->identifier == *hovered; });
        if (found != agents.end()) draw_tooltip(canvas, **found, true, mouse_x_, mouse_y_, simulation.temperature((*found)->y));
    }
}

void ColourGraph::draw_sort_button(Canvas& canvas, const Rect& button) {
    canvas.rect(button, {44, 57, 52}, 0, UI(4));
    if (menu_open_ || sort_ != 0) canvas.rect(button, {131, 205, 143}, 1, UI(4));
    std::string label = "SORT  " + sort_label(sort_);
    for (char& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    canvas.text_at(UI.font(Face::Sans, 12), label, {220, 229, 211},
                   [&](Rect& box) { box.midleft_at(button.x + UI(10), button.centery()); });
    const int x = button.right() - UI(14), y = button.centery();
    canvas.polygon({{x - UI(5), y - UI(2)}, {x + UI(5), y - UI(2)}, {x, y + UI(4)}}, {220, 229, 211});
}

void ColourGraph::draw_menu(Canvas& canvas) {
    const auto items = menu_items();
    const Rect& first = items.front().second;
    const Rect& last = items.back().second;
    const Rect back = Rect{first.x, first.y, first.w, last.bottom() - first.y}.inflate(UI(4), UI(4));
    canvas.rect(back, {19, 26, 27}, 0, UI(4));
    canvas.rect(back, {80, 99, 85}, 1, UI(4));
    TTF_Font* small = UI.font(Face::Sans, 12);
    for (const auto& [sort, item] : items) {
        if (item.contains(mouse_x_, mouse_y_)) canvas.fill_rect(item, {44, 57, 52});
        canvas.text_at(small, sort_label(sort), sort == sort_ ? Color{131, 205, 143} : Color{220, 229, 211},
                       [&](Rect& box) { box.midleft_at(item.x + UI(10), item.centery()); });
    }
}

void ColourGraph::label_species(Canvas& canvas, const Simulation& simulation, const Rect& chart) {
    // Name each species' band, with its icon, inside the right end of the chart, where its latest
    // bar is tall enough to hold the label.
    if (bars_.empty()) return;
    std::map<int, std::pair<double, double>> bands;  // species -> (top, bottom)
    for (const auto& [identifier, top, bottom] : bars_.back().slices) {
        const int species = decode_body(simulation.lineage(identifier))[SpeciesTrait];
        const auto found = bands.find(species);
        if (found == bands.end()) {
            bands.emplace(species, std::pair{top, bottom});
        } else {
            found->second.first = std::min(found->second.first, top);
            found->second.second = std::max(found->second.second, bottom);
        }
    }
    TTF_Font* font = UI.font(Face::Sans, 12);
    const int height = TTF_FontHeight(font);
    for (const auto& [species, band] : bands) {
        if (band.second - band.first < height) continue;
        std::string label(species_name(static_cast<Species>(species)));
        for (char& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        label += "S";
        const Rect text = Canvas::text_box(font, label);
        Rect box{0, 0, text.w + height + UI(14), text.h + UI(4)};
        box.midright_at(chart.right() - UI(6), static_cast<int>(py_round((band.first + band.second) / 2)));
        canvas.fill_rect(box, {19, 26, 27, 190});
        draw_icon(canvas, static_cast<Species>(species), box.x + UI(4) + height / 2, box.centery(), height * 0.42, {238, 238, 220});
        canvas.text(font, label, {238, 238, 220}, box.x + UI(8) + height, box.y + UI(2));
    }
}

void ColourGraph::draw_timeline(Canvas& canvas, const Rect& chart, const Rect& timeline, int first, int last) {
    TTF_Font* small = UI.font(Face::Sans, 12);
    canvas.rect(timeline, {22, 30, 30}, 0, UI(4));
    draw_seasons(canvas, chart, timeline, first, last);
    canvas.rect(timeline, {54, 68, 62}, 1, UI(4));
    const std::string middle = !view_ ? format("TURN %d", (first + last) / 2)
                                      : format("TURNS %d - %d  (ZOOM x%.1f)", first, last,
                                               static_cast<double>(frontier_ + 1) / (last - first + 1));
    const std::array<std::tuple<std::string, int, double>, 3> labels = {
        std::tuple{format("TURN %d", first), timeline.x + UI(8), 0.0}, std::tuple{middle, timeline.centerx(), 0.5},
        std::tuple{format("TURN %d", last), timeline.right() - UI(8), 1.0}};
    for (const auto& [text, x, align] : labels) {
        canvas.text_at(small, text, {149, 170, 150}, [&](Rect& box) {
            box.x = x - static_cast<int>(py_round(box.w * align));
            box.y = timeline.centery() - box.h / 2;
        });
    }
    const std::array<std::pair<std::optional<int>, Color>, 2> markers = {std::pair{std::optional<int>(now_), Color{245, 238, 206}},
                                                                        std::pair{scrub_, Color{238, 183, 110}}};
    for (const auto& [turn, colour] : markers) {
        if (!turn || !(first <= *turn && *turn <= last)) continue;
        const int x = x_of(*turn);
        canvas.line(x, chart.y, x, timeline.bottom() - 1, colour, std::max(1, UI(2)));
        canvas.rect(Rect{0, 0, UI(10), timeline.h}.midtop_at(x, timeline.y), colour, 0, UI(3));
        const std::string label = format("TURN %d", *turn);
        Rect tag = Canvas::text_box(small, label).midbottom_at(x, chart.y - UI(2));
        tag = tag.clamp(chart.inflate(0, UI(40)));
        canvas.text(small, label, colour, tag.x, tag.y);
    }
}

void ColourGraph::draw_seasons(Canvas& canvas, const Rect& chart, const Rect& timeline, int first, int last) {
    // Each year begins with a line up through the chart, labelled while the labels have room.
    const int length = YEAR_LENGTH;
    const double span = static_cast<double>(chart.w) / (last - first + 1);  // pixels a turn
    const auto left_of = [&](int turn) { return static_cast<int>(py_round(chart.x + (turn - first) * span)); };
    const auto ceiling = [](long long top, long long bottom) { return static_cast<int>((top + bottom - 1) / bottom); };
    for (int quarter = first * 4 / length; static_cast<long long>(quarter) * length / 4 <= last; ++quarter) {
        if (quarter % 4 != 1 && quarter % 4 != 3) continue;
        const int start = std::max(first, ceiling(static_cast<long long>(quarter) * length, 4));
        const int end = std::min(last + 1, ceiling(static_cast<long long>(quarter + 1) * length, 4));
        if (end > start) canvas.fill_rect({left_of(start), timeline.y + 1, left_of(end) - left_of(start), timeline.h - 2}, {30, 42, 56});
    }
    const int year_pixels = std::max(1, static_cast<int>(py_round(length * span)));
    const int every = std::max(1, ceiling(UI(YEAR_LABEL_SPACING), year_pixels));
    TTF_Font* small = UI.font(Face::Sans, 12);
    for (int turn = ceiling(std::max(1, first), length) * length; turn <= last; turn += length) {
        const int x = left_of(turn);
        canvas.line(x, chart.y, x, timeline.bottom() - 1, {88, 104, 112});
        if ((turn / length) % every == 0) canvas.text(small, format("YEAR %d", turn / length + 1), {140, 185, 230}, x + UI(3), chart.y + UI(2));
    }
}

std::string ColourGraph::describe(const Simulation& simulation, std::optional<int> hovered) const {
    std::set<int> living;
    for (const Bitzkey* agent : simulation.living()) living.insert(agent->identifier);
    std::string chosen = selection_.empty() ? "Nothing selected" : format("%zu selected", selection_.size());
    if (sort_ != 0) chosen = "The living sorted by " + sort_name(sort_) + ", lowest at the bottom.     " + chosen;
    if (simulation.turn() < simulation.frontier()) {
        chosen = format("Rewound to turn %d of %d.     ", simulation.turn(), simulation.frontier()) + chosen;
    } else if (const EndReason reason = simulation.end_reason(); reason != EndReason::None) {
        chosen = format("Ended at turn %d: %s.     ", simulation.turn(),
                        reason == EndReason::Extinct ? "every animal died" : "the world filled up") + chosen;
    }
    if (!hovered) return chosen;
    const Dna dna = simulation.lineage(*hovered);
    if (!living.contains(*hovered)) {  // remembered by its colour alone
        return chosen + format("     |     dead   ring %08X   dot %08X", body_word(dna), mind_word(dna));
    }
    return chosen + format("     |     #%03d  %s  alive   ring %08X   dot %08X", *hovered,
                           std::string(SPECIES[static_cast<std::size_t>(decode_body(dna)[SpeciesTrait])]).c_str(),
                           body_word(dna), mind_word(dna));
}

}  // namespace bitzkey::ui
