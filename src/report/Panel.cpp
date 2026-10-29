#include "report/Panel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <map>

#include "ui/Platform.hpp"
#include "ui/Ui.hpp"

namespace bitzkey::report {

using namespace bitzkey::ui;

namespace {

constexpr Color BACK{22, 30, 30};
constexpr Color CARD{30, 40, 40};
constexpr Color EDGE{80, 99, 85};
constexpr Color TEXT{187, 199, 181};
constexpr Color BRIGHT{229, 239, 220};
constexpr Color DIM{144, 170, 151};
constexpr Color GREEN{131, 205, 143};
constexpr Color AMBER{238, 183, 110};
constexpr Color RED{230, 120, 105};
// Each animal species' line on the chart, and the plants'.
constexpr std::array<std::pair<const char*, Color>, 4> SERIES = {{{"plant", {96, 160, 86}},
                                                                  {"grazer", {120, 185, 235}},
                                                                  {"omnivore", {236, 186, 96}},
                                                                  {"predator", {226, 92, 80}}}};
constexpr int LINE = 18;    // design pixels to a line of the report
constexpr int GAP = 18;     // design pixels between a table's columns
constexpr int CHART = 230;  // the population chart's height, on its tab

std::string format(const char* pattern, auto... values) {
    char buffer[512];
    std::snprintf(buffer, sizeof buffer, pattern, values...);
    return buffer;
}

// A run of table lines from `at`: where it ends, and its columns' widths.
std::vector<int> table_widths(const std::vector<std::string>& lines, std::size_t at, std::size_t& end, TTF_Font* font) {
    std::vector<int> widths;
    for (end = at; end < lines.size() && lines[end].find('\t') != std::string::npos; ++end) {
        std::vector<std::string> cells = cells_of(lines[end]);
        if (!cells[0].empty() && cells[0][0] == '~') cells[0].erase(0, 1);
        widths.resize(std::max(widths.size(), cells.size()));
        for (std::size_t c = 0; c < cells.size(); ++c) widths[c] = std::max(widths[c], Canvas::text_width(font, cells[c]));
    }
    return widths;
}

}  // namespace

Panel::Panel(Window& window) : window_(window) {
    address_.placeholder = "port, or host:port";
    address_.max = 80;
}

Panel::~Panel() {
    if (reader_.joinable()) reader_.join();
}

void Panel::tell(const std::string& note, bool trouble) {
    note_ = note;
    trouble_ = trouble;
    note_until_ = SDL_GetTicks() + (trouble ? 12000 : 6000);
}

void Panel::begin(std::function<std::unique_ptr<Source>()> make, const std::string& doing) {
    if (busy_) return;
    if (reader_.joinable()) reader_.join();
    busy_ = true;
    doing_ = doing;
    Source* current = source_.get();
    reader_ = std::thread([this, make = std::move(make), current] {
        std::unique_ptr<Source> made;
        std::optional<World> read;
        std::string error;
        try {
            if (make) made = make();
            Source* from = made ? made.get() : current;
            if (!from) throw std::runtime_error("nothing to read");
            read = gather(*from);
        } catch (const std::exception& problem) {
            error = problem.what();
        }
        std::lock_guard lock(done_mutex_);
        read_ = std::move(read);
        read_source_ = std::move(made);
        read_error_ = error;
        done_ = true;
    });
}

void Panel::collect() {
    {
        std::lock_guard lock(done_mutex_);
        if (!done_) return;
        done_ = false;
        if (!read_error_.empty()) {
            tell(read_error_, true);
            if (read_source_) read_source_.reset();
            live_ = live_ && source_ && source_->live();
        } else if (read_) {
            const bool new_source = read_source_ != nullptr;
            if (new_source) source_ = std::move(read_source_);
            world_ = std::move(read_);
            analysis_ = analyse(*world_);
            if (new_source) {
                scrolls_.assign(analysis_.sections.size(), 0);
                across_ = 0;
                if (!source_->live()) live_ = false;
            }
            scrolls_.resize(analysis_.sections.size(), 0);
            tab_ = std::min(tab_, analysis_.sections.size() - 1);
            tell("Read " + source_->name() + " at turn " + std::to_string(world_->state.value("turn", 0)) + ".");
        }
        read_.reset();
    }
    if (reader_.joinable()) reader_.join();
    last_read_ = SDL_GetTicks();
    busy_ = false;
}

void Panel::open_save(const std::filesystem::path& path) {
    begin([path] { return Source::save(path); }, "Loading " + utf8_name(path) + "...");
}

void Panel::connect(const std::string& address) {
    address_.text = address;
    begin([address] { return Source::port(address); }, "Connecting to " + address + "...");
}

void Panel::refresh() {
    if (!source_) return tell("Open a save or connect to a probe first.");
    begin(nullptr, "Reading " + source_->name() + "...");
}

void Panel::copy() {
    if (!world_) return tell("There's no report to copy yet.");
    std::string error;
    if (set_clipboard_text(as_text(analysis_), error)) tell("The whole report is on the clipboard.");
    else tell("Couldn't copy: " + error, true);
}

void Panel::save() {
    if (!world_) return tell("There's no report to save yet.");
    std::string stem = world_->name;
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '-';
    }
    const auto path = data_dir() / "reports" / format("world-report-%s-turn-%05d.txt", stem.c_str(), world_->state.value("turn", 0));
    try {
        write_file(path, as_text(analysis_));
        const std::u8string named = path.u8string();
        tell("Saved " + std::string(named.begin(), named.end()));
    } catch (const std::exception& error) {
        tell(std::string("Couldn't save: ") + error.what(), true);
    }
}

