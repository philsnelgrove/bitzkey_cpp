#include "ui/WorkersScreen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "core/About.hpp"
#include "ui/Language.hpp"

namespace bitzkey::ui {

namespace {

constexpr std::uint64_t MB = 1 << 20;
constexpr Color TITLE{229, 239, 220};
constexpr Color INK{220, 229, 211};
constexpr Color SOFT{175, 192, 172};
constexpr Color FAINT{130, 152, 136};
constexpr Color GREEN{131, 205, 143};
constexpr Color AMBER{238, 183, 110};

std::string state_label(const std::string& state) {
    if (state == "working") return "WORKING";
    if (state == "connecting") return "CONNECTING";
    if (state == "lost") return "LOST";
    if (state == "stopping") return "STOPPING";
    if (state == "away") return "AWAY";
    if (state == "refused") return "REFUSED";
    return state;
}

// A whole number with its thousands marked: 210,410.
std::string grouped(double value) {
    std::string digits = std::to_string(static_cast<long long>(std::llround(value)));
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) digits.insert(static_cast<std::size_t>(at), ",");
    return digits;
}

std::string speed_text(double speed) { return grouped(speed) + " a second"; }

std::string shares(const net::Json& worker) {
    std::string text = std::to_string(worker.value("threads", 0)) + " cores, " + std::to_string(worker.value("memory", std::uint64_t{0}) / MB) + " MB";
    for (const auto& gpu : worker.value("gpus", net::Json::array())) text += ", " + gpu.get<std::string>();
    return text;
}

// Text shortened, with an ellipsis, to fit the width, in the language it is shown in.
std::string fitted(TTF_Font* font, const std::string& english, int width) {
    std::string text = tr(english);
    const AsShown measuring;
    if (Canvas::text_width(font, text) <= width) return as_shown(text);
    while (!text.empty() && Canvas::text_width(font, text + "...") > width) pop_character(text);
    return as_shown(text + "...");
}

}  // namespace

WorkersScreen::WorkersScreen(Window& window, net::Pool& pool, net::Worker& worker, Simulation* simulation)
    : window_(window), pool_(pool), worker_(worker), simulation_(simulation) {
    host_.placeholder = "IP address";
    host_.max = 253;
    port_.placeholder = std::to_string(net::DEFAULT_WORKER_PORT);
    port_.digits = true;
    port_.max = 5;
    message_.placeholder = "A message for the worker's page (optional); it can hold a link";
    message_.max = 1000;
    checkpoint_.digits = true;
    checkpoint_.max = 6;
    checkpoint_.text = std::to_string(pool_.checkpoint());
    host_share_.digits = true;
    host_share_.max = 3;
    host_share_.text = std::to_string(pool_.host_share());
    worker_port_.digits = true;
    worker_port_.max = 5;
    worker_port_.text = std::to_string(net::DEFAULT_WORKER_PORT);
    focus_ = &host_;
    if (!simulation_) start_becoming();  // from the configuration panel: no world to list workers for
}

void WorkersScreen::start_becoming() {
    const net::Machine machine = net::scan_machine();
    net::Resources shared;
    shared.threads = std::max(1, machine.threads / 2);
    shared.memory = std::max<std::uint64_t>(64 * MB, machine.memory / 2 / MB * MB);
    for (std::size_t index = 0; index < machine.gpus.size(); ++index) shared.gpus.push_back(static_cast<int>(index));
    share_.emplace(machine, shared);
    focus_ = &worker_port_;
}

std::vector<TextField*> WorkersScreen::fields() {
    if (share_) {
        std::vector<TextField*> all = {&worker_port_};
        for (TextField* field : share_->fields()) all.push_back(field);
        return all;
    }
    return {&host_, &port_, &message_, &checkpoint_, &host_share_};
}

void WorkersScreen::tell(const std::string& note) {
    note_ = note;
    note_until_ = SDL_GetTicks() + 6000;
}

