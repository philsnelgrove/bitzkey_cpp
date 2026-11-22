// Goal bench: can a real Bitzkey brain hold a goal in its memory neurons, and act on it later, unrewarded meanwhile?
//
// A toy delayed choice, touching no world. Each episode a brain is shown, on its first turn only, which of K things it
// wants (the goal: one of K outside senses at 1, the rest 0). Then come D turns with the goal gone from its senses
// (and, with --distract, a decoy thing lit at random each of them: interruptions). On the last turn it chooses one of K
// actions, by choice_odds as in the world, and gains 1 only if it chose the goal's; nothing is gained in between.
// Its memory is the world's: each turn the brain's gates write its MEMORY_NEURONS (memory_after) and it senses what
// they held (SenseMemory). It learns as the world does: the choice by Brain::learn, and back in time by
// Brain::learn_memory over up to --steps earlier turns (the world's MEMORY_STEPS, 1 by default).
//
// It reports, per seed, how often the last window's choices were right, against chance 1/K, so one can see whether a
// goal is held over D turns, and what memory steps it needs. --no-memory wipes memory every turn (the control).
#include "core/Brain.hpp"
#include "core/PyRandom.hpp"
#include "core/Simulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bitzkey;

namespace {

struct Options {
    int things = 4, delay = 1, steps = 1, seeds = 3, first_seed = 1, trials = 200000, window = 5000;
    double temperature = 0.1, intellect = 0.5, noise = 0.3;
    bool memory = true, distract = false;
    double recur = 0.0;  // --recur p: one life, a want that recurs (see run_recurring); 0 the delayed choice
    int population = 1;  // --population N: with --life, N animals living side by side (see run_lives)
    int life = 0;        // --life L: turns each lives, then a child of two takes its place, its brain inherited
    bool select = false; // --select: parents the better of two at random by how often they were right (else at random)
};

Options parse(int argc, char** argv) {
    Options o;
    for (int at = 1; at < argc; ++at) {
        const std::string flag = argv[at];
        const auto next = [&]() -> const char* {
            if (at + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", flag.c_str()); std::exit(2); }
            return argv[++at];
        };
        if (flag == "--things") o.things = std::atoi(next());
        else if (flag == "--delay") o.delay = std::atoi(next());
        else if (flag == "--steps") o.steps = std::atoi(next());
        else if (flag == "--seeds") o.seeds = std::atoi(next());
        else if (flag == "--first-seed") o.first_seed = std::atoi(next());
        else if (flag == "--trials") o.trials = std::atoi(next());
        else if (flag == "--window") o.window = std::atoi(next());
        else if (flag == "--temperature") o.temperature = std::atof(next());
        else if (flag == "--intellect") o.intellect = std::atof(next());
        else if (flag == "--noise") o.noise = std::atof(next());
        else if (flag == "--no-memory") o.memory = false;
        else if (flag == "--distract") o.distract = true;
        else if (flag == "--recur") o.recur = std::atof(next());
        else if (flag == "--population") o.population = std::atoi(next());
        else if (flag == "--life") o.life = std::atoi(next());
        else if (flag == "--select") o.select = true;
        else {
            std::fprintf(stderr,
                         "goal_bench [--things K] [--delay D] [--steps S] [--seeds N] [--first-seed F] [--trials T] [--window W]\n"
                         "           [--temperature T] [--intellect I] [--noise N] [--no-memory] [--distract] [--recur P]\n"
                         "           [--population N --life L] [--select]\n");
            std::exit(2);
        }
    }
    o.things = std::clamp(o.things, 2, OUTSIDE_SEEN);
    o.steps = std::clamp(o.steps, 0, MAX_MEMORY_STEPS);
    o.delay = std::max(0, o.delay);
    return o;
}

// A want that recurs (--recur p): one long life of --trials turns. Each turn, with chance p, it sees where food is: one
// of K places lit in its senses, the place then kept until the next sighting; on other turns nothing shows where (a
// glimpse of a decoy with --distract). Every turn it chooses one of K places to go to and gains 1 when it goes where
// the food was last seen, so holding the place pays each turn, not once at the end. It learns each turn as the world
// does (learn, and learn_memory over --steps turns back). Reported: how often it is right on the last window's turns,
// by how many turns since it last saw the food.
void run_recurring(const Options& o, double rate) {
    constexpr int GAPS = 9;  // turns since the sighting: 0 (seen now), 1 .. 7, 8 or more
    std::array<double, GAPS> mean_right{};
    std::array<int, GAPS> seeds_with{};
    for (int seed = o.first_seed; seed < o.first_seed + o.seeds; ++seed) {
        PyRandom rng(static_cast<std::uint64_t>(seed));
        Brain brain = Brain::random(rng);
        std::vector<int> actions(static_cast<std::size_t>(o.things));
        for (int k = 0; k < o.things; ++k) actions[static_cast<std::size_t>(k)] = k;
        Held held{};
        std::vector<Senses> seen;  // the latest first, as many as memory may be taught back
        int place = 0, since = 0;
        std::array<int, GAPS> right{}, counted{};
        for (int turn = 0; turn < o.trials; ++turn) {
            const bool sighted = turn == 0 || rng.random() < o.recur;
            if (sighted) {
                place = static_cast<int>(rng.random() * o.things);
                since = 0;
            } else {
                ++since;
            }
            Senses senses{};
            for (int sense = 0; sense < INPUT_SIZE; ++sense) {
                senses[static_cast<std::size_t>(sense)] = signed_sense(sense) ? o.noise * (2 * rng.random() - 1) : o.noise * rng.random();
            }
            for (int k = 0; k < o.things; ++k) senses[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(k)])] = 0.0;
            if (sighted) {
                senses[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(place)])] = 1.0;
            } else if (o.distract) {
                const int decoy = static_cast<int>(rng.random() * o.things);
                senses[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(decoy)])] = 0.5;  // a glimpse
            }
            for (int neuron = 0; neuron < MEMORY_NEURONS; ++neuron) {
                senses[static_cast<std::size_t>(SenseMemory + neuron)] = o.memory ? held[static_cast<std::size_t>(neuron)] : 0.0;
            }
            const Values values = brain.values(senses);
            int choice = 0;
            double roll = rng.random();
            for (const auto& [action, chance] : choice_odds(values, actions, o.temperature)) {
                roll -= chance;
                choice = action;
                if (roll < 0) break;
            }
            const double gained = choice == place ? 1.0 : 0.0;
            brain.learn(senses, choice, gained, rate);
            const int back = std::min(o.steps, static_cast<int>(seen.size()));
            if (o.memory && back > 0) {
                std::vector<const Senses*> past;
                for (int step = 0; step < back; ++step) past.push_back(&seen[static_cast<std::size_t>(step)]);
                brain.learn_memory(senses, past, choice, gained, rate * MEMORY_LEARNING);
            }
            held = o.memory ? memory_after(values, held) : Held{};
            seen.insert(seen.begin(), senses);
            if (static_cast<int>(seen.size()) > MAX_MEMORY_STEPS) seen.pop_back();
            if (turn >= o.trials - o.window) {
                const int gap = std::min(since, GAPS - 1);
                right[static_cast<std::size_t>(gap)] += choice == place;
                ++counted[static_cast<std::size_t>(gap)];
            }
        }
        std::printf("  seed %d by turns since seen:", seed);
        for (int gap = 0; gap < GAPS; ++gap) {
            if (counted[static_cast<std::size_t>(gap)] == 0) continue;
            const double share = static_cast<double>(right[static_cast<std::size_t>(gap)]) / counted[static_cast<std::size_t>(gap)];
            mean_right[static_cast<std::size_t>(gap)] += share;
            ++seeds_with[static_cast<std::size_t>(gap)];
            std::printf(" %d%s:%.2f", gap, gap == GAPS - 1 ? "+" : "", share);
        }
        std::printf("\n");
        std::fflush(stdout);
    }
    std::printf("mean by turns since seen:");
    for (int gap = 0; gap < GAPS; ++gap) {
        if (seeds_with[static_cast<std::size_t>(gap)] > 0)
            std::printf(" %d%s:%.2f", gap, gap == GAPS - 1 ? "+" : "", mean_right[static_cast<std::size_t>(gap)] / seeds_with[static_cast<std::size_t>(gap)]);
    }
    std::printf("  (chance %.2f)\n", 1.0 / o.things);
}

