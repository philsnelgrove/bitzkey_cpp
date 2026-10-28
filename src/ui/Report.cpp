#include "ui/Report.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

#include "core/PyFormat.hpp"
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

Report::Layout Report::layout(int width, int height) const {
    Layout places;
    int widest = 0;  // the tables' columns and the gaps between them, or the widest line beside them
    for (const auto& [count, columns] : column_widths(lines)) {
        int table = 0;
        for (const int column : columns) table += column + UI(GAP);
        widest = std::max(widest, table);
    }
    for (const std::string& line : lines) {
        if (line.find('\t') == std::string::npos) widest = std::max(widest, Canvas::text_width(UI.font(Face::Mono, 13), line));
    }
    places.panel = Rect{0, 0, std::min(width - UI(40), std::max(UI(560), widest + UI(48))),
                        std::min(height - UI(40), UI(160) + UI(LINE) * static_cast<int>(lines.size()))}
                       .center_at(width / 2, height / 2);
    const Rect& panel = places.panel;
    places.body = {panel.x + UI(24), panel.y + UI(84), panel.w - UI(48), panel.h - UI(84 + 70)};
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
    } else if (event.type == SDL_MOUSEWHEEL) {
        scroll_by(-3 * event.wheel.y, width, height);
    } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        return layout(width, height).close.contains(event.button.x, event.button.y);
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
    canvas.text(UI.fitting(UI.font(Face::Sans, 13), note, panel.w - UI(48)), note, {144, 170, 151}, panel.x + UI(24), panel.y + UI(54));
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
            canvas.text(mono, cells[index], colour, words ? x : x + widths[index] - cell_width, y);
            x += widths[index] + UI(GAP);
        }
    }
    canvas.set_clip(std::nullopt);
    if (static_cast<int>(lines.size()) > count) {
        const std::string more = "lines " + std::to_string(scroll + 1) + "-" +
                                 std::to_string(std::min(static_cast<int>(lines.size()), scroll + count)) + " of " +
                                 std::to_string(lines.size()) + "   (wheel or UP/DOWN)";
        canvas.text(UI.font(Face::Sans, 12), more, {116, 139, 124}, panel.x + UI(24), panel.bottom() - UI(44));
    }
    canvas.rect(places.close, {131, 205, 143}, 0, UI(6));
    canvas.label(UI.font(Face::Sans, 13, true), "CLOSE", {19, 26, 27}, places.close);
}

}  // namespace bitzkey::ui