WorkersScreen::Outcome WorkersScreen::run() {
    while (true) {
        const Uint32 frame_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window_.handle(event);
            if (event.type == SDL_QUIT) return Outcome::Quit;
            if (dialog_) {
                if (const auto choice = dialog_->handle(event, window_.width(), window_.height())) {
                    if (const auto outcome = answer(*choice)) return *outcome;
                }
                continue;
            }
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    if (!share_ || !simulation_) return Outcome::Close;
                    share_.reset();  // back to the list
                    focus_ = &host_;
                } else if (key == SDLK_F11) {
                    window_.toggle_fullscreen();
                } else if (key == SDLK_TAB) {
                    auto all = fields();
                    auto at = std::find(all.begin(), all.end(), focus_);
                    const bool back = event.key.keysym.mod & KMOD_SHIFT;
                    const std::ptrdiff_t index = at == all.end() ? 0 : at - all.begin();
                    focus_ = all[static_cast<std::size_t>((index + (back ? -1 : 1) + static_cast<std::ptrdiff_t>(all.size())) %
                                                          static_cast<std::ptrdiff_t>(all.size()))];
                } else if (key == SDLK_BACKSPACE && focus_) {
                    focus_->backspace();
                } else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && !share_) {
                    if (focus_ == &checkpoint_) click(set_button_.centerx(), set_button_.centery());
                    else if (focus_ == &host_share_) click(share_button_.centerx(), share_button_.centery());
                    else add();
                } else if (key == SDLK_v && (event.key.keysym.mod & KMOD_CTRL) && focus_) {
                    if (char* pasted = SDL_GetClipboardText()) {
                        focus_->type(pasted);
                        SDL_free(pasted);
                    }
                }
            } else if (event.type == SDL_TEXTINPUT && focus_) {
                focus_->type(event.text.text);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (const auto outcome = click(event.button.x, event.button.y)) return *outcome;
            }
        }
        if (simulation_) pool_.tend(*simulation_);  // workers join, and the brains are shared out, while this is open
        list_ = pool_.list();
        Canvas canvas = window_.frame();
        draw(canvas);
        window_.present();
        const Uint32 spent = SDL_GetTicks() - frame_start;
        if (spent < 33) SDL_Delay(33 - spent);
    }
}

void WorkersScreen::add() {
    std::string host = host_.text;
    while (!host.empty() && host.back() == ' ') host.pop_back();
    while (!host.empty() && host.front() == ' ') host.erase(host.begin());
    int port = port_.number().value_or(net::DEFAULT_WORKER_PORT);
    if (const auto colon = host.rfind(':'); colon != std::string::npos && host.find(':') == colon) {  // 192.168.1.20:50001
        try {
            port = std::stoi(host.substr(colon + 1));
        } catch (const std::exception&) {
            port = 0;
        }
        host.erase(colon);
    }
    try {
        pool_.add(host, port, message_.text);
        pool_.adopt(*simulation_);
        tell("Added " + host + ": connecting.");
        host_.text.clear();
        port_.text.clear();
        message_.text.clear();
    } catch (const std::invalid_argument& error) {
        tell(error.what());
    }
}

std::optional<WorkersScreen::Outcome> WorkersScreen::click(int x, int y) {
    if (language_menu_.click(x, y)) return std::nullopt;
    for (TextField* field : fields()) {
        if (field->rect.contains(x, y)) {
            focus_ = field;
            return std::nullopt;
        }
    }
    if (share_) {
        if (TextField* field = share_->click(x, y)) focus_ = field;
        if (cancel_button_.contains(x, y)) {
            if (!simulation_) return Outcome::Close;
            share_.reset();
            focus_ = &host_;
        } else if (start_button_.contains(x, y)) {
            dialog_kind_ = "become";
            dialog_.emplace("Become a worker?",
                            std::vector<std::string>{simulation_ ? "This world is let go, and the app shows only that it is a worker." : "The app shows only that it is a worker.",
                                                     "Worker mode is turned off from that screen."},
                            std::vector<std::pair<std::string, std::string>>{{"stay", "CANCEL"}, {"start", "BECOME A WORKER"}}, "stay");
        }
        return std::nullopt;
    }
    if (close_button_.contains(x, y)) return Outcome::Close;
    if (add_button_.contains(x, y)) {
        add();
    } else if (set_button_.contains(x, y)) {
        try {
            pool_.set_checkpoint(checkpoint_.number().value_or(0));
            tell("Every brain is brought back every " + checkpoint_.text + " turns.");
        } catch (const std::invalid_argument& error) {
            tell(error.what());
        }
    } else if (share_button_.contains(x, y)) {
        try {
            pool_.set_host_share(host_share_.number().value_or(-1));
            tell(pool_.host_share() ? "This computer thinks at " + host_share_.text + "% of its pace; the workers take the rest."
                                    : "This computer thinks for none: its brains go to the workers.");
        } catch (const std::invalid_argument& error) {
            tell(error.what());
        }
    } else if (become_button_.contains(x, y)) {
        start_becoming();
    } else {
        for (const Row& row : rows_) {
            if (!row.remove.contains(x, y)) continue;
            removing_ = std::pair{row.host, row.port};
            dialog_kind_ = "remove";
            dialog_.emplace("Remove " + row.host + ":" + std::to_string(row.port) + "?",
                            std::vector<std::string>{"Its brains are brought back to this service first, and it no longer works for it."},
                            std::vector<std::pair<std::string, std::string>>{{"keep", "CANCEL"}, {"remove", "REMOVE"}}, "keep");
        }
    }
    return std::nullopt;
}

