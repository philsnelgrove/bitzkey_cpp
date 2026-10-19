#pragma once
// While this app is a worker, all it shows: a gray panel saying so, the main service it thinks for and that service's
// message (its links open in the browser), the port it listens on and what it shares, each changeable, and a way to
// stop being a worker, its brains handed back first. Closing the window hands them back too; the app opens as a
// worker again next time.

#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "net/Worker.hpp"
#include "ui/Dialog.hpp"
#include "ui/Fields.hpp"
#include "ui/LanguageMenu.hpp"
#include "ui/Window.hpp"

namespace bitzkey::ui {

struct Screens;

class WorkerScreen {
public:
    enum class Outcome { Stopped, Quit };  // Stopped: a main service again, back to the configuration panel

    WorkerScreen(Window& window, net::Worker& worker);
    ~WorkerScreen();
    Outcome run();

private:
    friend struct Screens;  // the screenshot tool
    void draw(Canvas& canvas);
    void click(int x, int y);
    void tell(const std::string& note);
    void hand_back(const std::string& note);  // draw that the brains are going back, while they go

    Window& window_;
    net::Worker& worker_;
    net::Json status_;
    TextField port_;
    std::optional<ShareForm> share_;
    TextField* focus_ = nullptr;
    std::optional<Dialog> dialog_;
    LanguageMenu language_menu_;
    std::vector<std::pair<Rect, std::string>> links_;  // in the main service's message
    Rect change_button_{}, apply_button_{}, stop_button_{};
    std::thread stopping_;
    std::string note_;
    Uint32 note_until_ = 0;
};

// Open these ports in Windows' firewall for a worker, and close any it opened before (none: close them all). Windows
// asks the user to approve it. Nothing elsewhere.
void open_worker_ports(const std::vector<int>& ports);

}  // namespace bitzkey::ui
