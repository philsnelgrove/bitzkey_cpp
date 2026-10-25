// Run a Bitzkey world without a window: for benchmarks, long experiments, exports, and checking
// the C++ simulation against the Python one turn by turn (--trace).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/Exchange.hpp"
#include "core/Insights.hpp"
#include "core/PyFormat.hpp"
#include "core/Rules.hpp"
#include "core/Simulation.hpp"
#include "core/WorldFile.hpp"

using namespace bitzkey;

namespace {

void usage() {
    std::cout <<
        "bitzkey_cli: run a Bitzkey world without a window.\n"
        "  --width N --height N        world size in cells (default 100 x 100)\n"
        "  --population N              Bitzkeys spawned at the start (default 90)\n"
        "  --seed N                    seed the world for a repeatable run\n"
        "  --grass-density N           percent of cells with grass at the start (default 50)\n"
        "  --population-cap N          most animals alive at once (default: no limit)\n"
        "  --mind-mutation N           percent chance per roll of a mind mutation (default 50)\n"
        "  --body-mutation N           percent chance a birth flips a body bit (default 50)\n"
        "  --year-length N             turns in a year, of four seasons (default 360)\n"
        "  --import FILE               add the Bitzkeys exported in FILE\n"
        "  --turns N                   turns to run (default 400)\n"
        "  --report-every N            print a population line every N turns (default 100)\n"
        "  --trace FILE                write one line per turn, for comparing with the Python version\n"
        "  --diagnose FILE             write how the predators fare, a line a turn, saved as it goes; how every species\n"
        "                              fares, a line a species a turn (FILE.ecology.tsv); and a summary of both\n"
        "                              beside them (FILE.summary.txt)\n"
        "  --migration FILE            write, each season, whether animals moved ahead of the coming weather, and a summary\n"
        "  --export FILE               write every living Bitzkey at the end as JSON\n"
        "  --load FILE                 carry on the world saved in FILE, by its own settings and rules, rather than start one\n"
        "  --save FILE                 save the whole world at the end, to load and carry on\n"
        "  --brain-report FILE         write the brain report at the end\n"
        "  --death-report FILE         write the death report, and the first thinking animal's brain inspected, at the end\n"
        "  --rule KEY=VALUE            change one of the advanced rules; may be repeated\n"
        "  --rule-at TURN:KEY=VALUE    change a rule once the world reaches TURN, as the map's ADVANCED does; may be repeated\n"
        "  --deploy-at TURN:X,Y:FILE   deploy the Bitzkeys exported in FILE at cell X,Y once the world reaches TURN,\n"
        "                              as the catalog's DEPLOY does; may be repeated\n"
        "  --rules                     list the advanced rules, with their defaults and limits\n"
        "  --gpu                       think on every GPU found (the USE_GPU rule), in floats\n"
        "  --profile                   say at the end how long each part of a turn took\n";
}

// Every rule, one line each, as the Python version lists them:
//   KEY  group  default  minimum..maximum by step  label: hint
void list_rules() {
    for (const rules::Rule& rule : rules::all()) {
        std::cout << rule.key << "  " << rule.group << "  " << rules::shown(rule, rule.get()) << "  "
                  << rules::shown(rule, rule.minimum) << ".." << rules::shown(rule, rule.maximum) << " by "
                  << rules::shown(rule, rule.step) << "  " << rule.label << ": " << rule.hint << '\n';
    }
}

std::string trace_line(const Simulation& world) {
    const auto alive = world.living();
    int counts[4] = {0, 0, 0, 0};
    std::vector<double> hunger, health;
    long long places = 0;
    for (const Bitzkey* agent : alive) {
        counts[static_cast<int>(agent->species)] += 1;
        hunger.push_back(agent->hunger);
        health.push_back(agent->health);
        places += static_cast<long long>(agent->x) * 1000 + agent->y;
    }
    std::ostringstream line;
    line << world.turn() << ' ' << alive.size() << ' ' << counts[0] << ' ' << counts[1] << ' ' << counts[2] << ' '
         << counts[3] << ' ' << world.deaths() << ' ' << world.next_identifier() << ' ' << world.grass_tufts() << ' '
         << places << ' ' << py_float_repr(py_sum(hunger)) << ' ' << py_float_repr(py_sum(health));
    return line.str();
}

// A turn-by-turn look at how the predators fare, for finding out why they thrive or don't: written one
// line a turn, tab-separated, each line saved as it is written so a run can be stopped at any time.
class Diagnosis {
public:
    explicit Diagnosis(const std::string& path) : file_(path) {
        file_ << "turn\tplants\tgrazers\tomnivores\tpredators\tpredators_born"
                 "\tpredators_died\tby_predator\tby_omnivore\tby_grazer\tby_plant\tkilled_other\tstarved\tfroze\toverheated"
                 "\told_age\texhausted\tdied_other"
                 "\tgrazers_killed_by_predators\tomnivores_killed_by_predators\tgrazers_died\tomnivores_died"
                 "\tpredator_hunger\tpredator_health\tpredator_age\tpredator_generation\tin_primal_fear\tpredator_actions\n";
        file_.flush();
    }

