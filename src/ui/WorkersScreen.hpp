#pragma once
// The workers a world shares its thinking with, opened from the map: other Bitzkeys, this app or the Linux daemon, in
// worker mode. Each is listed with its state, ping, what it shares, its speed and the brains it holds; workers are added
// by IP address (and port, if not 50000) and removed, their brains brought back first. From here this app can become a
// worker itself.

#include <optional>
#include <string>
#include <vector>

#include "core/Simulation.hpp"
#include "net/Pool.hpp"
#include "net/Worker.hpp"
#include "ui/Dialog.hpp"
#include "ui/Fields.hpp"
#include "ui/LanguageMenu.hpp"
#include "ui/Window.hpp"

namespace bitzkey::ui {

struct Screens;

class WorkersScreen {
public:
    enum class Outcome { Close, Quit, Worker };  // Worker: it is one now, its world to be let go

    // Without a world (from the configuration panel), it opens on becoming a worker, and goes no further.
    WorkersScreen(Window& window, net::Pool& pool, net::Worker& worker, Simulation* simulation);
    Outcome run();

private:
    friend struct Screens;  // the screenshot tool
    struct Row {
        std::string host;
        int port;
        Rect remove;
    };

    void draw(Canvas& canvas);
    void draw_list(Canvas& canvas, const Rect& card, int& y);
    void draw_becoming(Canvas& canvas, const Rect& card, int& y);
    std::optional<Outcome> click(int x, int y);
    std::optional<Outcome> answer(const std::string& choice);
    void add();
    void tell(const std::string& note);
    void start_becoming();  // the form for becoming a worker, filled with half of what this computer has
    std::vector<TextField*> fields();
    Rect card() const;

    Window& window_;
    net::Pool& pool_;
    net::Worker& worker_;
    Simulation* simulation_;  // none from the configuration panel
    net::Json list_;
    std::vector<Row> rows_;
    TextField host_, port_, message_, checkpoint_, worker_port_;
    TextField* focus_ = nullptr;
    std::optional<ShareForm> share_;  // while becoming a worker
    std::optional<Dialog> dialog_;
    std::string dialog_kind_;
    std::optional<std::pair<std::string, int>> removing_;
    LanguageMenu language_menu_;
    Rect add_button_{}, set_button_{}, become_button_{}, close_button_{}, start_button_{}, cancel_button_{};
    std::string note_;
    Uint32 note_until_ = 0;
};

}  // namespace bitzkey::ui
