#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

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

TEST_CASE("a brain is twice as deep as the inborn intellect, and at least two layers") {
    std::vector<int> depths;
    for (int intellect = 0; intellect < 8; ++intellect) depths.push_back(depth_for(intellect));
    CHECK(depths == std::vector<int>{2, 2, 4, 6, 8, 10, 12, 14});
    PyRandom rng(7);
    const Brain brain = Brain::random(rng, 6);
    CHECK((brain.depth() == 6 && brain.layers[0].reads == INPUT_SIZE && brain.layers[5].reads == HIDDEN_SIZE));
    CHECK(Brain::random(rng).depth() == MIN_DEPTH);
}

TEST_CASE("there is an input for each action and each ring and dot trait") {
    for (const auto action : ACTIONS) CHECK(sense_index("open_" + std::string(action)) >= 0);
    for (const auto trait : MIND_TRAITS) CHECK(sense_index("inborn_" + std::string(trait)) >= 0);
    for (const auto trait : BODY_TRAITS) CHECK(sense_index(trait) >= 0);
    Senses senses{};
    gene_senses(encode_dna(Species::Predator, {{"temperature", 2}, {"cold_tolerance", 1}, {"size", 7}, {"intellect", 7}}), senses);
    CHECK((senses[SenseBody] == 1.0 && senses[SenseBody + Size] == 1.0 && senses[SenseInborn + Intellect] == 1.0));
    CHECK((senses[SenseHomeTemperature] == doctest::Approx(2.0 / 3) && senses[SenseColdTolerance] == 1.0 &&
           senses[SenseHeatTolerance] == 0.0));
}

TEST_CASE("a child takes each hidden neuron whole from one parent") {
    PyRandom rng(3);
    const Brain mother = Brain::random(rng, 4), father = Brain::random(rng, 4);
    const Brain child = Brain::inherit(mother, father, rng, 4, 0.0);
    for (int index = 0; index < 4; ++index) {
        const Layer& layer = child.layers[static_cast<std::size_t>(index)];
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            const Layer& from_mother = mother.layers[static_cast<std::size_t>(index)];
            const bool mothers = std::equal(layer.row(neuron), layer.row(neuron) + layer.reads, from_mother.row(neuron));
            const Layer& donor = (mothers ? mother : father).layers[static_cast<std::size_t>(index)];
            CHECK(std::equal(layer.row(neuron), layer.row(neuron) + layer.reads, donor.row(neuron)));
            CHECK(layer.biases[neuron] == donor.biases[neuron]);
        }
    }
    for (int action = 0; action < ACTION_COUNT; ++action) {
        const Brain& donor = child.output_weights[action] == mother.output_weights[action] ? mother : father;
        CHECK(child.output_weights[action] == donor.output_weights[action]);
        CHECK(child.output_biases[action] == donor.output_biases[action]);
    }
}

TEST_CASE("a child deeper than a parent keeps the deeper parent's layers, then passes input on") {
    PyRandom rng(3);
    const Brain shallow = Brain::random(rng, 2), deep = Brain::random(rng, 4);
    const Brain child = Brain::inherit(shallow, deep, rng, 6, 0.0);
    CHECK(child.depth() == 6);
    for (const int index : {2, 3}) CHECK(child.layers[static_cast<std::size_t>(index)] == deep.layers[static_cast<std::size_t>(index)]);
    for (const int index : {4, 5}) CHECK(child.layers[static_cast<std::size_t>(index)] == Layer::identity());
    CHECK(Brain::inherit(deep, deep, rng, 2, 0.0).depth() == 2);
    const Brain deeper = deep.resized(6), shallower = deep.resized(2);
    CHECK((deeper.depth() == 6 && shallower.depth() == 2));
    CHECK((deeper.layers[3] == deep.layers[3] && deeper.layers[5] == Layer::identity() && shallower.layers[1] == deep.layers[1]));
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
    for (auto& row : renamed["layers"][0]["weights"]) row[0] = 9.0;
    const Brain partial = Brain::from_record(renamed);
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) CHECK(partial.layers[0].row(neuron)[SenseHunger] == 0.0);
    nlohmann::json broken = record;
    broken["layers"][0]["biases"] = "lots";
    CHECK_THROWS_AS(Brain::from_record(broken), std::invalid_argument);
    broken = record;
    broken["layers"] = nlohmann::json::array();
    CHECK_THROWS_AS(Brain::from_record(broken), std::invalid_argument);

    nlohmann::json single = record;  // saved before brains grew deeper: one hidden layer
    single["hidden_weights"] = record["layers"][0]["weights"];
    single["hidden_biases"] = record["layers"][0]["biases"];
    single.erase("layers");
    const Brain old = Brain::from_record(single);
    CHECK(old.depth() == 1);
    CHECK(old.layers[0].weights[5] == doctest::Approx(brain.layers[0].weights[5]).epsilon(1e-5));
}
