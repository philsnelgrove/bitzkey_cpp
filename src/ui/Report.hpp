#pragma once
// A report drawn over the screen: a title, a note, and lines of text in a fixed-width font, scrolled with the
// wheel when they don't all fit; it may have tabs, each a note and lines of its own, chosen by a click or LEFT/RIGHT.
// Esc, Enter or CLOSE closes it.

#include <string>
#include <vector>

#include <SDL.h>

#include "core/Insights.hpp"
#include "ui/Canvas.hpp"

namespace bitzkey::ui {

class Report {
public:
    struct Tab {
        std::string name, note;
        std::vector<std::string> lines;
    };
    Report(std::string title, std::string note, std::vector<std::string> lines)
        : title(std::move(title)), note(std::move(note)), lines(std::move(lines)) {}
    Report(std::string title, std::vector<Tab> tabs) : title(std::move(title)), tabs(std::move(tabs)) { choose(0); }

    // Whether an event closes it.
    bool handle(const SDL_Event& event, int width, int height);
    void draw(Canvas& canvas);

    std::string title, note;
    std::vector<std::string> lines;
    int scroll = 0;  // lines scrolled past
    std::vector<Tab> tabs;  // none for a report of one page
    int active = 0;
    void choose(int tab);  // that tab shown, from its top

private:
    struct Layout {
        Rect panel, body, close;
        std::vector<Rect> tabs;
    };
    Layout layout(int width, int height) const;
    int shown(int width, int height) const;
    void scroll_by(int lines_down, int width, int height);
};

// The death report and the brain matrix in the language shown: the tables' cells parted by tabs, drawn
// in columns as wide as their widest cell, so they line up in any script.
std::vector<std::string> death_report_table(const DeathTally& tally);
std::vector<std::string> brain_matrix_table(const Bitzkey& animal);
// The words, names and deed-words it holds (see LEXICON_SIZE), and its needs, goal and what it has learnt of them
// (see GOAL_JOY, DESIRE, OPENS_RATE), as of turn `turn`.
std::vector<std::string> lexicon_table(const Bitzkey& animal);
std::vector<std::string> goals_table(const Bitzkey& animal, int turn);

}  // namespace bitzkey::ui
