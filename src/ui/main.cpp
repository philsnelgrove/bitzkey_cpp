// Bitzkey: configure a world, run it, and come back to the configuration panel until the user quits.

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include <SDL.h>
#include <SDL_ttf.h>

#include "core/Rules.hpp"
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
                         {"body_mutation", static_cast<int>(100 * BODY_MUTATION_CHANCE)}};
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
        Window window(!options.windowed);
        SetupScreen setup(window, options.defaults);
        while (const auto settings = setup.run()) {
            rules::apply(setup.rules());  // the advanced rules hold for this world and its restarts
            App app(window, *settings);
            if (app.run() == App::Result::Quit) break;
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
