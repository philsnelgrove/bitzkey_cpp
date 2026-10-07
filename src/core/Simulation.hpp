#pragma once
// Turn-based Bitzkey world and its deterministic history.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Brain.hpp"
#include "core/Census.hpp"
#include "core/Exchange.hpp"
#include "core/Genome.hpp"
#include "core/Insights.hpp"
#include "core/PyRandom.hpp"

namespace bitzkey {

constexpr int GRID_SIZE = 100;
constexpr int INITIAL_POPULATION = 90;
constexpr double GRASS_DENSITY = 50;      // percent of cells that start with grass
inline double GRASS_REGROWTH = 0.004;  // share of the starting grass that grows back each turn
constexpr int MAX_POPULATION = 600;       // the most Bitzkeys spawned at the start
inline int CROWDING_RADIUS = 4;
inline int CROWDING_LIMIT = 10;
constexpr std::array<int, 2> SPAWN_GROUP = {3, 5};
constexpr int MAX_IMPORTED = MAX_POPULATION / SPAWN_GROUP[1];
inline int SPAWN_SPREAD = 2;
constexpr int DENSE_HISTORY = 500;
constexpr int CHECKPOINTS = 200;
// History keeps to a memory budget: when a snapshot of the world grows large, fewer recent turns
// are kept and fewer checkpoints, so that all of them fit.
constexpr std::size_t HISTORY_BUDGET = std::size_t{1} << 30;  // bytes
constexpr std::size_t MIN_SNAPSHOTS = 20;
inline int CORPSE_FERTILITY = 2;
inline int CORPSE_TURNS = 30;
inline double BREED_RESERVE = 10;
inline double MATE_RING_DIFFERENCE = 0.05;
inline double MATE_DOT_DIFFERENCE = 0.20;
inline std::array<int, 2> LITTER_RANGE = {1, 6};
inline double LITTER_SIZE_WEIGHT = 0.5;
inline double PLANT_LIFESPAN = 4000;
inline std::array<int, 2> ANIMAL_LIFESPAN = {250, 900};
inline std::array<double, 2> METABOLISM = {1.6, 0.6};
inline std::array<double, 2> STOMACH = {0.5, 2.0};
inline double SIZE_IN_MIGHT = 0.5;
inline double AGEING = 0.7;
inline double DEATH_PENALTY = 2.0;
inline double BREED_JOY = 1.0;
inline double FIGHT_JOY = 0.5;
inline double COMPANY_JOY = 0.1;
inline double CURIOSITY_JOY = 0.15;
inline double FEAR_DREAD = 0.3;
inline double UNEASE = 0.1;
inline double EMPATHY_PAIN = 0.15;
inline double OTHER_KIND = 0.4;
inline double LEARNING_RATE = 0.045;
inline std::array<double, 2> PANIC = {0.3, 1.0};
inline double THORN_FORCE = 15;
inline double THORN_SIZE_SHARE = 0.2;
inline int TERRITORY_RADIUS = 6;
inline int TERRITORY_RAMP = 3;
inline double RIVAL_UNEASE = 0.3;
inline double RIVAL_KILL_JOY = 1.0;
inline int HERD_RADIUS = 4;
inline int HERD_SPREAD = 3;
inline double HERD_JOY = 0.15;
inline double HERD_LONELINESS = 0.1;
inline double HERD_SAFETY = 0.6;
inline double HERD_DEFEND_JOY = 0.5;
inline double PACK_BONUS = 0.5;
inline double KIND_DIFFERENCE = 0.05;
// Breeding responsibly: before breeding, an animal weighs the food it eats within FORAGE_RADIUS
// against the mouths that share it, itself included. Below FOOD_NEEDED meals a mouth it doesn't
// breed; between that and twice that, its litter shrinks in proportion.
inline int FORAGE_RADIUS = 6;
inline double FOOD_NEEDED = 3.0;
inline double PLANT_MEALS = 2.0;   // meals a plant is worth to a full-efficiency browser
inline double PREY_MEALS = 2.0;    // meals a prey animal is worth
inline double SHARED_DIET = 0.5;
// Prey switching: a hunter goes after a kind of prey only while it makes up at least SWITCH_SHARE
// of the food it weighs within FORAGE_RADIUS (for omnivores, grazers against plants; for predators,
// grazers against omnivores), so a scarce prey is left alone to recover.
inline double SWITCH_SHARE = 0.25;   // how much another species eating some of the same food counts as a mouth
inline double STRIKE_EFFORT = 0.2;
inline double PREDATOR_LITTERS = 0.5;
inline double STRIKE_FORCE = 1.5;
inline double KILL_MEAL = 25;
inline double KILL_MEAL_FOR_SIZE = 45;
inline double CARRION_MEAT = 30;
inline double CARRION_MEAT_FOR_SIZE = 50;
inline double SCAVENGE_BITE = 25;
inline double GRAZER_SPEED = 1.5;
inline int FLEE_RANGE = 3;          // a hunter is only worth fleeing within this many steps, unless alarmed
inline double HERD_VIGILANCE = 0.5; // how far three herd-mates close by, at full instinct, shrink that range
inline double GRASS_OUTLOOK = 150;  // judging grass before breeding, count what regrows in this many turns
inline double STANDING_GRASS = 0.25;  // and this share of the grass standing now
inline double GRASS_MEAL = 35;
inline double PLANT_MEAL = 45;
inline double PLANT_MEAL_FOR_SIZE = 25;
inline double PLANT_BITE = 15;
inline double PLANT_REGROWTH = 0.02;
inline int PLANT_RECOVERY = 8;
inline std::array<double, 2> SEED_CHANCE = {0.03, 0.10};
inline double SEED_COST = 20;
inline double SEED_HUNGER = 40;
inline double SEEDLING_HUNGER = 50;
inline int SEED_REACH = 5;
inline int LIGHT_RADIUS = 3;  // plants compete for light with the plants within this many cells
inline int LIGHT_LIMIT = 3;   // each cuts a plant's chance of seeding by this share, to none at this many
inline int PLANT_CROWDING = 3;
inline double TEMPERAMENT_RELAPSE = 0.005;
constexpr int MAX_SIGHT = 2 + TRAIT_MAX;
// For each territorial species, predators then omnivores: how many rivals it tolerates from the
// least to the most empathetic, and how strongly overcrowding moves it (its rivalry at most).
struct Territory {
    int fewest, most;
    double strength;
};
inline std::array<Territory, 2> TERRITORY = {{{1, 5, 1.0}, {2, 10, 0.5}}};
// Separate niches, by species: grazers are the specialists, getting more from grass and plants than
// anyone; omnivores live on meat, getting only three quarters of the food in plants and half of grass.
inline std::array<double, 4> GRAZING = {0.0, 1.25, 0.5, 0.0};   // share of the food in grass each species gets
inline std::array<double, 4> BROWSING = {0.0, 1.25, 0.75, 0.0}; // share of the food in plants
// How experiences reshape temperament, in shares of a trait's full range before scaling by
// plasticity: for each experience, the mind traits it shifts.
struct Shift {
    int trait;
    double change;
};
constexpr std::array<std::string_view, 13> EXPERIENCE_NAMES = {
    "kill", "hit", "stung", "wounded", "wounded_alone", "bred", "bred_into_hunger", "starving", "foraged_meal",
    "discovery", "ambushed", "defended", "panicked"};
extern std::array<std::vector<Shift>, 13> EXPERIENCES;

struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell&) const = default;
};

