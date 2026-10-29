// Bitzkey Reports: a whole world's report, from a save or a running world's probe.
//   bitzkey_report [WORLD.bzworld | --probe PORT | --probe HOST:PORT] [--fullscreen] [--text FILE]
// With --text, no window: the report is written to FILE as text, and it ends.

#include <string>

#include <SDL.h>
#include <SDL_ttf.h>

#include "report/Analysis.hpp"
#include "report/Panel.hpp"
#include "ui/Language.hpp"
#include "ui/Platform.hpp"
#include "ui/Ui.hpp"
#include "ui/Window.hpp"

using namespace bitzkey;

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bitzkey Reports", SDL_GetError(), nullptr);
        return 1;
    }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    int status = 0;
    try {
        ui::start_language();  // the app's fonts for its language; the reports themselves are in English
        bool fullscreen = false;
        std::string save, probe, text;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--fullscreen") fullscreen = true;
            else if (argument == "--probe" && index + 1 < argc) probe = argv[++index];
            else if (argument == "--text" && index + 1 < argc) text = argv[++index];
            else if (argument.rfind("--", 0) != 0) save = argument;
        }
        if (!text.empty()) {
            if (save.empty() == probe.empty()) throw std::runtime_error("--text reports on a save or a --probe: give one");
            const auto source = save.empty() ? report::Source::port(probe) : report::Source::save(ui::path_from_utf8(save));
            ui::write_file(ui::path_from_utf8(text), report::as_text(report::analyse(report::gather(*source))));
        } else {
            ui::Window window(fullscreen, "Bitzkey Reports");
            report::Panel panel(window);
            if (!save.empty()) panel.open_save(ui::path_from_utf8(save));
            else if (!probe.empty()) panel.connect(probe);
            panel.run();
        }
    } catch (const std::exception& error) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bitzkey Reports", error.what(), nullptr);
        status = 1;
    }
    ui::UI.close_fonts();
    TTF_Quit();
    SDL_Quit();
    return status;
}