Panel::Layout Panel::layout() const {
    Layout places;
    const int width = window_.width(), height = window_.height();
    const int top = UI(16), button = UI(36);
    int x = width - UI(16);
    const auto take = [&](int w) {
        x -= w;
        const Rect rect{x, top, w, button};
        x -= UI(8);
        return rect;
    };
    places.save = take(UI(70));
    places.copy = take(UI(70));
    places.live = take(UI(70));
    places.refresh = take(UI(90));
    x -= UI(16);
    places.connect = take(UI(96));
    places.address = take(UI(170));
    places.open = take(UI(110));
    const int sections = world_ ? static_cast<int>(analysis_.sections.size()) : 0;
    const int below = UI(104);
    for (int at = 0; at < sections; ++at) places.tabs.push_back({UI(16), below + at * UI(42), UI(176), UI(36)});
    const Rect content{UI(208), below, width - UI(224), height - below - UI(16)};
    const bool chart = world_ && tab_ < analysis_.sections.size() && analysis_.sections[tab_].title == "Population";
    places.chart = chart ? Rect{content.x, content.y, content.w, UI(CHART)} : Rect{};
    places.body = chart ? Rect{content.x, content.y + UI(CHART + 12), content.w, content.h - UI(CHART + 12)} : content;
    return places;
}

int Panel::lines_shown() const { return std::max(1, (layout().body.h - UI(24)) / UI(LINE)); }

void Panel::scroll_by(int lines) {
    if (!world_) return;
    const int total = static_cast<int>(analysis_.sections[tab_].lines.size());
    int& scroll = scrolls_[tab_];
    scroll = std::max(0, std::min(total - lines_shown(), scroll + lines));
}

void Panel::click(int x, int y) {
    const Layout places = layout();
    focused_ = places.address.contains(x, y);
    if (places.open.contains(x, y)) {
        if (const auto path = choose_world_file(data_dir() / "worlds")) open_save(*path);
    } else if (places.connect.contains(x, y)) {
        if (address_.text.empty()) {
            focused_ = true;
            tell("Type the probe's port: the app's --probe PORT, or probe.json's.");
        } else {
            connect(address_.text);
        }
    } else if (places.refresh.contains(x, y)) {
        refresh();
    } else if (places.live.contains(x, y)) {
        if (!source_ || !source_->live()) {
            tell("LIVE reads a running world again every " + std::to_string(LIVE_SECONDS) + " seconds: connect to a probe first.");
        } else {
            live_ = !live_;
            tell(live_ ? "LIVE: read again every " + std::to_string(LIVE_SECONDS) + " seconds." : "LIVE is off.");
        }
    } else if (places.copy.contains(x, y)) {
        copy();
    } else if (places.save.contains(x, y)) {
        save();
    }
    for (std::size_t at = 0; at < places.tabs.size(); ++at) {
        if (places.tabs[at].contains(x, y)) {
            tab_ = at;
            across_ = 0;
        }
    }
}

