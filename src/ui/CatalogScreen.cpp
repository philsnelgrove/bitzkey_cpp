#include "ui/CatalogScreen.hpp"

#include <algorithm>
#include <stdexcept>

#include "ui/Designer.hpp"
#include "ui/Inspector.hpp"
#include "ui/Look.hpp"
#include "ui/Platform.hpp"
#include "ui/Ui.hpp"

namespace bitzkey::ui {

namespace {
constexpr int ROW_HEIGHT = 40;    // each entry's row in the list
constexpr int PREVIEW_ZOOM = 9;   // the large picture is the in-world look, this many times larger
}  // namespace

std::filesystem::path catalog_path() { return data_dir() / "catalog.json"; }

CatalogScreen::CatalogScreen(Window& window, bool browsing) : window_(window), browsing_(browsing) {
    try {
        entries_ = catalog::load(catalog_path());
    } catch (const std::exception& error) {
        note_ = error.what();
    }
}

std::pair<CatalogScreen::Outcome, std::vector<catalog::Entry>> CatalogScreen::run() {
    if (!entries_.empty() && chosen_.empty()) chosen_ = {0};
    while (true) {
        const Uint32 frame_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window_.handle(event);
            if (event.type == SDL_QUIT) return {Outcome::Quit, {}};
            if (dialog_) {
                if (const auto choice = dialog_->handle(event, window_.width(), window_.height())) {
                    dialog_.reset();
                    if (*choice == "remove") remove();
                }
            } else if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                const Uint16 mods = event.key.keysym.mod;
                if (key == SDLK_ESCAPE) return {Outcome::Close, {}};
                if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && !chosen_.empty()) {
                    return {browsing_ ? Outcome::Deploy : Outcome::Import, chosen_entries()};
                }
                if (key == SDLK_F11) window_.toggle_fullscreen();
                else if ((key == SDLK_UP || key == SDLK_DOWN) && !entries_.empty()) {
                    const int moved = std::max(0, std::min(static_cast<int>(entries_.size()) - 1, picked_ + (key == SDLK_DOWN ? 1 : -1)));
                    choose(moved, static_cast<Uint16>(mods & KMOD_SHIFT));
                } else if (key == SDLK_a && (mods & KMOD_CTRL)) {
                    for (int index = 0; index < static_cast<int>(entries_.size()); ++index) chosen_.insert(index);
                } else if (key == SDLK_DELETE && !chosen_.empty()) {
                    ask_to_remove();
                }
            } else if (event.type == SDL_MOUSEWHEEL) {
                scroll_to(scroll_ - event.wheel.y);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (const auto outcome = click(event.button.x, event.button.y)) {
                    if (*outcome == Outcome::Import || *outcome == Outcome::Deploy) return {*outcome, chosen_entries()};
                    return {*outcome, {}};
                }
            }
        }
        Canvas canvas = window_.frame();
        draw(canvas);
        window_.present();
        const Uint32 spent = SDL_GetTicks() - frame_start;
        if (spent < 33) SDL_Delay(33 - spent);
    }
}

CatalogScreen::Layout CatalogScreen::layout() const {
    UI.fit(window_.width(), window_.height());
    Layout places;
    places.card = Rect{0, 0, std::min(UI(940), window_.width() - UI(40)), std::min(UI(760), window_.height() - UI(40))}
                      .center_at(window_.width() / 2, window_.height() / 2);
    const Rect& card = places.card;
    places.details = {card.right() - UI(24) - UI(330), card.y + UI(96), UI(330), card.h - UI(186)};
    places.list = {card.x + UI(24), card.y + UI(96), places.details.x - card.x - UI(48), card.h - UI(186)};
    places.close = Rect{0, 0, UI(120), UI(36)}.bottomright_at(card.right() - UI(24), card.bottom() - UI(18));
    places.import = Rect{0, 0, UI(180), UI(36)}.bottomright_at(places.close.x - UI(10), places.close.bottom());
    places.remove = Rect{0, 0, UI(110), UI(36)}.bottomleft_at(card.x + UI(24), card.bottom() - UI(18));
    places.design = Rect{0, 0, UI(110), UI(36)}.bottomleft_at(places.remove.right() + UI(10), places.remove.bottom());
    return places;
}

int CatalogScreen::rows_shown() const { return std::max(1, layout().list.h / UI(ROW_HEIGHT)); }

Rect CatalogScreen::row(int index) const {
    const Rect list = layout().list;
    return {list.x, list.y + (index - scroll_) * UI(ROW_HEIGHT), list.w, UI(ROW_HEIGHT) - UI(4)};
}

void CatalogScreen::scroll_to(int first) {
    scroll_ = std::max(0, std::min(static_cast<int>(entries_.size()) - rows_shown(), first));
}