struct Bitzkey {
    Bitzkey(int identifier, Dna dna, int x, int y);

    int identifier;
    Dna dna;
    int x, y;
    double hunger = 0.0;
    double health = 100.0;
    int age = 0;
    int cooldown = 0;
    std::string_view last_action = "new";
    std::shared_ptr<Brain> brain;  // shared with history snapshots; copied before it learns
    int generation = 0;
    int children = 0;
    std::optional<int> died_turn;
    std::optional<int> assailant;       // the animal that struck it since it last acted
    bool panicked = false;              // failed a morale check when struck
    double rivalry = 0.0;               // how overcrowded its territory was with rivals
    double own_rivalry = 0.0;           // the same, counting only its own species
    bool hunts_grazers = true;          // hunters: whether grazers were common enough to hunt
    bool hunts_omnivores = true;        // predators: the same for omnivores
    std::optional<int> alarm;           // what struck a herd-mate close by since it last acted
    std::optional<int> herd_assailant;  // what struck a herd-mate close by, from alongside it
    double meat = 0.0;                  // a corpse's meat left for scavengers
    double rot = 0.0;                   // meat lost to rot each turn
    Mind mind{};                        // current temperament, reshaped by experience
    // The last decision, kept until the next one so the brain can learn from how it turned out.
    bool has_last_senses = false;
    Senses last_senses{};
    int last_choice = 0;
    double last_wellbeing = 0.0;
    double reward = 0.0;
    std::array<double, ACTION_COUNT> odds{};  // chance of each action at the last choice
    std::uint16_t odds_offered = 0;           // which actions were open then, one bit each

    // Fixed by the genome.
    Species species;
    Traits inborn;
    Body body;
    double max_health, metabolism, stomach, burn, might, toughness, lifespan, breeding_cost, digestion;
    double grazing;   // share of the food in grass it gets
    double browsing;  // share of the food in plants
    int steps, sight;