void Panel::key(const SDL_Keysym& key) {
    const bool control = key.mod & KMOD_CTRL;
    if (focused_) {
        if (key.sym == SDLK_BACKSPACE) address_.backspace();
        else if (key.sym == SDLK_RETURN || key.sym == SDLK_KP_ENTER) {
            focused_ = false;
            if (!address_.text.empty()) connect(address_.text);
        } else if (key.sym == SDLK_ESCAPE) focused_ = false;
        else if (control && key.sym == SDLK_v) {
            if (const auto pasted = clipboard_text()) address_.type(*pasted);
        }
        return;
    }
    const int sections = world_ ? static_cast<int>(analysis_.sections.size()) : 0;
    if (key.sym == SDLK_F11) window_.toggle_fullscreen();
    else if (key.sym == SDLK_F5) refresh();
    else if (control && key.sym == SDLK_c) copy();
    else if (control && key.sym == SDLK_s) save();
    else if (control && key.sym == SDLK_o) {
        if (const auto path = choose_world_file(data_dir() / "worlds")) open_save(*path);
    } else if (key.sym == SDLK_DOWN) scroll_by(1);
    else if (key.sym == SDLK_UP) scroll_by(-1);
    else if (key.sym == SDLK_PAGEDOWN || key.sym == SDLK_SPACE) scroll_by(lines_shown() - 2);
    else if (key.sym == SDLK_PAGEUP) scroll_by(2 - lines_shown());
    else if (key.sym == SDLK_HOME) scroll_by(-1000000);
    else if (key.sym == SDLK_END) scroll_by(1000000);
    else if (sections && (key.sym == SDLK_TAB || key.sym == SDLK_RIGHT || key.sym == SDLK_LEFT)) {
        const bool back = key.sym == SDLK_LEFT || (key.sym == SDLK_TAB && (key.mod & KMOD_SHIFT));
        tab_ = static_cast<std::size_t>((static_cast<int>(tab_) + (back ? sections - 1 : 1)) % sections);
        across_ = 0;
    }
}

void Panel::run() {
    SDL_StartTextInput();
    for (;;) {
        const Uint32 frame_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window_.handle(event);
            if (event.type == SDL_QUIT) {
                SDL_StopTextInput();
                return;
            }
            if (event.type == SDL_KEYDOWN) key(event.key.keysym);
            else if (event.type == SDL_TEXTINPUT && focused_) address_.type(event.text.text);
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) click(event.button.x, event.button.y);
            else if (event.type == SDL_MOUSEWHEEL) {
                if (SDL_GetModState() & KMOD_SHIFT) across_ = std::max(0, across_ - UI(40) * (event.wheel.y + event.wheel.x));
                else {
                    scroll_by(-3 * event.wheel.y);
                    across_ = std::max(0, across_ + UI(40) * event.wheel.x);
                }
            } else if (event.type == SDL_DROPFILE) {
                const std::filesystem::path path = path_from_utf8(event.drop.file);
                SDL_free(event.drop.file);
                open_save(path);
            }
        }
        collect();
        if (live_ && !busy_ && source_ && SDL_GetTicks() - last_read_ > LIVE_SECONDS * 1000u) refresh();
        UI.fit(window_.width(), window_.height());
        Canvas canvas = window_.frame();
        draw(canvas);
        window_.present();
        const Uint32 took = SDL_GetTicks() - frame_start;
        if (took < 33) SDL_Delay(33 - took);
    }
}