    // Before a turn: who exists, so the turn's births can be told.
    void before(const Simulation& world) { first_new_ = world.next_identifier(); }

    void after(const Simulation& world) {
        const int turn = world.turn();
        int counts[4] = {0, 0, 0, 0};
        int born = 0, primal = 0;
        double hunger = 0, health = 0, age = 0, generation = 0;
        std::map<std::string, int> actions;
        const auto alive = world.living();
        for (const Bitzkey* agent : alive) {
            counts[static_cast<int>(agent->species)] += 1;
            if (agent->species != Species::Predator) continue;
            born += agent->identifier >= first_new_;
            primal += agent->primal_fear;
            hunger += agent->hunger;
            health += agent->health / agent->max_health;
            age += agent->age;
            generation += agent->generation;
            actions[std::string(agent->last_action)] += 1;
        }
        // Deaths this turn: how each predator died and, for a killing, who most likely struck the blow (a
        // living animal beside it that attacked or struck back this turn, a predator first); and the
        // grazers and omnivores the predators killed.
        std::map<std::string, int> died;
        int predators_died = 0, grazers_by_predators = 0, omnivores_by_predators = 0, grazers_died = 0, omnivores_died = 0;
        for (const auto& agent : world.agents()) {
            if (!agent->died_turn || *agent->died_turn != turn || agent->species == Species::Plant) continue;
            const std::string cause(agent->last_action);
            std::string killer;
            if (cause == "killed") killer = killed_by(world, *agent);
            if (agent->species == Species::Predator) {
                ++predators_died;
                if (cause == "killed") died["by_" + killer] += 1;
                else died[cause] += 1;
            } else if (agent->species == Species::Grazer) {
                ++grazers_died;
                grazers_by_predators += killer == "predator";
            } else {
                ++omnivores_died;
                omnivores_by_predators += killer == "predator";
            }
        }
        const int predators = counts[3];
        const auto mean = [predators](double total) { return predators ? total / predators : 0.0; };
        const auto of = [&died](const char* key) {
            const auto found = died.find(key);
            return found == died.end() ? 0 : found->second;
        };
        const int known = of("by_predator") + of("by_omnivore") + of("by_grazer") + of("by_plant") + of("by_unknown") + of("starved") +
                          of("froze") + of("overheated") + of("old age") + of("exhausted");
        std::ostringstream actions_text;
        std::vector<std::pair<int, std::string>> ranked;
        for (const auto& [name, count] : actions) ranked.emplace_back(-count, name);
        std::sort(ranked.begin(), ranked.end());
        for (const auto& [count, name] : ranked) actions_text << (actions_text.tellp() > 0 ? "," : "") << name << '=' << -count;
        char numbers[256];
        std::snprintf(numbers, sizeof numbers, "%.1f\t%.2f\t%.0f\t%.1f", mean(hunger), mean(health), mean(age), mean(generation));
        file_ << turn << '\t' << counts[0] << '\t' << counts[1] << '\t' << counts[2] << '\t' << predators << '\t' << born << '\t'
              << predators_died << '\t' << of("by_predator") << '\t' << of("by_omnivore") << '\t' << of("by_grazer") << '\t'
              << of("by_plant") << '\t' << of("by_unknown") << '\t' << of("starved") << '\t' << of("froze") << '\t' << of("overheated")
              << '\t' << of("old age") << '\t' << of("exhausted") << '\t' << predators_died - known << '\t' << grazers_by_predators
              << '\t' << omnivores_by_predators << '\t' << grazers_died << '\t' << omnivores_died << '\t' << numbers << '\t' << primal
              << '\t' << actions_text.str() << '\n';
        file_.flush();
        // Running totals, for the summary.
        totals_["predators born"] += born;
        totals_["predators died"] += predators_died;
        for (const auto& [cause, count] : died) totals_["predator death: " + cause] += count;
        totals_["grazers killed by predators"] += grazers_by_predators;
        totals_["omnivores killed by predators"] += omnivores_by_predators;
        for (const auto& [name, count] : actions) totals_["predator turns: " + name] += count;
        peak_ = std::max(peak_, predators);
    }

