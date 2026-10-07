#include "core/Rules.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <type_traits>
#include <utility>

#include "core/Brain.hpp"
#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"

namespace bitzkey::rules {

double Rule::clamp(double value) const {
    value = std::max(minimum, std::min(maximum, value));
    return integer ? py_round(value) : value;
}

double Rule::get() const {
    return std::visit([](auto* place) { return static_cast<double>(*place); }, target);
}

void Rule::set(double value) const {
    std::visit([value](auto* place) { *place = static_cast<std::remove_reference_t<decltype(*place)>>(value); }, target);
}

namespace {

std::vector<Rule> make_rules() {
    // Generated from the Python table, to keep the two identical.
    std::vector<Rule> rules = {
        {"GRASS_REGROWTH", "World", "Grass regrowth", "share of the starting grass that grows back each turn", 0.0, 0.1, 0.001, false, &GRASS_REGROWTH},
        {"CORPSE_FERTILITY", "World", "Corpse fertility", "grass grows this many times as fast on and around a corpse", 1, 10, 1, true, &CORPSE_FERTILITY},
        {"CORPSE_TURNS", "World", "Corpse turns", "turns a corpse lasts while its meat rots away", 1, 500, 5, true, &CORPSE_TURNS},
        {"SPAWN_SPREAD", "World", "Spawn spread", "a starting group appears within this many cells of its centre", 0, 4, 1, true, &SPAWN_SPREAD},
        {"LIFESPAN_YEARS.0", "Bodies", "Lifespan, shortest", "years of life expected for the smallest bodies, plant or animal", 0.5, 30.0, 0.5, false, &LIFESPAN_YEARS[0]},
        {"LIFESPAN_YEARS.1", "Bodies", "Lifespan, middle", "years of life expected for middling bodies", 0.5, 30.0, 0.5, false, &LIFESPAN_YEARS[1]},
        {"LIFESPAN_YEARS.2", "Bodies", "Lifespan, longest", "years of life expected for the largest bodies", 0.5, 30.0, 0.5, false, &LIFESPAN_YEARS[2]},
        {"AGEING", "Bodies", "Ageing", "from this share of its lifespan, an animal may die of old age", 0.0, 0.95, 0.05, false, &AGEING},
        {"METABOLISM.0", "Bodies", "Metabolism, smallest", "metabolic rate of the smallest bodies", 0.1, 5.0, 0.1, false, &METABOLISM[0]},
        {"METABOLISM.1", "Bodies", "Metabolism, largest", "metabolic rate of the largest bodies", 0.1, 5.0, 0.1, false, &METABOLISM[1]},
        {"STOMACH.0", "Bodies", "Stomach, smallest", "food the smallest body holds, relative to the middle", 0.1, 5.0, 0.1, false, &STOMACH[0]},
        {"STOMACH.1", "Bodies", "Stomach, largest", "food the largest body holds, relative to the middle", 0.1, 5.0, 0.1, false, &STOMACH[1]},
        {"SIZE_IN_MIGHT", "Bodies", "Size in might", "share of strength and toughness that comes from body size", 0.0, 1.0, 0.05, false, &SIZE_IN_MIGHT},
        {"GRAZER_SPEED", "Bodies", "Grazer speed", "grazers move this many times as far a step, rounded half up", 0.5, 4.0, 0.25, false, &GRAZER_SPEED},
        {"BREED_RESERVE", "Breeding", "Breeding reserve", "hunger a parent must still have spare after paying to breed", 0, 100, 5, true, &BREED_RESERVE},
        {"MATE_RING_DIFFERENCE", "Breeding", "Mate ring difference", "most a partner's ring colour may differ, at no tolerance", 0.0, 1.0, 0.01, false, &MATE_RING_DIFFERENCE},
        {"MATE_DOT_DIFFERENCE", "Breeding", "Mate dot difference", "most a partner's dot colour may differ, at no tolerance", 0.0, 1.0, 0.01, false, &MATE_DOT_DIFFERENCE},
        {"LITTER_RANGE.0", "Breeding", "Smallest litter", "fewest young per birth, while there is room", 1, 20, 1, true, &LITTER_RANGE[0]},
        {"LITTER_RANGE.1", "Breeding", "Largest litter", "most young per birth, while there is room", 1, 20, 1, true, &LITTER_RANGE[1]},
        {"LITTER_SIZE_WEIGHT", "Breeding", "Small-body litters", "share of litter size from small bodies; the rest from the trait", 0.0, 1.0, 0.05, false, &LITTER_SIZE_WEIGHT},
        {"PREDATOR_LITTERS", "Breeding", "Predator litters", "predators' litters are this share of the usual, rounded up", 0.05, 2.0, 0.05, false, &PREDATOR_LITTERS},
        {"CROWDING_RADIUS", "Breeding", "Crowding radius", "animals within this many cells crowd a would-be parent", 1, 12, 1, true, &CROWDING_RADIUS},
        {"CROWDING_LIMIT", "Breeding", "Crowding limit", "litters shrink as crowding nears this; at it, no breeding", 1, 100, 1, true, &CROWDING_LIMIT},
        {"FORAGE_RADIUS", "Breeding", "Forage radius", "food and mouths are weighed within this many cells", 1, 12, 1, true, &FORAGE_RADIUS},
        {"FOOD_NEEDED", "Breeding", "Food needed", "meals each mouth needs nearby to breed; twice this for a full litter", 0.5, 20.0, 0.5, false, &FOOD_NEEDED},
        {"PLANT_MEALS", "Breeding", "Plant meals", "meals a plant is worth to a full-efficiency browser", 0.0, 20.0, 0.5, false, &PLANT_MEALS},
        {"PREY_MEALS", "Breeding", "Prey meals", "meals a prey animal is worth", 0.0, 20.0, 0.5, false, &PREY_MEALS},
        {"SHARED_DIET", "Breeding", "Shared diet", "how much another species eating the same food counts as a mouth", 0.0, 1.0, 0.05, false, &SHARED_DIET},
        {"GRASS_OUTLOOK", "Breeding", "Grass outlook", "grass is judged by what regrows in this many turns", 0, 1000, 10, true, &GRASS_OUTLOOK},
        {"STANDING_GRASS", "Breeding", "Standing grass", "plus this share of the grass standing now", 0.0, 1.0, 0.05, false, &STANDING_GRASS},
        {"DEATH_PENALTY", "Feelings", "Death penalty", "how sharply a brain learns from the choice that killed it", 0.0, 10.0, 0.1, false, &DEATH_PENALTY},
        {"BREED_JOY", "Feelings", "Breeding joy", "pleasure of breeding, scaled by libido", 0.0, 5.0, 0.05, false, &BREED_JOY},
        {"FIGHT_JOY", "Feelings", "Fighting joy", "pleasure of a blow, scaled by aggression", 0.0, 5.0, 0.05, false, &FIGHT_JOY},
        {"COMPANY_JOY", "Feelings", "Company joy", "pleasure of company, scaled by sociability", 0.0, 5.0, 0.05, false, &COMPANY_JOY},
        {"CURIOSITY_JOY", "Feelings", "Curiosity joy", "pleasure of exploring, scaled by curiosity", 0.0, 5.0, 0.05, false, &CURIOSITY_JOY},
        {"FEAR_DREAD", "Feelings", "Fear dread", "dread of hunters nearby, scaled by fear", 0.0, 5.0, 0.05, false, &FEAR_DREAD},
        {"UNEASE", "Feelings", "Unease", "how much an intolerant animal minds other species close by", 0.0, 5.0, 0.05, false, &UNEASE},
        {"EMPATHY_PAIN", "Feelings", "Empathy pain", "how much an empathetic animal feels another's wound", 0.0, 5.0, 0.05, false, &EMPATHY_PAIN},
        {"OTHER_KIND", "Feelings", "Other kind", "how much of that pain reaches it for another species", 0.0, 1.0, 0.05, false, &OTHER_KIND},
        {"LEARNING_RATE", "Mind", "Learning rate", "how fast a brain learns, at the highest intellect", 0.0, 0.5, 0.005, false, &LEARNING_RATE},
        {"TEMPERATURE", "Mind", "Choice temperature", "how loosely a brain picks among its options", 0.01, 5.0, 0.05, false, &TEMPERATURE},
        {"MUTATION", "Mind", "Brain mutation", "how much a newborn's brain varies from its parents'", 0.0, 1.0, 0.01, false, &MUTATION},
        {"TEMPERAMENT_RELAPSE", "Mind", "Temperament relapse", "share of the gap back to the inborn value closed each turn", 0.0, 0.1, 0.001, false, &TEMPERAMENT_RELAPSE},
        {"PANIC.0", "Mind", "Panic, unhurt", "chance a fully fearful animal breaks when struck, unhurt", 0.0, 1.0, 0.05, false, &PANIC[0]},
        {"PANIC.1", "Mind", "Panic, near death", "chance a fully fearful animal breaks when struck, near death", 0.0, 1.0, 0.05, false, &PANIC[1]},
        {"STRIKE_FORCE", "Fighting", "Strike force", "scales every blow", 0.0, 10.0, 0.1, false, &STRIKE_FORCE},
        {"STRIKE_EFFORT", "Fighting", "Strike effort", "food burned for each point of damage a blow deals", 0.0, 2.0, 0.05, false, &STRIKE_EFFORT},
        {"THORN_FORCE", "Fighting", "Thorn force", "a plant's counterblow at full weapons and size", 0, 100, 1, true, &THORN_FORCE},
        {"THORN_SIZE_SHARE", "Fighting", "Thorn size share", "share of that from the plant's size; the rest its weapons", 0.0, 1.0, 0.05, false, &THORN_SIZE_SHARE},
        {"PACK_BONUS", "Fighting", "Pack bonus", "extra force at prey that a herd-mate is also alongside", 0.0, 5.0, 0.05, false, &PACK_BONUS},
        {"KILL_MEAL", "Fighting", "Kill meal", "hunger a kill sates", 0, 200, 5, true, &KILL_MEAL},
        {"KILL_MEAL_FOR_SIZE", "Fighting", "Kill meal for size", "more hunger sated, up to this for the largest prey", 0, 200, 5, true, &KILL_MEAL_FOR_SIZE},
        {"FRESH_KILL_TURNS", "Fighting", "Fresh kill turns", "turns after the one it died in that a body is a fresh kill", 0, 10, 1, true, &FRESH_KILL_TURNS},
        {"FRESH_KILL_MEALS", "Fighting", "Fresh kill meals", "times the kill meal a predator or omnivore gets eating a fresh kill", 0.0, 10.0, 0.5, false, &FRESH_KILL_MEALS},
        {"KIND_DIFFERENCE", "Fighting", "Kind difference", "predators whose rings differ more are different kinds", 0.0, 1.0, 0.01, false, &KIND_DIFFERENCE},
        {"FLEE_RANGE", "Fighting", "Flee range", "a hunter is only worth fleeing within this many steps, unless alarmed", 0, 12, 1, true, &FLEE_RANGE},
        {"HERD_VIGILANCE", "Fighting", "Herd vigilance", "how far a close herd shrinks that range", 0.0, 1.0, 0.05, false, &HERD_VIGILANCE},
        {"SWITCH_SHARE", "Fighting", "Prey switching", "a prey is hunted while it is at least this share of the food", 0.0, 1.0, 0.05, false, &SWITCH_SHARE},
        {"TERRITORY_RADIUS", "Territory", "Territory radius", "a hunter's territory: the cells within this many steps", 1, 12, 1, true, &TERRITORY_RADIUS},
        {"TERRITORY_RAMP", "Territory", "Territory ramp", "rivals past the limit that bring rivalry to its full strength", 1, 20, 1, true, &TERRITORY_RAMP},
        {"RIVAL_UNEASE", "Territory", "Rival unease", "discomfort of a fully overcrowded territory", 0.0, 5.0, 0.05, false, &RIVAL_UNEASE},
        {"RIVAL_KILL_JOY", "Territory", "Rival kill joy", "relief at killing a rival in an overcrowded territory", 0.0, 5.0, 0.05, false, &RIVAL_KILL_JOY},
        {"TERRITORY.predator.fewest", "Territory", "Predator rivals, cruel", "rivals the least empathetic predator tolerates", 0, 50, 1, true, &TERRITORY[0].fewest},
        {"TERRITORY.predator.most", "Territory", "Predator rivals, kind", "rivals the most empathetic predator tolerates", 0, 50, 1, true, &TERRITORY[0].most},
        {"TERRITORY.predator.strength", "Territory", "Predator rivalry", "how strongly overcrowding moves a predator", 0.0, 2.0, 0.05, false, &TERRITORY[0].strength},
        {"TERRITORY.omnivore.fewest", "Territory", "Omnivore rivals, cruel", "rivals the least empathetic omnivore tolerates", 0, 50, 1, true, &TERRITORY[1].fewest},
        {"TERRITORY.omnivore.most", "Territory", "Omnivore rivals, kind", "rivals the most empathetic omnivore tolerates", 0, 50, 1, true, &TERRITORY[1].most},
        {"TERRITORY.omnivore.strength", "Territory", "Omnivore rivalry", "how strongly overcrowding moves an omnivore", 0.0, 2.0, 0.05, false, &TERRITORY[1].strength},
        {"HERD_RADIUS", "Herding", "Herd radius", "herd-mates are sought within this many steps", 1, 12, 1, true, &HERD_RADIUS},
        {"HERD_SPREAD", "Herding", "Herd spread", "how far from the herd's middle a fearless animal is content", 0, 12, 1, true, &HERD_SPREAD},
        {"HERD_JOY", "Herding", "Herd joy", "pleasure of being among three or more herd-mates", 0.0, 5.0, 0.05, false, &HERD_JOY},
        {"HERD_LONELINESS", "Herding", "Herd loneliness", "unease at having no herd-mate in range", 0.0, 5.0, 0.05, false, &HERD_LONELINESS},
        {"HERD_SAFETY", "Herding", "Herd safety", "how much three close herd-mates calm fear", 0.0, 1.0, 0.05, false, &HERD_SAFETY},
        {"HERD_DEFEND_JOY", "Herding", "Herd defence joy", "pleasure of striking back at a herd-mate's attacker", 0.0, 5.0, 0.05, false, &HERD_DEFEND_JOY},
        {"GRASS_MEAL", "Food", "Grass meal", "hunger a tuft of grass sates", 0, 200, 5, true, &GRASS_MEAL},
        {"PLANT_MEAL", "Food", "Plant meal", "hunger a bite of a plant sates", 0, 200, 5, true, &PLANT_MEAL},
        {"PLANT_MEAL_FOR_SIZE", "Food", "Plant meal for size", "more hunger sated, up to this for the largest plants", 0, 200, 5, true, &PLANT_MEAL_FOR_SIZE},
        {"PLANT_BITE", "Food", "Plant bite", "health a plant loses to each bite", 0, 200, 1, true, &PLANT_BITE},
        {"PLANT_REGROWTH", "Food", "Plant regrowth", "share of its health a plant regrows each turn", 0.0, 1.0, 0.005, false, &PLANT_REGROWTH},
        {"PLANT_RECOVERY", "Food", "Plant recovery", "turns after a bite before a plant can be bitten again", 0, 100, 1, true, &PLANT_RECOVERY},
        {"CARRION_MEAT", "Food", "Carrion meat", "meat a well-fed corpse leaves", 0, 200, 5, true, &CARRION_MEAT},
        {"CARRION_MEAT_FOR_SIZE", "Food", "Carrion meat for size", "more meat, up to this for the largest", 0, 200, 5, true, &CARRION_MEAT_FOR_SIZE},
        {"SCAVENGE_BITE", "Food", "Scavenge bite", "meat eaten in one go", 0, 200, 5, true, &SCAVENGE_BITE},
        {"GRAZING.grazer", "Food", "Grazer on grass", "share of the food in grass a grazer gets", 0.0, 3.0, 0.05, false, &GRAZING[1]},
        {"GRAZING.omnivore", "Food", "Omnivore on grass", "share of the food in grass an omnivore gets", 0.0, 3.0, 0.05, false, &GRAZING[2]},
        {"BROWSING.grazer", "Food", "Grazer on plants", "share of the food in plants a grazer gets", 0.0, 3.0, 0.05, false, &BROWSING[1]},
        {"BROWSING.omnivore", "Food", "Omnivore on plants", "share of the food in plants an omnivore gets", 0.0, 3.0, 0.05, false, &BROWSING[2]},
        {"SEED_CHANCE.0", "Plants", "Seeding, least fertile", "a well-fed plant's chance each turn of seeding", 0.0, 1.0, 0.01, false, &SEED_CHANCE[0]},
        {"SEED_CHANCE.1", "Plants", "Seeding, most fertile", "a well-fed plant's chance each turn of seeding", 0.0, 1.0, 0.01, false, &SEED_CHANCE[1]},
        {"SEED_COST", "Plants", "Seed cost", "hunger seeding costs a plant", 0, 100, 1, true, &SEED_COST},
        {"SEED_HUNGER", "Plants", "Seed hunger", "a plant hungrier than this doesn't seed", 0, 100, 1, true, &SEED_HUNGER},
        {"SEEDLING_HUNGER", "Plants", "Seedling hunger", "hunger a seedling starts with", 0, 100, 1, true, &SEEDLING_HUNGER},
        {"SEED_REACH", "Plants", "Seed reach", "how far, in cells, a seed may land from its plant", 1, 9, 1, true, &SEED_REACH},
        {"LIGHT_RADIUS", "Plants", "Light radius", "plants compete for light with plants within this many cells", 0, 12, 1, true, &LIGHT_RADIUS},
        {"LIGHT_LIMIT", "Plants", "Light limit", "this many such plants stop a plant seeding", 1, 50, 1, true, &LIGHT_LIMIT},
        {"PLANT_CROWDING", "Plants", "Plant crowding", "a seed doesn't take where more plants grow within two cells", 0, 50, 1, true, &PLANT_CROWDING},
        {"ARCTIC_SHARE", "Climate", "Arctic", "share of the map's height that is arctic, at the top and again at the bottom", 0.0, 0.5, 0.05, false, &ARCTIC_SHARE},
        {"EQUATOR_SHARE", "Climate", "Equator", "share of the map's height across its middle that is the equator", 0.0, 1.0, 0.05, false, &EQUATOR_SHARE},
        {"CLIMATE_HUNGER", "Climate", "Climate hunger", "times the food an animal needs a step or more from its home temperature", 1.0, 5.0, 0.1, false, &CLIMATE_HUNGER},
        {"CLIMATE_DAMAGE", "Climate", "Climate damage", "share of full health lost a turn two or more steps from home", 0.0, 1.0, 0.01, false, &CLIMATE_DAMAGE},
        {"PLANT_CLIMATE_YIELD", "Climate", "Plant climate yield", "share of its food a plant gives out of its temperature", 0.0, 1.0, 0.05, false, &PLANT_CLIMATE_YIELD},
        {"GRASS_DIEBACK", "Climate", "Grass dieback", "where it isn't medium, turns for each tuft of a cell's grass to die", 1, 500, 1, true, &GRASS_DIEBACK},
        {"SEED_VIABILITY", "Climate", "Seed viability", "years a seed waits for its temperature before it dies", 0.0, 5.0, 0.05, false, &SEED_VIABILITY},
        {"STARTING_TOLERANCE", "Climate", "Starting tolerance", "chance a random starting genome carries each tolerance flag", 0.0, 1.0, 0.05, false, &STARTING_TOLERANCE},
    };
    for (std::size_t event = 0; event < EXPERIENCES.size(); ++event) {
        std::string name(EXPERIENCE_NAMES[event]);
        std::replace(name.begin(), name.end(), '_', ' ');
        std::string title = name;
        title[0] = static_cast<char>(title[0] - 'a' + 'A');
        for (Shift& shift : EXPERIENCES[event]) {
            const std::string trait(MIND_TRAITS[static_cast<std::size_t>(shift.trait)]);
            rules.push_back({"EXPERIENCES." + std::string(EXPERIENCE_NAMES[event]) + "." + trait, "Experiences",
                             title + ": " + trait, "how " + name + " shifts " + trait + ", as a share of its range", -0.5,
                             0.5, 0.0005, false, &shift.change});
        }
    }
    return rules;
}

// Pairs that must stay in order, smaller first, and pairs that must differ (lifespan divides by the gap).
constexpr std::pair<std::string_view, std::string_view> ORDERED[] = {
    {"LITTER_RANGE.0", "LITTER_RANGE.1"}, {"LIFESPAN_YEARS.0", "LIFESPAN_YEARS.1"}, {"LIFESPAN_YEARS.1", "LIFESPAN_YEARS.2"}};
constexpr std::pair<std::string_view, std::string_view> DISTINCT[] = {{"METABOLISM.0", "METABOLISM.1"}};

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

}  // namespace

const std::vector<Rule>& all() {
    static const std::vector<Rule> rules = make_rules();
    return rules;
}

const Rule* find(std::string_view key) {
    const auto& rules = all();
    const auto found = std::find_if(rules.begin(), rules.end(), [key](const Rule& rule) { return rule.key == key; });
    return found == rules.end() ? nullptr : &*found;
}

const Values& defaults() {
    static const Values values = current();
    return values;
}

Values current() {
    Values values;
    for (const Rule& rule : all()) values[rule.key] = rule.get();
    return values;
}

std::vector<std::string> problems(const Values& values) {
    std::vector<std::string> found;
    for (const auto& [low, high] : ORDERED) {
        if (values.find(low)->second > values.find(high)->second) {
            found.push_back(find(low)->label + " must not exceed " + lower(find(high)->label));
        }
    }
    for (const auto& [first, second] : DISTINCT) {
        if (values.find(first)->second == values.find(second)->second) {
            found.push_back(find(first)->label + " must differ from " + lower(find(second)->label));
        }
    }
    if (2 * values.find("ARCTIC_SHARE")->second + values.find("EQUATOR_SHARE")->second > 1 + 1e-9) {
        found.push_back("The two arctic zones and the equator must fit in the map together");
    }
    return found;
}

void apply(const Values& values) {
    const Values& start = defaults();
    for (const Rule& rule : all()) {
        const auto given = values.find(rule.key);
        rule.set(rule.clamp(given != values.end() ? given->second : start.find(rule.key)->second));
    }
}

Values changed(const Values& values) {
    Values result;
    for (const auto& [key, value] : values) {
        if (value != defaults().find(key)->second) result[key] = value;
    }
    return result;
}

std::string shown(const Rule& rule, double value) {
    if (rule.integer) return std::to_string(static_cast<long long>(value));
    char text[32];
    std::snprintf(text, sizeof text, "%.6g", value);
    return text;
}

}  // namespace bitzkey::rules