    double physique(int trait) const { return share(body[trait]); }
    double feeling(int trait) const { return share(mind[trait]); }
    double herd_instinct() const { return std::min(feeling(Empathy), feeling(Patience)); }
    Profile profile() const;
    bool can_breed() const { return cooldown == 0 && hunger + breeding_cost <= 100 - BREED_RESERVE; }
    bool alive() const { return health > 0 && hunger < 100; }
    void nourish(double food) { hunger = std::max(0.0, hunger - food / stomach); }
    void heal(double amount) { health = std::min(max_health, health + max_health * amount * metabolism); }
    Brain& own_brain();  // the brain, made this animal's own before it changes
    Rgb dot_color() const { return color_from_word(mind_word(dna)); }
    Rgb ring_color() const { return color_from_word(body_word(dna)); }
};

int litter_size(std::span<const std::pair<int, int>> parents, Species species);

enum class EndReason { None, Extinct, Full };

class Simulation {
public:
    struct Settings {
        int width = GRID_SIZE;
        int height = GRID_SIZE;
        int population = INITIAL_POPULATION;
        std::optional<std::uint64_t> seed;
        double grass_density = GRASS_DENSITY;
        std::vector<Imported> imported;
        std::optional<int> population_cap;
        double mind_mutation = 100 * MIND_MUTATION_CHANCE;
        double body_mutation = 100 * BODY_MUTATION_CHANCE;
    };

    explicit Simulation(Settings settings);
    Simulation(int width, int height, int population, std::optional<std::uint64_t> seed);

    int width() const { return width_; }
    int height() const { return height_; }
    int turn() const { return turn_; }
    int frontier() const { return frontier_; }
    int deaths() const { return deaths_; }
    int next_identifier() const { return next_identifier_; }
    std::optional<int> population_cap() const { return population_cap_; }
    void set_population_cap(std::optional<int> cap) { population_cap_ = cap; }
    double mind_mutation() const { return mind_mutation_; }
    double body_mutation() const { return body_mutation_; }
    void set_mutation(double mind, double body) { mind_mutation_ = mind; body_mutation_ = body; }

    const std::vector<std::unique_ptr<Bitzkey>>& agents() const { return agents_; }
    std::vector<Bitzkey*> living() const;
    int grass_at(int x, int y) const;
    int& grass_cell(int x, int y) { return grass_[static_cast<std::size_t>(y) * width_ + x]; }
    void clear_grass() { std::fill(grass_.begin(), grass_.end(), 0); }
    std::size_t grass_tufts() const;
    const Insights& insights() const { return insights_; }
    const Census& census() const { return census_; }
    Dna lineage(int identifier) const { return lineage_.at(static_cast<std::size_t>(identifier)); }
    PyRandom& random() { return random_; }
    double grass_regrowth() const { return grass_regrowth_; }
    void set_grass_regrowth(double tufts) { grass_regrowth_ = tufts; }

    // Step one turn: replaying the recorded run, or simulating a new turn, unless it has ended.
    void advance();
    // Move the world to any turn already simulated; replaying repeats the recorded run exactly.
    void seek(int target);
    bool rewind();
    int replay_cost(int target) const;
    EndReason end_reason() const;
    bool ended() const { return turn_ == frontier_ && end_reason() != EndReason::None; }

    // The rest is public so tests can drive single steps, as the Python tests do.
    void take_turn(bool replay = false);
    Bitzkey& add_agent(Dna dna, int x, int y, std::shared_ptr<Brain> brain = nullptr,
                       const std::optional<PartialMind>& mind = std::nullopt);
    void index_cells();

    struct Situation {
        Bitzkey* mate = nullptr;
        std::vector<Cell> nurseries;
        Bitzkey* prey = nullptr;
        Bitzkey* threat = nullptr;
        int threats = 0;
        Bitzkey* quarry = nullptr;
        Bitzkey* hunter = nullptr;
        Bitzkey* suitor = nullptr;
        std::optional<Cell> grass;
        bool grass_here = false;
        Bitzkey* carrion = nullptr;
        Bitzkey* flora = nullptr;
        Bitzkey* assailant = nullptr;
        double rivalry = 0.0;
        int kin = 0;
        int others = 0;
        Bitzkey* defend_target = nullptr;
        bool for_herd = false;
        bool herd = true;
        int herd_near = 0;
        std::optional<Cell> herd_middle;
        bool panic = false;
    };
    struct Sensed {
        Senses senses{};
        std::vector<int> options;
        Situation situation;
    };

