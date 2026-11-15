// Call bench: can two real Bitzkey brains, learning by the world's own rule, settle on a code of calls?
//
// A toy signalling game, touching no world. Each trial one brain (the speaker) senses a hidden fact, one of K, among
// background noise in every other sense, and chooses silence or one of the five calls by choice_odds, as in the world.
// The other (the listener) senses its own noise and the call heard (the real heard-call sense), and chooses one of K
// actions; it gains REWARD when its action fits the fact. Both learn with Brain::learn at the world's learning rate.
//
// How the speaker learns its call (--mode):
//   empathy  as the world now: its own reward (0 here) + empathy x the listener's surprise (target - chosen value);
//            with --silence, a call also teaches silence its own reward (the SILENCE_LESSON's counterfactual)
//   shared   the listener's whole reward, as if the speaker gained what the listener did (kin, the best case)
//
// It reports, per seed, the trials until the listener's action carries --needed (default a quarter) of the fact's
// information in a window (at temperature 0.5 and reward 1 even a perfect code gives only 0.47 bits of 1), and
// the information in calls and in actions at the end, in bits.
#include "core/Brain.hpp"
#include "core/PyRandom.hpp"
#include "core/Simulation.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bitzkey;

namespace {

struct Options {
    int facts = 2, seeds = 10, trials = 200000, window = 2000;
    double needed = 0.25, reward = 1.0, noise = 1.0, empathy = 0.5, temperature = 0.5, intellect = 0.5;
    std::string mode = "empathy";
    bool silence = true, swap = true, deaf = false;  // deaf: the control, listeners never hear
};

constexpr int FACT_SENSES[] = {SenseThreatCloseness, SenseCarrionCloseness, SenseHerdSuccess, SenseHerdFailure};

double mutual_information(const std::vector<std::vector<int>>& counts) {
    double total = 0;
    std::vector<double> rows(counts.size(), 0), cols(counts[0].size(), 0);
    for (std::size_t r = 0; r < counts.size(); ++r)
        for (std::size_t c = 0; c < counts[r].size(); ++c) {
            rows[r] += counts[r][c];
            cols[c] += counts[r][c];
            total += counts[r][c];
        }
    double bits = 0;
    for (std::size_t r = 0; r < counts.size(); ++r)
        for (std::size_t c = 0; c < counts[r].size(); ++c)
            if (counts[r][c] > 0) bits += counts[r][c] / total * std::log2(counts[r][c] * total / (rows[r] * cols[c]));
    return bits;
}

int pick(const Values& values, const std::vector<int>& options, PyRandom& rng) {
    double roll = rng.random();
    int chosen = options.back();
    for (const auto& [option, chance] : choice_odds(values, options)) {
        roll -= chance;
        chosen = option;
        if (roll < 0) break;
    }
    return chosen;
}

struct Result {
    long converged = -1;
    double call_bits = 0, action_bits = 0, accuracy = 0;
};

Result run(const Options& o, std::uint64_t seed) {
    PyRandom rng(seed);
    Brain brains[2] = {Brain::random(rng), Brain::random(rng)};
    const double rate = LEARNING_RATE * (LEARNING_FLOOR + o.intellect * (1 + LEARNING_BOOST * o.intellect));
    std::vector<int> calls{call_output(Silent)};
    for (int call = FirstCall; call < CALL_OPTIONS; ++call) calls.push_back(call_output(call));
    std::vector<int> actions;
    for (int action = 0; action < o.facts; ++action) actions.push_back(action);
    const double needed = o.needed * std::log2(static_cast<double>(o.facts));
    std::vector<std::vector<int>> said(o.facts, std::vector<int>(calls.size(), 0)), done(o.facts, std::vector<int>(o.facts, 0));
    int right = 0, in_window = 0;
    Result result;
    const auto noisy = [&] {
        Senses senses{};
        for (double& sense : senses) sense = o.noise * rng.random();
        for (const int sense : FACT_SENSES) senses[static_cast<std::size_t>(sense)] = 0;
        for (int call = FirstCall; call < CALL_OPTIONS; ++call)
            for (int heard = 0; heard < HEARD_SENSES; ++heard) senses[static_cast<std::size_t>(heard_sense(call, heard))] = 0;
        return senses;
    };
    for (long trial = 1; trial <= o.trials; ++trial) {
        const int speaking = o.swap ? static_cast<int>(rng.randbelow(2)) : 0;
        Brain& speaker = brains[speaking];
        Brain& listener = brains[1 - speaking];
        const int fact = static_cast<int>(rng.randbelow(static_cast<std::uint64_t>(o.facts)));
        Senses spoken = noisy();
        spoken[static_cast<std::size_t>(FACT_SENSES[fact])] = 1.0;
        const int call_out = pick(speaker.values(spoken), calls, rng);
        const int call = call_out - ACTION_COUNT;
        Senses heard = noisy();
        if (call >= FirstCall && !o.deaf) heard[static_cast<std::size_t>(heard_sense(call, 0))] = 1.0;  // heard, loud
        const Values listened = listener.values(heard);
        const int action = pick(listened, actions, rng);
        const double gained = action == fact ? o.reward : 0.0;
        listener.learn(heard, action, gained, rate);
        if (o.mode == "shared") {
            speaker.learn(spoken, call_out, gained, rate);
        } else {
            speaker.learn(spoken, call_out, o.empathy * (gained - listened[action]), rate);
            if (o.silence && call >= FirstCall) speaker.learn(spoken, call_output(Silent), 0.0, rate);
        }
        ++said[fact][static_cast<std::size_t>(call < FirstCall ? 0 : call - FirstCall + 1)];  // silence, then the five
        ++done[fact][action];
        right += action == fact;
        if (++in_window == o.window) {
            result.call_bits = mutual_information(said);
            result.action_bits = mutual_information(done);
            result.accuracy = right / static_cast<double>(in_window);
            if (result.converged < 0 && result.action_bits >= needed) result.converged = trial;
            for (auto& row : said) std::fill(row.begin(), row.end(), 0);
            for (auto& row : done) std::fill(row.begin(), row.end(), 0);
            right = in_window = 0;
            if (result.converged >= 0 && trial >= result.converged + 5L * o.window) break;  // held a while: enough
        }
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int at = 1; at + 1 < argc; at += 2) {
        const std::string key = argv[at], value = argv[at + 1];
        if (key == "--facts") o.facts = std::stoi(value);
        else if (key == "--seeds") o.seeds = std::stoi(value);
        else if (key == "--trials") o.trials = std::stoi(value);
        else if (key == "--window") o.window = std::stoi(value);
        else if (key == "--needed") o.needed = std::stod(value);
        else if (key == "--reward") o.reward = std::stod(value);
        else if (key == "--noise") o.noise = std::stod(value);
        else if (key == "--empathy") o.empathy = std::stod(value);
        else if (key == "--temperature") o.temperature = std::stod(value);
        else if (key == "--intellect") o.intellect = std::stod(value);
        else if (key == "--mode") o.mode = value;
        else if (key == "--silence") o.silence = value != "0";
        else if (key == "--swap") o.swap = value != "0";
        else if (key == "--deaf") o.deaf = value != "0";
        else { std::fprintf(stderr, "unknown option %s\n", key.c_str()); return 2; }
    }
    if (o.facts < 2 || o.facts > 4) { std::fprintf(stderr, "--facts 2..4\n"); return 2; }
    TEMPERATURE = o.temperature;
    std::printf("mode %s facts %d reward %g noise %g empathy %g temperature %g intellect %g silence %d swap %d\n", o.mode.c_str(),
                o.facts, o.reward, o.noise, o.empathy, o.temperature, o.intellect, o.silence, o.swap);
    int converged = 0;
    std::vector<long> times;
    for (int seed = 1; seed <= o.seeds; ++seed) {
        const Result r = run(o, static_cast<std::uint64_t>(seed));
        std::printf("  seed %2d: %s  calls %.3f bits, actions %.3f bits, right %.2f\n", seed,
                    r.converged >= 0 ? ("code by trial " + std::to_string(r.converged)).c_str() : "no code", r.call_bits,
                    r.action_bits, r.accuracy);
        if (r.converged >= 0) { ++converged; times.push_back(r.converged); }
        std::fflush(stdout);
    }
    std::sort(times.begin(), times.end());
    std::printf("converged %d of %d", converged, o.seeds);
    if (!times.empty()) std::printf(", median trial %ld", times[times.size() / 2]);
    std::printf(" (of %d)\n", o.trials);
}
