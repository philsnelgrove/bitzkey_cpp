#include "ui/Report.hpp"

#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"
#include "ui/Language.hpp"
#include "ui/Ui.hpp"

namespace bitzkey::ui {

namespace {
constexpr int LINE = 18;  // design pixels to a line of the report
constexpr int GAP = 16;   // design pixels between a table's columns

std::string format(const char* pattern, auto... values) {
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, pattern, values...);
    return buffer;
}

std::vector<std::string> cells_of(const std::string& line) {
    std::vector<std::string> cells;
    for (std::size_t start = 0;;) {
        const std::size_t end = line.find('\t', start);
        cells.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) return cells;
        start = end + 1;
    }
}

// Each column's width: the widest of its cells, among the rows with as many cells (a report's tables
// line up together; the deadliest killers' rows have columns of their own).
std::map<std::size_t, std::vector<int>> column_widths(const std::vector<std::string>& lines) {
    std::map<std::size_t, std::vector<int>> widths;
    TTF_Font* font = UI.font(Face::Mono, 13);
    for (const std::string& line : lines) {
        if (line.find('\t') == std::string::npos) continue;
        const std::vector<std::string> cells = cells_of(line);
        std::vector<int>& these = widths[cells.size()];
        these.resize(cells.size());
        for (std::size_t index = 0; index < cells.size(); ++index) {
            these[index] = std::max(these[index], Canvas::text_width(font, cells[index]));
        }
    }
    return widths;
}

std::string with_tabs(const std::vector<std::string>& cells) {
    std::string line;
    for (std::size_t index = 0; index < cells.size(); ++index) line += (index ? "\t" : "") + as_shown(cells[index]);
    return line;
}

std::string upper_of(std::string_view english) {
    std::string text(english);
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}
}  // namespace

std::vector<std::string> death_report_table(const DeathTally& tally) {
    const DeathReport report = death_report(tally);
    std::vector<std::string> lines;
    const auto table = [&](const std::string& title, const std::vector<DeathReport::Row>& rows, bool heading) {
        if (heading) {
            std::vector<std::string> cells = {title.empty() ? std::string() : tr(title)};
            for (const auto species : SPECIES) cells.push_back(tr(upper_of(species)));
            cells.push_back(tr("ALL"));
            lines.push_back(with_tabs(cells));
        }
        for (const DeathReport::Row& row : rows) {
            std::vector<std::string> cells = {tr(row.label)};
            for (const int count : row.counts) cells.push_back(std::to_string(count));
            cells.push_back(std::to_string(row.total));
            lines.push_back(with_tabs(cells));
        }
    };
    table("CAUSE", report.causes, true);
    table("", {report.all}, false);
    if (!report.ages.empty()) {
        lines.insert(lines.end(), {"", tr("AGE AT DEATH, by species")});
        table("AGE", report.ages, true);
    }
    if (report.kills == 0) {
        lines.insert(lines.end(), {"", tr("Nothing has killed anything yet.")});
        return lines;
    }
    lines.insert(lines.end(), {"", tr(format("WHO KILLED WHOM: %d killed, by the species of the dead (rows) and of their killers (columns)",
                                             report.kills))});
    table("THE DEAD", report.whom, true);
    lines.insert(lines.end(), {"", tr("WHAT THE KILLERS WERE DOING, by the killers' species")});
    table("ACTIVITY", report.activities, true);
    lines.insert(lines.end(), {"", tr(format("THE DEADLIEST KILLERS, %zu of %d", report.deadliest.size(), report.killers))});
    for (const DeathReport::Killer& killer : report.deadliest) {
        std::string what;
        for (const auto& [name, count] : killer.doing) what += (what.empty() ? "" : ", ") + tr(name) + " " + std::to_string(count);
        lines.push_back(with_tabs({format("#%03d", killer.identifier), tr(species_name(killer.species)), std::to_string(killer.kills),
                                   tr("killed"), what}));
    }
    return lines;
}

