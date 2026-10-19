#include "ui/WorkerScreen.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <regex>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include "ui/Language.hpp"

namespace bitzkey::ui {

namespace {

constexpr Color BACK{88, 92, 92};
constexpr Color CARD{62, 66, 66};
constexpr Color EDGE{120, 126, 126};
constexpr Color BRIGHT{236, 238, 236};
constexpr Color TEXT{228, 230, 228};
constexpr Color DIM{200, 204, 200};
constexpr Color LINK{131, 205, 143};
constexpr Color AMBER{238, 183, 110};

std::string grouped(double value) {
    std::string digits = std::to_string(static_cast<long long>(std::llround(value)));
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) digits.insert(static_cast<std::size_t>(at), ",");
    return digits;
}

}  // namespace

void open_worker_ports(const std::vector<int>& ports) {
#ifdef _WIN32
    // One rule, named for Bitzkey, replaced each time: its ports, or none.
    std::wstring command = L"/c netsh advfirewall firewall delete rule name=\"Bitzkey worker\" >nul 2>&1";
    std::wstring list;
    for (const int port : ports) {
        if (port < 1 || port > 65535) continue;
        list += (list.empty() ? L"" : L",") + std::to_wstring(port);
    }
    if (!list.empty()) {
        command += L" & netsh advfirewall firewall add rule name=\"Bitzkey worker\" dir=in action=allow protocol=TCP localport=" + list;
    }
    SHELLEXECUTEINFOW run{sizeof run};
    run.fMask = SEE_MASK_NOASYNC;
    run.lpVerb = L"runas";  // the firewall is the administrator's: Windows asks
    run.lpFile = L"cmd.exe";
    run.lpParameters = command.c_str();
    run.nShow = SW_HIDE;
    ShellExecuteExW(&run);  // declined, it stays as it was; Windows may ask in its own way when the port opens
#else
    (void)ports;
#endif
}

WorkerScreen::WorkerScreen(Window& window, net::Worker& worker) : window_(window), worker_(worker) {
    port_.digits = true;
    port_.max = 5;
}

WorkerScreen::~WorkerScreen() {
    if (stopping_.joinable()) stopping_.join();
}

void WorkerScreen::tell(const std::string& note) {
    note_ = note;
    note_until_ = SDL_GetTicks() + 6000;
}

void WorkerScreen::hand_back(const std::string& note) {
    note_ = note;
    note_until_ = SDL_GetTicks() + 60000;
    status_ = worker_.status();
    Canvas canvas = window_.frame();
    draw(canvas);
    window_.present();
    SDL_PumpEvents();
}

WorkerScreen::Outcome WorkerScreen::run() {
    while (true) {
        const Uint32 frame_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window_.handle(event);
            if (event.type == SDL_QUIT) {
                if (stopping_.joinable()) stopping_.join();
                if (worker_.on()) {
                    hand_back("Handing the brains back to the main service...");
                    worker_.stop(false, std::chrono::seconds(20));  // a worker again when the app next opens
                }
                return Outcome::Quit;
            }
            if (dialog_) {
                if (const auto choice = dialog_->handle(event, window_.width(), window_.height())) {
                    dialog_.reset();
                    if (*choice == "stop" && !stopping_.joinable()) {
                        stopping_ = std::thread([this] { worker_.stop(true); });
                    }
                }
                continue;
            }
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_F11) {
                    window_.toggle_fullscreen();
                } else if (key == SDLK_BACKSPACE && focus_) {
                    focus_->backspace();
                } else if (key == SDLK_TAB && share_) {
                    std::vector<TextField*> all = {&port_};
                    for (TextField* field : share_->fields()) all.push_back(field);
                    const auto at = std::find(all.begin(), all.end(), focus_);
                    focus_ = all[at == all.end() ? 0 : static_cast<std::size_t>(at - all.begin() + 1) % all.size()];
                }
            } else if (event.type == SDL_TEXTINPUT && focus_) {
                focus_->type(event.text.text);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                click(event.button.x, event.button.y);
            }
        }
        if (stopping_.joinable() && !worker_.on()) {  // handed back: a main service again
            stopping_.join();
            return Outcome::Stopped;
        }
        status_ = worker_.status();
        if (!share_ && worker_.on()) {
            const net::Json& shared = status_["shared"];
            net::Resources resources;
            resources.threads = shared.value("threads", 1);
            resources.memory = shared.value("memory", std::uint64_t{0});
            resources.gpus = shared.value("gpus", std::vector<int>());
            share_.emplace(net::scan_machine(), resources);
            port_.text = std::to_string(status_.value("port", net::DEFAULT_WORKER_PORT));
        }
        Canvas canvas = window_.frame();
        draw(canvas);
        window_.present();
        const Uint32 spent = SDL_GetTicks() - frame_start;
        if (spent < 50) SDL_Delay(50 - spent);
    }
}

void WorkerScreen::click(int x, int y) {
    if (language_menu_.click(x, y)) return;
    for (const auto& [rect, url] : links_) {
        if (rect.contains(x, y)) {
            SDL_OpenURL(url.c_str());
            return;
        }
    }
    if (port_.rect.contains(x, y)) {
        focus_ = &port_;
        return;
    }
    if (share_) {
        if (TextField* field = share_->click(x, y)) {
            focus_ = field;
            return;
        }
    }
    if (stopping_.joinable()) return;
    if (change_button_.contains(x, y)) {
        try {
            worker_.move_to(port_.number().value_or(0));
            tell("Now listening on port " + port_.text + "; the main service follows.");
        } catch (const std::invalid_argument& error) {
            tell(error.what());
        }
    } else if (apply_button_.contains(x, y) && share_) {
        std::string problem;
        if (const auto shared = share_->read(problem)) {
            try {
                worker_.share(*shared);
                tell("Shared as chosen.");
            } catch (const std::invalid_argument& error) {
                tell(error.what());
            }
        } else {
            tell(problem);
        }
    } else if (stop_button_.contains(x, y)) {
        dialog_.emplace("Stop being a worker?",
                        std::vector<std::string>{"Its brains are handed back to the main service first, which takes a few seconds.",
                                                 "This app then runs worlds of its own again."},
                        std::vector<std::pair<std::string, std::string>>{{"stay", "CANCEL"}, {"stop", "STOP"}}, "stay");
    }
}