    Sensed sense(Bitzkey& agent);
    void think(Bitzkey& agent);
    void act(Bitzkey& agent, int action, Situation& situation);
    double comfort(const Bitzkey& agent, const Situation& situation) const;
    double wellbeing(const Bitzkey& agent) const;
    void die(Bitzkey& agent, std::string_view cause);
    void attack(Bitzkey& agent, Bitzkey& victim);
    void defend(Bitzkey& agent, Bitzkey& assailant, bool for_herd = false);
    void struck(Bitzkey& victim, const Bitzkey& striker);
    void exhausted(Bitzkey& agent);
    void breed(Bitzkey& first, Bitzkey& second, std::vector<Cell> cells);
    void browse(Bitzkey& agent, Bitzkey& plant);
    void scavenge(Bitzkey& agent, Bitzkey& corpse);
    void sow(int tufts);
    void experience(Bitzkey& agent, int event);
    std::vector<Bitzkey*> nearby(const Bitzkey& agent, int radius) const;
    std::vector<Bitzkey*> nearby_cell(Cell cell, int radius, const Bitzkey* except) const;
    std::vector<Cell> free_cells_near(const Bitzkey& agent) const;
    std::pair<double, double> territory(const Bitzkey& hunter) const;
    struct HerdView {
        bool any = false;
        int close = 0;
        std::optional<Cell> middle;
    };
    HerdView herd(const Bitzkey& agent) const;
    double room() const;
    int crowding(const Bitzkey& agent, const Bitzkey& partner) const;

    static double damage(const Bitzkey& attacker, const Bitzkey& defender);
    static bool compatible(const Bitzkey& first, const Bitzkey& second);
    static bool willing(const Bitzkey& agent, const Bitzkey& other);
    static bool same_kind(const Bitzkey& first, const Bitzkey& second);
    static double cannibal_reluctance(const Bitzkey& animal);
    static double rival_weight(const Bitzkey& hunter, const Bitzkey& other);
    static bool is_prey(const Bitzkey& hunter, const Bitzkey& other);
    static bool feeds_on(const Bitzkey& eater, const Bitzkey& other);
    static double diet_share(const Bitzkey& eater, const Bitzkey& other);
    double food_supply(const Bitzkey& agent) const;
    std::pair<bool, bool> prey_switching(const Bitzkey& hunter) const;
    static double flee_range(const Bitzkey& agent, int herd_near);
    bool herd_mate(const Bitzkey& agent, const Bitzkey& other) const;

private:
    struct Snapshot {
        int turn = 0;
        int deaths = 0;
        int next_identifier = 0;
        std::vector<Bitzkey> agents;
        std::vector<int> grass;
        PyRandom::State random;
        Insights::State insights;
    };

    std::vector<int> group_sizes(int population);
    std::pair<int, int> history_room() const;  // recent turns kept every turn, and older checkpoints
    void spawn_group(Dna dna, int size, const std::shared_ptr<const Brain>& brain = nullptr,
                     const std::optional<PartialMind>& mind = std::nullopt);
    void record();
    void restore(const Snapshot& snapshot);
    std::pair<const Snapshot*, int> start_for(int target) const;
    void take_census();
    bool dies_of_age(Bitzkey& agent);
    void seed_plant(Bitzkey& plant);
    void settle(Bitzkey& agent);
    void learn(Bitzkey& agent, double target);
    std::optional<Cell> nearest_grass(const Bitzkey& agent, int sight) const;
    Bitzkey* nearest_carrion(const Bitzkey& agent, int sight) const;
    struct Seen {
        Bitzkey* quarry = nullptr;
        Bitzkey* hunter = nullptr;
        Bitzkey* suitor = nullptr;
        Bitzkey* flora = nullptr;
    };
    Seen look_around(const Bitzkey& agent, int sight) const;
    void move(Bitzkey& agent, int x, int y);
    void move_toward(Bitzkey& agent, int x, int y);
    void move_away(Bitzkey& agent, const Bitzkey& threat);
    bool in_bounds(int x, int y) const { return 0 <= x && x < width_ && 0 <= y && y < height_; }
    std::vector<Bitzkey*>& cell_at(int x, int y) { return cells_[static_cast<std::size_t>(y) * width_ + x]; }
    const std::vector<Bitzkey*>* cell_if(int x, int y) const {
        return in_bounds(x, y) ? &cells_[static_cast<std::size_t>(y) * width_ + x] : nullptr;
    }

    int width_, height_;
    PyRandom random_;
    Insights insights_;
    Census census_;
    std::vector<Dna> lineage_;
    int turn_ = 0;
    int deaths_ = 0;
    int next_identifier_ = 0;
    std::vector<std::unique_ptr<Bitzkey>> agents_;
    std::vector<std::unique_ptr<Bitzkey>> departed_;  // removed corpses, kept until the cells are re-indexed
    std::vector<std::vector<Bitzkey*>> cells_;
    std::vector<std::vector<Bitzkey*>> carrion_;
    std::size_t carrion_count_ = 0;
    int population_ = 0;
    std::optional<int> population_cap_;
    double mind_mutation_, body_mutation_;
    int plants_ = 0;
    std::vector<int> grass_;
    double grass_regrowth_;
    std::vector<Snapshot> history_;
    int frontier_ = 0;
    int checkpoint_stride_ = 1;
};

}  // namespace bitzkey