    std::string summary() const {
        std::ostringstream text;
        text << "predators at most: " << peak_ << '\n';
        for (const auto& [name, count] : totals_) text << name << ": " << count << '\n';
        return text.str();
    }

private:
    static std::string killed_by(const Simulation& world, const Bitzkey& victim) {
        std::string found = "unknown";
        for (const auto& other : world.agents()) {
            if (other.get() == &victim || !other->alive()) continue;
            if (std::abs(other->x - victim.x) + std::abs(other->y - victim.y) > 1) continue;
            const std::string_view did = other->last_action;
            if (other->species == Species::Plant) {
                if (found == "unknown") found = "plant";  // thorns, struck back at a browser
                continue;
            }
            if (did != "attacked" && did != "defended" && did != "pounced") continue;
            if (other->species == Species::Predator) return "predator";
            found = other->species == Species::Omnivore ? "omnivore" : "grazer";
        }
        return found;
    }

    std::ofstream file_;
    int first_new_ = 0;
    int peak_ = 0;
    std::map<std::string, long long> totals_;
};

// A turn-by-turn look at every species, for finding out why a world collapses: one line a species a turn,
// tab-separated, saved as it is written. Deaths come from the world's own records (cause, and the killer's species);
// the rest is each species' state at the turn's end: how fed and well, how many feeble or away from home, and how
// many are ready to breed, and of those how many their home's food and their prey's numbers would let.
class Ecology {
public:
    explicit Ecology(const std::string& path) : file_(path) {
        file_ << "turn\tspecies\talive\tborn\tdied\tby_grazer\tby_omnivore\tby_predator\tby_plant\tstarved\tfroze\toverheated"
                 "\told_age\texhausted\teaten\tother\thunger\tstarving\thealth\tfeeble\taway\tready\twell_fed\tprey_rare\tpregnant"
                 "\tage\tdied_infant\tdied_newborn\tdeath_age\tgrass\tseeds\tactions\n";
        file_.flush();
    }

    void before(const Simulation& world) { first_new_ = world.next_identifier(); }

