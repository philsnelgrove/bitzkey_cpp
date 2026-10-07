// Zones, seasons and temperature, ported from the Python tests.

#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "core/Simulation.hpp"

using namespace bitzkey;

namespace {

// An empty world at a turn, its grass cleared.
Simulation world_at(int turn, int size = 100) {
    Simulation simulation(Simulation::Settings{size, size, 0, 3});
    for (int step = 0; step < turn; ++step) simulation.take_turn();
    simulation.clear_grass();
    return simulation;
}

std::vector<int> rows_on(int turn) {
    std::vector<int> rows;
    for (int y = 0; y < 100; ++y) rows.push_back(temperature_at(y, 100, turn));
    return rows;
}

std::vector<int> bands(std::initializer_list<std::pair<int, int>> runs) {
    std::vector<int> rows;
    for (const auto& [temperature, count] : runs) rows.insert(rows.end(), static_cast<std::size_t>(count), temperature);
    return rows;
}

}  // namespace

TEST_CASE("zones band the map arctic, temperate, equator, temperate, arctic") {
    std::vector<int> rows;
    for (int y = 0; y < 100; ++y) rows.push_back(zone_temperature(y, 100));
    CHECK(rows == bands({{Cold, 20}, {Medium, 20}, {Hot, 20}, {Medium, 20}, {Cold, 20}}));
}

TEST_CASE("a year is 360 turns in four quarters by default") {
    std::vector<int> seasons;
    for (const int turn : {0, 89, 90, 179, 180, 269, 270, 359, 360}) seasons.push_back(season(turn));
    CHECK(seasons == std::vector<int>{0, 0, 1, 1, 2, 2, 3, 3, 0});
    CHECK((year(0) == 1 && year(359) == 1 && year(360) == 2 && year(800) == 3));
}

TEST_CASE("a world sets the year length for seasons and lifespans") {
    Simulation::Settings settings{20, 20, 0, 1};
    settings.year_length = 100;
    Simulation simulation(settings);
    CHECK((season(25) == 1 && season(50) == 2 && year(100) == 2));
    CHECK(simulation.add_agent(encode_dna(Species::Grazer), 1, 1).lifespan == doctest::Approx(100));  // the smallest live a year
    settings.year_length = 3;
    CHECK_THROWS_AS(Simulation{settings}, std::invalid_argument);
    const Simulation standard(Simulation::Settings{20, 20, 0, 1});
    CHECK(year(360) == 2);  // back to the default
}

TEST_CASE("each hemisphere and the equator are a step colder in its winter") {
    CHECK(rows_on(100) == bands({{VeryCold, 20}, {Cold, 20}, {Medium, 20}, {Medium, 20}, {Cold, 20}}));  // second quarter
    CHECK(rows_on(300) == bands({{Cold, 20}, {Medium, 20}, {Medium, 20}, {Cold, 20}, {VeryCold, 20}}));  // fourth
    const std::vector<int> normal = bands({{Cold, 20}, {Medium, 20}, {Hot, 20}, {Medium, 20}, {Cold, 20}});
    for (const int turn : {10, 200, 370}) CHECK(rows_on(turn) == normal);  // the mild quarters
    CHECK(rows_on(460) == rows_on(100));  // the next year repeats
}

TEST_CASE("hardiness halves an animal's exposure in one direction, and plants have none") {
    const Bitzkey tender(0, encode_dna(Species::Grazer), 1, 1);
    const Bitzkey hardy(1, encode_dna(Species::Grazer, {{"cold_tolerance", 1}}), 1, 1);
    const Bitzkey warm(2, encode_dna(Species::Grazer, {{"heat_tolerance", 1}}), 1, 1);
    const Bitzkey plant(3, encode_dna(Species::Plant, {{"heat_tolerance", 1}, {"cold_tolerance", 1}}), 1, 1);
    for (const Bitzkey* agent : {&tender, &hardy, &warm, &plant}) {  // every step is felt
        std::vector<int> steps;
        for (const int here : {Hot, Medium, Cold, VeryCold}) steps.push_back(agent->climate_steps(here));
        CHECK(steps == std::vector<int>{1, 0, 1, 2});
    }
    const auto shares = [](const Bitzkey& agent) {
        std::vector<double> found;
        for (const int here : {Hot, Medium, Cold, VeryCold}) found.push_back(agent.exposure_share(here));
        return found;
    };
    CHECK(shares(tender) == std::vector<double>{1.0, 1.0, 1.0, 1.0});
    CHECK(shares(hardy) == std::vector<double>{1.0, 1.0, 0.5, 0.5});
    CHECK(shares(warm) == std::vector<double>{0.5, 1.0, 1.0, 1.0});
    CHECK(shares(plant) == std::vector<double>{1.0, 1.0, 1.0, 1.0});
}

