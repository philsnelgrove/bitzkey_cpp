// Draw every screen off-screen and save it as a BMP, to check the interface without a person at
// the keyboard:  bitzkey_screens OUTPUT_FOLDER [WIDTH HEIGHT]

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <SDL.h>
#include <SDL_ttf.h>

#include "ui/Advanced.hpp"
#include "ui/App.hpp"
#include "ui/Designer.hpp"
#include "ui/Setup.hpp"
#include "ui/Window.hpp"

namespace bitzkey::ui {

struct Screens {
    static void save(Canvas& canvas, const std::filesystem::path& path) {
        SDL_SaveBMP(canvas.surface(), path.string().c_str());
        std::cout << "saved " << path.string() << "\n";
    }

    static void run(Window& window, const std::filesystem::path& folder) {
        Defaults defaults = {{"width", 100}, {"height", 100}, {"population", 90}, {"grass_density", 50},
                             {"seed", std::nullopt}, {"population_cap", std::nullopt}, {"mind_mutation", 50},
                             {"body_mutation", 50}};
        SetupScreen setup(window, defaults);
        Canvas canvas = window.frame();
        setup.draw(canvas);
        save(canvas, folder / "setup.bmp");
        setup.ask_to_quit();
        setup.draw(canvas);
        save(canvas, folder / "setup-quit.bmp");
        setup.dialog_.reset();
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

        Designer designer(window, encode_dna(Species::Predator, {{"size", 6}, {"strength", 5}, {"weapons", 7}, {"armour", 3},
                                                                 {"speed", 4}, {"sight", 5}, {"fertility", 4}, {"aggression", 6},
                                                                 {"fear", 1}, {"intellect", 5}, {"curiosity", 4}}));
        designer.form_.select(field_position({FieldKind::Body, Size}));
        designer.draw(canvas);
        save(canvas, folder / "designer.bmp");

        Simulation::Settings settings;
        settings.seed = 42;
        App app(window, settings);
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
        app.show_census_ = false;
        app.ask_to_leave();
        app.draw(canvas);
        save(canvas, folder / "app-leave.bmp");
    }
};

}  // namespace bitzkey::ui

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: bitzkey_screens OUTPUT_FOLDER\n";
        return 2;
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) {
        std::cerr << SDL_GetError() << "\n";
        return 1;
    }
    {
        bitzkey::ui::Window window(false);
        bitzkey::ui::Screens::run(window, argv[1]);
    }
    bitzkey::ui::UI.close_fonts();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