std::optional<WorkersScreen::Outcome> WorkersScreen::answer(const std::string& choice) {
    const std::string kind = dialog_kind_;
    dialog_.reset();
    if (kind == "remove" && choice == "remove" && removing_) {
        tell("Bringing its brains back...");
        Canvas canvas = window_.frame();
        draw(canvas);
        window_.present();
        try {
            pool_.remove(removing_->first, removing_->second);
            pool_.adopt(*simulation_);
            tell("Removed " + removing_->first + ", its brains brought back first.");
        } catch (const std::invalid_argument& error) {
            tell(error.what());
        }
        removing_.reset();
    } else if (kind == "become" && choice == "start" && share_) {
        std::string problem;
        const auto shared = share_->read(problem);
        if (!shared) {
            tell(problem);
            return std::nullopt;
        }
        if (pool_.active()) {
            tell("This service has workers of its own: remove them first.");
            return std::nullopt;
        }
        try {
            worker_.start(worker_port_.number().value_or(0), *shared);
            return Outcome::Worker;
        } catch (const std::invalid_argument& error) {
            tell(error.what());
        }
    }
    return std::nullopt;
}

Rect WorkersScreen::card() const {
    return Rect{0, 0, std::min(window_.width() - UI(24), UI(980)), std::min(window_.height() - UI(24), UI(780))}.center_at(window_.width() / 2,
                                                                                                                        window_.height() / 2);
}

void WorkersScreen::draw(Canvas& canvas) {
    const Rect area = card();
    canvas.fill({19, 26, 27});
    canvas.rect(area, {27, 37, 37}, 0, UI(8));
    canvas.rect(area, {80, 99, 85}, 1, UI(8));
    const int title = canvas.text(UI.font(Face::Sans, 22, true), "BITZKEY", TITLE, area.x + UI(28), area.y + UI(24));
    const int subtitle = canvas.text(UI.font(Face::Sans, 12),
                !simulation_ ? "NEW WORLD  /  BECOME A WORKER" : share_ ? "THIS WORLD  /  WORKERS  /  BECOME A WORKER" : "THIS WORLD  /  WORKERS",
                {144, 170, 151},
                area.x + UI(52) + title, area.y + UI(33));
    canvas.text(UI.font(Face::Sans, 12), "/  " + about(), {144, 170, 151}, area.x + UI(60) + title + subtitle, area.y + UI(33));  // the version, and the cores
    language_menu_.draw_button(canvas, area.right() - UI(24), area.y + UI(36));
    int y = area.y + UI(70);
    if (share_) draw_becoming(canvas, area, y);
    else draw_list(canvas, area, y);
    if (SDL_GetTicks() < note_until_) {
        canvas.text(UI.fitting(UI.font(Face::Sans, 13), note_, area.w - UI(420)), note_, AMBER, area.x + UI(28), area.bottom() - UI(52));
    }
    if (dialog_) dialog_->draw(canvas);
    language_menu_.draw_list(canvas);
}

