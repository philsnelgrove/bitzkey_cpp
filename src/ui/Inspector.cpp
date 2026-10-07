#include "ui/Inspector.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "core/PyFormat.hpp"
#include "ui/Icons.hpp"
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
    return heat && cold ? "HEAT & COLD-HARDY" : heat ? "HEAT-HARDY" : cold ? "COLD-HARDY" : "";
}

// Its home temperature, the temperature where it is (coloured by how far that is from home, as it
// feels it), and what it tolerates; temperatures are shortened if the line is too narrow.
void draw_climate(Canvas& canvas, const Bitzkey& agent, int x, int y, int width, std::optional<int> here) {
    TTF_Font* small = UI.font(Face::Sans, 12);
    const std::string hardy = hardiness(agent.climate.heat, agent.climate.cold);
    const int hardy_width = hardy.empty() ? 0 : Canvas::text_width(small, hardy);
    const int home = agent.climate.temperature;
    std::vector<std::pair<std::string, Color>> parts;
    for (const auto* names : {&TEMPERATURES, &SHORT_TEMPERATURES}) {
        parts.clear();
        parts.emplace_back("HOME " + upper((*names)[static_cast<std::size_t>(home)]), TEMPERATURE_COLOURS[static_cast<std::size_t>(home)]);
        if (here) {
            const int steps = agent.climate_steps(*here);
            const Color colour = !steps ? Color{175, 192, 172} : steps == 1 ? Color{238, 183, 110} : Color{235, 100, 90};
            parts.emplace_back("HERE " + upper((*names)[static_cast<std::size_t>(*here)]), colour);
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
                  std::optional<int> here) {
    TTF_Font* font = UI.font(Face::Sans, 15);
    TTF_Font* small = UI.font(Face::Sans, 12);
    TTF_Font* mono = UI.font(Face::Mono, 14);
    TTF_Font* mono_small = UI.font(Face::Mono, 12);
    draw_bitzkey(canvas, agent.dna, x + UI(10), at(11), dot_radius(agent.body[Size], UI(DOT_RADIUS_LARGEST)), RING_WIDTH,
                 agent.alive() || !known);
    draw_icon(canvas, agent.species, x + width - UI(12), at(11), UI(9), {220, 229, 211});
    canvas.text(font, format("#%03d  %s", agent.identifier, upper(species_name(agent.species)).c_str()), {238, 238, 220},
                x + UI(28), at(0));
    canvas.text(mono, format("DOT / MIND   %08X", mind_word(agent.dna)), {211, 219, 201}, x, at(28));
    canvas.text(mono, format("RING / BODY  %08X", body_word(agent.dna)), {211, 219, 201}, x, at(47));

    const int body_x = x + std::min(UI(168), width / 2 + UI(18));
    canvas.text(small, known ? "MIND      born   now" : "MIND      born", {149, 170, 150}, x, at(74));
    canvas.text(small, "BODY", {149, 170, 150}, body_x, at(74));
    for (int trait = 0; trait < MIND_TRAIT_COUNT; ++trait) {
        const int born = agent.inborn[trait];
        const double now = agent.mind[trait];
        const std::string name(MIND_TRAITS[trait]);
        if (!known) {
            canvas.text(mono_small, format("%-11s%3d", name.c_str(), born), {175, 192, 172}, x, at(92 + trait * 16));
            continue;
        }
        const Color colour = std::abs(now - born) < 0.25 ? Color{175, 192, 172}
                             : now > born               ? Color{131, 205, 143}
                                                        : Color{238, 183, 110};
        canvas.text(mono_small, format("%-11s%3d%6.1f", name.c_str(), born, now), colour, x, at(92 + trait * 16));
    }
    for (int trait = 1; trait < BODY_TRAIT_COUNT; ++trait) {
        canvas.text(mono_small, format("%-9s%3d", std::string(BODY_TRAITS[trait]).c_str(), agent.body[trait]), {175, 192, 172},
                    body_x, at(92 + (trait - 1) * 16));
    }
    canvas.text(mono_small, format("%-7s%5.2f", "burn", agent.burn), {175, 192, 172}, body_x, at(92 + 9 * 16));
    draw_climate(canvas, agent, x, at(254), width, known ? here : std::nullopt);

    if (!known) {
        canvas.text(small, format("LIFESPAN ~%.0f", agent.lifespan), {145, 164, 146}, x, at(276));
        canvas.text(small, "Not in the world at this turn: only its genome is known.", {116, 139, 124}, x, at(292));
        return;
    }
    canvas.text(mono, format("HUNGER  %5.1f%%", std::min(100.0, agent.hunger)), {224, 177, 118}, x, at(276));
    bar(canvas, x, at(295), width, agent.hunger / 100, {224, 150, 94});
    canvas.text(mono, format("HEALTH  %5.1f / %.0f", std::max(0.0, agent.health), agent.max_health), {161, 207, 162}, x, at(305));
    bar(canvas, x, at(324), width, agent.health / agent.max_health, {124, 190, 131});
    canvas.text(small, format("AGE %d / ~%.0f    GENERATION %d    CHILDREN %d", agent.age, agent.lifespan, agent.generation,
                              agent.children),
                {145, 164, 146}, x, at(337));
    std::string state = "LAST ACTION: " + upper(agent.last_action);
    if (!agent.alive()) state += format("     MEAT LEFT %.0f", agent.meat);
    canvas.text(small, state, {145, 164, 146}, x, at(353));
    if (agent.odds_offered) {
        std::vector<std::pair<int, double>> odds;
        for (int action = 0; action < ACTION_COUNT; ++action) {
            if (agent.odds_offered & (1U << action)) odds.emplace_back(action, agent.odds[static_cast<std::size_t>(action)]);
        }
        std::stable_sort(odds.begin(), odds.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::string line = "CHANCES  ";
        for (std::size_t index = 0; index < std::min<std::size_t>(3, odds.size()); ++index) {
            if (index) line += "   ";
            line += std::string(ACTIONS[static_cast<std::size_t>(odds[index].first)]) + " " + py_percent(odds[index].second, 0);
        }
        canvas.text(small, line, {175, 192, 172}, x, at(369));
    }
}

Rect draw_tooltip(Canvas& canvas, const Bitzkey& agent, bool known, int mouse_x, int mouse_y, std::optional<int> here) {
    const int width = UI(300), padding = UI(12);
    Rect box{mouse_x + UI(18), mouse_y + UI(12), width + 2 * padding, UI(known ? DETAILS_HEIGHT : GENOME_HEIGHT) + 2 * padding};
    if (box.right() > canvas.width()) box.x = mouse_x - UI(18) - box.w;
    box = box.clamp({0, 0, canvas.width(), canvas.height()});
    canvas.rect(box, {19, 26, 27}, 0, UI(6));
    canvas.rect(box, {131, 205, 143}, 1, UI(6));
    const int top = box.y + padding;
    draw_details(canvas, agent, box.x + padding, [top](int offset) { return top + UI(offset); }, width, known, here);
    return box;
}

}  // namespace bitzkey::ui
