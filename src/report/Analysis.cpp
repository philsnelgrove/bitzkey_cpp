#include "report/Analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace bitzkey::report {

namespace {

constexpr std::array<const char*, 3> ANIMAL_NAMES = {"grazer", "omnivore", "predator"};
constexpr std::array<const char*, 4> ALL_NAMES = {"plant", "grazer", "omnivore", "predator"};
constexpr std::array<const char*, 4> TEMPERATURE_NAMES = {"hot", "medium", "cold", "very cold"};
constexpr std::size_t HISTORY_POINTS = 400;
constexpr std::size_t DEATHS_READ = 5000;
constexpr int CLIMATE_BANDS = 12;
constexpr std::size_t HERDS_LISTED = 12;
// What stands out: a species this far below its peak, a cause this large a share of a species' deaths, a brain value
// running away in this share of a species, neurons jammed in this share, and hunger or health this far gone.
constexpr double CRASH = 0.5;
constexpr int CRASH_PEAK = 20;  // a species that never reached this many isn't said to crash
constexpr double DOMINANT_CAUSE = 0.5;
constexpr double RUNAWAY_SHARE = 0.25;
constexpr double JAMMED_SHARE = 0.2;
constexpr double HUNGRY = 0.75;  // of a full hunger, or stamina, of 100
constexpr double FULL = 100;
constexpr double FRAIL = 0.5;
constexpr double OFF_HOME = 0.5;  // a herd this far from its home temperature is away from home

std::string format(const char* pattern, auto... values) {
    char buffer[512];
    std::snprintf(buffer, sizeof buffer, pattern, values...);
    return buffer;
}

std::string percent(double share) { return format("%.0f%%", 100 * share); }
std::string number(double value, int places = 1) { return format("%.*f", places, value); }

std::string capital(std::string word) {
    if (!word.empty()) word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
    return word;
}
std::string plural(const std::string& species) { return species + "s"; }

std::string row(std::initializer_list<std::string> cells) {
    std::string line;
    for (const std::string& cell : cells) line += (line.empty() ? "" : "\t") + cell;
    return line;
}
std::string row(const std::vector<std::string>& cells) {
    std::string line;
    for (std::size_t index = 0; index < cells.size(); ++index) line += (index ? "\t" : "") + cells[index];
    return line;
}

double number_in(const Json& value, const char* key, double fallback = 0) {
    if (!value.is_object() || !value.contains(key) || !value[key].is_number()) return fallback;
    return value[key].get<double>();
}

// A row of the map's temperature now, 0 hot to 3 very cold, as a herd's home is measured.
double temperature_at(const Json& climate, double y) {
    if (!climate.is_object() || !climate.contains("rows") || climate["rows"].empty()) return 0;
    const Json& rows = climate["rows"];
    const auto at = static_cast<std::size_t>(std::clamp<long>(std::lround(y), 0, static_cast<long>(rows.size()) - 1));
    return number_in(rows[at], "temperature");
}

std::string temperature_name(double temperature) {
    return TEMPERATURE_NAMES[static_cast<std::size_t>(std::clamp<long>(std::lround(temperature), 0, 3))];
}

struct Findings {
    std::vector<std::string> lines;
    void add(const std::string& line) { lines.push_back("! " + line); }
};

// ---- the sections ----

Section population(const World& world, Findings& findings) {
    Section section{"Population", {}};
    auto& lines = section.lines;
    const Json& alive = world.state["alive"];
    std::map<std::string, std::vector<std::pair<int, int>>> series;  // by species: (turn, alive)
    if (world.history.is_object()) {
        for (const Json& point : world.history["history"]) {
            for (const char* species : ALL_NAMES) series[species].emplace_back(point.value("turn", 0), point["alive"].value(species, 0));
        }
    }
    lines.push_back("# Numbers now and over the run");
    lines.push_back(row({"~SPECIES", "ALIVE", "PEAK", "PEAK TURN", "LOW SINCE", "LAST QUARTER", "SWINGS"}));
    for (const char* species : ALL_NAMES) {
        const int now = alive.value(species, 0);
        const auto& points = series[species];
        if (points.empty()) {
            lines.push_back(row({capital(species), std::to_string(now), "", "", "", "", ""}));
            continue;
        }
        auto peak = *std::max_element(points.begin(), points.end(), [](const auto& p, const auto& q) { return p.second < q.second; });
        if (now > peak.second) peak = {world.state.value("turn", 0), now};  // the history's last point may lag the turn
        int low = now;
        for (const auto& [turn, count] : points) {
            if (turn >= peak.first) low = std::min(low, count);
        }
        const int quarter = points[points.size() * 3 / 4].second;
        const double change = quarter > 0 ? static_cast<double>(now - quarter) / quarter : 0;
        // Swings: how often the numbers cross their mean, over the run, two crossings to a rise and fall.
        double mean = 0;
        for (const auto& [_, count] : points) mean += count;
        mean /= static_cast<double>(points.size());
        int crossings = 0;
        for (std::size_t at = 1; at < points.size(); ++at) crossings += (points[at - 1].second < mean) != (points[at].second < mean);
        lines.push_back(row({capital(species), std::to_string(now), std::to_string(peak.second), std::to_string(peak.first),
                             std::to_string(low), quarter > 0 ? (change >= 0 ? "+" : "") + percent(change) : "", std::to_string(crossings / 2)}));
        if (std::string(species) == "plant") continue;
        if (now == 0 && peak.second > 0) {
            findings.add(format("%s have died out: their peak was %d at turn %d.", capital(plural(species)).c_str(), peak.second, peak.first));
        } else if (peak.second >= CRASH_PEAK && now < (1 - CRASH) * peak.second) {
            findings.add(format("%s are down to %d, %s below their peak of %d at turn %d.", capital(plural(species)).c_str(), now,
                                percent(1 - static_cast<double>(now) / peak.second).c_str(), peak.second, peak.first));
        } else if (quarter >= CRASH_PEAK && change <= -CRASH) {
            findings.add(format("%s are falling fast: %s over the last quarter of the run.", capital(plural(species)).c_str(), percent(change).c_str()));
        }
    }
    const int grazers = alive.value("grazer", 0), omnivores = alive.value("omnivore", 0), predators = alive.value("predator", 0);
    lines.push_back("");
    if (predators > 0) {
        lines.push_back(format("Prey to each predator: %.1f (%d grazers and omnivores to %d predators).",
                               static_cast<double>(grazers + omnivores) / predators, grazers + omnivores, predators));
    }
    if (world.state.contains("infants")) {
        lines.push_back("");
        lines.push_back("# The young");
        lines.push_back(row({"~SPECIES", "INFANTS", "SHARE", "BESIDE A PARENT"}));
        for (const char* species : ANIMAL_NAMES) {
            const Json& young = world.state["infants"][species];
            const int infants = young.value("infants", 0), sheltered = young.value("sheltered", 0), count = alive.value(species, 0);
            lines.push_back(row({capital(species), std::to_string(infants), count ? percent(static_cast<double>(infants) / count) : "",
                                 infants ? percent(static_cast<double>(sheltered) / infants) : ""}));
        }
    }
    if (world.state.contains("by_terrain")) {
        lines.push_back("");
        lines.push_back("# Where they live");
        std::vector<std::string> heading = {"~SPECIES"};
        std::vector<std::string> terrains;
        for (const auto& [terrain, _] : world.state["by_terrain"]["grazer"].items()) {
            terrains.push_back(terrain);
            heading.push_back(terrain);
        }
        for (std::string& cell : heading) std::transform(cell.begin(), cell.end(), cell.begin(), [](unsigned char c) { return std::toupper(c); });
        heading[0] = "~SPECIES";
        lines.push_back(row(heading));
        for (const char* species : ALL_NAMES) {
            std::vector<std::string> cells = {capital(species)};
            for (const std::string& terrain : terrains) cells.push_back(std::to_string(world.state["by_terrain"][species].value(terrain, 0)));
            lines.push_back(row(cells));
        }
    }
    return section;
}

Section condition(const World& world, Findings& findings) {
    Section section{"Condition", {}};
    auto& lines = section.lines;
    struct Tally {
        int count = 0;
        double age = 0, health = 0, hunger = 0, stamina = 0;
        int nocturnal = 0, wet = 0;
        std::map<std::string, int> actions;
    };
    std::map<std::string, Tally> tallies;
    if (!world.animals.is_object()) {
        lines.push_back("The probe gave no Bitzkeys to read.");
        return section;
    }
    for (const Json& animal : world.animals["animals"]) {
        Tally& tally = tallies[animal.value("species", "")];
        ++tally.count;
        tally.age += number_in(animal, "age");
        tally.health += number_in(animal, "health");
        tally.hunger += number_in(animal, "hunger");
        tally.stamina += number_in(animal, "stamina");
        tally.nocturnal += animal.value("nocturnal", false);
        tally.wet += animal.value("wet", false);
        tally.actions[animal.value("action", "")] += 1;
    }
    lines.push_back("# How they are");
    lines.push_back(row({"~SPECIES", "READ", "MEAN AGE", "HEALTH", "HUNGER", "STAMINA", "NOCTURNAL", "WET"}));
    for (const char* species : ANIMAL_NAMES) {
        const Tally& tally = tallies[species];
        if (!tally.count) {
            lines.push_back(row({capital(species), "0", "", "", "", "", "", ""}));
            continue;
        }
        const double n = tally.count;
        lines.push_back(row({capital(species), std::to_string(tally.count), number(tally.age / n, 0), percent(tally.health / n),
                             percent(tally.hunger / n / FULL), percent(tally.stamina / n / FULL), percent(tally.nocturnal / n), percent(tally.wet / n)}));
        if (tally.count >= 10 && tally.hunger / n / FULL > HUNGRY) findings.add(format("%s are hungry: their hunger is %s on average.", capital(plural(species)).c_str(), percent(tally.hunger / n / FULL).c_str()));
        if (tally.count >= 10 && tally.health / n < FRAIL) findings.add(format("%s are in poor health: %s on average.", capital(plural(species)).c_str(), percent(tally.health / n).c_str()));
    }
    if (const std::size_t matched = world.animals.value("matched", std::size_t{0}); matched > world.animals["animals"].size()) {
        lines.push_back(format("(%zu of %zu read)", world.animals["animals"].size(), matched));
    }
    lines.push_back("");
    lines.push_back("# What they did last turn");
    for (const char* species : ANIMAL_NAMES) {
        const Tally& tally = tallies[species];
        if (!tally.count) continue;
        std::vector<std::pair<int, std::string>> ranked;
        for (const auto& [action, count] : tally.actions) ranked.emplace_back(count, action);
        std::sort(ranked.rbegin(), ranked.rend());
        std::string text;
        for (std::size_t at = 0; at < std::min<std::size_t>(8, ranked.size()); ++at) {
            text += (text.empty() ? "" : ", ") + ranked[at].second + " " + percent(static_cast<double>(ranked[at].first) / tally.count);
        }
        lines.push_back(capital(plural(species)) + ": " + text);
    }
    return section;
}

Section deaths(const World& world, Findings& findings) {
    Section section{"Deaths", {}};
    auto& lines = section.lines;
    if (!world.deaths.is_object()) {
        lines.push_back("The probe gave no deaths to read.");
        return section;
    }
    const Json& totals = world.deaths["since_the_start"];
    std::set<std::string> causes;
    for (const char* species : ANIMAL_NAMES) {
        for (const auto& [cause, _] : totals[species].items()) causes.insert(cause);
    }
    std::map<std::string, int> all_of;
    for (const char* species : ANIMAL_NAMES) {
        for (const auto& [cause, count] : totals[species].items()) all_of[species] += count.get<int>();
    }
    lines.push_back("# Every death since the start, by cause");
    lines.push_back(row({"~CAUSE", "GRAZER", "", "OMNIVORE", "", "PREDATOR", ""}));
    std::vector<std::pair<int, std::string>> ordered;
    for (const std::string& cause : causes) {
        int total = 0;
        for (const char* species : ANIMAL_NAMES) total += totals[species].value(cause, 0);
        ordered.emplace_back(total, cause);
    }
    std::sort(ordered.rbegin(), ordered.rend());
    for (const auto& [_, cause] : ordered) {
        std::vector<std::string> cells = {cause};
        for (const char* species : ANIMAL_NAMES) {
            const int count = totals[species].value(cause, 0);
            cells.push_back(std::to_string(count));
            cells.push_back(all_of[species] ? percent(static_cast<double>(count) / all_of[species]) : "");
        }
        lines.push_back(row(cells));
    }
    lines.push_back(row({"all", std::to_string(all_of["grazer"]), "", std::to_string(all_of["omnivore"]), "", std::to_string(all_of["predator"]), ""}));
    for (const char* species : ANIMAL_NAMES) {
        if (all_of[species] < 20) continue;
        for (const auto& [cause, count] : totals[species].items()) {
            const double share = static_cast<double>(count.get<int>()) / all_of[species];
            if (share > DOMINANT_CAUSE) findings.add(format("%s of %s deaths have been by %s.", percent(share).c_str(), species, cause.c_str()));
        }
    }

    // Lately: the deaths the probe listed, latest first.
    const Json& listed = world.deaths["deaths"];
    if (listed.empty()) return section;
    const int latest = listed.front().value("turn", 0), earliest = listed.back().value("turn", 0);
    const int span = std::max(1, latest - earliest + 1);
    std::map<std::string, std::map<std::string, int>> recent;  // by species, then cause
    std::map<std::string, std::map<std::string, int>> whom;    // by killer species, then the dead's species
    std::map<std::string, std::map<std::string, int>> doing;   // by killer species, then what it was doing
    std::map<std::string, int> recent_of;
    for (const Json& death : listed) {
        const std::string species = death.value("species", "");
        recent[species][death.value("cause", "")] += 1;
        recent_of[species] += 1;
        if (death.contains("killer_species")) {
            whom[death["killer_species"].get<std::string>()][species] += 1;
            doing[death["killer_species"].get<std::string>()][death.value("activity", "")] += 1;
        }
    }
    lines.push_back("");
    lines.push_back(format("# Lately: the last %zu deaths, turns %d to %d", listed.size(), earliest, latest));
    lines.push_back(row({"~SPECIES", "DIED", "A TURN", "A TURN PER 1,000", "CHIEF CAUSES"}));
    for (const char* species : ANIMAL_NAMES) {
        const int died = recent_of[species];
        const int living = world.state["alive"].value(species, 0);
        std::vector<std::pair<int, std::string>> ranked;
        for (const auto& [cause, count] : recent[species]) ranked.emplace_back(count, cause);
        std::sort(ranked.rbegin(), ranked.rend());
        std::string chief;
        for (std::size_t at = 0; at < std::min<std::size_t>(3, ranked.size()); ++at) {
            chief += (chief.empty() ? "" : ", ") + ranked[at].second + " " + percent(static_cast<double>(ranked[at].first) / died);
        }
        const double rate = static_cast<double>(died) / span;
        lines.push_back(row({capital(species), std::to_string(died), number(rate, 2), living ? number(1000 * rate / living, 2) : "", chief}));
    }
    if (!whom.empty()) {
        lines.push_back("");
        lines.push_back("# Who killed whom lately (killers in rows)");
        lines.push_back(row({"~KILLER", "PLANTS", "GRAZERS", "OMNIVORES", "PREDATORS", "WHILE"}));
        for (const char* killer : ANIMAL_NAMES) {
            if (!whom.contains(killer)) continue;
            std::vector<std::pair<int, std::string>> ranked;
            int kills = 0;
            for (const auto& [what, count] : doing[killer]) {
                ranked.emplace_back(count, what);
                kills += count;
            }
            std::sort(ranked.rbegin(), ranked.rend());
            std::string text;
            for (std::size_t at = 0; at < std::min<std::size_t>(3, ranked.size()); ++at) {
                text += (text.empty() ? "" : ", ") + ranked[at].second + " " + percent(static_cast<double>(ranked[at].first) / kills);
            }
            lines.push_back(row({capital(plural(killer)), std::to_string(whom[killer]["plant"]), std::to_string(whom[killer]["grazer"]), std::to_string(whom[killer]["omnivore"]),
                                 std::to_string(whom[killer]["predator"]), text}));
        }
        if (whom["predator"]["predator"] > 0 && whom["predator"]["predator"] * 4 > whom["predator"]["grazer"] + whom["predator"]["omnivore"]) {
            findings.add(format("Predators are turning on their own kind: %d of their recent kills were predators.", whom["predator"]["predator"]));
        }
    }
    return section;
}

Section brains(const World& world, Findings& findings) {
    Section section{"Brains", {}};
    auto& lines = section.lines;
    if (world.minds.is_object()) {
        const double runaway = number_in(world.minds, "runaway_value", 50), jammed_at = number_in(world.minds, "jammed_at", 0.99);
        lines.push_back("# Their brains at a glance");
        lines.push_back(format("The farthest value from nothing each brain gives an action, for what it last sensed; neurons beyond %.2f are jammed.", jammed_at));
        lines.push_back(row({"~SPECIES", "THINKING", "DEPTH", "WIDTH", "MEDIAN VALUE", "90TH", "HIGHEST", format("BEYOND %.0f", runaway), "JAMMED"}));
        for (const char* species : ANIMAL_NAMES) {
            const Json& mind = world.minds["minds"][species];
            const double valued = number_in(mind, "valued"), neurons = number_in(mind, "neurons");
            const double runaway_share = valued ? number_in(mind, "runaway") / valued : 0, jammed_share = neurons ? number_in(mind, "jammed") / neurons : 0;
            lines.push_back(row({capital(species), std::to_string(static_cast<int>(number_in(mind, "thinking"))), number(number_in(mind, "depth")),
                                 number(number_in(mind, "width"), 0), number(number_in(mind, "value_median")), number(number_in(mind, "value_p90")),
                                 number(number_in(mind, "value_max")), valued ? percent(runaway_share) : "", neurons ? percent(jammed_share) : ""}));
            if (valued >= 10 && runaway_share > RUNAWAY_SHARE) {
                findings.add(format("%s of %s value an action beyond %.0f: their brains are running away.", percent(runaway_share).c_str(), plural(species).c_str(), runaway));
            }
            if (neurons >= 50 && jammed_share > JAMMED_SHARE) {
                findings.add(format("%s of %s' neurons are jammed at either end.", percent(jammed_share).c_str(), species));
            }
        }
        lines.push_back("");
    } else {
        lines.push_back("(This probe has no minds route: brain values and jammed neurons need a newer Bitzkey.)");
        lines.push_back("");
    }
    if (world.insights.is_object()) {
        const Json& insights = world.insights["insights"];
        std::set<std::string> actions;
        for (const char* species : ANIMAL_NAMES) {
            for (const auto& [action, _] : insights[species].items()) actions.insert(action);
        }
        lines.push_back("# How often each action is taken when it is open, lately");
        lines.push_back(row({"~ACTION", "GRAZER", "OMNIVORE", "PREDATOR"}));
        for (const std::string& action : actions) {
            std::vector<std::string> cells = {action};
            for (const char* species : ANIMAL_NAMES) {
                const Json& rate = insights[species];
                cells.push_back(rate.contains(action) && rate[action]["taken"].is_number() ? percent(rate[action]["taken"].get<double>()) : "");
            }
            lines.push_back(row(cells));
        }
    }
    return section;
}

Section herds(const World& world, Findings& findings) {
    Section section{"Herds & migration", {}};
    auto& lines = section.lines;
    lines.push_back("# The herds as they stand");
    lines.push_back("A herd is herd-mates within two steps of one another, and theirs. Away from home: its middle's row is half a band or more");
    lines.push_back("from the herd's mean home temperature. Drift is since the last look, so it shows from the second look of a running world.");
    lines.push_back(row({"~SPECIES", "HERDS", "ALONE", "IN HERDS", "LARGEST", "MEAN SIZE", "MEAN SPREAD", "AWAY FROM HOME", "MOVED", "TOWARD HOME"}));
    bool any_drift = false;
    std::vector<std::pair<std::string, Json>> listed;
    for (const char* species : ANIMAL_NAMES) {
        const auto found = world.herds.find(species);
        if (found == world.herds.end() || !found->second.is_object()) continue;
        const Json& herds = found->second["herds"];
        const int alone = found->second.value("alone", 0);
        int members = 0, largest = 0, away_members = 0, moved = 0, toward = 0;
        double spread = 0;
        for (const Json& herd : herds) {
            const int size = herd.value("size", 0);
            members += size;
            largest = std::max(largest, size);
            spread += number_in(herd, "spread") * size;
            const double y = number_in(herd, "y"), home = number_in(herd, "home");
            const double off = temperature_at(world.climate, y) - home;
            const bool away = std::abs(off) >= OFF_HOME;
            if (away) away_members += size;
            if (herd.contains("drift") && herd["drift"].is_array()) {
                any_drift = true;
                const double dy = herd["drift"][1].get<double>();
                if (away && std::abs(dy) >= 0.5) {
                    ++moved;
                    toward += std::abs(temperature_at(world.climate, y + dy) - home) < std::abs(off);
                }
            }
            if (listed.size() < HERDS_LISTED * 3) listed.emplace_back(species, herd);
        }
        const int count = static_cast<int>(herds.size());
        lines.push_back(row({capital(species), std::to_string(count), std::to_string(alone),
                             members + alone ? percent(static_cast<double>(members) / (members + alone)) : "", std::to_string(largest),
                             count ? number(static_cast<double>(members) / count) : "", members ? number(spread / members) : "",
                             members ? percent(static_cast<double>(away_members) / members) : "", any_drift ? std::to_string(moved) : "",
                             moved ? percent(static_cast<double>(toward) / moved) : ""}));
        if (members >= 20 && static_cast<double>(away_members) / members > 0.5) {
            findings.add(format("Most %s in herds (%s) are away from their home temperature.", plural(species).c_str(),
                                percent(static_cast<double>(away_members) / members).c_str()));
        }
        if (moved >= 5) {
            findings.add(format("Of the %s herds away from home that moved, %s moved toward it.", species, percent(static_cast<double>(toward) / moved).c_str()));
        }
    }
    if (!any_drift) lines.push_back("(No drift yet: it shows on a running world's second look. Press REFRESH, or turn on LIVE.)");
    lines.push_back("");
    lines.push_back("# The largest herds");
    lines.push_back(row({"~SPECIES", "HERD", "SIZE", "COLUMN", "ROW", "SPREAD", "HOME", "HERE", "DRIFT", "HISTORY"}));
    std::stable_sort(listed.begin(), listed.end(), [](const auto& p, const auto& q) { return p.second.value("size", 0) > q.second.value("size", 0); });
    for (std::size_t at = 0; at < std::min(HERDS_LISTED, listed.size()); ++at) {
        const auto& [species, herd] = listed[at];
        const double y = number_in(herd, "y");
        std::string drift, past;
        if (herd.contains("drift") && herd["drift"].is_array()) drift = format("%+d, %+.1f", herd["drift"][0].get<int>(), herd["drift"][1].get<double>());
        if (herd.contains("split_from")) past += "split from " + herd["split_from"].dump();
        if (herd.contains("merged_from")) past += (past.empty() ? "" : "; ") + std::string("joined by ") + herd["merged_from"].dump();
        lines.push_back(row({capital(species), "#" + std::to_string(herd.value("id", 0)), std::to_string(herd.value("size", 0)),
                             number(number_in(herd, "x"), 0), number(y, 0), number(number_in(herd, "spread")),
                             number(number_in(herd, "home"), 2), number(temperature_at(world.climate, y), 2), drift, past}));
    }
    return section;
}

Section climate(const World& world) {
    Section section{"Climate & grass", {}};
    auto& lines = section.lines;
    if (!world.climate.is_object()) {
        lines.push_back("The probe gave no climate to read.");
        return section;
    }
    const Json& rows = world.climate["rows"];
    const int height = static_cast<int>(rows.size());
    if (!height) return section;
    const int bands = std::min(CLIMATE_BANDS, height);
    std::vector<std::array<int, 3>> living(static_cast<std::size_t>(bands));  // by band, then animal species
    const auto band_of = [&](int y) { return std::clamp(y * bands / height, 0, bands - 1); };
    if (world.animals.is_object()) {
        for (const Json& animal : world.animals["animals"]) {
            const std::string species = animal.value("species", "");
            for (std::size_t kind = 0; kind < ANIMAL_NAMES.size(); ++kind) {
                if (species == ANIMAL_NAMES[kind]) living[static_cast<std::size_t>(band_of(animal.value("y", 0)))][kind] += 1;
            }
        }
    }
    lines.push_back(format("# The map from north to south, in %d bands; the season: %s", bands, world.climate.value("season", "").c_str()));
    lines.push_back("Temperatures run 0 hot to 3 very cold; usual is the zone's temperature with no season.");
    lines.push_back(row({"~ROWS", "NOW", "USUAL", "", "GROWTH", "GRASS", "SEA", "GRAZERS", "OMNIVORES", "PREDATORS"}));
    for (int band = 0; band < bands; ++band) {
        const int from = band * height / bands, to = (band + 1) * height / bands;
        double now = 0, usual = 0, growth = 0;
        int grass = 0, sea = 0;
        for (int y = from; y < to; ++y) {
            const Json& line = rows[static_cast<std::size_t>(y)];
            now += number_in(line, "temperature");
            usual += number_in(line, "zone");
            growth += number_in(line, "growth");
            grass += line.value("grass", 0);
            sea += line.value("sea", 0);
        }
        const double n = std::max(1, to - from);
        const auto& here = living[static_cast<std::size_t>(band)];
        lines.push_back(row({format("%d-%d", from, to - 1), number(now / n, 2), number(usual / n, 2), temperature_name(now / n), number(growth / n, 2),
                             std::to_string(grass), std::to_string(sea), std::to_string(here[0]), std::to_string(here[1]), std::to_string(here[2])}));
    }
    return section;
}

Section rules(const World& world) {
    Section section{"Rules & settings", {}};
    auto& lines = section.lines;
    if (world.settings.is_object()) {
        lines.push_back("# The world's settings");
        for (const auto& [key, value] : world.settings.items()) lines.push_back(row({key, value.is_string() ? value.get<std::string>() : value.dump()}));
        lines.push_back("");
    }
    if (!world.rules.is_object()) return section;
    std::map<std::string, std::vector<std::pair<std::string, double>>> grouped;
    for (const auto& [key, rule] : world.rules["rules"].items()) grouped[rule.value("group", "")].emplace_back(rule.value("label", key), number_in(rule, "value"));
    for (auto& [group, entries] : grouped) {
        lines.push_back("# " + group);
        for (const auto& [label, value] : entries) {
            lines.push_back(row({label, value == std::floor(value) && std::abs(value) < 1e9 ? format("%.0f", value) : format("%g", value)}));
        }
        lines.push_back("");
    }
    return section;
}

Section from_text(const std::string& title, const std::string& text) {
    Section section{title, {}};
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        section.lines.push_back(line);
    }
    if (section.lines.empty()) section.lines.push_back("The probe gave no " + title + ".");
    return section;
}

}  // namespace

