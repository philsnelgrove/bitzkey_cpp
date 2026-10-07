#pragma once
// Per-animal neural network: what a Bitzkey senses, what it can do, and how it learns.

#include <algorithm>
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
    SenseCarrionCloseness, // 1 when meat lies alongside, falling to 0 beyond sight (not grazers)
    SenseKillCloseness,    // 1 when a fresh kill it eats lies alongside, falling to 0 beyond sight (hunters only)
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
enum Action : int { Eat, Forage, Browse, Scavenge, Feast, Rest, Court, Breed, Hunt, Attack, Defend, Flee, Herd, Explore };
constexpr int ACTION_COUNT = 14;
constexpr std::array<std::string_view, ACTION_COUNT> ACTIONS = {
    "eat", "forage", "browse", "scavenge", "feast", "rest", "court", "breed", "hunt", "attack", "defend", "flee",
    "herd", "explore"};
constexpr int SenseOpen = SenseMind + MIND_TRAIT_COUNT;    // then 1 for each action open to it now
constexpr int SenseInborn = SenseOpen + ACTION_COUNT;      // then its dot's traits, as born, 0..1
constexpr int SenseBody = SenseInborn + MIND_TRAIT_COUNT;  // then its ring's traits, 0..1 (species 0 plant to 1 predator)
constexpr int SenseHomeTemperature = SenseBody + BODY_TRAIT_COUNT;  // then its climate genes: home 0 hot to 1 very cold,
constexpr int SenseHeatTolerance = SenseHomeTemperature + 1;        // and the tolerance flags, 0 or 1
constexpr int SenseColdTolerance = SenseHeatTolerance + 1;
constexpr int INPUT_SIZE = SenseColdTolerance + 1;
constexpr std::array<std::string_view, INPUT_SIZE> SENSES = {
    "hunger", "health", "grass_here", "grass_seen", "grass_closeness", "mate_near", "mate_closeness",
    "ready_to_breed", "prey_near", "prey_frailty", "prey_closeness", "threat_near", "threat_danger",
    "threat_closeness", "kin_near", "others_near", "carrion_closeness", "kill_closeness", "plant_closeness",
    "assailant_near", "counter_frailty", "rivalry", "herd_near", "herd_closeness", "alarm",
    "climate_discomfort", "comfort_closeness", "age", "aggression", "libido", "sociability", "curiosity",
    "fear", "appetite", "patience", "intellect", "empathy", "tolerance", "open_eat", "open_forage",
    "open_browse", "open_scavenge", "open_feast", "open_rest", "open_court", "open_breed", "open_hunt",
    "open_attack", "open_defend", "open_flee", "open_herd", "open_explore", "inborn_aggression",
    "inborn_libido", "inborn_sociability", "inborn_curiosity", "inborn_fear", "inborn_appetite",
    "inborn_patience", "inborn_intellect", "inborn_empathy", "inborn_tolerance", "species", "size", "strength",
    "armour", "weapons", "speed", "sight", "fertility", "litter", "carrion", "home_temperature",
    "heat_tolerance", "cold_tolerance"};
int action_index(std::string_view name);  // -1 when unknown
int sense_index(std::string_view name);   // -1 when unknown

constexpr int HIDDEN_SIZE = 12;  // neurons in each hidden layer
constexpr int MIN_DEPTH = 2;     // hidden layers: twice the inborn intellect, and never fewer than this
inline double TEMPERATURE = 0.2;
inline double MUTATION = 0.05;
constexpr int RECORD_DIGITS = 6;  // decimal places kept when a brain is exported

using Senses = std::array<double, INPUT_SIZE>;
using Values = std::array<double, ACTION_COUNT>;

// How many hidden layers a brain has, by its owner's inborn intellect.
inline int depth_for(int intellect) { return std::max(MIN_DEPTH, 2 * intellect); }
// Fill in the inputs for a genome's traits, ring and dot, and its climate genes.
void gene_senses(Dna dna, Senses& senses);

// Softmax over the available actions' values, in the options' order.
std::vector<std::pair<int, double>> choice_odds(const Values& values, std::span<const int> options);

// One hidden layer: a row of weights for each neuron, reading the senses (the first layer) or the
// layer before.
struct Layer {
    int reads = HIDDEN_SIZE;
    std::vector<double> weights;  // HIDDEN_SIZE rows of `reads`
    std::array<double, HIDDEN_SIZE> biases{};

    double* row(int neuron) { return weights.data() + static_cast<std::size_t>(neuron) * reads; }
    const double* row(int neuron) const { return weights.data() + static_cast<std::size_t>(neuron) * reads; }
    static Layer identity();  // passes its input on, near enough
    bool operator==(const Layer&) const = default;
};

// A tanh network of hidden layers estimating the long-run value of each action. It learns online
// from temporal-difference targets and is inherited neuron by neuron.
class Brain {
public:
    std::vector<Layer> layers;
    std::array<std::array<double, HIDDEN_SIZE>, ACTION_COUNT> output_weights{};
    std::array<double, ACTION_COUNT> output_biases{};

    int depth() const { return static_cast<int>(layers.size()); }
    static Brain random(PyRandom& rng, int depth = MIN_DEPTH);
    // A child's brain, `depth` layers deep. Where both parents have a layer, each of its neurons
    // comes from one parent or the other; where only one has it, from that one; deeper still, it
    // starts passing its input on. Each action's output comes from one parent or the other. Then
    // it mutates.
    static Brain inherit(const Brain& first, const Brain& second, PyRandom& rng, int depth = MIN_DEPTH,
                         double mutation = MUTATION);
    // A copy `depth` layers deep: deeper with layers that pass their input on, or shallower
    // without its last layers.
    Brain resized(int depth) const;

    Values values(const Senses& inputs) const;
    // Move one action's value toward a target, with the error clipped for stability, back through
    // every layer.
    void learn(const Senses& inputs, int action, double target, double learning_rate);

    nlohmann::ordered_json to_record() const;
    // Weights are matched to senses and actions by name; a brain saved with a single hidden
    // layer loads one layer deep. Throws std::invalid_argument.
    static Brain from_record(const nlohmann::json& record);

    bool operator==(const Brain&) const = default;

private:
    std::vector<std::vector<double>> activations(const Senses& inputs) const;  // the senses, then each layer's output
};

}  // namespace bitzkey
