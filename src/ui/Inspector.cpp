#include "ui/Inspector.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "core/PyFormat.hpp"
#include "ui/Icons.hpp"
#include "ui/Language.hpp"
#include "ui/Look.hpp"

namespace bitzkey::ui {

namespace {

std::string format(const char* pattern, auto... values) {
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, pattern, values...);
    return buffer;
}

std::string upper(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

constexpr std::array<std::string_view, 4> SHORT_TEMPERATURES = {"hot", "medium", "cold", "v. cold"};

// What its tolerance flags spare it, in a word or two.
std::string hardiness(bool heat, bool cold) {
    return heat ? "HEAT-HARDY" : cold ? "COLD-HARDY" : "";
}

// Its home temperature, the temperature where it is (coloured by how far that is from home, as it
// feels it), and what it tolerates; temperatures are shortened if the line is too narrow.
void draw_climate(Canvas& canvas, const Bitzkey& agent, int x, int y, int width, std::optional<double> here) {
    TTF_Font* small = UI.font(Face::Sans, 12);
    const std::string hardy = hardiness(agent.climate.heat, agent.climate.cold);
    const int hardy_width = hardy.empty() ? 0 : Canvas::text_width(small, hardy);
    const int home = agent.climate.temperature;
    std::vector<std::pair<std::string, Color>> parts;
    for (const auto* names : {&TEMPERATURES, &SHORT_TEMPERATURES}) {
        parts.clear();
        parts.emplace_back("HOME " + upper((*names)[static_cast<std::size_t>(home)]), TEMPERATURE_COLOURS[static_cast<std::size_t>(home)]);
        if (here) {
            const double steps = agent.climate_steps(*here);  // part steps while the weather turns: shown as the nearer
            const Color colour = steps < 0.5 ? Color{175, 192, 172} : steps < 1.5 ? Color{238, 183, 110} : Color{235, 100, 90};
            parts.emplace_back("HERE " + upper((*names)[static_cast<std::size_t>(*here + 0.5)]), colour);
        }
        int total = hardy_width;
        for (const auto& [text, colour] : parts) total += Canvas::text_width(small, text) + UI(14);
        if (total <= width) break;
    }
    int left = x;
    for (const auto& [text, colour] : parts) left += canvas.text(small, text, colour, left, y) + UI(14);
    if (!hardy.empty()) canvas.text(small, hardy, {149, 170, 150}, x + width - hardy_width, y);
}

void bar(Canvas& canvas, int x, int y, int width, double value, Color color) {
    canvas.fill_rect({x, y, width, UI(7)}, {44, 57, 52});
    canvas.fill_rect({x, y, static_cast<int>(py_round(width * std::max(0.0, std::min(1.0, value)))), UI(7)}, color);
}

}  // namespace

void draw_details(Canvas& canvas, const Bitzkey& agent, int x, const std::function<int(int)>& at, int width, bool known,
                  std::optional<double> here, const std::string& note) {
    TTF_Font* font = UI.font(Face::Sans, 15);
    TTF_Font* small = UI.font(Face::Sans, 12);
    TTF_Font* mono_small = UI.font(Face::Mono, 12);
    draw_bitzkey(canvas, agent.dna, x + UI(10), at(11), dot_radius(agent.body[Size], UI(DOT_RADIUS_LARGEST)), RING_WIDTH,
                 agent.alive() || !known);
    draw_icon(canvas, agent.species, x + width - UI(12), at(11), UI(9), {220, 229, 211});
    canvas.text(font, format("#%03d  %s", agent.identifier, upper(species_name(agent.species)).c_str()), {238, 238, 220},
                x + UI(28), at(0));
    canvas.text(mono_small, format("DOT %08X RING %08X SOCIAL %08X", mind_word(agent.dna), body_word(agent.dna), social_word(agent.dna)),
                {211, 219, 201}, x, at(26));

    // Temperament: the mind's traits on the left, the social background's on the right. Names are
    // in the language shown, and the numbers stand in columns after the longest of them.
    const auto widest = [&](int first, int count) {
        int widest_name = Canvas::text_width(mono_small, "neurodiv.");
        for (int trait = first; trait < first + count; ++trait) widest_name = std::max(widest_name, Canvas::text_width(mono_small, TEMPERAMENT[trait]));
        return widest_name + UI(6);
    };
    const int digits = Canvas::text_width(mono_small, "00");
    const int column_width = widest(0, DOT_TRAIT_COUNT) + digits + Canvas::text_width(mono_small, " 0.0") + UI(10);
    const int right = x + std::max(std::min(UI(168), width / 2 + UI(18)), column_width);
    for (const auto& [left, heading, first, count] : {std::tuple{x, "MIND", 0, DOT_TRAIT_COUNT},
                                                     std::tuple{right, "SOCIAL", DOT_TRAIT_COUNT, SOCIAL_TRAIT_COUNT}}) {
        const int numbers = left + widest(first, count);
        canvas.text(mono_small, heading, {149, 170, 150}, left, at(46));
        const int born_at = numbers + digits - Canvas::text_width(mono_small, "born");
        canvas.text(mono_small, "born", {149, 170, 150}, std::max(born_at, left + Canvas::text_width(mono_small, heading) + UI(6)), at(46));
        if (known) canvas.text(mono_small, "now", {149, 170, 150}, numbers + digits + UI(8), at(46));
        for (int index = 0; index < count; ++index) {
            const int trait = first + index;
            const int born = agent.inborn[trait];
            const double now = agent.mind[trait];
            const Color colour = !known || std::abs(now - born) < 0.25 ? Color{175, 192, 172}
                                 : now > born                       ? Color{131, 205, 143}
                                                                    : Color{238, 183, 110};
            canvas.text(mono_small, TEMPERAMENT[trait], colour, left, at(62 + index * 14));
            canvas.text(mono_small, format("%2d", born), colour, numbers, at(62 + index * 14));
            if (known) canvas.text(mono_small, format("%5.1f", now), colour, numbers + digits, at(62 + index * 14));
        }
        if (first == DOT_TRAIT_COUNT) {
            canvas.text(mono_small, "neurodiv.", {175, 192, 172}, left, at(62 + SOCIAL_TRAIT_COUNT * 14));
            canvas.text(mono_small, format("%2d", decode_neurodivergence(agent.dna)), {175, 192, 172}, numbers,
                        at(62 + SOCIAL_TRAIT_COUNT * 14));
        }
    }

    // The body, three to a row: each name, and its number at the cell's right.
    canvas.text(mono_small, "BODY", {149, 170, 150}, x, at(206));
    for (int index = 0; index < BODY_TRAIT_COUNT; ++index) {
        const int row = index / 3, column = index % 3;
        const bool last = index == BODY_TRAIT_COUNT - 1;
        const std::string name = last ? std::string("burn") : std::string(BODY_TRAITS[index + 1]);
        const std::string number = last ? format("%5.2f", agent.burn) : format("%2d", agent.body[index + 1]);
        const int cell = x + column * width / 3;
        const int number_at = cell + width / 3 - UI(10) - Canvas::text_width(mono_small, number);
        canvas.text(mono_small, name, {175, 192, 172}, cell, at(222 + row * 14));
        canvas.text(mono_small, number, {175, 192, 172}, number_at, at(222 + row * 14));
    }
    draw_climate(canvas, agent, x, at(280), width, known ? here : std::nullopt);

    if (!known) {
        canvas.text(small, format("LIFESPAN ~%.0f", agent.lifespan), {145, 164, 146}, x, at(302));
        canvas.text(small, !note.empty() ? note : std::string("Not in the world at this turn: only its genome is known."), {116, 139, 124}, x,
                    at(318));
        return;
    }
    const int half = (width - UI(12)) / 2;
    canvas.text(mono_small, format("HUNGER %5.1f%%", std::min(100.0, agent.hunger)), {224, 177, 118}, x, at(302));
    bar(canvas, x, at(320), half, agent.hunger / 100, {224, 150, 94});
    const int health = x + width - half;
    canvas.text(mono_small, format("HEALTH %5.1f/%.0f", std::max(0.0, agent.health), agent.max_health), {161, 207, 162}, health,
                at(302));
    bar(canvas, health, at(320), half, agent.health / agent.max_health, {124, 190, 131});
    canvas.text(small, format("AGE %d / ~%.0f%s    GENERATION %d    CHILDREN %d", agent.age, agent.lifespan,
                              agent.feeble ? " FEEBLE" : "", agent.generation, agent.children),
                {145, 164, 146}, x, at(334));
    std::string state = "LAST ACTION: " + upper(agent.last_action);
    if (!agent.alive()) state += format("     MEAT LEFT %.0f", agent.meat);
    canvas.text(small, state, {145, 164, 146}, x, at(350));
    if (agent.odds_offered) {
        std::vector<std::pair<int, double>> odds;
        for (int action = 0; action < ACTION_COUNT; ++action) {
            if (agent.odds_offered & (1U << action)) odds.emplace_back(action, agent.odds[static_cast<std::size_t>(action)]);
        }
        std::stable_sort(odds.begin(), odds.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::string line = tr("CHANCES") + "  ";  // each action in the language shown
        for (std::size_t index = 0; index < std::min<std::size_t>(3, odds.size()); ++index) {
            if (index) line += "   ";
            line += tr(ACTIONS[static_cast<std::size_t>(odds[index].first)]) + " " + py_percent(odds[index].second, 0);
        }
        canvas.text(small, as_shown(line), {175, 192, 172}, x, at(366));
    }
}

Rect draw_tooltip(Canvas& canvas, const Bitzkey& agent, bool known, int mouse_x, int mouse_y, std::optional<double> here,
                  const std::string& note) {
    const int width = UI(300), padding = UI(12);
    Rect box{mouse_x + UI(18), mouse_y + UI(12), width + 2 * padding, UI(known ? DETAILS_HEIGHT : GENOME_HEIGHT) + 2 * padding};
    if (box.right() > canvas.width()) box.x = mouse_x - UI(18) - box.w;
    box = box.clamp({0, 0, canvas.width(), canvas.height()});
    canvas.rect(box, {19, 26, 27}, 0, UI(6));
    canvas.rect(box, {131, 205, 143}, 1, UI(6));
    const int top = box.y + padding;
    draw_details(canvas, agent, box.x + padding, [top](int offset) { return top + UI(offset); }, width, known, here, note);
    return box;
}

}  // namespace bitzkey::ui
