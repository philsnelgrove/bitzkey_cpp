// Run a Bitzkey world without a window: for benchmarks, long experiments, exports, and checking
// the C++ simulation against the Python one turn by turn (--trace).

#include <chrono>
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
        "  --export FILE               write every living Bitzkey at the end as JSON\n"
        "  --brain-report FILE         write the brain report at the end\n"
        "  --rule KEY=VALUE            change one of the advanced rules; may be repeated\n"
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

}  // namespace

int main(int argc, char** argv) {
    Simulation::Settings settings;
    int turns = 400, report_every = 100;
    bool profile = false;
    std::optional<std::string> trace_path, export_path, report_path;
    rules::Values changes;
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
            else if (flag == "--export") export_path = value;
            else if (flag == "--brain-report") report_path = value;
            else if (flag == "--rule") {
                const std::size_t equals = value.find('=');
                const std::string key = value.substr(0, equals);
                if (equals == std::string::npos || !rules::find(key)) throw std::invalid_argument("unknown rule " + value);
                changes[key] = std::stod(value.substr(equals + 1));
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
        using Clock = std::chrono::steady_clock;
        auto last = Clock::now();
        const auto start = last;
        int last_turn = 0;
        for (int step = 1; step <= turns; ++step) {
            world.advance();
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
    } catch (const std::exception& error) {
        std::cerr << "bitzkey_cli: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