std::vector<std::string> brain_matrix_table(const Bitzkey& animal) {
    const std::vector<BrainRow> rows = brain_matrix(animal);
    std::vector<int> used;
    for (int action = 0; action < ACTION_COUNT; ++action) {
        for (const BrainRow& row : rows) {
            if (std::any_of(row.chances.begin(), row.chances.end(), [action](const auto& entry) { return entry.first == action; })) {
                used.push_back(action);
                break;
            }
        }
    }
    std::vector<std::string> heading = {tr("SITUATION")};
    for (const int action : used) heading.push_back(tr(short_action(action)));
    std::vector<std::string> lines = {with_tabs(heading)};
    for (const BrainRow& row : rows) {
        int best = row.chances.front().first;
        double most = row.chances.front().second;
        for (const auto& [action, chance] : row.chances) {  // the first of the likeliest
            if (chance > most) {
                best = action;
                most = chance;
            }
        }
        std::vector<std::string> cells = {tr(row.situation)};
        for (const int action : used) {
            const auto found = std::find_if(row.chances.begin(), row.chances.end(), [action](const auto& entry) { return entry.first == action; });
            cells.push_back(found == row.chances.end() ? "· " : py_percent(found->second, 0) + (action == best ? "*" : " "));
        }
        lines.push_back(with_tabs(cells));
    }
    return lines;
}

namespace {
constexpr int TAB_ROW = 36;  // design pixels the tabs take, when there are tabs

int widest_of(const std::vector<std::string>& lines) {  // the tables' columns and the gaps between them, or the widest line beside them
    int widest = 0;
    for (const auto& [count, columns] : column_widths(lines)) {
        int table = 0;
        for (const int column : columns) table += column + UI(GAP);
        widest = std::max(widest, table);
    }
    for (const std::string& line : lines) {
        if (line.find('\t') == std::string::npos) widest = std::max(widest, Canvas::text_width(UI.font(Face::Mono, 13), line));
    }
    return widest;
}
}  // namespace

void Report::choose(int tab) {
    if (tabs.empty()) return;
    active = std::clamp(tab, 0, static_cast<int>(tabs.size()) - 1);
    note = tabs[static_cast<std::size_t>(active)].note;
    lines = tabs[static_cast<std::size_t>(active)].lines;
    scroll = 0;
}

namespace {
std::string word_text(const LexiconWord& word) {  // its sounds, numbered 1 to 5
    std::string text = std::to_string(word.call - FirstCall + 1);
    for (const int sound : word.sounds) {
        if (sound < 0) break;
        text += "-" + std::to_string(sound + 1);
    }
    return text;
}

std::string thing_name(int thing) {  // an outside thing, as "wood pile"
    std::string name(SENSES[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(thing)])]);
    if (const std::size_t end = name.rfind("_closeness"); end != std::string::npos) name.resize(end);
    std::replace(name.begin(), name.end(), '_', ' ');
    return tr(name);
}

std::string node_name(int node) {  // a thing to come near, or a deed to do (see OPENS_RATE)
    return node < OUTSIDE_SEEN ? tr("near") + " " + thing_name(node) : tr("do") + " " + tr(short_action(node - OUTSIDE_SEEN));
}

constexpr std::array<std::string_view, GOAL_NEEDS> NEED_NAMES = {"hunger", "fatigue", "pain", "fear", "loneliness", "discomfort"};

std::string fixed(double value, int places) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.*f", places, value);
    return buffer;
}
}  // namespace

std::vector<std::string> lexicon_table(const Bitzkey& animal) {
    std::vector<std::string> lines;
    const auto table = [&](const std::string& heading, const std::string& meaning, const std::vector<LexiconWord>& words, auto&& means) {
        if (!lines.empty()) lines.push_back("");
        lines.push_back(with_tabs({heading, tr(meaning), tr("REGARD"), tr("USES")}));
        if (words.empty()) lines.push_back(tr("None yet."));
        std::vector<const LexiconWord*> sorted;
        for (const LexiconWord& word : words) sorted.push_back(&word);
        std::stable_sort(sorted.begin(), sorted.end(), [](const LexiconWord* a, const LexiconWord* b) { return a->uses > b->uses; });
        for (const LexiconWord* word : sorted) lines.push_back(with_tabs({word_text(*word), means(word->thing), fixed(word->regard, 2), std::to_string(word->uses)}));
    };
    table(tr("WORD"), "MEANS", animal.lexicon, [](int thing) { return thing >= 0 && thing < OUTSIDE_SEEN ? thing_name(thing) : std::string("?"); });
    table(tr("NAME"), "FOR", animal.names, [](int one) { return format("#%03d", one); });
    table(tr("DEED-WORD"), "DEED", animal.deeds, [](int deed) { return deed >= 0 && deed < ACTION_COUNT ? tr(short_action(deed)) : std::string("?"); });
    if (animal.own_call >= FirstCall) {
        LexiconWord own;
        own.call = animal.own_call;
        own.sounds = animal.own_sounds;
        lines.insert(lines.end(), {"", tr("Its own name") + ": " + word_text(own)});
    }
    return lines;
}