void WorkersScreen::draw_list(Canvas& canvas, const Rect& area, int& y) {
    TTF_Font* text = UI.font(Face::Sans, 15);
    TTF_Font* small = UI.font(Face::Sans, 12);
    TTF_Font* cell = UI.font(Face::Sans, 13);
    TTF_Font* heading = UI.font(Face::Sans, 13, true);
    const int left = area.x + UI(28), width = area.w - UI(56);
    for (const std::string& line : wrapped_lines(text,
                                                 "Workers are other Bitzkeys, this app or the Linux daemon, in worker mode. Each holds some of this "
                                                 "world's brains, and thinks and learns for them; this service shares the brains out by each one's "
                                                 "speed, and keeps a share itself.",
                                                 width)) {
        canvas.text(text, line, SOFT, left, y);
        y += UI(21);
    }
    y += UI(10);
    // The table: each column's left edge.
    const int columns[] = {left, left + UI(230), left + UI(360), left + UI(440), left + UI(680), left + UI(800), area.right() - UI(28) - UI(84)};
    const char* names[] = {"WORKER", "STATE", "PING", "SHARES", "SPEED", "BRAINS"};
    for (int index = 0; index < 6; ++index) canvas.text(UI.font(Face::Sans, 11), names[index], {149, 170, 150}, columns[index], y);
    y += UI(20);
    canvas.line(left, y, area.right() - UI(28), y, {54, 68, 62});
    y += UI(6);
    const auto clip = [&](const std::string& words, int column, int end) { return fitted(cell, words, end - column - UI(10)); };
    const net::Json& local = list_["local"];
    canvas.text(cell, "This service", INK, columns[0], y);
    canvas.text(cell, "WORKING", GREEN, columns[1], y);
    canvas.text(cell, clip(local.value("description", std::string()), columns[3], columns[4]), INK, columns[3], y);
    canvas.text(cell, speed_text(local.value("pace", 0.0) > 0 ? local.value("pace", 0.0) : local.value("speed", 0.0)), INK, columns[4], y);
    canvas.text(cell, std::to_string(local.value("brains", 0)), INK, columns[5], y);
    y += UI(30);
    rows_.clear();
    const int room_bottom = area.bottom() - UI(250);
    for (const auto& worker : list_.value("workers", net::Json::array())) {
        if (y > room_bottom) {
            canvas.text(small, "More workers are listed than fit here.", FAINT, columns[0], y);
            y += UI(20);
            break;
        }
        const std::string host = worker.value("host", std::string());
        const int port = worker.value("port", net::DEFAULT_WORKER_PORT);
        const std::string state = worker.value("state", std::string());
        const bool working = state == "working";
        canvas.text(cell, as_shown(host + ":" + std::to_string(port)), INK, columns[0], y);
        canvas.text(cell, state_label(state), working ? GREEN : AMBER, columns[1], y);
        const double ping = worker.value("ping_ms", -1.0);
        if (ping >= 0) {
            char shown[32];
            std::snprintf(shown, sizeof shown, "%.1f ms", ping);
            canvas.text(cell, shown, INK, columns[2], y);
        }
        if (working) {
            canvas.text(cell, clip(shares(worker), columns[3], columns[4]), INK, columns[3], y);
            canvas.text(cell, speed_text(worker.value("pace", 0.0) > 0 ? worker.value("pace", 0.0) : worker.value("speed", 0.0)), INK, columns[4], y);
        }
        canvas.text(cell, std::to_string(worker.value("brains", 0)), INK, columns[5], y);
        const Rect remove{columns[6], y - UI(3), UI(84), UI(24)};
        draw_button(canvas, remove, "REMOVE");
        rows_.push_back({host, port, remove});
        // Beneath: its message, and what went wrong, if anything.
        const std::string message = worker.value("message", std::string()), error = worker.value("error", std::string());
        if (!message.empty() || !error.empty()) {
            y += UI(18);
            if (!message.empty()) canvas.text(small, fitted(small, as_shown(message), columns[1] - columns[0] - UI(10)), FAINT, columns[0], y);
            if (!error.empty()) canvas.text(small, fitted(small, error, columns[6] - columns[1] - UI(10)), FAINT, columns[1], y);
        }
        y += UI(30);
    }

    y = std::max(y + UI(10), area.bottom() - UI(318));
    canvas.text(heading, "ADD A WORKER", GREEN, left, y);
    y += UI(24);
    host_.rect = {left, y, UI(200), UI(32)};
    port_.rect = {host_.rect.right() + UI(8), y, UI(90), UI(32)};
    add_button_ = Rect{0, 0, UI(90), UI(32)}.topright_at(area.right() - UI(28), y);
    message_.rect = {port_.rect.right() + UI(8), y, add_button_.x - UI(8) - port_.rect.right() - UI(8), UI(32)};
    for (TextField* field : {&host_, &port_, &message_}) field->draw(canvas, focus_ == field);
    draw_button(canvas, add_button_, "ADD", Tone::Primary);
    y += UI(46);
    canvas.text(heading, "CHECKPOINTS", GREEN, left, y);
    y += UI(24);
    const int said = canvas.text(text, "Bring every brain back every", SOFT, left, y + UI(5));
    checkpoint_.rect = {left + said + UI(10), y, UI(90), UI(32)};
    checkpoint_.draw(canvas, focus_ == &checkpoint_);
    const int turns = canvas.text(text, "turns", SOFT, checkpoint_.rect.right() + UI(10), y + UI(5));
    set_button_ = {checkpoint_.rect.right() + UI(20) + turns, y, UI(70), UI(32)};
    draw_button(canvas, set_button_, "SET");
    y += UI(38);
    canvas.text(small, "Should a worker fail, its brains go to the others as they were at the last checkpoint.", FAINT, left, y);
    y += UI(30);
    canvas.text(heading, "HOST SHARE", GREEN, left, y);
    y += UI(24);
    const int thinks = canvas.text(text, "This computer thinks at", SOFT, left, y + UI(5));
    host_share_.rect = {left + thinks + UI(10), y, UI(70), UI(32)};
    host_share_.draw(canvas, focus_ == &host_share_);
    const int pace = canvas.text(text, "% of its pace", SOFT, host_share_.rect.right() + UI(10), y + UI(5));
    share_button_ = {host_share_.rect.right() + UI(20) + pace, y, UI(70), UI(32)};
    draw_button(canvas, share_button_, "SET");
    y += UI(38);
    canvas.text(small, "Brains are shared out by the pace each machine keeps on real turns. At 0, this computer only runs the world.", FAINT, left, y);

    close_button_ = Rect{0, 0, UI(120), UI(40)}.bottomright_at(area.right() - UI(28), area.bottom() - UI(24));
    become_button_ = Rect{0, 0, std::max(UI(190), Canvas::text_width(heading, "BECOME A WORKER...") + UI(32)), UI(40)}.bottomright_at(
        close_button_.x - UI(10), close_button_.bottom());
    draw_button(canvas, become_button_, "BECOME A WORKER...");
    draw_button(canvas, close_button_, "CLOSE", Tone::Primary);
}