void WorkerScreen::draw(Canvas& canvas) {
    canvas.fill(BACK);
    const Rect card = Rect{0, 0, std::min(window_.width() - UI(24), UI(680)), std::min(window_.height() - UI(24), UI(720))}.center_at(
        window_.width() / 2, window_.height() / 2);
    canvas.rect(card, CARD, 0, UI(8));
    canvas.rect(card, EDGE, 1, UI(8));
    TTF_Font* text = UI.font(Face::Sans, 15);
    TTF_Font* small = UI.font(Face::Sans, 13);
    const int left = card.x + UI(28), width = card.w - UI(56);
    int y = card.y + UI(24);
    canvas.text(UI.font(Face::Sans, 26, true), "WORKER", BRIGHT, left, y);
    language_menu_.draw_button(canvas, card.right() - UI(24), y + UI(16));
    y += UI(50);

    const std::string main = status_.value("main", std::string());
    const bool leaving = status_.value("leaving", false) || stopping_.joinable();
    std::vector<std::string> lead;
    if (leaving) {
        lead = {"Handing the brains back to the main service..."};
    } else if (status_.value("connected", false)) {
        lead = wrapped_lines(text, "This Bitzkey is a worker, thinking for the main service at " + main + ".", width);
    } else if (!main.empty()) {
        lead = wrapped_lines(text, "This Bitzkey is a worker for the main service at " + main + ", which hasn't been in touch lately.", width);
    } else {
        lead = wrapped_lines(text, "This Bitzkey is a worker, waiting on port " + std::to_string(status_.value("port", 0)) +
                                       " for a main service to add it.", width);
    }
    for (const std::string& line : lead) {
        canvas.text(text, line, TEXT, left, y);
        y += UI(22);
    }
    y += UI(8);

    links_.clear();
    const std::string message = status_.value("message", std::string());
    if (!message.empty()) {
        canvas.text(small, "Message from the main service", DIM, left, y);
        y += UI(22);
        TTF_Font* font = UI.font(Face::Sans, 14);
        const AsShown untranslated;  // the main service's own words
        const std::vector<std::string> lines = wrapped_lines(font, message, width - UI(24));
        const Rect box{left, y, width, UI(16) + UI(20) * static_cast<int>(lines.size())};
        canvas.rect(box, {52, 56, 56}, 0, UI(5));
        int line_y = y + UI(8);
        static const std::regex url(R"(https?://[^\s<>"]+)");
        for (const std::string& line : lines) {
            // Each piece in turn: the links green and underlined, and clickable.
            int x = left + UI(12);
            auto begin = std::sregex_iterator(line.begin(), line.end(), url);
            std::size_t at = 0;
            for (auto it = begin; it != std::sregex_iterator(); ++it) {
                const std::string before = line.substr(at, static_cast<std::size_t>(it->position()) - at);
                x += canvas.text(font, before, TEXT, x, line_y);
                const std::string link = it->str();
                const int link_width = canvas.text(font, link, LINK, x, line_y);
                canvas.line(x, line_y + UI(17), x + link_width, line_y + UI(17), LINK);
                links_.push_back({Rect{x, line_y, link_width, UI(20)}, link});
                x += link_width;
                at = static_cast<std::size_t>(it->position() + it->length());
            }
            canvas.text(font, line.substr(at), TEXT, x, line_y);
            line_y += UI(20);
        }
        y = box.bottom() + UI(14);
    }

    for (const std::string& fact : {"Brains held: " + std::to_string(status_.value("held", 0)),
                                    "Thinking on " + status_.value("description", std::string()),
                                    "Speed: " + grouped(status_.value("speed", 0.0)) + " a second"}) {
        canvas.text(small, fact, DIM, left, y);
        y += UI(20);
    }
    y += UI(12);
    canvas.text(UI.font(Face::Sans, 13, true), "Shared with the main service", DIM, left, y);
    y += UI(26);
    canvas.text_at(UI.font(Face::Sans, 13), "Port", {175, 192, 172}, [&](Rect& box) { box.midleft_at(left, y + UI(19)); });
    port_.rect = {left + UI(130), y + UI(4), UI(100), UI(30)};
    port_.draw(canvas, focus_ == &port_);
    change_button_ = {port_.rect.right() + UI(10), y + UI(4), UI(100), UI(30)};
    draw_button(canvas, change_button_, "CHANGE");
    y += UI(40);
    if (share_) y += share_->draw(canvas, left, y, width, focus_);
    apply_button_ = Rect{0, 0, UI(110), UI(34)}.topright_at(card.right() - UI(28), y);
    draw_button(canvas, apply_button_, "APPLY");
    y += UI(44);
    if (SDL_GetTicks() < note_until_) canvas.text(UI.fitting(small, note_, width), note_, AMBER, left, y);

    stop_button_ = Rect{0, 0, UI(240), UI(42)}.midbottom_at(card.centerx(), card.bottom() - UI(24));
    draw_button(canvas, stop_button_, leaving ? "HANDING BACK..." : "STOP BEING A WORKER", Tone::Warning);
    if (dialog_) dialog_->draw(canvas);
    language_menu_.draw_list(canvas);
}

}  // namespace bitzkey::ui