std::vector<std::string> goals_table(const Bitzkey& animal, int turn) {
    std::vector<std::string> lines = {with_tabs({tr("NEED"), tr("NOW")})};
    for (std::size_t need = 0; need < GOAL_NEEDS; ++need) lines.push_back(with_tabs({tr(NEED_NAMES[need]), py_percent(animal.needs[need], 0)}));
    lines.insert(lines.end(), {"", tr("GOAL")});
    if (animal.goal_kind == NoGoal) {
        lines.push_back(tr("No goal now."));
    } else {
        const std::string target = animal.goal_kind == GoalForThing ? tr("near") + " " + thing_name(animal.goal_target)
                                 : animal.goal_kind == GoalForDeed ? tr("do") + " " + tr(short_action(animal.goal_target))
                                                                   : tr("a companion") + format(" #%03d", animal.goal_target);
        const std::string why = animal.goal_desire ? tr("desired") : animal.goal_project ? tr("its project") : animal.goal_instructed ? tr("instructed")
                              : tr("for") + " " + tr(NEED_NAMES[static_cast<std::size_t>(std::clamp(animal.goal_need, 0, GOAL_NEEDS - 1))]);
        lines.push_back(tr("Seeking") + ": " + target + " (" + why + ")");
        lines.push_back(tr("Urgency") + ": " + fixed(animal.goal_urgency, 2) + "   " + tr("Held for") + ": " + std::to_string(turn - animal.goal_turn) + " " + tr("turns"));
        if (animal.stuck) lines.push_back(tr("Stuck: shut to it"));
        if (!animal.goal_steps.empty()) {
            std::string steps;
            for (auto it = animal.goal_steps.rbegin(); it != animal.goal_steps.rend(); ++it) steps += (steps.empty() ? "" : " > ") + node_name(*it);
            lines.push_back(tr("Steps toward it") + ": " + steps);
        }
    }
    lines.insert(lines.end(), {"", tr("PROJECT")});  // its long aim (see PROJECT_PULL)
    if (animal.project_aim < 0) {
        lines.push_back(tr("No project now."));
    } else {
        const std::string way = animal.project_dir > 0 ? tr("raise") : tr("lower");
        lines.push_back(tr("Aim") + ": " + way + " " + tr(AIM_NAMES[static_cast<std::size_t>(animal.project_aim)]) +
                        (animal.project_confided ? " (" + tr("confided by") + format(" #%03d)", animal.project_from) : std::string()));
        if (AIM_TARGET[static_cast<std::size_t>(animal.project_aim)] == TargetSite)
            lines.push_back(tr("Site") + format(": %d, %d", animal.project_x, animal.project_y));
        lines.push_back(tr("Held for") + ": " + std::to_string(turn - animal.project_turn) + " " + tr("turns") + "   " + tr("last progress") + ": " +
                        std::to_string(turn - animal.project_progress_turn) + " " + tr("turns ago"));
    }
    if (animal.goal_kind != NoGoal && animal.goal_instructed) lines.push_back(tr("Following an instruction from") + format(" #%03d", animal.instructed_by));
    // what it has found eases its worst need most, and what it likes most
    const auto worst = static_cast<std::size_t>(std::max_element(animal.needs.begin(), animal.needs.end()) - animal.needs.begin());
    std::vector<std::pair<double, int>> eases, likes;
    for (int thing = 0; thing < OUTSIDE_SEEN; ++thing) {
        eases.emplace_back(animal.relief_things[worst][static_cast<std::size_t>(thing)], thing);
        likes.emplace_back(animal.want_things[static_cast<std::size_t>(thing)], thing);
    }
    for (int deed = 0; deed < ACTION_COUNT; ++deed) {
        eases.emplace_back(animal.relief_deeds[worst][static_cast<std::size_t>(deed)], OUTSIDE_SEEN + deed);
        likes.emplace_back(animal.want_deeds[static_cast<std::size_t>(deed)], OUTSIDE_SEEN + deed);
    }
    const auto top = [&](const std::string& heading, std::vector<std::pair<double, int>>& found, std::size_t count) {
        std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        lines.insert(lines.end(), {"", heading});
        std::size_t shown = 0;
        for (const auto& [value, node] : found) {
            if (value <= 0 || shown == count) break;
            lines.push_back("  " + node_name(node) + "   " + fixed(value, 3));
            ++shown;
        }
        if (shown == 0) lines.push_back("  " + tr("None yet."));
    };
    top(tr("EASES ITS WORST NEED MOST") + " (" + tr(NEED_NAMES[worst]) + ")", eases, 5);
    top(tr("LIKES MOST"), likes, 6);
    lines.insert(lines.end(), {"", tr("LEARNT TO OPEN")});
    std::vector<OpensLink> links = animal.opens;
    std::stable_sort(links.begin(), links.end(), [](const OpensLink& a, const OpensLink& b) { return a.strength > b.strength; });
    if (links.empty()) lines.push_back("  " + tr("None yet."));
    for (std::size_t at = 0; at < std::min<std::size_t>(8, links.size()); ++at)
        lines.push_back("  " + node_name(links[at].from) + " " + tr("opens") + " " + node_name(links[at].to) + "   " + fixed(links[at].strength, 2));
    lines.insert(lines.end(), {"", tr("IN ITS LIFE")});
    const auto count = [&](const char* label, long long value) { lines.push_back(with_tabs({tr(label), std::to_string(value)})); };
    count("goals formed", animal.goals_formed);
    count("reached", animal.goals_reached);
    count("given up", animal.goals_abandoned);
    count("switched", animal.goals_switched);
    count("let go", animal.goals_dropped);
    count("desires formed", animal.desires_formed);
    count("desires reached", animal.desires_reached);
    count("chains begun", animal.chains_begun);
    count("steps reached", animal.steps_reached);
    count("steps given up", animal.steps_given_up);
    count("turns stuck", animal.stuck_turns);
    count("calls while stuck", animal.stuck_calls);
    count("projects taken up", animal.projects_taken);
    count("projects given up", animal.projects_given_up);
    count("instructions done", animal.instructions_taken);
    count("plans confided", animal.confided);
    count("plans confided to it", animal.confided_to);
    return lines;
}

