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
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/Exchange.hpp"
#include "core/Insights.hpp"
#include "core/PyFormat.hpp"
#include "core/Rules.hpp"
#include "core/Simulation.hpp"

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
        "  --diagnose FILE             write how the predators fare, a line a turn, saved as it goes, and a summary\n"
        "                              beside it (FILE.summary.txt)\n"
        "  --export FILE               write every living Bitzkey at the end as JSON\n"
        "  --brain-report FILE         write the brain report at the end\n"
        "  --death-report FILE         write the death report, and the first thinking animal's brain inspected, at the end\n"
        "  --rule KEY=VALUE            change one of the advanced rules; may be repeated\n"
        "  --rule-at TURN:KEY=VALUE    change a rule once the world reaches TURN, as the map's ADVANCED does; may be repeated\n"
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

}  // namespace

int main(int argc, char** argv) {
    Simulation::Settings settings;
    int turns = 400, report_every = 100;
    bool profile = false;
    std::optional<std::string> trace_path, export_path, report_path, diagnose_path, deaths_path;
    rules::Values changes;
    std::map<int, rules::Values> later;  // rules changed once the world reaches a turn, by turn
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
            else if (flag == "--year-length") settings.year_length = std::stoi(value);
            else if (flag == "--turns") turns = std::stoi(value);
            else if (flag == "--report-every") report_every = std::stoi(value);
            else if (flag == "--trace") trace_path = value;
            else if (flag == "--diagnose") diagnose_path = value;
            else if (flag == "--export") export_path = value;
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
        Simulation world(settings);
        if (USE_GPU) std::printf("%s\n", world.gpu_status().c_str());
        std::ofstream trace;
        if (trace_path) {
            trace.open(*trace_path);
            trace << trace_line(world) << '\n';
        }
        std::optional<Diagnosis> diagnosis;
        if (diagnose_path) diagnosis.emplace(*diagnose_path);
        using Clock = std::chrono::steady_clock;
        auto last = Clock::now();
        const auto start = last;
        int last_turn = 0;
        for (int step = 1; step <= turns; ++step) {
            if (const auto due = later.find(world.turn()); due != later.end()) {  // as the map's ADVANCED does
                for (const auto& [key, value] : due->second) changes[key] = value;
                rules::apply(changes);
                world.rules_changed();
            }
            if (diagnosis) diagnosis->before(world);
            world.advance();
            if (diagnosis) diagnosis->after(world);
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
        std::printf("%d turns in %.2f s (%.2f ms/turn)\n", world.turn(), total, total * 1000 / std::max(1, world.turn()));
        if (diagnosis) {
            const std::string summary = diagnosis->summary();
            std::ofstream(*diagnose_path + ".summary.txt") << summary;
            std::printf("%s", summary.c_str());
        }

        if (profile) {
            const Simulation::Timings& took = world.timings;
            const double each = 1.0 / std::max(1, world.turn());
            std::printf("ms a turn: upkeep %.2f  sense %.2f  think %.2f  learn %.2f  choose %.2f  act %.2f  after %.2f  record %.2f\n",
                        took.upkeep * each, took.sense * each, took.think * each, took.learn * each, took.choose * each,
                        took.act * each, took.after * each, took.record * each);
        }
        world.sync_brains();  // the brains as they now are, when they think on the GPUs
        const auto alive = world.living();
        if (export_path) {
            std::ofstream(*export_path) << export_bitzkeys(std::vector<const Bitzkey*>(alive.begin(), alive.end())) << '\n';
        }
        if (report_path) {
            const std::vector<const Bitzkey*> animals(alive.begin(), alive.end());
            std::ofstream(*report_path) << brain_report(world.turn(), animals, world.insights());
        }
        if (deaths_path) {
            std::ofstream file(*deaths_path);
            for (const std::string& line : death_report_lines(world.death_causes())) file << line << '\n';
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