void WorkersScreen::draw_becoming(Canvas& canvas, const Rect& area, int& y) {
    TTF_Font* text = UI.font(Face::Sans, 15);
    TTF_Font* small = UI.font(Face::Sans, 12);
    const int left = area.x + UI(28), width = area.w - UI(56);
    canvas.text(UI.font(Face::Sans, 18, true), "Become a Worker", TITLE, left, y);
    y += UI(34);
    for (const std::string& line : wrapped_lines(text,
                                                 "A worker runs no world of its own: it holds brains for a main service elsewhere, and thinks for "
                                                 "them. The main service adds it by this computer's IP address and the port below.",
                                                 width)) {
        canvas.text(text, line, SOFT, left, y);
        y += UI(21);
    }
    y += UI(14);
    canvas.text_at(UI.font(Face::Sans, 13), "Port", SOFT, [&](Rect& box) { box.midleft_at(left, y + UI(19)); });
    worker_port_.rect = {left + UI(130), y + UI(4), UI(100), UI(30)};
    worker_port_.draw(canvas, focus_ == &worker_port_);
    canvas.text_at(small, "to listen on for the main service", FAINT, [&](Rect& box) { box.midleft_at(worker_port_.rect.right() + UI(10), y + UI(19)); });
    y += UI(38);
    y += share_->draw(canvas, left, y, width, focus_);
    y += UI(10);
    for (const std::string& line : wrapped_lines(small,
                                                 "Windows asks to let Bitzkey open the port in its firewall: approve it, so the main service can reach "
                                                 "this computer. A router in front of it needs the port forwarded by hand.",
                                                 width)) {
        canvas.text(small, line, FAINT, left, y);
        y += UI(17);
    }
    start_button_ = Rect{0, 0, UI(200), UI(40)}.bottomright_at(area.right() - UI(28), area.bottom() - UI(24));
    cancel_button_ = Rect{0, 0, UI(120), UI(40)}.bottomright_at(start_button_.x - UI(10), start_button_.bottom());
    draw_button(canvas, cancel_button_, "CANCEL");
    draw_button(canvas, start_button_, "BECOME A WORKER", Tone::Primary);
}

}  // namespace bitzkey::ui
