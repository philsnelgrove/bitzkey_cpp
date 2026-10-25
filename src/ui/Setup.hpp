#pragma once
// The configuration panel shown before a world starts.

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Exchange.hpp"
#include "core/Rules.hpp"
#include "core/Simulation.hpp"
#include "net/Pool.hpp"
#include "net/Worker.hpp"
#include "ui/Canvas.hpp"
#include "ui/Dialog.hpp"
#include "ui/LanguageMenu.hpp"
#include "ui/Window.hpp"

namespace bitzkey::ui {

constexpr std::size_t MAX_IMPORT_BYTES = 5'000'000;
constexpr std::size_t MAX_DIGITS = 9;

// One adjustable launch setting.
struct Screens;

struct Setting {
    std::string_view key;
    std::string_view label;
    std::string_view hint;
    int minimum, maximum, step;
    bool optional = false;     // may be left blank, meaning "none"
    std::string_view blank;    // what a blank optional setting shows
};
extern const std::array<Setting, 10> SETTINGS;

using Defaults = std::map<std::string, std::optional<int>, std::less<>>;

// The panel's state, independent of drawing: one text value per setting, the import text, and
// which of them has focus (the import box comes after the settings).
class SetupForm {
public:
    explicit SetupForm(const Defaults& defaults);

    bool on_import() const { return focus == static_cast<int>(SETTINGS.size()); }
    const Setting* focused() const { return on_import() ? nullptr : &SETTINGS[static_cast<std::size_t>(focus)]; }
    std::optional<int> value(const Setting& setting) const;
    // The Bitzkeys in the import text, and an error message if it cannot all be read.
    const std::vector<Imported>& imports(std::string* error = nullptr) const;
    bool can_start() const;
    Simulation::Settings values() const;
    void select(int index);
    void move(int offset) { select(focus + offset); }
    void adjust(int steps);
    void type_text(const std::string& text);
    void backspace();
    void paste(const std::string& text);
    void load(const std::string& text);
    void clear_import() { import_text.clear(); }
    void add_design(Dna dna);

    std::map<std::string_view, std::string> texts;
    std::string import_text;
    int focus = 0;
    bool fresh = true;  // the next digit typed replaces the value rather than extending it

private:
    void settle(const Setting& setting);
    mutable std::string parsed_text_ = "\x01";  // the import text last parsed
    mutable std::vector<Imported> parsed_;
    mutable std::string parse_error_;
};

std::string import_summary(const std::vector<Imported>& bitzkeys);

// Draws the form and handles input until the user starts a world or quits.
class SetupScreen {
public:
    SetupScreen(Window& window, const Defaults& defaults, net::Pool& pool, net::Worker& worker);
    // The chosen settings, or none if the user quit or became a worker. Running it again, after a simulation,
    // starts from the same values.
    std::optional<Simulation::Settings> run();
    bool became_worker() const { return became_worker_; }  // whether run() ended so
    // Start a new world at once, with the settings as they were and these Bitzkeys alone in the import
    // box, as if pasted there; if they can't be read, wait on the panel as usual.
    std::optional<Simulation::Settings> start_with(const std::string& text);
    // The advanced rules changed from their defaults, for the worlds started from here.
    const rules::Values& rules() const { return rules_; }
    // The saved world loaded to carry on, when LOAD WORLD started the run; taken, it is gone.
    std::unique_ptr<Simulation> take_loaded() { return std::move(loaded_); }

private:
    friend struct Screens;  // the screenshot tool
    static constexpr int ROW_HEIGHT = 46;
    static constexpr int IMPORT_HEIGHT = 170;
    static constexpr int TEXT_LINES = 5;

    struct Layout {
        Rect card, box, catalog, design, paste, load, clear, start, quit, advanced, world, worker;
        std::vector<Rect> rows;
    };
    Layout layout() const;
    static std::array<Rect, 3> buttons(const Rect& row);
    enum class Click { None, Start, Quit, WindowClosed, World, Worker };
    Click click(int x, int y);
    void key(const SDL_Event& event);
    bool ready();
    void tell(const std::string& note);
    void ask_to_quit();
    bool design();    // false if the window was closed from the designer
    bool catalog();   // false if the window was closed from the catalog
    bool advanced();  // false if the window was closed from the advanced rules
    void paste_clipboard();
    void choose_file();
    void load(const std::filesystem::path& path);
    bool load_world();  // ask for a saved world and load it into `loaded_`; false if cancelled or it can't be read
    void draw(Canvas& canvas);
    void draw_setting(Canvas& canvas, const Setting& setting, const Rect& row, bool focused);
    void draw_import(Canvas& canvas, const Layout& places);

    Window& window_;
    net::Pool& pool_;
    net::Worker& worker_;
    bool became_worker_ = false;
    SetupForm form_;
    std::unique_ptr<Simulation> loaded_;
    std::string note_;
    Uint32 note_until_ = 0;
    std::optional<Dialog> dialog_;   // the quit confirmation, while it is open
    LanguageMenu language_menu_;
    std::optional<Dna> last_design_;  // the designer reopens on the last design applied
    rules::Values rules_;             // the advanced rules changed from their defaults
};

}  // namespace bitzkey::ui