// Lives (--recur p with --life L): --population animals, each its own want that recurs as in run_recurring, each a life
// of L turns; at its end a child takes its place with a brain inherited from two (Brain::inherit, the world's way: the
// parents' brains as they learnt them, mixed and mutated), its memory blank. --trials turns of the whole population.
// Reported, over the last window: how often right by turns since seen, and for the young (the first fifth of a life)
// apart, to see what is born knowing rather than learnt again.
void run_lives(const Options& o, double rate) {
    constexpr int GAPS = 9;
    struct Animal {
        Brain brain;
        Held held{};
        std::vector<Senses> seen;
        int place = 0, since = 0, age = 0, right = 0;
    };
    std::array<double, GAPS> mean_all{}, mean_young{};
    std::array<int, GAPS> with_all{}, with_young{};
    for (int seed = o.first_seed; seed < o.first_seed + o.seeds; ++seed) {
        PyRandom rng(static_cast<std::uint64_t>(seed));
        std::vector<Animal> animals;
        for (int each = 0; each < std::max(2, o.population); ++each) {
            Animal animal{Brain::random(rng)};
            animal.age = static_cast<int>(rng.random() * o.life);  // ages spread, so deaths come one at a time
            animal.place = static_cast<int>(rng.random() * o.things);
            animals.push_back(std::move(animal));
        }
        std::vector<int> actions(static_cast<std::size_t>(o.things));
        for (int k = 0; k < o.things; ++k) actions[static_cast<std::size_t>(k)] = k;
        std::array<int, GAPS> right{}, counted{}, right_young{}, counted_young{};
        long long generations = 0;
        const auto parent = [&]() -> const Animal& {
            const auto& one = animals[static_cast<std::size_t>(rng.random() * animals.size())];
            if (!o.select) return one;
            const auto& two = animals[static_cast<std::size_t>(rng.random() * animals.size())];
            const double a = static_cast<double>(one.right) / std::max(1, one.age), b = static_cast<double>(two.right) / std::max(1, two.age);
            return a >= b ? one : two;
        };
        for (int turn = 0; turn < o.trials; ++turn) {
            for (std::size_t who = 0; who < animals.size(); ++who) {
                Animal& animal = animals[who];
                if (animal.age >= o.life) {  // its life over: a child of two takes its place
                    const Animal& first = parent();
                    const Animal& second = parent();
                    Brain child = Brain::inherit(first.brain, second.brain, rng);
                    animal.brain = std::move(child);
                    animal.held = Held{};
                    animal.seen.clear();
                    animal.age = animal.right = 0;
                    ++generations;
                }
                const bool sighted = animal.age == 0 || rng.random() < o.recur;
                if (sighted) {
                    animal.place = static_cast<int>(rng.random() * o.things);
                    animal.since = 0;
                } else {
                    ++animal.since;
                }
                Senses senses{};
                for (int sense = 0; sense < INPUT_SIZE; ++sense) {
                    senses[static_cast<std::size_t>(sense)] = signed_sense(sense) ? o.noise * (2 * rng.random() - 1) : o.noise * rng.random();
                }
                for (int k = 0; k < o.things; ++k) senses[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(k)])] = 0.0;
                if (sighted) {
                    senses[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(animal.place)])] = 1.0;
                } else if (o.distract) {
                    const int decoy = static_cast<int>(rng.random() * o.things);
                    senses[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(decoy)])] = 0.5;
                }
                for (int neuron = 0; neuron < MEMORY_NEURONS; ++neuron) {
                    senses[static_cast<std::size_t>(SenseMemory + neuron)] = o.memory ? animal.held[static_cast<std::size_t>(neuron)] : 0.0;
                }
                const Values values = animal.brain.values(senses);
                int choice = 0;
                double roll = rng.random();
                for (const auto& [action, chance] : choice_odds(values, actions, o.temperature)) {
                    roll -= chance;
                    choice = action;
                    if (roll < 0) break;
                }
                const bool hit = choice == animal.place;
                animal.brain.learn(senses, choice, hit ? 1.0 : 0.0, rate);
                const int back = std::min(o.steps, static_cast<int>(animal.seen.size()));
                if (o.memory && back > 0) {
                    std::vector<const Senses*> past;
                    for (int step = 0; step < back; ++step) past.push_back(&animal.seen[static_cast<std::size_t>(step)]);
                    animal.brain.learn_memory(senses, past, choice, hit ? 1.0 : 0.0, rate * MEMORY_LEARNING);
                }
                animal.held = o.memory ? memory_after(values, animal.held) : Held{};
                animal.seen.insert(animal.seen.begin(), senses);
                if (static_cast<int>(animal.seen.size()) > MAX_MEMORY_STEPS) animal.seen.pop_back();
                animal.right += hit;
                if (turn >= o.trials - o.window) {
                    const auto gap = static_cast<std::size_t>(std::min(animal.since, GAPS - 1));
                    right[gap] += hit;
                    ++counted[gap];
                    if (animal.age < o.life / 5) {
                        right_young[gap] += hit;
                        ++counted_young[gap];
                    }
                }
                ++animal.age;
            }
        }
        std::printf("  seed %d (%lld generations born):\n    all:  ", seed, generations);
        for (int gap = 0; gap < GAPS; ++gap) {
            const auto g = static_cast<std::size_t>(gap);
            if (counted[g] == 0) continue;
            const double share = static_cast<double>(right[g]) / counted[g];
            mean_all[g] += share;
            ++with_all[g];
            std::printf(" %d%s:%.2f", gap, gap == GAPS - 1 ? "+" : "", share);
        }
        std::printf("\n    young:");
        for (int gap = 0; gap < GAPS; ++gap) {
            const auto g = static_cast<std::size_t>(gap);
            if (counted_young[g] == 0) continue;
            const double share = static_cast<double>(right_young[g]) / counted_young[g];
            mean_young[g] += share;
            ++with_young[g];
            std::printf(" %d%s:%.2f", gap, gap == GAPS - 1 ? "+" : "", share);
        }
        std::printf("\n");
        std::fflush(stdout);
    }
    std::printf("mean all:  ");
    for (int gap = 0; gap < GAPS; ++gap)
        if (with_all[static_cast<std::size_t>(gap)]) std::printf(" %d%s:%.2f", gap, gap == GAPS - 1 ? "+" : "", mean_all[static_cast<std::size_t>(gap)] / with_all[static_cast<std::size_t>(gap)]);
    std::printf("\nmean young:");
    for (int gap = 0; gap < GAPS; ++gap)
        if (with_young[static_cast<std::size_t>(gap)]) std::printf(" %d%s:%.2f", gap, gap == GAPS - 1 ? "+" : "", mean_young[static_cast<std::size_t>(gap)] / with_young[static_cast<std::size_t>(gap)]);
    std::printf("  (chance %.2f)\n", 1.0 / o.things);
}

}  // namespace

