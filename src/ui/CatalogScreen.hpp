#pragma once
// The catalog of favourite Bitzkeys: each one listed, and the one picked shown large beside its
// inspector panel, as it was when catalogued, trained brain and all. Opened from the configuration
// panel; picking one adds it to the import list.

#include <filesystem>
#include <map>
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

    explicit CatalogScreen(Window& window);
    // A Bitzkey picked to import, or none when closed or the window closes.
    std::pair<Outcome, std::optional<catalog::Entry>> run();

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
    void ask_to_remove();
    void remove();
    const Bitzkey& inspect(int index);
    std::optional<Outcome> click(int x, int y);
    void draw(Canvas& canvas);

    Window& window_;
    std::vector<catalog::Entry> entries_;
    std::string note_;
    int picked_ = 0;  // the entry shown beside the list
    int scroll_ = 0;  // the first entry the list shows
    std::optional<Dialog> dialog_;
    std::map<int, Bitzkey> inspected_;
};

}  // namespace bitzkey::ui