std::vector<std::string> cells_of(const std::string& line) {
    std::vector<std::string> cells;
    for (std::size_t start = 0;;) {
        const std::size_t end = line.find('\t', start);
        cells.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) return cells;
        start = end + 1;
    }
}

World gather(Source& source) {
    World world;
    world.name = source.name();
    world.state = source.get("state");  // without the state, there's no world to report
    const auto read = [&](Json& into, const std::string& route) {
        try {
            into = source.get(route);
        } catch (const std::exception& error) {
            world.missing.push_back(error.what());
        }
    };
    read(world.settings, "settings");
    read(world.rules, "rules");
    read(world.history, "history?points=" + std::to_string(HISTORY_POINTS));
    read(world.deaths, "deaths?limit=" + std::to_string(DEATHS_READ));
    read(world.insights, "insights");
    read(world.climate, "climate");
    read(world.animals, "animals?limit=1000000");
    read(world.minds, "minds");
    for (const char* species : ANIMAL_NAMES) read(world.herds[species], std::string("herds?species=") + species);
    for (auto [into, route] : {std::pair{&world.brain_report, "reports/brain"}, std::pair{&world.death_report, "reports/deaths"}}) {
        try {
            *into = source.text(route);
        } catch (const std::exception& error) {
            world.missing.push_back(error.what());
        }
    }
    return world;
}

