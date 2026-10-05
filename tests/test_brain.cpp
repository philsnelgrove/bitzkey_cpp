#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "core/Brain.hpp"

using namespace bitzkey;

TEST_CASE("learning moves the chosen action's value toward its target") {
    PyRandom rng(7);
    Brain brain = Brain::random(rng);
    Senses senses{};
    senses.fill(0.4);
    const double before = brain.values(senses)[Rest];
    for (int round = 0; round < 50; ++round) brain.learn(senses, Rest, 3.0, 0.05);
    CHECK(std::abs(brain.values(senses)[Rest] - 3.0) < std::abs(before - 3.0));
}

TEST_CASE("a child takes each hidden neuron whole from one parent") {
    PyRandom rng(3);
    const Brain mother = Brain::random(rng), father = Brain::random(rng);
    const Brain child = Brain::inherit(mother, father, rng, 0.0);
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
        const Brain& donor = child.hidden_weights[neuron] == mother.hidden_weights[neuron] ? mother : father;
        CHECK(child.hidden_weights[neuron] == donor.hidden_weights[neuron]);
        CHECK(child.hidden_biases[neuron] == donor.hidden_biases[neuron]);
        for (int action = 0; action < ACTION_COUNT; ++action) {
            CHECK(child.output_weights[action][neuron] == donor.output_weights[action][neuron]);
        }
    }
}

TEST_CASE("odds favour the better-valued action and sum to one") {
    Values values{};
    values[Eat] = 1.0;
    values[Rest] = 0.5;
    const std::vector<int> options = {Eat, Rest};
    const auto odds = choice_odds(values, options);
    CHECK(odds[0].second > odds[1].second);
    CHECK(odds[0].second + odds[1].second == doctest::Approx(1.0));
}

TEST_CASE("an exported brain loads back, matching senses and actions by name") {
    PyRandom rng(7);
    const Brain brain = Brain::random(rng);
    const nlohmann::json record = nlohmann::json::parse(brain.to_record().dump());
    const Brain loaded = Brain::from_record(record);
    Senses senses{};
    senses.fill(0.4);
    for (int action = 0; action < ACTION_COUNT; ++action) {
        CHECK(loaded.values(senses)[action] == doctest::Approx(brain.values(senses)[action]).epsilon(1e-4));
    }
    nlohmann::json renamed = record;
    renamed["senses"][0] = "smell";
    for (auto& row : renamed["hidden_weights"]) row[0] = 9.0;
    const Brain partial = Brain::from_record(renamed);
    for (const auto& row : partial.hidden_weights) CHECK(row[SenseHunger] == 0.0);
    nlohmann::json broken = record;
    broken["hidden_biases"] = "lots";
    CHECK_THROWS_AS(Brain::from_record(broken), std::invalid_argument);
}