TEST_CASE("a hardy animal suffers half the extra hunger and damage") {
    Simulation simulation = world_at(199);  // mid-year, mild
    Bitzkey& tender = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}}), 20, 10);  // the arctic: two steps too cold
    Bitzkey& hardy = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}, {"cold_tolerance", 1}}), 40, 10);
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    // With nothing to eat, resting gives back at most half a turn's burn and a little health.
    CHECK((tender.hunger >= 1.5 * tender.burn - 1e-9 && tender.hunger <= 2 * tender.burn + 1e-9));
    CHECK((hardy.hunger >= 1.0 * hardy.burn - 1e-9 && hardy.hunger <= 1.5 * hardy.burn + 1e-9));
    CHECK((tender.health < 0.83 * tender.max_health && hardy.health >= 0.9 * hardy.max_health - 1e-9 &&
           hardy.health < 0.93 * hardy.max_health));
}

TEST_CASE("a step from home doubles an animal's food, and two steps also hurt") {
    Simulation simulation = world_at(199);  // the next turn is in the second quarter: no winter
    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}}), 10, 30);
    Bitzkey& one_away = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Cold}}), 30, 30);
    Bitzkey& two_away = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", VeryCold}}), 50, 30);
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    // With nothing to eat, resting gives back at most half a turn's burn.
    CHECK(at_home.hunger <= at_home.burn + 1e-9);
    CHECK(one_away.hunger >= 1.5 * one_away.burn - 1e-9);
    CHECK(two_away.hunger >= 1.5 * two_away.burn - 1e-9);
    CHECK(at_home.health == at_home.max_health);  // a tenth of its health a step from home, resting at most a little back
    CHECK(one_away.health < 0.93 * one_away.max_health);
    CHECK(two_away.health < 0.83 * two_away.max_health);
}

TEST_CASE("starving out of its temperature is dying of exposure") {
    Simulation simulation = world_at(200);
    Bitzkey& away = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Cold}}), 20, 50);  // on the equator: too hot
    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}}), 40, 50);
    away.hunger = at_home.hunger = 99.9;
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    CHECK((away.last_action == "overheated" && at_home.last_action == "starved"));
}

TEST_CASE("tolerance is rare in the starting genomes") {
    const Simulation simulation(Simulation::Settings{100, 100, 600, 5});
    int flags = 0, count = 0;
    for (const auto& agent : simulation.agents()) {
        flags += agent->climate.heat + agent->climate.cold;
        count += 2;
    }
    CHECK(static_cast<double>(flags) / count == doctest::Approx(0.1).epsilon(0.5));
}

TEST_CASE("winter turns a home zone hostile, and the cold can kill") {
    Simulation simulation = world_at(90);  // turn 91, next, is in the north's winter
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}}), 50, 10);
    grazer.health = 5;
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    CHECK(!grazer.alive());
    CHECK(grazer.last_action == "froze");
}

TEST_CASE("a plant out of its temperature withers without growing or seeding, and two steps hurt") {
    Simulation simulation = world_at(199);
    Bitzkey& one_away = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Hot}, {"fertility", 7}}), 50, 30);
    Bitzkey& two_away = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", VeryCold}, {"fertility", 7}}), 20, 30);
    for (Bitzkey* plant : {&one_away, &two_away}) {
        plant->health = plant->max_health / 2;
        plant->hunger = 70;  // too hungry to heal
    }
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    for (int turn = 0; turn < 2; ++turn) simulation.take_turn();
    CHECK((one_away.last_action == "withering" && two_away.last_action == "withering"));
    CHECK(one_away.health == doctest::Approx(0.5 * one_away.max_health));  // a step away doesn't hurt
    CHECK(two_away.health == doctest::Approx(0.1 * two_away.max_health));  // two steps, a fifth a turn
    CHECK((simulation.agents().size() == 3 && simulation.seeds().empty()));
}

TEST_CASE("a plant out of its temperature gives half the food") {
    Simulation simulation = world_at(200);
    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Medium}}), 50, 30);
    Bitzkey& away = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Cold}}), 60, 30);
    CHECK((simulation.plant_yield(at_home) == 1.0 && simulation.plant_yield(away) == 0.0));
    for (Bitzkey* plant : {&at_home, &away}) {
        Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), plant->x, plant->y + 1);  // a big stomach
        grazer.hunger = 90;
        simulation.browse(grazer, *plant);
        const double meal = PLANT_MEAL * grazer.browsing * simulation.plant_yield(*plant) / grazer.stomach;
        CHECK(grazer.hunger == doctest::Approx(90 - meal));
    }
}

TEST_CASE("spawned plants are at home in their zone, and imports keep theirs") {
    Simulation simulation(Simulation::Settings{60, 60, 300, 4});
    std::set<int> temperatures;
    for (const auto& agent : simulation.agents()) {
        if (agent->species != Species::Plant) continue;
        std::set<int> near;  // the group's centre is within two rows
        for (int dy = -2; dy <= 2; ++dy) {
            if (0 <= agent->y + dy && agent->y + dy < 60) near.insert(zone_temperature(agent->y + dy, 60));
        }
        CHECK(near.contains(agent->climate.temperature));
        temperatures.insert(agent->climate.temperature);
    }
    CHECK(temperatures.size() == 3);  // hot, medium and cold zones
    Imported saved;
    saved.dna = encode_dna(Species::Plant, {{"temperature", VeryCold}});
    Simulation::Settings settings{30, 30, 0, 4};
    settings.imported.push_back(saved);
    const Simulation imported(settings);
    for (const auto& agent : imported.agents()) CHECK(agent->climate.temperature == VeryCold);
}