Report::Layout Report::layout(int width, int height) const {
    Layout places;
    // as large as its largest tab, so it keeps its size as the tabs are chosen
    int widest = widest_of(lines), longest = static_cast<int>(lines.size());
    for (const Tab& tab : tabs) {
        widest = std::max(widest, widest_of(tab.lines));
        longest = std::max(longest, static_cast<int>(tab.lines.size()));
    }
    const int tab_row = tabs.empty() ? 0 : UI(TAB_ROW);
    places.panel = Rect{0, 0, std::min(width - UI(40), std::max(UI(560), widest + UI(48))),
                        std::min(height - UI(40), UI(160) + tab_row + UI(LINE) * longest)}
                       .center_at(width / 2, height / 2);
    const Rect& panel = places.panel;
    int x = panel.x + UI(24);
    for (const Tab& tab : tabs) {
        const int w = Canvas::text_width(UI.font(Face::Sans, 13, true), tab.name) + UI(28);
        places.tabs.push_back({x, panel.y + UI(52), w, UI(28)});
        x += w + UI(8);
    }
    places.body = {panel.x + UI(24), panel.y + UI(84) + tab_row, panel.w - UI(48), panel.h - UI(84 + 70) - tab_row};
    places.close = Rect{0, 0, UI(110), UI(40)}.bottomright_at(panel.right() - UI(24), panel.bottom() - UI(18));
    return places;
}

int Report::shown(int width, int height) const { return std::max(1, layout(width, height).body.h / UI(LINE)); }

void Report::scroll_by(int lines_down, int width, int height) {
    scroll = std::max(0, std::min(static_cast<int>(lines.size()) - shown(width, height), scroll + lines_down));
}

bool Report::handle(const SDL_Event& event, int width, int height) {
    if (event.type == SDL_KEYDOWN) {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) return true;
        if (key == SDLK_DOWN) scroll_by(1, width, height);
        else if (key == SDLK_UP) scroll_by(-1, width, height);
        else if (key == SDLK_PAGEDOWN) scroll_by(10, width, height);
        else if (key == SDLK_PAGEUP) scroll_by(-10, width, height);
        else if (key == SDLK_RIGHT && !tabs.empty()) choose((active + 1) % static_cast<int>(tabs.size()));
        else if (key == SDLK_LEFT && !tabs.empty()) choose((active + static_cast<int>(tabs.size()) - 1) % static_cast<int>(tabs.size()));
    } else if (event.type == SDL_MOUSEWHEEL) {
        scroll_by(-3 * event.wheel.y, width, height);
    } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        const Layout places = layout(width, height);
        for (std::size_t tab = 0; tab < places.tabs.size(); ++tab) {
            if (places.tabs[tab].contains(event.button.x, event.button.y)) {
                choose(static_cast<int>(tab));
                return false;
            }
        }
        return places.close.contains(event.button.x, event.button.y);
    }
    return false;
}

