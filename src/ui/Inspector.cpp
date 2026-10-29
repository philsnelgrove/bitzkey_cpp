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

std::array<Rect, 3> last_swatches{};


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
    // At twice the size it has on the map, beside its name and its colours
    draw_bitzkey(canvas, agent.dna, x + UI(16), at(16), 2 * dot_radius(agent.body[Size], UI(DOT_RADIUS_LARGEST)), 2 * RING_WIDTH,
                 agent.alive() || !known);
    draw_icon(canvas, agent.species, x + width - UI(12), at(11), UI(9), {220, 229, 211});
    canvas.text(font, format("#%03d  %s  %s  %s", agent.identifier, upper(species_name(agent.species)).c_str(), upper(terrain_name(agent.terrain)).c_str(),
                             agent.nocturnal ? "NOCTURNAL" : "DIURNAL"),
                {238, 238, 220}, x + UI(40), at(0));
    {  // its three colours as swatches, the hex beside each
        int across = x + UI(40), part = 0;  // its three colours as chits: the mouse over one shows that word, gene by gene
        for (const auto& [name, word] : {std::pair{"MIND", mind_word(agent.dna)}, std::pair{"BODY", body_word(agent.dna)},
                                         std::pair{"SOCIAL", social_word(agent.dna)}}) {
            const int swatch = UI(10), from = across;
            canvas.fill_rect({across, at(27), swatch, swatch}, to_color(color_from_word(word)));
            canvas.rect({across, at(27), swatch, swatch}, OUTLINE, 1);
            across += swatch + UI(3);
            const std::string label = name;
            canvas.text(mono_small, label, {211, 219, 201}, across, at(26));
            across += Canvas::text_width(mono_small, label) + UI(12);
            last_swatches[static_cast<std::size_t>(part++)] = Rect{from, at(24), across - from, UI(16)};  // the chit and its hex
        }
    }

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
            // A bar under the row: filled to now, a tick at birth, so the change shows at a glance
            const int bar_left = left, bar_width = numbers + digits + Canvas::text_width(mono_small, " 0.0+") - left;
            const int bar_y = at(62 + index * 14) + UI(12);
            canvas.fill_rect({bar_left, bar_y, bar_width, std::max(1, UI(2))}, {58, 70, 62});
            const double shown = known ? now : born;
            canvas.fill_rect({bar_left, bar_y, static_cast<int>(bar_width * std::clamp(shown / TRAIT_MAX, 0.0, 1.0)), std::max(1, UI(2))}, colour);
            const int tick = bar_left + static_cast<int>(bar_width * std::clamp(static_cast<double>(born) / TRAIT_MAX, 0.0, 1.0));
            canvas.fill_rect({tick - 1, bar_y - UI(1), 2, std::max(2, UI(4))}, {230, 236, 226});
            canvas.text(mono_small, TEMPERAMENT[trait], colour, left, at(62 + index * 14));
            canvas.text(mono_small, format("%2d", born), colour, numbers, at(62 + index * 14));
            const char* change = !known || std::abs(now - born) < 0.25 ? " " : now > born ? "+" : "-";  // not by colour alone
            if (known) canvas.text(mono_small, format("%5.1f%s", now, change), colour, numbers + digits, at(62 + index * 14));
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
        if (!last) {  // a bar under it on the traits' shared scale
            const int bar_width = width / 3 - UI(10), bar_y = at(222 + row * 14) + UI(12);
            canvas.fill_rect({cell, bar_y, bar_width, std::max(1, UI(2))}, {58, 70, 62});
            canvas.fill_rect({cell, bar_y, bar_width * agent.body[index + 1] / TRAIT_MAX, std::max(1, UI(2))}, {175, 192, 172});
        }
    }
    draw_climate(canvas, agent, x, at(280), width, known ? here : std::nullopt);

    if (!known) {
        canvas.text(small, format("LIFESPAN ~%.0f", agent.lifespan), {145, 164, 146}, x, at(302));
        canvas.text(small, !note.empty() ? note : std::string("Not in the world at this turn: only its genome is known."), {116, 139, 124}, x,
                    at(318));
        return;
    }
    const int third = (width - UI(24)) / 3;  // hunger, stamina and health side by side
    canvas.text(mono_small, format("HUNGER %.0f%%", std::min(100.0, agent.hunger)), {224, 177, 118}, x, at(302));
    bar(canvas, x, at(320), third, agent.hunger / 100, {224, 150, 94});
    const int stamina = x + third + UI(12);
    canvas.text(mono_small, format("STAMINA %.0f", agent.stamina), {150, 190, 230}, stamina, at(302));
    bar(canvas, stamina, at(320), third, agent.stamina / FULL_STAMINA, {110, 160, 220});
    const int health = x + width - third;
    canvas.text(mono_small, format("HEALTH %.0f/%.0f", std::max(0.0, agent.health), agent.max_health), {161, 207, 162}, health,
                at(302));
    bar(canvas, health, at(320), third, agent.health / agent.max_health, {124, 190, 131});
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

const std::array<Rect, 3>& genome_swatches() { return last_swatches; }