    void after(const Simulation& world) {
        const int turn = world.turn();
        struct Tally {
            int alive = 0, born = 0, died = 0, starving = 0, feeble = 0, away = 0, ready = 0, well_fed = 0, prey_rare = 0, pregnant = 0;
            int died_infant = 0, died_newborn = 0;  // deaths in infancy (the feeble start of life), and within NEWBORN_TURNS of birth
            double death_age = 0;
            double hunger = 0, health = 0, age = 0;
            std::map<std::string, int> causes, infant_causes, actions;
        };
        std::array<Tally, 4> tallies;
        for (const Bitzkey* agent : world.living()) {
            Tally& tally = tallies[static_cast<std::size_t>(agent->species)];
            ++tally.alive;
            tally.born += agent->identifier >= first_new_;
            tally.hunger += agent->hunger;
            tally.starving += agent->hunger >= 100;
            tally.health += agent->health / agent->max_health;
            tally.age += agent->age;
            tally.feeble += agent->feeble;
            tally.away += agent->climate_steps(world.temperature(agent->y)) >= 0.5;
            tally.pregnant += !agent->womb.empty();
            if (agent->species != Species::Plant && agent->can_breed()) {
                ++tally.ready;
                const bool fed = world.well_fed(*agent), rare = world.prey_rare(*agent);
                tally.well_fed += fed;
                tally.prey_rare += rare;
            }
            tally.actions[std::string(agent->last_action)] += 1;
        }
        for (const auto& agent : world.agents()) {  // this turn's deaths, as the world recorded them
            if (!agent->died_turn || *agent->died_turn != turn) continue;
            Tally& tally = tallies[static_cast<std::size_t>(agent->species)];
            ++tally.died;
            const double infancy = FEEBLE_YOUTH * (agent->species == Species::Predator ? PREDATOR_INFANCY : 1.0) * agent->lifespan;
            const bool infant = agent->species != Species::Plant && agent->age < infancy;
            tally.died_infant += infant;
            tally.died_newborn += agent->age <= NEWBORN_TURNS;
            tally.death_age += agent->age;
            const Simulation::DeathRecord* death = world.death_record(agent->identifier);
            const std::string cause = !death              ? std::string("other")
                                      : death->killer >= 0 ? "by_" + std::string(species_name(death->killer_species))
                                                           : death->cause;
            tally.causes[cause] += 1;
            if (infant) tally.infant_causes[cause] += 1;
        }
        const std::size_t grass = world.grass_tufts(), seeds = world.seeds().size();
        for (std::size_t species = 0; species < tallies.size(); ++species) {
            const Tally& tally = tallies[species];
            const auto of = [&](const char* cause) {
                const auto found = tally.causes.find(cause);
                return found == tally.causes.end() ? 0 : found->second;
            };
            int known = 0;
            for (const char* cause : KNOWN_CAUSES) known += of(cause);
            const double mouths = std::max(1, tally.alive);
            std::vector<std::pair<int, std::string>> ranked;
            for (const auto& [name, count] : tally.actions) ranked.emplace_back(-count, name);
            std::sort(ranked.begin(), ranked.end());
            std::string actions;
            for (std::size_t index = 0; index < std::min<std::size_t>(4, ranked.size()); ++index) {
                actions += (index ? "," : "") + ranked[index].second + '=' + std::to_string(-ranked[index].first);
            }
            char numbers[128];
            std::snprintf(numbers, sizeof numbers, "%.1f\t%d\t%.2f", tally.hunger / mouths, tally.starving, tally.health / mouths);
            file_ << turn << '\t' << SPECIES[species] << '\t' << tally.alive << '\t' << tally.born << '\t' << tally.died;
            for (const char* cause : KNOWN_CAUSES) file_ << '\t' << of(cause);
            file_ << '\t' << tally.died - known << '\t' << numbers << '\t' << tally.feeble << '\t' << tally.away << '\t' << tally.ready << '\t'
                  << tally.well_fed << '\t' << tally.prey_rare << '\t' << tally.pregnant << '\t' << static_cast<int>(tally.age / mouths) << '\t'
                  << tally.died_infant << '\t' << tally.died_newborn << '\t' << (tally.died ? static_cast<int>(tally.death_age / tally.died) : 0) << '\t'
                  << grass << '\t' << seeds << '\t' << actions << '\n';
            // Running totals, and when each species first died out, for the summary.
            Totals& totals = totals_[species];
            totals.born += tally.born;
            totals.died += tally.died;
            totals.died_infant += tally.died_infant;
            totals.died_newborn += tally.died_newborn;
            totals.peak = std::max(totals.peak, tally.alive);
            for (const auto& [cause, count] : tally.causes) totals.causes[cause] += count;
            for (const auto& [cause, count] : tally.infant_causes) totals.infant_causes[cause] += count;
            if (tally.alive == 0 && totals.peak > 0 && totals.gone < 0) totals.gone = turn;
        }
        file_.flush();
    }

    std::string summary() const {
        std::ostringstream text;
        for (std::size_t species = 0; species < totals_.size(); ++species) {
            const Totals& totals = totals_[species];
            text << SPECIES[species] << ": at most " << totals.peak << ", born " << totals.born;
            if (totals.gone >= 0) text << ", died out at turn " << totals.gone;
            text << "\n";
            if (totals.died) {
                text << "  of " << totals.died << " deaths, " << totals.died_infant << " in infancy (" << 100 * totals.died_infant / totals.died
                     << "%), " << totals.died_newborn << " within " << NEWBORN_TURNS << " turns of birth (" << 100 * totals.died_newborn / totals.died
                     << "%)\n";
            }
            for (const auto& [cause, count] : totals.causes) text << "  died " << cause << ": " << count << "\n";
            for (const auto& [cause, count] : totals.infant_causes) {
                text << "  infants died " << cause << ": " << count << " (" << 100 * count / std::max(1LL, totals.died_infant) << "% of infant deaths)\n";
            }
        }
        return text.str();
    }

private:
    static constexpr int NEWBORN_TURNS = 10;  // a death this soon after birth is a newborn's
    static constexpr std::array<const char*, 10> KNOWN_CAUSES = {"by_grazer", "by_omnivore", "by_predator", "by_plant", "starved", "froze",
                                                                "overheated", "old age", "exhausted", "eaten"};
    struct Totals {
        int peak = 0, born = 0, gone = -1;
        long long died = 0, died_infant = 0, died_newborn = 0;
        std::map<std::string, long long> causes, infant_causes;
    };
    std::ofstream file_;
    int first_new_ = 0;
    std::array<Totals, 4> totals_;
};

// Whether animals move with the weather, in two windows a season. In the wave, from the season's
// first turn until its weather settles, each animal is marked exposed if the season will leave its
// row away from its home temperature, and at the end those still alive are looked at again: an
// animal that was exposed and no longer is has moved out of a change it can feel. In the calm, from
// the settling to the season's last turn, the same is done for the next season's weather: one that
// gets clear has moved ahead of a change it cannot yet feel.
class Migration {
public:
    explicit Migration(const std::string& path) : file_(path) {
        file_ << "turn\tquarter\twindow\tspecies\ttracked\texposed_start\texposed_end\tfled\tstrayed\n";
        file_.flush();
    }