void Panel::draw(Canvas& canvas) {
    canvas.fill(BACK);
    const Layout places = layout();
    canvas.text(UI.font(Face::Sans, 22, true), "BITZKEY REPORTS", BRIGHT, UI(18), UI(18));
    draw_button(canvas, places.open, "OPEN SAVE");
    address_.rect = places.address;
    address_.draw(canvas, focused_);
    draw_button(canvas, places.connect, "CONNECT");
    draw_button(canvas, places.refresh, "REFRESH", source_ ? Tone::Primary : Tone::Plain);
    draw_button(canvas, places.live, live_ ? "LIVE ON" : "LIVE", live_ ? Tone::Primary : Tone::Plain);
    draw_button(canvas, places.copy, "COPY");
    draw_button(canvas, places.save, "SAVE");

    // The line under the buttons: what is read, and what is happening.
    std::string status;
    if (busy_) status = doing_;
    else if (SDL_GetTicks() < note_until_) status = note_;
    else if (world_) {
        status = world_->name + (source_ && source_->live() ? "  (running)" : "  (save)") + "   turn " + std::to_string(world_->state.value("turn", 0));
        if (live_) status += "   LIVE every " + std::to_string(LIVE_SECONDS) + " s";
    }
    const Color tone = busy_ ? AMBER : (SDL_GetTicks() < note_until_ && trouble_) ? RED : DIM;
    canvas.text(UI.fitting(UI.font(Face::Sans, 14), status, canvas.width() - UI(36)), status, tone, UI(18), UI(66));

    if (!world_) {
        const Rect card = Rect{0, 0, std::min(canvas.width() - UI(40), UI(720)), UI(250)}.center_at(canvas.width() / 2, canvas.height() / 2);
        canvas.rect(card, CARD, 0, UI(8));
        canvas.rect(card, EDGE, 1, UI(8));
        int y = card.y + UI(24);
        canvas.text(UI.font(Face::Sans, 18, true), "A whole world's report", BRIGHT, card.x + UI(24), y);
        y += UI(40);
        for (const char* line : {"OPEN SAVE, or drop a .bzworld file here, to report on a saved world.",
                                 "Or type a probe's port and CONNECT, to report on a running world:",
                                 "the app's (start it with --probe PORT, or put {\"port\": 49200} in probe.json",
                                 "in the Bitzkey folder in Documents), or the daemon's host:port.",
                                 "", "REFRESH reads it again; LIVE every 10 seconds. COPY and SAVE take the report as text."}) {
            canvas.text(UI.font(Face::Sans, 14), line, TEXT, card.x + UI(24), y);
            y += UI(24);
        }
        return;
    }

    for (std::size_t at = 0; at < places.tabs.size(); ++at) {
        const Rect& tab = places.tabs[at];
        const bool chosen = at == tab_;
        canvas.rect(tab, chosen ? GREEN : CARD, 0, UI(6));
        bool finding = false;  // the summary's tab is marked while anything stands out
        if (at == 0) {
            for (const std::string& line : analysis_.sections[0].lines) finding = finding || line.rfind("! ", 0) == 0;
        }
        canvas.text_at(UI.font(Face::Sans, 14, chosen), analysis_.sections[at].title, chosen ? Color{19, 26, 27} : TEXT,
                       [&](Rect& box) { box.midleft_at(tab.x + UI(12), tab.centery()); });
        if (finding) canvas.circle(tab.right() - UI(14), tab.centery(), UI(4), chosen ? Color{150, 80, 20} : AMBER);
    }
    if (places.chart.w > 0) draw_chart(canvas, places.chart);
    draw_lines(canvas, places.body);
}

void Panel::draw_chart(Canvas& canvas, const Rect& area) {
    canvas.rect(area, CARD, 0, UI(8));
    canvas.rect(area, EDGE, 1, UI(8));
    const Json& history = world_->history;
    if (!history.is_object() || history["history"].size() < 2) {
        canvas.text(UI.font(Face::Sans, 14), "No history to chart yet.", DIM, area.x + UI(16), area.y + UI(16));
        return;
    }
    const Json& points = history["history"];
    // Plants are drawn against their own scale, faintly, as they far outnumber the animals.
    const Rect plot{area.x + UI(56), area.y + UI(36), area.w - UI(72), area.h - UI(64)};
    int most_animals = 1, most_plants = 1;
    for (const Json& point : points) {
        for (const auto& [species, _] : SERIES) {
            const int count = point["alive"].value(species, 0);
            if (std::string(species) == "plant") most_plants = std::max(most_plants, count);
            else most_animals = std::max(most_animals, count);
        }
    }
    const int first = points.front().value("turn", 0), last = std::max(first + 1, points.back().value("turn", 0));
    TTF_Font* small = UI.font(Face::Sans, 12);
    for (int step = 0; step <= 4; ++step) {
        const int y = plot.bottom() - plot.h * step / 4;
        canvas.line(plot.x, y, plot.right(), y, {44, 58, 56});
        canvas.text_at(small, std::to_string(most_animals * step / 4), DIM, [&](Rect& box) { box.midright_at(plot.x - UI(8), y); });
    }
    canvas.text_at(small, "turn " + std::to_string(first), DIM, [&](Rect& box) { box.x = plot.x; box.y = plot.bottom() + UI(6); });
    canvas.text_at(small, "turn " + std::to_string(last), DIM, [&](Rect& box) { box.topright_at(plot.right(), plot.bottom() + UI(6)); });
    for (const auto& [species, colour] : SERIES) {
        const bool plants = std::string(species) == "plant";
        const int most = plants ? most_plants : most_animals;
        const Color shade = plants ? Color{colour.r, colour.g, colour.b, 140} : colour;
        int px = -1, py = -1;
        for (const Json& point : points) {
            const int x = plot.x + static_cast<int>(static_cast<long long>(point.value("turn", 0) - first) * plot.w / (last - first));
            const int y = plot.bottom() - static_cast<int>(static_cast<long long>(point["alive"].value(species, 0)) * plot.h / most);
            if (px >= 0) canvas.line(px, py, x, y, shade, plants ? 1 : std::max(1, UI(2)));
            px = x;
            py = y;
        }
    }
    int x = area.x + UI(16);
    for (const auto& [species, colour] : SERIES) {
        const bool plants = std::string(species) == "plant";
        const std::string label = std::string(species) + "s " + std::to_string(world_->state["alive"].value(species, 0)) +
                                  (plants ? " (own scale, to " + std::to_string(most_plants) + ")" : "");
        canvas.rect({x, area.y + UI(14), UI(14), UI(4)}, colour);
        x += UI(20);
        x += canvas.text(small, label, TEXT, x, area.y + UI(8)) + UI(22);
    }
}

