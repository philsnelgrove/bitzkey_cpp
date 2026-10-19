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
#include "net/Pool.hpp"
#include "net/Worker.hpp"
#include "ui/App.hpp"
#include "ui/Language.hpp"
#include "ui/Platform.hpp"
#include "ui/Setup.hpp"
#include "ui/Ui.hpp"
#include "ui/Window.hpp"
#include "ui/WorkerScreen.hpp"

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
        start_language();
        remove_stale_histories();  // left behind by a run that ended without closing its world
        if (gpu::available()) USE_GPU = 1;  // with a GPU, thinking on it is the default; before the rules' defaults are taken
        // Workers: those this app shares its worlds' thinking with, and worker mode, kept beside the reports.
        net::Pool pool(data_dir() / "workers.json");
        net::Worker worker(data_dir() / "worker.json");
        worker.on_ports = open_worker_ports;  // in Windows' firewall, as the user changes them
        worker.resume();                      // a worker when last closed: a worker again
        Window window(!options.windowed);
        // While a worker, the app shows only that; stopping, it comes back to the configuration panel.
        const auto working = [&] { return !worker.on() || WorkerScreen(window, worker).run() == WorkerScreen::Outcome::Stopped; };
        if (working()) {
            SetupScreen setup(window, options.defaults, pool, worker);
            // The panel, until the user starts a world or quits; becoming a worker there, the worker screen meanwhile.
            const auto configure = [&]() -> std::optional<Simulation::Settings> {
                for (;;) {
                    auto chosen = setup.run();
                    if (chosen || !setup.became_worker()) return chosen;
                    if (!working()) return std::nullopt;
                }
            };
            std::optional<Simulation::Settings> settings = configure();
            while (settings) {
                std::unique_ptr<Simulation> loaded = setup.take_loaded();
                if (!loaded) rules::apply(setup.rules());  // the advanced rules hold for this world and its restarts; a world loaded brought its own
                App::Result result;
                std::string renewal;
                {
                    App app(window, *settings, pool, worker, std::move(loaded));
                    result = app.run();
                    renewal = app.renewal();
                }  // the world let go, before it becomes a worker
                if (result == App::Result::Quit) break;
                if (result == App::Result::Worker && !working()) break;
                // A new world with the Bitzkeys picked on the graph starts at once; otherwise, back to the panel.
                settings = result == App::Result::Renew ? setup.start_with(renewal) : configure();
            }
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