    void after(const Simulation& world) {
        const int turn = world.turn(), quarter = season(turn);
        if (turn < YEAR_LENGTH) return;  // the first season starts settled, with no wave
        if (quarter != quarter_) {  // the wave begins, toward this season's weather
            quarter_ = quarter;
            settled_ = false;
            mark(world, Wave, quarter);
        }
        if (!settled_ && season_progress(turn) >= 1) {  // the calm begins, before the next season's weather
            settled_ = true;
            if (window_ == Wave) report(world);
            mark(world, Calm, (quarter + 1) % 4);
        }
        if (window_ == Calm && season(turn + 1) != quarter) report(world);
    }

    std::string summary() const {
        std::ostringstream text;
        text << "in the later half of the run (exposed: away from home in the season looked to):\n";
        text << " as each winter's wave washes in, of those it will leave away from home:\n";
        summarise(text, waves_);
        text << " in the calm ahead of each winter, of those the winter will leave away from home:\n";
        summarise(text, calms_);
        return text.str();
    }

private:
    enum Window { None, Wave, Calm };
    struct Reach {  // of those exposed as a window begins
        long long exposed = 0, rows = 0, sight = 0, in_sight = 0;
    };
    struct Tally {
        long long tracked = 0, before = 0, now = 0, fled = 0, strayed = 0;
        Reach reach;
    };

    void mark(const Simulation& world, Window window, int looked_to) {
        window_ = window;
        looked_to_ = looked_to;
        marked_.clear();
        reach_ = {};
        for (const Bitzkey* agent : world.living()) {
            if (agent->species == Species::Plant) continue;
            const bool out = exposed(world, *agent);
            marked_[agent->identifier] = {static_cast<int>(agent->species), out};
            if (!out) continue;
            // How far the nearest row it will be at home in lies, and whether it can see so far.
            Reach& reach = reach_[static_cast<std::size_t>(agent->species)];
            int rows = world.height();
            for (int y = 0; y < world.height(); ++y) {
                if (season_temperature(y, world.height(), looked_to_) == agent->climate.temperature) rows = std::min(rows, std::abs(y - agent->y));
            }
            ++reach.exposed;
            reach.rows += rows;
            reach.sight += agent->sight;
            reach.in_sight += rows <= agent->sight;
        }
    }

    bool exposed(const Simulation& world, const Bitzkey& agent) const {
        return season_temperature(agent.y, world.height(), looked_to_) != agent.climate.temperature;
    }

    void report(const Simulation& world) {
        std::array<Tally, 4> tallies;
        for (const Bitzkey* agent : world.living()) {
            const auto found = marked_.find(agent->identifier);
            if (found == marked_.end()) continue;
            Tally& tally = tallies[static_cast<std::size_t>(found->second.first)];
            const bool before = found->second.second, now = exposed(world, *agent);
            ++tally.tracked;
            tally.before += before;            tally.now += now;
            tally.fled += before && !now;
            tally.strayed += !before && now;
        }
        // The summary is of the winters: their waves (the second and fourth quarters), and the calms ahead of them.
        const bool winter = looked_to_ % 2 == 1;
        for (std::size_t species = 1; species < tallies.size(); ++species) {
            Tally& tally = tallies[species];
            tally.reach = reach_[species];
            file_ << world.turn() << '\t' << quarter_ << '\t' << (window_ == Wave ? "wave" : "calm") << '\t' << SPECIES[species] << '\t'
                  << tally.tracked << '\t' << tally.before << '\t' << tally.now << '\t' << tally.fled << '\t' << tally.strayed << '\n';
            if (winter) (window_ == Wave ? waves_ : calms_)[species].push_back(tally);
        }
        file_.flush();
        window_ = None;
    }

