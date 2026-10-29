#pragma once
// A whole world's report: everything the probe tells of it, gathered in one go, and read for what it says of the
// world. How many of each species live and how that has gone, their condition, what kills them, their brains, their
// herds and where those stand against the climate, the climate and grass, and the rules. What stands out is pulled
// together as findings at the top.
//
// A section's lines are text, some of them marked: "# " a heading, "! " a finding, "~" a table's heading row; a table's
// cells are parted by tabs.

#include <map>
#include <string>
#include <vector>

#include "report/Source.hpp"

namespace bitzkey::report {

// What the probe answered, route by route; a route that failed (an older app without it, say) is left null.
struct World {
    std::string name;
    Json state, settings, rules, history, deaths, insights, climate, animals, minds;
    std::map<std::string, Json> herds;  // by species
    std::string brain_report, death_report;
    std::vector<std::string> missing;  // the routes that failed, and why
};

// Every route a report reads. Throws std::runtime_error if even the world's state can't be had.
World gather(Source& source);

struct Section {
    std::string title;
    std::vector<std::string> lines;
};

struct Analysis {
    std::vector<Section> sections;  // the summary first
};

Analysis analyse(const World& world);

// The report as plain text, its tables lined up in spaces: for the clipboard and for a file.
std::string as_text(const Analysis& analysis);

// The table cells of a line, parted by tabs.
std::vector<std::string> cells_of(const std::string& line);

}  // namespace bitzkey::report
