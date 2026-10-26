// Draw every screen off-screen and save it as a BMP, to check the interface without a person at
// the keyboard:  bitzkey_screens OUTPUT_FOLDER [LANGUAGE]

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

#include <SDL.h>
#include <SDL_ttf.h>

#include "ui/Advanced.hpp"
#include "ui/App.hpp"
#include "ui/CatalogScreen.hpp"
#include "ui/Designer.hpp"
#include "ui/Language.hpp"
#include "ui/Setup.hpp"
#include "ui/Window.hpp"
#include "ui/WorkerScreen.hpp"
#include "ui/WorkersScreen.hpp"

namespace bitzkey::ui {

struct Screens {
    static void save(Canvas& canvas, const std::filesystem::path& path) {
        SDL_SaveBMP(canvas.surface(), path.string().c_str());
        std::cout << "saved " << path.string() << "\n";
    }

    static void run(Window& window, const std::filesystem::path& folder) {
        Defaults defaults = {{"width", 100}, {"height", 100}, {"population", 90}, {"grass_density", 50}, {"ocean", 67},
                             {"seed", std::nullopt}, {"population_cap", std::nullopt}, {"mind_mutation", 50},
                             {"body_mutation", 50}, {"year_length", 360}};
        // Workers: a pool and a worker of its own, in a scratch folder, so the user's own are left alone.
        const auto scratch = std::filesystem::temp_directory_path() / "bitzkey-screens-workers";
        std::filesystem::remove_all(scratch);
        net::Pool pool(scratch / "main" / "workers.json");
        net::Worker worker(scratch / "worker" / "worker.json");
        SetupScreen setup(window, defaults, pool, worker);
        Canvas canvas = window.frame();
        setup.draw(canvas);
        save(canvas, folder / "setup.bmp");
        setup.ask_to_quit();
        setup.draw(canvas);
        save(canvas, folder / "setup-quit.bmp");
        setup.dialog_.reset();
        setup.language_menu_.click(setup.language_menu_.button().centerx(), setup.language_menu_.button().centery());
        setup.draw(canvas);
        save(canvas, folder / "setup-languages.bmp");
        setup.language_menu_.click(0, 0);  // closed again
        setup.rules_ = {{"CORPSE_TURNS", 60}};
        setup.draw(canvas);
        save(canvas, folder / "setup-advanced.bmp");

        AdvancedScreen advanced(window, setup.rules_);
        advanced.form_.select(2);
        advanced.show_focus();
        advanced.draw(canvas);
        save(canvas, folder / "advanced.bmp");
        advanced.form_.select(70);
        advanced.show_focus();
        advanced.form_.type_text("0.3");
        advanced.draw(canvas);
        save(canvas, folder / "advanced-mid.bmp");
        advanced.searching_ = true;  // searching, narrowed as it is typed
        advanced.search_ = "stamina";
        advanced.find();
        advanced.draw(canvas);
        save(canvas, folder / "advanced-search.bmp");
        advanced.search_ = "unicorns";
        advanced.find();
        advanced.draw(canvas);
        save(canvas, folder / "advanced-search-none.bmp");
        AdvancedScreen running(window, setup.rules_, true);  // opened from the map: this world's rules
        running.form_.select(4);
        running.show_focus();
        running.draw(canvas);
        save(canvas, folder / "advanced-world.bmp");

        Designer designer(window, encode_dna(Species::Predator, {{"size", 6}, {"strength", 5}, {"weapons", 7}, {"armour", 3},
                                                                 {"speed", 4}, {"sight", 5}, {"fertility", 4}, {"aggression", 6},
                                                                 {"fear", 1}, {"intellect", 5}, {"curiosity", 4}}));
        designer.form_.select(field_position({FieldKind::Body, Size}));
        designer.draw(canvas);
        save(canvas, folder / "designer.bmp");

        WorkersScreen becoming(window, pool, worker, nullptr);  // from the configuration panel
        becoming.draw(canvas);
        save(canvas, folder / "setup-worker.bmp");

        int port = 0;
        for (int tries = 0; tries < 40 && !port; ++tries) {
            try {
                worker.start(53100 + tries, net::Resources{2, std::uint64_t{1024} << 20, {}});
                port = 53100 + tries;
            } catch (const std::invalid_argument&) {
            }
        }
        {
            Simulation::Settings small;
            small.seed = 7;
            Simulation pooled(small);
            pool.add("127.0.0.1", port, "Thank you for thinking for us! The world: https://bitzkey.barriegamersociety.com/");
            pool.add("192.168.1.77", net::DEFAULT_WORKER_PORT, "");
            pool.adopt(pooled);
            for (int frame = 0; frame < 100; ++frame) {
                pool.tend(pooled);
                if (pool.list()["workers"][0]["state"] == "working") break;
                SDL_Delay(50);
            }
            for (int turn = 0; turn < 10; ++turn) {
                pooled.advance();
                pool.tend(pooled);
            }
            WorkersScreen workers(window, pool, worker, &pooled);
            workers.list_ = pool.list();
            workers.draw(canvas);
            save(canvas, folder / "workers.bmp");
            workers.share_.emplace(net::scan_machine(), net::Resources{4, std::uint64_t{8192} << 20, {0}});
            workers.draw(canvas);
            save(canvas, folder / "workers-become.bmp");
            WorkerScreen panel(window, worker);
            panel.status_ = worker.status();
            panel.share_.emplace(net::scan_machine(), net::Resources{2, std::uint64_t{1024} << 20, {}});
            panel.port_.text = std::to_string(port);
            panel.draw(canvas);
            save(canvas, folder / "worker.bmp");
            pool.remove("127.0.0.1", port);
            pool.remove("192.168.1.77", net::DEFAULT_WORKER_PORT);
            pool.adopt(pooled);
        }
        worker.stop(true, std::chrono::seconds(0));

        Simulation::Settings settings;
        settings.seed = 42;
        settings.ocean = DEFAULT_OCEAN;
        App app(window, settings, pool, worker);
        for (int turn = 0; turn < 150; ++turn) app.simulation_->advance();
        for (const auto& agent : app.simulation_->agents()) {
            if (agent->brain && agent->odds_offered) {
                app.selected_id_ = agent->identifier;
                break;
            }
        }
        app.toggle_export();
        const Uint64 start = SDL_GetPerformanceCounter();
        for (int frame = 0; frame < 20; ++frame) app.draw(canvas);
        const double ms = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency() / 20;
        std::cout << "drawing a frame with " << app.simulation_->living().size() << " alive: " << ms << " ms\n";
        save(canvas, folder / "app-inspector.bmp");
        app.open_death_report();
        app.draw(canvas);
        save(canvas, folder / "app-deaths.bmp");
        app.inspect_brains();
        app.draw(canvas);
        save(canvas, folder / "app-brains.bmp");
        app.report_.reset();
        app.exemplar_menu_ = 6;  // the right column's second card, its menu of statistics open
        app.draw(canvas);
        save(canvas, folder / "app-exemplar-menu.bmp");
        app.exemplar_menu_.reset();
        app.map_zoom_ = 4.0;  // zoomed in on the north-west of the map
        app.map_centre_ = std::pair{25.0, 30.0};
        app.draw(canvas);
        save(canvas, folder / "app-zoomed.bmp");
        app.map_zoom_ = 1.0;
        app.map_centre_.reset();
        app.globe_ = true;  // the globe, then zoomed in with the HUD hidden
        app.draw(canvas);
        save(canvas, folder / "app-globe.bmp");
        app.globe_zoom_ = 2.5;
        app.hud_hidden_ = true;
        app.draw(canvas);
        save(canvas, folder / "app-globe-full.bmp");
        app.globe_ = app.hud_hidden_ = false;
        app.globe_zoom_ = 1.0;
        CatalogScreen catalog_screen(window);  // filled here, so the user's own catalog is left alone
        catalog_screen.entries_.clear();
        catalog_screen.note_.clear();
        for (const Bitzkey* agent : app.simulation_->living()) {
            if (agent->brain && catalog_screen.entries_.size() < 7) {
                catalog_screen.entries_.push_back(catalog::entry_for(*agent, app.simulation_->turn()));
            }
        }
        catalog_screen.picked_ = 1;
        catalog_screen.draw(canvas);
        save(canvas, folder / "catalog.bmp");
        catalog_screen.entries_.push_back(catalog::design_entry(encode_dna(Species::Grazer, {{"size", 4}, {"speed", 6}})));
        catalog_screen.inspected_.clear();
        catalog_screen.picked_ = static_cast<int>(catalog_screen.entries_.size()) - 1;
        catalog_screen.draw(canvas);
        save(canvas, folder / "catalog-designed.bmp");
        app.show_insights_ = true;
        app.draw(canvas);
        save(canvas, folder / "app-insights.bmp");
        app.scroll_to(app.max_scroll() / 2 + 140);
        app.draw(canvas);
        save(canvas, folder / "app-insights-mid.bmp");
        app.scroll_to(1000000);
        app.draw(canvas);
        save(canvas, folder / "app-insights-end.bmp");
        app.show_insights_ = false;
        app.show_census_ = true;
        app.draw(canvas);
        save(canvas, folder / "app-graph.bmp");
        ColourGraph& graph = app.graph_;
        graph.sort_ = 8;  // intellect
        graph.menu_open_ = true;
        const Rect item = graph.menu_items()[8].second;
        graph.mouse_x_ = item.centerx();
        graph.mouse_y_ = item.centery();
        app.draw(canvas);
        save(canvas, folder / "app-graph-menu.bmp");
        graph.menu_open_ = false;
        const Bar& bar = graph.bars_[graph.bars_.size() - 12];
        const auto& [hovered, top, bottom] = bar.slices[bar.slices.size() / 2];
        graph.mouse_x_ = bar.left + 1;
        graph.mouse_y_ = static_cast<int>((top + bottom) / 2);
        app.draw(canvas);
        save(canvas, folder / "app-graph-tooltip.bmp");
        for (const Bar& any : graph.bars_) {  // and over a dead one: its genome and how it died
            const auto dead = std::find_if(any.slices.begin(), any.slices.end(),
                                           [&](const auto& slice) { return !graph.alive_.contains(std::get<0>(slice)); });
            if (dead == any.slices.end()) continue;
            graph.mouse_x_ = any.left + 1;
            graph.mouse_y_ = static_cast<int>((std::get<1>(*dead) + std::get<2>(*dead)) / 2);
            app.draw(canvas);
            save(canvas, folder / "app-graph-tooltip-dead.bmp");
            break;
        }
        std::set<int> predators;
        for (const Bitzkey* agent : app.simulation_->living()) {
            if (agent->species == Species::Predator) predators.insert(agent->identifier);
        }
        graph.select(predators, ColourGraph::Mode::Replace);
        app.close_graph();
        app.draw(canvas);
        save(canvas, folder / "app-graph-picked.bmp");
        app.show_census_ = false;
        app.ask_to_leave();
        app.draw(canvas);
        save(canvas, folder / "app-leave.bmp");
        app.dialog_.reset();
        graph.select({}, ColourGraph::Mode::Replace);
        while (app.simulation_->turn() < 380) app.simulation_->advance();  // year 3, a northern winter
        for (const Bitzkey* agent : app.simulation_->living()) {
            if (agent->species != Species::Plant) app.selected_id_ = agent->identifier;
        }
        app.draw(canvas);
        save(canvas, folder / "app-winter.bmp");
        app.show_census_ = true;
        app.draw(canvas);
        save(canvas, folder / "app-graph-years.bmp");
    }
};

}  // namespace bitzkey::ui

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: bitzkey_screens OUTPUT_FOLDER [LANGUAGE]\n";
        return 2;
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) {
        std::cerr << SDL_GetError() << "\n";
        return 1;
    }
    if (argc > 2) bitzkey::ui::set_language(argv[2], false);  // the user's own choice left alone
    {
        bitzkey::ui::Window window(false);
        bitzkey::ui::Screens::run(window, argv[1]);
    }
    bitzkey::ui::UI.close_fonts();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