    static void summarise(std::ostringstream& text, const std::array<std::vector<Tally>, 4>& totals) {
        for (std::size_t species = 1; species < totals.size(); ++species) {
            const auto& seasons = totals[species];
            Tally sum;
            for (std::size_t index = seasons.size() / 2; index < seasons.size(); ++index) {
                sum.tracked += seasons[index].tracked;
                sum.before += seasons[index].before;
                sum.now += seasons[index].now;
                sum.fled += seasons[index].fled;
                sum.strayed += seasons[index].strayed;
                sum.reach.exposed += seasons[index].reach.exposed;
                sum.reach.rows += seasons[index].reach.rows;
                sum.reach.sight += seasons[index].reach.sight;
                sum.reach.in_sight += seasons[index].reach.in_sight;
            }
            char line[256];
            std::snprintf(line, sizeof line, "  %-9s tracked %7lld  exposed %5.1f%% -> %5.1f%%  fled %5.1f%% of exposed  strayed %5.1f%% of safe\n",
                          std::string(SPECIES[species]).c_str(), sum.tracked, 100.0 * sum.before / std::max(1LL, sum.tracked),
                          100.0 * sum.now / std::max(1LL, sum.tracked), 100.0 * sum.fled / std::max(1LL, sum.before),
                          100.0 * sum.strayed / std::max(1LL, sum.tracked - sum.before));
            text << line;
            const double exposed = std::max(1LL, sum.reach.exposed);
            std::snprintf(line, sizeof line, "            home lay %5.1f rows away on average, with sight %4.1f; %5.1f%% could see it\n",
                          sum.reach.rows / exposed, sum.reach.sight / exposed, 100.0 * sum.reach.in_sight / exposed);
            text << line;
        }
    }

    std::ofstream file_;
    int quarter_ = -1, looked_to_ = 0;
    bool settled_ = false;
    Window window_ = None;
    std::map<int, std::pair<int, bool>> marked_;  // by identifier: species, and whether exposed as the window began
    std::array<std::vector<Tally>, 4> waves_, calms_;
    std::array<Reach, 4> reach_;  // by species, for the window under way
};

}  // namespace

