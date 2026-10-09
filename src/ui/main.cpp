// Bitzkey: configure a world, run it, and come back to the configuration panel until the user quits.

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include <SDL.h>
#include <SDL_ttf.h>

#include "core/GpuBrains.hpp"
#include "core/Rules.hpp"
#include "core/Simulation.hpp"
#include "ui/App.hpp"
#include "ui/Setup.hpp"
#include "ui/Ui.hpp"
#include "ui/Window.hpp"

using namespace bitzkey;
using namespace bitzkey::ui;

namespace {

struct Options {
    Defaults defaults = {{"width", GRID_SIZE},
                         {"height", GRID_SIZE},
                         {"population", INITIAL_POPULATION},
                         {"grass_density", static_cast<int>(GRASS_DENSITY)},
                         {"seed", std::nullopt},
                         {"population_cap", std::nullopt},
                         {"mind_mutation", static_cast<int>(100 * MIND_MUTATION_CHANCE)},
                         {"body_mutation", static_cast<int>(100 * BODY_MUTATION_CHANCE)},
                         {"year_length", DEFAULT_YEAR_LENGTH}};
    bool windowed = false;
};

Options parse(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        if (flag == "--windowed") {
            options.windowed = true;
            continue;
        }
        if (index + 1 >= argc || flag.rfind("--", 0) != 0) continue;
        std::string key = flag.substr(2);
        for (char& c : key) {
            if (c == '-') c = '_';
        }
        if (options.defaults.contains(key)) {
            try {
                options.defaults[key] = std::stoi(argv[++index]);
            } catch (const std::exception&) {
            }
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parse(argc, argv);
    if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bitzkey", SDL_GetError(), nullptr);
        return 1;
    }
    int status = 0;
    try {
        remove_stale_histories();  // left behind by a run that ended without closing its world
        if (gpu::available()) USE_GPU = 1;  // with a GPU, thinking on it is the default; before the rules' defaults are taken
        Window window(!options.windowed);
        SetupScreen setup(window, options.defaults);
        std::optional<Simulation::Settings> settings = setup.run();
        while (settings) {
            rules::apply(setup.rules());  // the advanced rules hold for this world and its restarts
            App app(window, *settings);
            const App::Result result = app.run();
            if (result == App::Result::Quit) break;
            // A new world with the Bitzkeys picked on the graph starts at once; otherwise, back to the panel.
            settings = result == App::Result::Renew ? setup.start_with(app.renewal()) : setup.run();
        }
    } catch (const std::exception& error) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bitzkey", error.what(), nullptr);
        status = 1;
    }
    UI.close_fonts();
    TTF_Quit();
    SDL_Quit();
    return status;
}
