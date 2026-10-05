#include "core/Brain.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "core/PyFormat.hpp"

namespace bitzkey {

int action_index(std::string_view name) {
    for (int index = 0; index < ACTION_COUNT; ++index) {
        if (ACTIONS[index] == name) return index;
    }
    return -1;
}

int sense_index(std::string_view name) {
    for (int index = 0; index < INPUT_SIZE; ++index) {
        if (SENSES[index] == name) return index;
    }
    return -1;
}

std::vector<std::pair<int, double>> choice_odds(const Values& values, std::span<const int> options) {
    double best = values[options[0]];
    for (const int option : options) best = std::max(best, values[option]);
    std::vector<double> weights;
    weights.reserve(options.size());
    for (const int option : options) weights.push_back(std::exp((values[option] - best) / TEMPERATURE));
    const double total = py_sum(weights);
    std::vector<std::pair<int, double>> odds;
    odds.reserve(options.size());
    for (std::size_t index = 0; index < options.size(); ++index) odds.emplace_back(options[index], weights[index] / total);
    return odds;
}

Brain Brain::random(PyRandom& rng) {
    Brain brain;
    const double hidden_bound = std::sqrt(6.0 / (INPUT_SIZE + HIDDEN_SIZE));
    for (auto& row : brain.hidden_weights) {
        for (double& weight : row) weight = rng.uniform(-hidden_bound, hidden_bound);
    }
    for (auto& row : brain.output_weights) {
        for (double& weight : row) weight = rng.uniform(-0.3, 0.3);
    }
    for (double& bias : brain.output_biases) bias = rng.uniform(-0.2, 0.2);
    return brain;
}

Brain Brain::inherit(const Brain& first, const Brain& second, PyRandom& rng, double mutation) {
    std::array<bool, HIDDEN_SIZE> from_first{};
    for (bool& choice : from_first) choice = rng.random() < 0.5;
    std::array<bool, ACTION_COUNT> bias_from_first{};
    for (bool& choice : bias_from_first) choice = rng.random() < 0.5;
    Brain child;
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
        const Brain& donor = from_first[neuron] ? first : second;
        child.hidden_weights[neuron] = donor.hidden_weights[neuron];
        child.hidden_biases[neuron] = donor.hidden_biases[neuron];
        for (int action = 0; action < ACTION_COUNT; ++action) {
            child.output_weights[action][neuron] = donor.output_weights[action][neuron];
        }
    }
    for (int action = 0; action < ACTION_COUNT; ++action) {
        child.output_biases[action] = (bias_from_first[action] ? first : second).output_biases[action];
    }
    if (mutation != 0.0) {
        for (auto& row : child.hidden_weights) {
            for (double& weight : row) weight += rng.gauss(0, mutation);
        }
        for (auto& row : child.output_weights) {
            for (double& weight : row) weight += rng.gauss(0, mutation);
        }
        for (double& bias : child.hidden_biases) bias += rng.gauss(0, mutation);
        for (double& bias : child.output_biases) bias += rng.gauss(0, mutation);
    }
    return child;
}

void Brain::forward(const Senses& inputs, std::array<double, HIDDEN_SIZE>& hidden, Values& values) const {
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
        double sum = 0.0;
        const auto& weights = hidden_weights[neuron];
        for (int input = 0; input < INPUT_SIZE; ++input) sum += weights[input] * inputs[input];
        hidden[neuron] = std::tanh(sum + hidden_biases[neuron]);
    }
    for (int action = 0; action < ACTION_COUNT; ++action) {
        double sum = 0.0;
        const auto& weights = output_weights[action];
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) sum += weights[neuron] * hidden[neuron];
        values[action] = sum + output_biases[action];
    }
}

Values Brain::values(const Senses& inputs) const {
    std::array<double, HIDDEN_SIZE> hidden{};
    Values values{};
    forward(inputs, hidden, values);
    return values;
}

void Brain::learn(const Senses& inputs, int action, double target, double learning_rate) {
    if (learning_rate <= 0) return;
    std::array<double, HIDDEN_SIZE> hidden{};
    Values values{};
    forward(inputs, hidden, values);
    const double error = std::max(-1.0, std::min(1.0, values[action] - target));
    std::array<double, HIDDEN_SIZE> hidden_delta{};
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
        hidden_delta[neuron] = error * output_weights[action][neuron] * (1 - hidden[neuron] * hidden[neuron]);
    }
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
        output_weights[action][neuron] -= learning_rate * error * hidden[neuron];
    }
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) hidden_biases[neuron] -= learning_rate * hidden_delta[neuron];
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
        for (int input = 0; input < INPUT_SIZE; ++input) {
            hidden_weights[neuron][input] -= learning_rate * (hidden_delta[neuron] * inputs[input]);
        }
    }
    output_biases[action] -= learning_rate * error;
}

