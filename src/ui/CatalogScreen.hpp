#pragma once
// The catalog of favourite Bitzkeys: each one listed, and the one picked shown large beside its
// inspector panel, as it was when catalogued, trained brain and all. Opened from the configuration
// panel, picking one adds it to the import list; opened from a running world to browse, it only shows
// them (and can remove them).

#include <filesystem>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Catalog.hpp"
#include "ui/Canvas.hpp"
#include "ui/Dialog.hpp"
#include "ui/Window.hpp"

namespace bitzkey::ui {

std::filesystem::path catalog_path();  // catalog.json beside exports/

class CatalogScreen {
public:
    enum class Outcome { Import, Close, Quit };

    explicit CatalogScreen(Window& window, bool browsing = false);
    // The Bitzkeys chosen to import, in catalog order, or none when closed or the window closes. They
    // are chosen as in Windows: a click chooses one, CTRL+click adds one or takes it out, SHIFT+click
    // chooses all from the last one clicked to this one (CTRL+SHIFT adds them), and CTRL+A all.
    std::pair<Outcome, std::vector<catalog::Entry>> run();

private:
    friend struct Screens;  // the screenshot tool
    struct Layout {
        Rect card, list, details, remove, import, close;
    };
    Layout layout() const;
    Rect row(int index) const;
    int rows_shown() const;
    void scroll_to(int first);
    void pick(int index);
    void choose(int index, Uint16 mods);  // as in Windows, by the modifier keys held
    std::vector<catalog::Entry> chosen_entries() const;
    void ask_to_remove();
    void remove();
    const Bitzkey& inspect(int index);
    std::optional<Outcome> click(int x, int y);
    void draw(Canvas& canvas);

    Window& window_;
    bool browsing_ = false;  // only to look through: nothing is imported
    std::vector<catalog::Entry> entries_;
    std::string note_;
    int picked_ = 0;  // the entry shown beside the list, the last one clicked or moved to
    int anchor_ = 0;  // where a SHIFT+click's run of entries starts
    std::set<int> chosen_;  // the entries to import or remove
    int scroll_ = 0;  // the first entry the list shows
    std::optional<Dialog> dialog_;
    std::map<int, Bitzkey> inspected_;
};

}  // namespace bitzkey::ui
