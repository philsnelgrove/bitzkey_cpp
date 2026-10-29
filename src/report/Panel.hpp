#pragma once
// The reporting panel: a whole world's report, read from a save or from a probe port, a tab for each part of it. A
// save is opened by OPEN SAVE or dropped on the window; a running world by its probe's port, as the app serves it
// (--probe PORT, or probe.json) or as the daemon does. A running world is read again by REFRESH, or every
// LIVE_SECONDS while LIVE is on, and its herds then show how they have drifted. COPY puts the whole report on the
// clipboard; SAVE writes it beside the app's other reports.

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>

#include "report/Analysis.hpp"
#include "report/Source.hpp"
#include "ui/Canvas.hpp"
#include "ui/Fields.hpp"
#include "ui/Window.hpp"

namespace bitzkey::report {

constexpr int LIVE_SECONDS = 10;

class Panel {
public:
    explicit Panel(ui::Window& window);
    ~Panel();
    // What to read first, from the command line: a save's path, or a probe's address.
    void open_save(const std::filesystem::path& path);
    void connect(const std::string& address);
    void run();  // until the window closes

private:
    struct Layout {
        ui::Rect open, address, connect, refresh, live, copy, save;
        std::vector<ui::Rect> tabs;
        ui::Rect chart, body;
    };
    Layout layout() const;
    void draw(ui::Canvas& canvas);
    void draw_chart(ui::Canvas& canvas, const ui::Rect& area);
    void draw_lines(ui::Canvas& canvas, const ui::Rect& area);
    void click(int x, int y);
    void key(const SDL_Keysym& key);
    void scroll_by(int lines);
    int lines_shown() const;

    // Reading happens on a thread of its own, so a large save or a slow probe never stalls the window.
    void begin(std::function<std::unique_ptr<Source>()> make, const std::string& doing);
    void collect();  // a reading done: its report shown
    void refresh();
    void copy();
    void save();
    void tell(const std::string& note, bool trouble = false);

    ui::Window& window_;
    std::unique_ptr<Source> source_;
    std::optional<World> world_;
    Analysis analysis_;
    std::size_t tab_ = 0;
    std::vector<int> scrolls_;  // lines scrolled past, tab by tab
    int across_ = 0;            // pixels scrolled right, for long lines

    ui::TextField address_;
    bool focused_ = false;
    bool live_ = false;
    Uint32 last_read_ = 0;

    std::thread reader_;
    std::atomic<bool> busy_ = false;
    std::mutex done_mutex_;
    bool done_ = false;
    std::optional<World> read_;
    std::unique_ptr<Source> read_source_;
    std::string read_error_;
    std::string doing_;

    std::string note_;
    bool trouble_ = false;
    Uint32 note_until_ = 0;
};

}  // namespace bitzkey::report