nlohmann::ordered_json Brain::to_record() const {
    using json = nlohmann::ordered_json;
    const auto rounded = [](auto begin, auto end) {
        json list = json::array();
        for (auto it = begin; it != end; ++it) list.push_back(py_round(*it, RECORD_DIGITS));
        return list;
    };
    json record = json::object();
    record["senses"] = json::array();
    for (const auto name : SENSES) record["senses"].push_back(std::string(name));
    record["actions"] = json::array();
    for (const auto name : ACTIONS) record["actions"].push_back(std::string(name));
    record["hidden_weights"] = json::array();
    for (const auto& row : hidden_weights) record["hidden_weights"].push_back(rounded(row.begin(), row.end()));
    record["hidden_biases"] = rounded(hidden_biases.begin(), hidden_biases.end());
    record["output_weights"] = json::array();
    for (const auto& row : output_weights) record["output_weights"].push_back(rounded(row.begin(), row.end()));
    record["output_biases"] = rounded(output_biases.begin(), output_biases.end());
    return record;
}

namespace {
std::vector<double> numbers(const nlohmann::json& list) {
    if (!list.is_array()) throw std::invalid_argument("expected a list of numbers");
    std::vector<double> values;
    for (const auto& item : list) {
        if (!item.is_number()) throw std::invalid_argument("expected a number, found " + item.dump());
        values.push_back(item.get<double>());
    }
    return values;
}
std::vector<std::string> names(const nlohmann::json& list) {
    if (!list.is_array()) throw std::invalid_argument("expected a list of names");
    std::vector<std::string> values;
    for (const auto& item : list) values.push_back(item.is_string() ? item.get<std::string>() : item.dump());
    return values;
}
}  // namespace

Brain Brain::from_record(const nlohmann::json& record) {
    std::vector<std::string> senses, actions;
    std::vector<std::vector<double>> hidden_rows, output_rows;
    std::vector<double> hidden_bias, output_bias;
    try {
        if (!record.is_object()) throw std::invalid_argument("not an object");
        senses = names(record.at("senses"));
        actions = names(record.at("actions"));
        for (const auto& row : record.at("hidden_weights")) hidden_rows.push_back(numbers(row));
        hidden_bias = numbers(record.at("hidden_biases"));
        for (const auto& row : record.at("output_weights")) output_rows.push_back(numbers(row));
        output_bias = numbers(record.at("output_biases"));
    } catch (const std::exception& error) {
        throw std::invalid_argument(std::string("The brain is incomplete or not numeric (") + error.what() + ").");
    }
    if (hidden_rows.size() != HIDDEN_SIZE || hidden_bias.size() != HIDDEN_SIZE) {
        throw std::invalid_argument("The brain has " + std::to_string(hidden_rows.size()) +
                                    " hidden neurons; this version uses " + std::to_string(HIDDEN_SIZE) + ".");
    }
    bool mismatched = output_rows.size() != actions.size() || output_bias.size() != actions.size();
    for (const auto& row : hidden_rows) mismatched = mismatched || row.size() != senses.size();
    for (const auto& row : output_rows) mismatched = mismatched || row.size() != HIDDEN_SIZE;
    if (mismatched) throw std::invalid_argument("The brain's weights don't match its listed senses and actions.");

    std::unordered_map<std::string, std::size_t> sense_at, action_at;
    // As in a Python dict built from the lists, a repeated name keeps its last position.
    for (std::size_t index = 0; index < senses.size(); ++index) sense_at[senses[index]] = index;
    for (std::size_t index = 0; index < actions.size(); ++index) action_at[actions[index]] = index;
    Brain brain;
    for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
        for (int input = 0; input < INPUT_SIZE; ++input) {
            const auto found = sense_at.find(std::string(SENSES[input]));
            brain.hidden_weights[neuron][input] = found == sense_at.end() ? 0.0 : hidden_rows[neuron][found->second];
        }
        brain.hidden_biases[neuron] = hidden_bias[neuron];
    }
    for (int action = 0; action < ACTION_COUNT; ++action) {
        const auto found = action_at.find(std::string(ACTIONS[action]));
        for (int neuron = 0; neuron < HIDDEN_SIZE; ++neuron) {
            brain.output_weights[action][neuron] = found == action_at.end() ? 0.0 : output_rows[found->second][neuron];
        }
        brain.output_biases[action] = found == action_at.end() ? 0.0 : output_bias[found->second];
    }
    return brain;
}

}  // namespace bitzkey