int main(int argc, char** argv) {
    const Options o = parse(argc, argv);
    const double rate = LEARNING_RATE * (LEARNING_FLOOR + o.intellect * (1 + LEARNING_BOOST * o.intellect));
    if (o.recur > 0 && o.life > 0) {
        std::printf("goal bench, lives: things %d, sighting %.2f a turn, steps %d, memory %s%s, %d animals living %d turns%s, %d turns\n",
                    o.things, o.recur, o.steps, o.memory ? "on" : "WIPED", o.distract ? ", glimpses" : "", std::max(2, o.population), o.life,
                    o.select ? ", parents selected" : "", o.trials);
        run_lives(o, rate);
        return 0;
    }
    if (o.recur > 0) {
        std::printf("goal bench, a want that recurs: things %d, sighting %.2f a turn, steps %d, memory %s%s, %d turns\n", o.things, o.recur,
                    o.steps, o.memory ? "on" : "WIPED", o.distract ? ", glimpses" : "", o.trials);
        run_recurring(o, rate);
        return 0;
    }
    std::printf("goal bench: things %d, delay %d, steps %d, memory %s%s, temperature %.2f, rate %.4f, %d trials\n", o.things, o.delay,
                o.steps, o.memory ? "on" : "WIPED", o.distract ? ", decoys" : "", o.temperature, rate, o.trials);
    double sum = 0.0;
    for (int seed = o.first_seed; seed < o.first_seed + o.seeds; ++seed) {
        PyRandom rng(static_cast<std::uint64_t>(seed));
        Brain brain = Brain::random(rng);
        std::vector<int> actions(static_cast<std::size_t>(o.things));
        for (int k = 0; k < o.things; ++k) actions[static_cast<std::size_t>(k)] = k;  // any K action outputs serve
        int right = 0, counted = 0;
        std::vector<Senses> seen;
        for (int trial = 0; trial < o.trials; ++trial) {
            const int goal = static_cast<int>(rng.random() * o.things);
            Held held{};
            seen.clear();
            int choice = 0;
            for (int turn = 0; turn <= o.delay; ++turn) {
                Senses senses{};
                for (int sense = 0; sense < INPUT_SIZE; ++sense) {  // the background: a little of everything else
                    senses[static_cast<std::size_t>(sense)] = signed_sense(sense) ? o.noise * (2 * rng.random() - 1) : o.noise * rng.random();
                }
                for (int k = 0; k < o.things; ++k) senses[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(k)])] = 0.0;
                if (turn == 0) {
                    senses[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(goal)])] = 1.0;  // what it wants
                } else if (o.distract) {
                    const int decoy = static_cast<int>(rng.random() * o.things);
                    senses[static_cast<std::size_t>(OUTSIDE_SENSES[static_cast<std::size_t>(decoy)])] = 1.0;  // an interruption
                }
                for (int neuron = 0; neuron < MEMORY_NEURONS; ++neuron) {
                    senses[static_cast<std::size_t>(SenseMemory + neuron)] = o.memory ? held[static_cast<std::size_t>(neuron)] : 0.0;
                }
                const Values values = brain.values(senses);
                if (turn == o.delay) {
                    double roll = rng.random();
                    for (const auto& [action, chance] : choice_odds(values, actions, o.temperature)) {
                        roll -= chance;
                        choice = action;
                        if (roll < 0) break;
                    }
                }
                held = o.memory ? memory_after(values, held) : Held{};
                seen.push_back(senses);
            }
            const double gained = choice == goal ? 1.0 : 0.0;
            const Senses& last = seen.back();
            brain.learn(last, choice, gained, rate);
            const int back = std::min(o.steps, static_cast<int>(seen.size()) - 1);
            if (o.memory && back > 0) {
                std::vector<const Senses*> past;
                for (int step = 1; step <= back; ++step) past.push_back(&seen[seen.size() - 1 - static_cast<std::size_t>(step)]);
                brain.learn_memory(last, past, choice, gained, rate * MEMORY_LEARNING);
            }
            if (trial >= o.trials - o.window) {
                right += choice == goal;
                ++counted;
            }
        }
        const double share = static_cast<double>(right) / std::max(1, counted);
        sum += share;
        std::printf("  seed %d: right %.3f of the last %d (chance %.3f)\n", seed, share, counted, 1.0 / o.things);
        std::fflush(stdout);
    }
    std::printf("mean right %.3f (chance %.3f)\n", sum / o.seeds, 1.0 / o.things);
    return 0;
}