void Panel::draw_lines(Canvas& canvas, const Rect& area) {
    canvas.rect(area, CARD, 0, UI(8));
    canvas.rect(area, EDGE, 1, UI(8));
    const Rect body{area.x + UI(18), area.y + UI(12), area.w - UI(36), area.h - UI(24)};
    const std::vector<std::string>& lines = analysis_.sections[tab_].lines;
    const int shown = lines_shown();
    int& scroll = scrolls_[tab_];
    scroll = std::max(0, std::min(std::max(0, static_cast<int>(lines.size()) - shown), scroll));
    TTF_Font* mono = UI.font(Face::Mono, 13);
    TTF_Font* heading = UI.font(Face::Sans, 15, true);
    canvas.set_clip(body);
    // Each table's columns are as wide as its widest cells, measured over the whole table, so they hold still as it
    // scrolls.
    std::map<std::size_t, std::vector<int>> widths;  // by a table's first line
    std::vector<std::size_t> table_of(lines.size(), lines.size());
    for (std::size_t at = 0; at < lines.size();) {
        if (lines[at].find('\t') == std::string::npos) {
            ++at;
            continue;
        }
        std::size_t end = at;
        widths[at] = table_widths(lines, at, end, mono);
        for (std::size_t line = at; line < end; ++line) table_of[line] = at;
        at = end;
    }
    const int left = body.x - across_;
    for (int row = 0; row < shown && scroll + row < static_cast<int>(lines.size()); ++row) {
        const std::size_t index = static_cast<std::size_t>(scroll + row);
        const std::string& line = lines[index];
        const int y = body.y + row * UI(LINE);
        if (table_of[index] == lines.size()) {
            if (line.rfind("# ", 0) == 0) canvas.text(heading, line.substr(2), BRIGHT, left, y - UI(1));
            else if (line.rfind("! ", 0) == 0) {
                canvas.circle(left + UI(5), y + UI(LINE) / 2, UI(4), AMBER);
                canvas.text(UI.font(Face::Sans, 14), line.substr(2), AMBER, left + UI(18), y);
            } else if (!line.empty()) canvas.text(mono, line, TEXT, left, y);
            continue;
        }
        // A table's row: its first cell to the left, the rest right-aligned in their columns; a last column of words
        // (as long as it is wider than a number) to the left.
        std::vector<std::string> cells = cells_of(line);
        const bool header = !cells[0].empty() && cells[0][0] == '~';
        if (header) cells[0].erase(0, 1);
        const std::vector<int>& columns = widths[table_of[index]];
        int x = left;
        for (std::size_t c = 0; c < cells.size(); ++c) {
            const int width = Canvas::text_width(mono, cells[c]);
            const bool words = c == 0 || (c + 1 == columns.size() && columns[c] > UI(120));
            canvas.text(mono, cells[c], header ? GREEN : c == 0 ? BRIGHT : TEXT, words ? x : x + columns[c] - width, y);
            x += columns[c] + UI(GAP);
        }
    }
    canvas.set_clip(std::nullopt);
    if (static_cast<int>(lines.size()) > shown) {
        const std::string more = format("lines %d-%d of %zu", scroll + 1, std::min(static_cast<int>(lines.size()), scroll + shown), lines.size());
        canvas.text_at(UI.font(Face::Sans, 12), more, DIM, [&](Rect& box) { box.bottomright_at(area.right() - UI(14), area.bottom() - UI(4)); });
    }
}

}  // namespace bitzkey::report