int main(int argc, char** argv) {
    Simulation::Settings settings;
    int turns = 400, report_every = 100;
    bool profile = false, year_length_given = false;
    std::optional<std::string> trace_path, migration_path, export_path, report_path, diagnose_path, deaths_path, load_path, save_path;
    rules::Values changes;
    std::map<int, rules::Values> later;  // rules changed once the world reaches a turn, by turn
    struct Deployment {
        int x, y;
        std::vector<Imported> bitzkeys;
    };
    std::multimap<int, Deployment> deployments;  // Bitzkeys deployed once the world reaches a turn, by turn
    try {
        for (int index = 1; index < argc; ++index) {
            const std::string flag = argv[index];
            if (flag == "--help" || flag == "-h") {
                usage();
                return 0;
            }
            if (flag == "--rules") {
                list_rules();
                return 0;
            }
            if (flag == "--profile") {  // where each turn's time goes, at the end
                profile = true;
                continue;
            }
            if (flag == "--gpu") {  // think on the GPUs: the USE_GPU rule
                changes["USE_GPU"] = 1;
                continue;
            }
            if (index + 1 >= argc) throw std::invalid_argument("missing value for " + flag);
            const std::string value = argv[++index];
            if (flag == "--width") settings.width = std::stoi(value);
            else if (flag == "--height") settings.height = std::stoi(value);
            else if (flag == "--population") settings.population = std::stoi(value);
            else if (flag == "--seed") settings.seed = std::stoull(value);
            else if (flag == "--grass-density") settings.grass_density = std::stod(value);
            else if (flag == "--population-cap") settings.population_cap = std::stoi(value);
            else if (flag == "--mind-mutation") settings.mind_mutation = std::stod(value);
            else if (flag == "--body-mutation") settings.body_mutation = std::stod(value);
            else if (flag == "--year-length") {
                settings.year_length = std::stoi(value);
                year_length_given = true;
            }
            else if (flag == "--turns") turns = std::stoi(value);
            else if (flag == "--report-every") report_every = std::stoi(value);
            else if (flag == "--trace") trace_path = value;
            else if (flag == "--diagnose") diagnose_path = value;
            else if (flag == "--migration") migration_path = value;
            else if (flag == "--export") export_path = value;
            else if (flag == "--load") load_path = value;
            else if (flag == "--save") save_path = value;
            else if (flag == "--brain-report") report_path = value;
            else if (flag == "--death-report") deaths_path = value;
            else if (flag == "--rule" || flag == "--rule-at") {
                const std::size_t colon = flag == "--rule-at" ? value.find(':') : std::string::npos;
                if (flag == "--rule-at" && colon == std::string::npos) throw std::invalid_argument("--rule-at needs TURN:KEY=VALUE");
                const std::string rule = colon == std::string::npos ? value : value.substr(colon + 1);
                const std::size_t equals = rule.find('=');
                const std::string key = rule.substr(0, equals);
                if (equals == std::string::npos || !rules::find(key)) throw std::invalid_argument("unknown rule " + rule);
                if (colon == std::string::npos) changes[key] = std::stod(rule.substr(equals + 1));
                else later[std::stoi(value.substr(0, colon))][key] = std::stod(rule.substr(equals + 1));
            }
            else if (flag == "--deploy-at") {
                const std::size_t first = value.find(':'), comma = value.find(','), second = value.find(':', first + 1);
                if (first == std::string::npos || comma == std::string::npos || second == std::string::npos) {
                    throw std::invalid_argument("--deploy-at needs TURN:X,Y:FILE");
                }
                std::ifstream file(value.substr(second + 1), std::ios::binary);
                if (!file) throw std::invalid_argument("cannot read " + value.substr(second + 1));
                std::stringstream text;
                text << file.rdbuf();
                deployments.emplace(std::stoi(value.substr(0, first)),
                                    Deployment{std::stoi(value.substr(first + 1, comma - first - 1)),
                                               std::stoi(value.substr(comma + 1, second - comma - 1)), import_bitzkeys(text.str())});
            }
            else if (flag == "--import") {
                std::ifstream file(value, std::ios::binary);
                if (!file) throw std::invalid_argument("cannot read " + value);
                std::stringstream text;
                text << file.rdbuf();
                auto found = import_bitzkeys(text.str());
                settings.imported.insert(settings.imported.end(), found.begin(), found.end());
            } else {
                throw std::invalid_argument("unknown option " + flag);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "bitzkey_cli: " << error.what() << "\n\n";
        usage();
        return 2;
    }

    rules::Values values = rules::defaults();
    for (const auto& [key, value] : changes) values[key] = value;
    if (const auto found = rules::problems(values); !found.empty()) {
        std::cerr << "bitzkey_cli: " << found.front() << '\n';
        return 2;
    }
    rules::apply(changes);

    try {
        std::unique_ptr<Simulation> made;
        if (load_path) {
            made = WorldFile::load(*load_path);
            changes = rules::current();  // the saved world's rules, which changes later in the run build on
            if (year_length_given) YEAR_LENGTH = settings.year_length;  // a loaded world's seasons, retimed
        } else {
            made = std::make_unique<Simulation>(settings);
        }
        Simulation& world = *made;
        if (USE_GPU) std::printf("%s\n", world.gpu_status().c_str());
        std::ofstream trace;
        if (trace_path) {
            trace.open(*trace_path);
            trace << trace_line(world) << '\n';
        }
        std::optional<Diagnosis> diagnosis;
        std::optional<Ecology> ecology;
        if (diagnose_path) {
            diagnosis.emplace(*diagnose_path);
            ecology.emplace(*diagnose_path + ".ecology.tsv");
        }
        std::optional<Migration> migration;
        if (migration_path) migration.emplace(*migration_path);
        using Clock = std::chrono::steady_clock;
        auto last = Clock::now();
        const auto start = last;
        const int first_turn = world.turn();  // a loaded world starts part way
        int last_turn = first_turn;
        for (int step = 1; step <= turns; ++step) {
            if (const auto due = later.find(world.turn()); due != later.end()) {  // as the map's ADVANCED does
                for (const auto& [key, value] : due->second) changes[key] = value;
                rules::apply(changes);
                world.rules_changed();
            }
            const auto [from, to] = deployments.equal_range(world.turn());  // as the catalog's DEPLOY does
            for (auto due = from; due != to; ++due) world.deploy(due->second.bitzkeys, due->second.x, due->second.y);
            if (diagnosis) diagnosis->before(world);
            if (ecology) ecology->before(world);
            world.advance();
            if (diagnosis) diagnosis->after(world);
            if (ecology) ecology->after(world);
            if (migration) migration->after(world);
            if (trace) trace << trace_line(world) << '\n';
            const EndReason reason = world.end_reason();
            const bool report = report_every > 0 && world.turn() % report_every == 0;
            if (report || reason != EndReason::None || step == turns) {
                const auto now = Clock::now();
                const double ms = std::chrono::duration<double, std::milli>(now - last).count() /
                                  std::max(1, world.turn() - last_turn);
                int counts[4] = {0, 0, 0, 0};
                for (const Bitzkey* agent : world.living()) counts[static_cast<int>(agent->species)] += 1;
                std::printf("turn %5d  plants %4d  grazers %5d  omnivores %5d  predators %5d  %7.2f ms/turn\n",
                            world.turn(), counts[0], counts[1], counts[2], counts[3], ms);
                std::fflush(stdout);  // seen at once, even written to a file
                last = now;
                last_turn = world.turn();
            }
            if (reason != EndReason::None) {
                std::printf("ended at turn %d: %s\n", world.turn(),
                            reason == EndReason::Extinct ? "every animal has died" : "the world is full");
                break;
            }
        }
        const double total = std::chrono::duration<double>(Clock::now() - start).count();
        std::printf("%d turns in %.2f s (%.2f ms/turn)\n", world.turn() - first_turn, total, total * 1000 / std::max(1, world.turn() - first_turn));
        if (diagnosis) {
            const std::string summary = diagnosis->summary() + "\n" + ecology->summary();
            std::ofstream(*diagnose_path + ".summary.txt") << summary;
            std::printf("%s", summary.c_str());
        }

        if (migration) {
            const std::string summary = migration->summary();
            std::ofstream(*migration_path + ".summary.txt") << summary;
            std::printf("%s", summary.c_str());
        }

        if (profile) {
            const Simulation::Timings& took = world.timings;
            const double each = 1.0 / std::max(1, world.turn() - first_turn);
            std::printf("ms a turn: upkeep %.2f  sense %.2f  think %.2f  learn %.2f  choose %.2f  act %.2f  after %.2f  record %.2f\n",
                        took.upkeep * each, took.sense * each, took.think * each, took.learn * each, took.choose * each,
                        took.act * each, took.after * each, took.record * each);
            // Within acting, the costliest actions first: ms a turn, how many took it a turn, and µs each.
            std::vector<int> order(ACTION_COUNT);
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](int a, int b) { return took.actions[a] > took.actions[b]; });
            std::printf("acting: witnessed %.2f", took.witnessed * each);
            for (const int action : order) {
                if (took.taken[action] == 0) continue;
                std::printf("  %s %.2f (%.0f, %.1f us)", std::string(ACTIONS[action]).c_str(), took.actions[action] * each,
                            took.taken[action] * each, 1000 * took.actions[action] / took.taken[action]);
            }
            std::printf("\n");
        }
        world.sync_brains();  // the brains as they now are, when they think on the GPUs
        const auto alive = world.living();
        if (save_path) WorldFile::save(world, *save_path);
        if (export_path) {
            std::ofstream(*export_path) << export_bitzkeys(std::vector<const Bitzkey*>(alive.begin(), alive.end())) << '\n';
        }
        if (report_path) {
            const std::vector<const Bitzkey*> animals(alive.begin(), alive.end());
            std::ofstream(*report_path) << brain_report(world.turn(), animals, world.insights());
        }
        if (deaths_path) {
            std::ofstream file(*deaths_path);
            for (const std::string& line : death_report_lines(world.deaths_so_far())) file << line << '\n';
            for (const Bitzkey* agent : alive) {  // and the first thinking animal's brain, as inspected
                if (!agent->brain) continue;
                file << "\n#" << agent->identifier << '\n';
                for (const std::string& line : brain_matrix_lines(*agent)) file << line << '\n';
                break;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "bitzkey_cli: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
