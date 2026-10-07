#pragma once
// Per-animal neural network: what a Bitzkey senses, what it can do, and how it learns.

#include <array>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "core/Genome.hpp"
#include "core/PyRandom.hpp"

namespace bitzkey {

enum Sense : int {
    SenseHunger,           // 0..1
    SenseHealth,           // 0..1
    SenseGrassHere,        // 1 when grass lies on the animal's own cell
    SenseGrassSeen,        // 1 when grass is visible elsewhere
    SenseGrassCloseness,   // 1 next to the nearest visible grass, falling to 0 at the edge of sight
    SenseMateNear,         // 1 when a compatible, ready mate is adjacent
    SenseMateCloseness,    // 1 when a willing mate is adjacent, falling to 0 beyond sight
    SenseReadyToBreed,     // 1 when the animal's breeding cooldown has passed
    SensePreyNear,         // adjacent prey, 0..4 scaled to 0..1
    SensePreyFrailty,      // share of the frailest adjacent prey's health one blow would take
    SensePreyCloseness,    // 1 when prey is adjacent, falling to 0 beyond sight
    SenseThreatNear,       // adjacent hunters, 0..4 scaled to 0..1
    SenseThreatDanger,     // share of its own health the worst adjacent hunter's blow would take
    SenseThreatCloseness,  // 1 when a hunter is adjacent, falling to 0 beyond sight
    SenseKinNear,          // same-species animals within two cells, 0..3 scaled to 0..1
    SenseOthersNear,       // animals of other species within two cells, 0..3 scaled to 0..1
    SenseCarrionCloseness, // 1 when meat lies alongside, falling to 0 beyond sight
    SensePlantCloseness,   // 1 when a plant stands alongside, falling to 0 beyond sight
    SenseAssailantNear,    // 1 when the animal that just struck it is still alongside
    SenseCounterFrailty,   // share of that assailant's health one counterblow would take
    SenseRivalry,          // predators and omnivores: how overcrowded its territory is with rivals
    SenseHerdNear,         // herd-mates within two cells, 0..3 scaled to 0..1
    SenseHerdCloseness,    // 1 at the middle of its herd in range, falling to 0 at the range's edge
    SenseAlarm,            // 1 when a herd-mate close by was struck since it last acted
    SenseClimateDiscomfort,  // how far the temperature here is from home, as it feels it: 0, 0.5 a step, 1 two or more
    SenseComfortCloseness,   // 1 where it feels at home or next to such a row, falling to 0 beyond sight
    SenseAge,              // 0 at birth, 1 at 400 turns or older
    SenseMind,             // then its current temperament, each trait 0..7 scaled to 0..1
};
constexpr int INPUT_SIZE = SenseMind + MIND_TRAIT_COUNT;
constexpr std::array<std::string_view, INPUT_SIZE> SENSES = {
    "hunger", "health", "grass_here", "grass_seen", "grass_closeness", "mate_near", "mate_closeness",
    "ready_to_breed", "prey_near", "prey_frailty", "prey_closeness", "threat_near", "threat_danger",
    "threat_closeness", "kin_near", "others_near", "carrion_closeness", "plant_closeness", "assailant_near",
    "counter_frailty", "rivalry", "herd_near", "herd_closeness", "alarm", "climate_discomfort", "comfort_closeness", "age",
    "aggression", "libido", "sociability", "curiosity", "fear", "appetite", "patience", "intellect",
    "empathy", "tolerance"};

enum Action : int { Eat, Forage, Browse, Scavenge, Rest, Court, Breed, Hunt, Attack, Defend, Flee, Herd, Explore };
constexpr int ACTION_COUNT = 13;
constexpr std::array<std::string_view, ACTION_COUNT> ACTIONS = {
    "eat", "forage", "browse", "scavenge", "rest", "court", "breed", "hunt", "attack", "defend", "flee",
    "herd", "explore"};
int action_index(std::string_view name);  // -1 when unknown
int sense_index(std::string_view name);   // -1 when unknown

constexpr int HIDDEN_SIZE = 12;
inline double TEMPERATURE = 0.2;
inline double MUTATION = 0.05;
constexpr int RECORD_DIGITS = 6;  // decimal places kept when a brain is exported

using Senses = std::array<double, INPUT_SIZE>;
using Values = std::array<double, ACTION_COUNT>;

// Softmax over the available actions' values, in the options' order.
std::vector<std::pair<int, double>> choice_odds(const Values& values, std::span<const int> options);

// One-hidden-layer tanh network estimating the long-run value of each action. It learns online
// from temporal-difference targets and is inherited neuron by neuron.
class Brain {
public:
    std::array<std::array<double, INPUT_SIZE>, HIDDEN_SIZE> hidden_weights{};
    std::array<double, HIDDEN_SIZE> hidden_biases{};
    std::array<std::array<double, HIDDEN_SIZE>, ACTION_COUNT> output_weights{};
    std::array<double, ACTION_COUNT> output_biases{};

    static Brain random(PyRandom& rng);
    // Take each hidden neuron, with its outgoing weights, from one parent, then mutate.
    static Brain inherit(const Brain& first, const Brain& second, PyRandom& rng, double mutation = MUTATION);

    Values values(const Senses& inputs) const;
    // Move one action's value toward a target, with the error clipped for stability.
    void learn(const Senses& inputs, int action, double target, double learning_rate);

    nlohmann::ordered_json to_record() const;
    // Weights are matched to senses and actions by name. Throws std::invalid_argument.
    static Brain from_record(const nlohmann::json& record);

    bool operator==(const Brain&) const = default;

private:
    void forward(const Senses& inputs, std::array<double, HIDDEN_SIZE>& hidden, Values& values) const;
};

}  // namespace bitzkey