Analysis analyse(const World& world) {
    Findings findings;
    std::vector<Section> sections = {population(world, findings), condition(world, findings), deaths(world, findings),
                                     brains(world, findings),     herds(world, findings),     climate(world),
                                     rules(world),                from_text("Brain report", world.brain_report),
                                     from_text("Death report", world.death_report)};
    Section summary{"Summary", {}};
    auto& lines = summary.lines;
    const Json& state = world.state;
    lines.push_back("# " + world.name);
    lines.push_back(format("Turn %d, year %d, season %s. The map is %d by %d.", state.value("turn", 0), state.value("year", 0),
                           state.value("season", "").c_str(), state.value("width", 0), state.value("height", 0)));
    if (world.settings.is_object() && world.settings.value("ended", false)) lines.push_back("! This world has ended.");
    lines.push_back("");
    lines.push_back(row({"~", "PLANTS", "GRAZERS", "OMNIVORES", "PREDATORS"}));
    lines.push_back(row({"Alive", std::to_string(state["alive"].value("plant", 0)), std::to_string(state["alive"].value("grazer", 0)),
                         std::to_string(state["alive"].value("omnivore", 0)), std::to_string(state["alive"].value("predator", 0))}));
    lines.push_back("");
    lines.push_back("# What stands out");
    if (findings.lines.empty()) lines.push_back("Nothing stands out: every species is near its usual numbers, and nothing is going wrong.");
    lines.insert(lines.end(), findings.lines.begin(), findings.lines.end());
    if (!world.missing.empty()) {
        lines.push_back("");
        lines.push_back("# Not read");
        lines.insert(lines.end(), world.missing.begin(), world.missing.end());
    }
    Analysis analysis;
    analysis.sections.push_back(std::move(summary));
    for (Section& section : sections) analysis.sections.push_back(std::move(section));
    return analysis;
}

