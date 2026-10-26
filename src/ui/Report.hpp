#pragma once
// A report drawn over the screen: a title, a note, and lines of text in a fixed-width font, scrolled with the
// wheel when they don't all fit. Esc, Enter or CLOSE closes it.

#include <string>
#include <vector>

#include <SDL.h>

#include "core/Insights.hpp"
#include "ui/Canvas.hpp"

namespace bitzkey::ui {

class Report {
public:
    Report(std::string title, std::string note, std::vector<std::string> lines)
        : title(std::move(title)), note(std::move(note)), lines(std::move(lines)) {}

    // Whether an event closes it.
    bool handle(const SDL_Event& event, int width, int height);
    void draw(Canvas& canvas);

    std::string title, note;
    std::vector<std::string> lines;
    int scroll = 0;  // lines scrolled past

private:
    struct Layout {
        Rect panel, body, close;
    };
    Layout layout(int width, int height) const;
    int shown(int width, int height) const;
    void scroll_by(int lines_down, int width, int height);
};

// The death report and the brain matrix in the language shown: the tables' cells parted by tabs, drawn
// in columns as wide as their widest cell, so they line up in any script.
std::vector<std::string> death_report_table(const DeathTally& tally);
std::vector<std::string> brain_matrix_table(const Bitzkey& animal);

}  // namespace bitzkey::ui
