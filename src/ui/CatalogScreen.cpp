#include "ui/CatalogScreen.hpp"

#include <algorithm>
#include <stdexcept>

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

CatalogScreen::CatalogScreen(Window& window) : window_(window) {
    try {
        entries_ = catalog::load(catalog_path());
    } catch (const std::exception& error) {
        note_ = error.what();
    }
}

std::pair<CatalogScreen::Outcome, std::optional<catalog::Entry>> CatalogScreen::run() {
    while (true) {
        const Uint32 frame_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window_.handle(event);
            if (event.type == SDL_QUIT) return {Outcome::Quit, std::nullopt};
            if (dialog_) {
                if (const auto choice = dialog_->handle(event, window_.width(), window_.height())) {
                    dialog_.reset();
                    if (*choice == "remove") remove();
                }
            } else if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) return {Outcome::Close, std::nullopt};
                if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && !entries_.empty()) {
                    return {Outcome::Import, entries_[static_cast<std::size_t>(picked_)]};
                }
                if (key == SDLK_F11) window_.toggle_fullscreen();
                else if (key == SDLK_UP || key == SDLK_DOWN) pick(picked_ + (key == SDLK_DOWN ? 1 : -1));
                else if (key == SDLK_DELETE && !entries_.empty()) ask_to_remove();
            } else if (event.type == SDL_MOUSEWHEEL) {
                scroll_to(scroll_ - event.wheel.y);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (const auto outcome = click(event.button.x, event.button.y)) {
                    if (*outcome == Outcome::Import) return {Outcome::Import, entries_[static_cast<std::size_t>(picked_)]};
                    return {*outcome, std::nullopt};
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
    places.details = {card.right() - UI(24) - UI(330), card.y + UI(96), UI(330), card.h - UI(170)};
    places.list = {card.x + UI(24), card.y + UI(96), places.details.x - card.x - UI(48), card.h - UI(170)};
    places.close = Rect{0, 0, UI(120), UI(36)}.bottomright_at(card.right() - UI(24), card.bottom() - UI(18));
    places.import = Rect{0, 0, UI(160), UI(36)}.bottomright_at(places.close.x - UI(10), places.close.bottom());
    places.remove = Rect{0, 0, UI(110), UI(36)}.bottomleft_at(card.x + UI(24), card.bottom() - UI(18));
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

void CatalogScreen::ask_to_remove() {
    dialog_.emplace("Remove from the catalog?",
                    std::vector<std::string>{catalog::describe(entries_[static_cast<std::size_t>(picked_)]),
                                             "It will be gone from the catalog for good."},
                    std::vector<std::pair<std::string, std::string>>{{"keep", "KEEP"}, {"remove", "REMOVE"}}, "keep");
}

void CatalogScreen::remove() {
    try {
        catalog::remove(static_cast<std::size_t>(picked_), catalog_path());
        entries_ = catalog::load(catalog_path());
        note_ = "Removed from the catalog.";
    } catch (const std::exception& error) {
        note_ = error.what();
    }
    inspected_.clear();
    pick(std::min(picked_, static_cast<int>(entries_.size()) - 1));
    scroll_to(scroll_);
}

const Bitzkey& CatalogScreen::inspect(int index) {
    auto found = inspected_.find(index);
    if (found == inspected_.end()) found = inspected_.emplace(index, catalog::inspected(entries_[static_cast<std::size_t>(index)])).first;
    return found->second;
}

std::optional<CatalogScreen::Outcome> CatalogScreen::click(int x, int y) {
    const Layout places = layout();
    if (places.close.contains(x, y)) return Outcome::Close;
    if (places.import.contains(x, y) && !entries_.empty()) return Outcome::Import;
    if (places.remove.contains(x, y) && !entries_.empty()) {
        ask_to_remove();
        return std::nullopt;
    }
    if (places.list.contains(x, y)) {
        const int last = std::min(static_cast<int>(entries_.size()), scroll_ + rows_shown());
        for (int index = scroll_; index < last; ++index) {
            if (row(index).contains(x, y)) picked_ = index;
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
    canvas.text(small, "NEW WORLD  /  YOUR FAVOURITES", {144, 170, 151}, card.x + UI(48) + title, card.y + UI(31));
    canvas.text(font, "Each one kept as it was when catalogued, its trained brain and all. Pick one to import.", {157, 177, 159},
                card.x + UI(24), card.y + UI(60));

    canvas.rect(list, {22, 30, 30}, 0, UI(6));
    if (entries_.empty()) {
        canvas.text(font, "The catalog is empty.", {157, 177, 159}, list.x + UI(16), list.y + UI(16));
        canvas.text(small, "Add Bitzkeys from a running world: ADD TO CATALOG in the sidebar,", {116, 139, 124}, list.x + UI(16),
                    list.y + UI(44));
        canvas.text(small, "the export list or the colour graph, or press K.", {116, 139, 124}, list.x + UI(16), list.y + UI(62));
    }
    const int last = std::min(static_cast<int>(entries_.size()), scroll_ + rows_shown());
    for (int index = scroll_; index < last; ++index) {
        const Rect place = row(index);
        if (index == picked_) {
            canvas.rect(place, {44, 57, 52}, 0, UI(4));
            canvas.rect(place, {131, 205, 143}, 1, UI(4));
        }
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

    for (const auto& [button, label, lit] : {std::tuple{places.remove, "REMOVE", false}, std::tuple{places.import, "ADD TO IMPORT", true},
                                            std::tuple{places.close, "CLOSE", false}}) {
        const bool active = lit && !entries_.empty();
        canvas.rect(button, active ? Color{131, 205, 143} : Color{44, 57, 52}, 0, UI(6));
        canvas.text_at(UI.font(Face::Sans, 13, true), label, active ? Color{19, 26, 27} : Color{220, 229, 211},
                       [&](Rect& box) { box.center_at(button.centerx(), button.centery()); });
    }
    const std::string hint = !note_.empty() ? note_ : "UP/DOWN  choose    ENTER  add to import    DELETE  remove    ESC  close";
    canvas.text(small, hint, !note_.empty() ? Color{238, 183, 110} : Color{116, 139, 124}, places.remove.right() + UI(16),
                places.remove.centery() - UI(8));
    if (dialog_) dialog_->draw(canvas);
}

}  // namespace bitzkey::ui