void CatalogScreen::pick(int index) {
    if (entries_.empty()) return;
    picked_ = std::max(0, std::min(static_cast<int>(entries_.size()) - 1, index));
    if (picked_ < scroll_) scroll_to(picked_);  // keep it in view
    else if (picked_ >= scroll_ + rows_shown()) scroll_to(picked_ - rows_shown() + 1);
}

void CatalogScreen::choose(int index, Uint16 mods) {
    if (mods & KMOD_SHIFT) {
        std::set<int> run;
        for (int at = std::min(anchor_, index); at <= std::max(anchor_, index); ++at) run.insert(at);
        if (mods & KMOD_CTRL) chosen_.insert(run.begin(), run.end());
        else chosen_ = run;
    } else if (mods & KMOD_CTRL) {
        if (!chosen_.erase(index)) chosen_.insert(index);
        anchor_ = index;
    } else {
        chosen_ = {index};
        anchor_ = index;
    }
    pick(index);
}

std::vector<catalog::Entry> CatalogScreen::chosen_entries() const {
    std::vector<catalog::Entry> found;
    for (const int index : chosen_) found.push_back(entries_[static_cast<std::size_t>(index)]);
    return found;
}

void CatalogScreen::ask_to_remove() {
    const std::size_t count = chosen_.size();
    dialog_.emplace("Remove from the catalog?",
                    std::vector<std::string>{count == 1 ? catalog::describe(entries_[static_cast<std::size_t>(*chosen_.begin())])
                                                        : std::to_string(count) + " Bitzkeys",
                                             count == 1 ? "It will be gone from the catalog for good."
                                                        : "They will be gone from the catalog for good."},
                    std::vector<std::pair<std::string, std::string>>{{"keep", "KEEP"}, {"remove", "REMOVE"}}, "keep");
}

void CatalogScreen::remove() {
    try {
        for (auto index = chosen_.rbegin(); index != chosen_.rend(); ++index) {  // the last first, so the rest keep their places
            catalog::remove(static_cast<std::size_t>(*index), catalog_path());
        }
        entries_ = catalog::load(catalog_path());
        note_ = chosen_.size() > 1 ? "Removed " + std::to_string(chosen_.size()) + " from the catalog." : "Removed from the catalog.";
    } catch (const std::exception& error) {
        note_ = error.what();
    }
    inspected_.clear();
    pick(std::min(picked_, static_cast<int>(entries_.size()) - 1));
    chosen_.clear();
    if (!entries_.empty()) chosen_.insert(picked_);
    anchor_ = picked_;
    scroll_to(scroll_);
}

bool CatalogScreen::design() {
    const std::optional<Dna> dna = entries_.empty() ? std::nullopt : std::optional(inspect(picked_).dna);
    const auto [outcome, designed] = Designer(window_, dna).run();
    if (outcome == Designer::Outcome::Quit) return false;
    if (outcome == Designer::Outcome::Apply && designed) {
        try {
            catalog::add({catalog::design_entry(*designed)}, catalog_path());
            entries_ = catalog::load(catalog_path());
            note_ = "Designed and added to the catalog.";
        } catch (const std::exception& error) {
            note_ = error.what();
        }
        inspected_.clear();
        if (!entries_.empty()) choose(static_cast<int>(entries_.size()) - 1, 0);
    }
    return true;
}

const Bitzkey& CatalogScreen::inspect(int index) {
    auto found = inspected_.find(index);
    if (found == inspected_.end()) found = inspected_.emplace(index, catalog::inspected(entries_[static_cast<std::size_t>(index)])).first;
    return found->second;
}

std::optional<CatalogScreen::Outcome> CatalogScreen::click(int x, int y) {
    const Layout places = layout();
    if (places.close.contains(x, y)) return Outcome::Close;
    if (places.import.contains(x, y) && !chosen_.empty()) return browsing_ ? Outcome::Deploy : Outcome::Import;  // browsing, it deploys
    if (places.remove.contains(x, y) && !chosen_.empty()) {
        ask_to_remove();
        return std::nullopt;
    }
    if (places.design.contains(x, y)) return design() ? std::nullopt : std::optional(Outcome::Quit);
    if (places.list.contains(x, y)) {
        const int last = std::min(static_cast<int>(entries_.size()), scroll_ + rows_shown());
        for (int index = scroll_; index < last; ++index) {
            if (row(index).contains(x, y)) choose(index, static_cast<Uint16>(SDL_GetModState()));
        }
    }
    return std::nullopt;
}