std::string as_text(const Analysis& analysis) {
    std::string text;
    for (const Section& section : analysis.sections) {
        const std::string title = "== " + section.title + " ==";
        text += title + "\n\n";
        // Each run of table lines lined up by its own columns.
        const auto& lines = section.lines;
        for (std::size_t at = 0; at < lines.size();) {
            if (lines[at].find('\t') == std::string::npos) {
                const std::string& line = lines[at++];
                text += (line.rfind("# ", 0) == 0 ? line.substr(2) : line) + "\n";
                continue;
            }
            std::size_t end = at;
            std::vector<std::size_t> widths;
            for (; end < lines.size() && lines[end].find('\t') != std::string::npos; ++end) {
                std::vector<std::string> cells = cells_of(lines[end]);
                if (!cells[0].empty() && cells[0][0] == '~') cells[0].erase(0, 1);
                widths.resize(std::max(widths.size(), cells.size()));
                for (std::size_t c = 0; c < cells.size(); ++c) widths[c] = std::max(widths[c], cells[c].size());
            }
            for (; at < end; ++at) {
                std::vector<std::string> cells = cells_of(lines[at]);
                if (!cells[0].empty() && cells[0][0] == '~') cells[0].erase(0, 1);
                std::string line;
                for (std::size_t c = 0; c < cells.size(); ++c) {
                    const std::string pad(widths[c] - cells[c].size(), ' ');
                    line += c == 0 ? cells[c] + pad : "  " + pad + cells[c];
                }
                while (!line.empty() && line.back() == ' ') line.pop_back();
                text += line + "\n";
            }
        }
        text += "\n";
    }
    return text;
}

}  // namespace bitzkey::report