void Report::draw(Canvas& canvas) {
    const Layout places = layout(canvas.width(), canvas.height());
    const Rect &panel = places.panel, &body = places.body;
    canvas.fill_rect({0, 0, canvas.width(), canvas.height()}, {8, 12, 12, 185});
    canvas.rect(panel, {30, 40, 40}, 0, UI(8));
    canvas.rect(panel, {80, 99, 85}, 1, UI(8));
    canvas.text(UI.font(Face::Sans, 22, true), title, {229, 239, 220}, panel.x + UI(24), panel.y + UI(20));
    const int tab_row = tabs.empty() ? 0 : UI(TAB_ROW);
    for (std::size_t tab = 0; tab < places.tabs.size(); ++tab) {  // the tabs, the chosen one lit
        const bool chosen = static_cast<int>(tab) == active;
        canvas.rect(places.tabs[tab], chosen ? Color{131, 205, 143} : Color{44, 58, 57}, 0, UI(6));
        canvas.label(UI.font(Face::Sans, 13, true), tabs[tab].name, chosen ? Color{19, 26, 27} : Color{187, 199, 181}, places.tabs[tab]);
    }
    canvas.text(UI.fitting(UI.font(Face::Sans, 13), note, panel.w - UI(48)), note, {144, 170, 151}, panel.x + UI(24), panel.y + UI(54) + tab_row);
    const int count = shown(canvas.width(), canvas.height());
    scroll = std::max(0, std::min(std::max(0, static_cast<int>(lines.size()) - count), scroll));
    canvas.set_clip(body);
    TTF_Font* mono = UI.font(Face::Mono, 13);
    const std::map<std::size_t, std::vector<int>> all_widths = column_widths(lines);
    for (int row = 0; row < count && scroll + row < static_cast<int>(lines.size()); ++row) {
        const bool heading = scroll + row == 0;
        const Color colour = heading ? Color{220, 229, 211} : Color{187, 199, 181};
        const std::string& line = lines[static_cast<std::size_t>(scroll + row)];
        const int y = body.y + row * UI(LINE);
        if (line.find('\t') == std::string::npos) {
            canvas.text(mono, line, colour, body.x, y);
            continue;
        }
        // A table's row: its first cell to the left and the figures right-aligned in their columns; a
        // deadliest killer's row ends with the words of what it was doing, to the left of their column.
        const std::vector<std::string> cells = cells_of(line);
        const std::vector<int>& widths = all_widths.at(cells.size());
        int x = body.x;
        for (std::size_t index = 0; index < cells.size(); ++index) {
            const bool words = index == 0 || (cells.size() == 5 && index >= 3);
            const int cell_width = Canvas::text_width(mono, cells[index]);
            if (!heading && !words && cells[index].find('%') != std::string::npos) {  // a share shaded by its size: a heatmap
                const double share = std::clamp(std::atof(cells[index].c_str()) / 100.0, 0.0, 1.0);
                const auto shade = [share](int low, int high) { return static_cast<std::uint8_t>(low + (high - low) * share); };
                canvas.fill_rect({x - UI(2), y, widths[index] + UI(4), UI(LINE) - 1}, {shade(30, 30), shade(40, 88), shade(40, 118)});
            }
            canvas.text(mono, cells[index], colour, words ? x : x + widths[index] - cell_width, y);
            x += widths[index] + UI(GAP);
        }
    }
    canvas.set_clip(std::nullopt);
    if (static_cast<int>(lines.size()) > count) {
        const std::string more = "lines " + std::to_string(scroll + 1) + "-" +
                                 std::to_string(std::min(static_cast<int>(lines.size()), scroll + count)) + " of " +
                                 std::to_string(lines.size()) + (tabs.empty() ? "   (wheel or UP/DOWN)" : "   (wheel or UP/DOWN; LEFT/RIGHT for tabs)");
        canvas.text(UI.font(Face::Sans, 12), more, {116, 139, 124}, panel.x + UI(24), panel.bottom() - UI(44));
    }
    canvas.rect(places.close, {131, 205, 143}, 0, UI(6));
    canvas.label(UI.font(Face::Sans, 13, true), "CLOSE", {19, 26, 27}, places.close);
    tooltip(places.close, "Close the report.");
}

}  // namespace bitzkey::ui