void CatalogScreen::draw(Canvas& canvas) {
    const Layout places = layout();
    const Rect &card = places.card, &list = places.list, &details = places.details;
    TTF_Font* font = UI.font(Face::Sans, 15);
    TTF_Font* small = UI.font(Face::Sans, 12);
    canvas.fill({19, 26, 27});
    canvas.rect(card, {27, 37, 37}, 0, UI(8));
    canvas.rect(card, {80, 99, 85}, 1, UI(8));
    const int title = canvas.text(UI.font(Face::Sans, 22, true), "BITZKEY CATALOG", {229, 239, 220}, card.x + UI(24), card.y + UI(22));
    canvas.text(small, browsing_ ? "YOUR FAVOURITES" : "NEW WORLD  /  YOUR FAVOURITES", {144, 170, 151}, card.x + UI(48) + title,
                card.y + UI(31));
    canvas.text(font,
                browsing_ ? "Each one kept as it was when catalogued, its trained brain and all."
                          : "Each one kept as it was when catalogued, its trained brain and all. Choose those to import.",
                {157, 177, 159}, card.x + UI(24), card.y + UI(60));

    canvas.rect(list, {22, 30, 30}, 0, UI(6));
    if (entries_.empty()) {
        canvas.text(font, "The catalog is empty.", {157, 177, 159}, list.x + UI(16), list.y + UI(16));
        const std::vector<std::string> lines = wrapped_lines(
            small, "Add Bitzkeys from a running world: ADD TO CATALOG in the sidebar, the export list or the colour graph, or press K.",
            list.w - UI(32));
        for (std::size_t index = 0; index < lines.size(); ++index) {
            canvas.text(small, lines[index], {116, 139, 124}, list.x + UI(16), list.y + UI(44 + 18 * static_cast<int>(index)));
        }
    }
    const int last = std::min(static_cast<int>(entries_.size()), scroll_ + rows_shown());
    for (int index = scroll_; index < last; ++index) {
        const Rect place = row(index);
        if (chosen_.contains(index)) canvas.rect(place, {44, 57, 52}, 0, UI(4));
        if (index == picked_) canvas.rect(place, {131, 205, 143}, 1, UI(4));  // the one shown beside the list
        const Bitzkey& agent = inspect(index);
        draw_bitzkey(canvas, agent.dna, place.x + UI(20), place.centery(), dot_radius(agent.body[Size]));
        canvas.text(font, catalog::describe(entries_[static_cast<std::size_t>(index)]), {220, 229, 211}, place.x + UI(42),
                    place.centery() - UI(9));
    }
    if (static_cast<int>(entries_.size()) > rows_shown()) {
        canvas.text(small,
                    std::to_string(scroll_ + 1) + "-" + std::to_string(last) + " of " + std::to_string(entries_.size()) +
                        "   (wheel or UP/DOWN to scroll)",
                    {116, 139, 124}, list.x, list.bottom() + UI(6));
    }

    canvas.rect(details, {22, 30, 30}, 0, UI(6));
    if (!entries_.empty()) {
        const Bitzkey& agent = inspect(picked_);
        draw_bitzkey(canvas, agent.dna, details.centerx(), details.y + UI(70), UI(PREVIEW_ZOOM * dot_radius(agent.body[Size])),
                     UI(PREVIEW_ZOOM * RING_WIDTH));
        const int inside_x = details.x + UI(12), inside_w = details.w - UI(24);
        draw_details(canvas, agent, inside_x, [&](int offset) { return details.y + UI(150 + offset); }, inside_w);
    }

    const std::string adding = browsing_ ? (chosen_.size() <= 1 ? "DEPLOY" : "DEPLOY " + std::to_string(chosen_.size()))  // onto the map
                               : chosen_.size() <= 1 ? "ADD TO IMPORT" : "ADD " + std::to_string(chosen_.size()) + " TO IMPORT";
    for (const auto& [button, label, lit] : {std::tuple{places.remove, std::string("REMOVE"), false}, std::tuple{places.design, std::string("DESIGNER"), false},
                                            std::tuple{places.import, adding, true},
                                            std::tuple{places.close, std::string("CLOSE"), false}}) {
        const bool active = lit && !chosen_.empty();
        canvas.rect(button, active ? Color{131, 205, 143} : Color{44, 57, 52}, 0, UI(6));
        canvas.label(UI.font(Face::Sans, 13, true), label, active ? Color{19, 26, 27} : Color{220, 229, 211}, button);
    }
    const std::string hint = !note_.empty() ? note_
                             : browsing_    ? "CLICK, SHIFT or CTRL+CLICK  choose    ENTER  deploy    DELETE  remove    ESC  close"
                                            : "CLICK, SHIFT or CTRL+CLICK  choose    ENTER  add to import    DELETE  remove    ESC  close";
    canvas.text_at(small, hint, !note_.empty() ? Color{238, 183, 110} : Color{116, 139, 124}, [&](Rect& box) {  // above the buttons
        box.x = places.card.right() - UI(24) - box.w;
        box.y = places.list.bottom() + UI(6);
    });
    if (dialog_) dialog_->draw(canvas);
}

}  // namespace bitzkey::ui