namespace {

struct Gene {
    std::string name;
    int low, bits;     // where in the word: its lowest bit, and how many
    std::string meaning;  // what its value means, if more than the number
};

// A word's genes, from its highest bits to its lowest, as they read left to right in binary.
std::vector<Gene> genes_of(int part, std::uint32_t word) {
    const auto value = [word](int low, int bits) { return static_cast<int>((word >> low) & ((1U << bits) - 1)); };
    std::vector<Gene> genes;
    if (part == 0) {  // mind: the cold and heat tolerance flags, then the ten mind traits
        genes.push_back({"cold tolerance", 31, 1, value(31, 1) ? "yes" : "no"});
        genes.push_back({"heat tolerance", 30, 1, value(30, 1) ? "yes" : "no"});
        for (int trait = DOT_TRAIT_COUNT - 1; trait >= 0; --trait) genes.push_back({std::string(TEMPERAMENT[trait]), TRAIT_BITS * trait, TRAIT_BITS, ""});
    } else if (part == 1) {  // body: the home temperature, the nine body traits, then nocturnal and the species
        genes.push_back({"home temperature", 30, 2, std::string(TEMPERATURES[static_cast<std::size_t>(value(30, 2))])});
        for (int trait = BODY_TRAIT_COUNT - 1; trait >= 1; --trait) genes.push_back({std::string(BODY_TRAITS[trait]), TRAIT_BITS * trait, TRAIT_BITS, ""});
        genes.push_back({"nocturnal", 2, 1, value(2, 1) ? "yes" : "no"});
        genes.push_back({"species", 0, 2, std::string(SPECIES[static_cast<std::size_t>(value(0, 2))])});
    } else {  // social: spare bits, the terrain, neurodivergence, then the seven social traits
        genes.push_back({"(unused)", TERRAIN_SHIFT + 2, 32 - TERRAIN_SHIFT - 2, ""});
        genes.push_back({"terrain", TERRAIN_SHIFT, 2, std::string(TERRAINS[static_cast<std::size_t>(value(TERRAIN_SHIFT, 2))])});
        genes.push_back({"neurodivergence", NEURODIVERGENCE_SHIFT, 4, ""});
        for (int trait = SOCIAL_TRAIT_COUNT - 1; trait >= 0; --trait) {
            genes.push_back({std::string(TEMPERAMENT[DOT_TRAIT_COUNT + trait]), TRAIT_BITS * trait, TRAIT_BITS, ""});
        }
    }
    return genes;
}

std::string binary(std::uint32_t word, int low, int bits) {
    std::string digits;
    for (int bit = low + bits - 1; bit >= low; --bit) digits += (word >> bit) & 1U ? '1' : '0';
    return digits;
}

}  // namespace

void draw_word_popup(Canvas& canvas, const Bitzkey& agent, int part, int mouse_x, int mouse_y) {
    const std::uint32_t word = part == 0 ? mind_word(agent.dna) : part == 1 ? body_word(agent.dna) : social_word(agent.dna);
    const std::vector<Gene> genes = genes_of(part, word);
    TTF_Font* mono = UI.font(Face::Mono, 12);
    TTF_Font* bold = UI.font(Face::Sans, 13, true);
    std::string all;  // the whole word in binary, a space between genes
    for (const Gene& gene : genes) all += (all.empty() ? "" : " ") + binary(word, gene.low, gene.bits);
    int name_width = 0;
    for (const Gene& gene : genes) name_width = std::max(name_width, Canvas::text_width(mono, tr(gene.name)));
    const int line = UI(15), padding = UI(10);
    const int width = std::max(Canvas::text_width(mono, all), name_width + Canvas::text_width(mono, "  bits 00-00  00000  15  very cold")) + 2 * padding;
    const int height = padding * 2 + UI(20) + line * (3 + static_cast<int>(genes.size()));
    Rect box{mouse_x - UI(18) - width, mouse_y + UI(12), width, height};  // to the left: the sidebar is at the right
    if (box.x < 0) box.x = mouse_x + UI(18);
    box = box.clamp({0, 0, canvas.width(), canvas.height()});
    canvas.rect(box, {19, 26, 27}, 0, UI(6));
    canvas.rect(box, {131, 205, 143}, 1, UI(6));
    int x = box.x + padding, y = box.y + padding;
    canvas.fill_rect({x, y + UI(2), UI(12), UI(12)}, to_color(color_from_word(word)));
    canvas.rect({x, y + UI(2), UI(12), UI(12)}, OUTLINE, 1);
    canvas.text(bold, std::array{"MIND", "BODY", "SOCIAL"}[static_cast<std::size_t>(part)], {229, 239, 220}, x + UI(18), y);
    y += UI(20);
    canvas.text(mono, format("hex     %08X", word), {220, 229, 211}, x, y);
    y += line;
    canvas.text(mono, "binary", {149, 170, 150}, x, y);
    y += line;
    canvas.text(mono, all, {220, 229, 211}, x, y);
    y += line;
    for (const Gene& gene : genes) {  // each gene: its name, where it lies, its bits, and its value
        const int value = static_cast<int>((word >> gene.low) & ((1U << gene.bits) - 1));
        const std::string where = gene.bits == 1 ? format("bit  %d", gene.low) : format("bits %d-%d", gene.low, gene.low + gene.bits - 1);
        canvas.text(mono, tr(gene.name), {175, 192, 172}, x, y);
        canvas.text(mono, format("%-10s %6s %3d  %s", where.c_str(), binary(word, gene.low, gene.bits).c_str(), value, tr(gene.meaning).c_str()),
                    {211, 219, 201}, x + name_width + UI(10), y);
        y += line;
    }
}

}  // namespace bitzkey::ui