TEST_CASE("an animal senses how far it is from home and how close home weather is") {
    Simulation simulation = world_at(200);
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Cold}, {"sight", 7}}), 50, 23);
    const auto comfort = simulation.comfort_row(grazer, grazer.sight);
    REQUIRE(comfort.has_value());
    CHECK((comfort->x == 50 && comfort->y == 19));  // the arctic, four rows north
    Simulation::Sensed sensed = simulation.sense(grazer);
    CHECK(sensed.senses[SenseClimateDiscomfort] == 0.5);  // temperate: a step too warm
    CHECK(sensed.senses[SenseComfortCloseness] == doctest::Approx(1 - 3.0 / 9));

    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}}), 50, 30);
    sensed = simulation.sense(at_home);
    CHECK((sensed.senses[SenseClimateDiscomfort] == 0.0 && sensed.senses[SenseComfortCloseness] == 1.0));

    Bitzkey& lost = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", VeryCold}, {"sight", 0}}), 50, 50);  // none in sight
    sensed = simulation.sense(lost);
    CHECK((sensed.senses[SenseClimateDiscomfort] == 1.0 && sensed.senses[SenseComfortCloseness] == 0.0));
    CHECK(action_index("migrate") == -1);  // moving toward home is left for brains to learn
}

TEST_CASE("a plant too hot needs twice the food, and too cold doesn't") {
    Simulation simulation = world_at(200);
    Bitzkey& too_hot = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Cold}}), 20, 30);  // temperate: a step too warm
    Bitzkey& too_cold = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Hot}}), 40, 30);
    too_hot.hunger = too_cold.hunger = 50;
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    CHECK(too_hot.hunger == doctest::Approx(50 + 2 * too_hot.burn - 1.5));
    CHECK(too_cold.hunger == doctest::Approx(50 + too_cold.burn - 1.5));
}

TEST_CASE("the year starts mild") {
    std::vector<int> seasons;
    for (const int turn : {0, 90, 180, 270}) seasons.push_back(season(turn));
    CHECK(seasons == std::vector<int>{0, 1, 2, 3});
    std::vector<int> start, zones;
    for (int y = 0; y < 100; ++y) {
        start.push_back(temperature_at(y, 100, 0));
        zones.push_back(zone_temperature(y, 100));
    }
    CHECK(start == zones);
}

TEST_CASE("grass grows only where it is medium now") {
    Simulation simulation = world_at(100);  // the north's winter: its temperate zone is cold, the equator medium
    simulation.sow(3000);
    std::set<int> rows;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            if (simulation.grass_at(x, y)) rows.insert(y);
        }
    }
    CHECK(rows.size() == 40);
    CHECK((*rows.begin() == 40 && *rows.rbegin() == 79));
}

TEST_CASE("a seed that falls where it can't grow waits for its temperature") {
    Simulation simulation = world_at(200);
    simulation.set_mutation(0.5, 0.0);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Cold}, {"fertility", 7}}), 50, 30);
    simulation.index_cells();
    for (int attempt = 0; attempt < 400 && simulation.seeds().empty(); ++attempt) {
        plant.hunger = 0;
        simulation.seed_plant(plant);
    }
    REQUIRE(simulation.seeds().size() == 1);
    CHECK(simulation.agents().size() == 1);
    const Simulation::Seed seed = simulation.seeds().front();
    CHECK((seed.generation == 1 && seed.sown == 200 && plant.children == 1));

    while (simulation.turn() < 460) simulation.take_turn();  // next year's northern winter makes its zone cold
    CHECK(simulation.seeds().empty());
    const auto& agents = simulation.agents();
    const auto sprouted = std::find_if(agents.begin(), agents.end(), [&](const auto& agent) { return agent->dna == seed.dna; });
    REQUIRE(sprouted != agents.end());
    CHECK(((*sprouted)->x == seed.x && (*sprouted)->y == seed.y && (*sprouted)->generation == 1));
}

TEST_CASE("a seed stays viable for three quarters of a year") {
    Simulation simulation = world_at(0);
    simulation.seeds().push_back({50, 30, encode_dna(Species::Plant, {{"temperature", VeryCold}}), 1, 0});  // never very cold there
    while (simulation.turn() < 269) simulation.take_turn();
    CHECK(simulation.seeds().size() == 1);
    simulation.take_turn();
    CHECK(simulation.seeds().empty());
}

TEST_CASE("waiting seeds rewind and replay with the world") {
    Simulation simulation(Simulation::Settings{40, 40, 60, 3});
    while (simulation.seeds().empty() && simulation.turn() < 300) simulation.advance();  // run until some seeds are waiting
    const std::vector<Simulation::Seed> seeds = simulation.seeds();
    const int turn = simulation.turn();
    CHECK(!seeds.empty());
    simulation.seek(turn / 2);
    simulation.seek(turn);
    CHECK(simulation.seeds() == seeds);
}
