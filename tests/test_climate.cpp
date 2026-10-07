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
    CHECK(rows_on(10) == bands({{VeryCold, 20}, {Cold, 20}, {Medium, 20}, {Medium, 20}, {Cold, 20}}));
    CHECK(rows_on(200) == bands({{Cold, 20}, {Medium, 20}, {Medium, 20}, {Cold, 20}, {VeryCold, 20}}));
    const std::vector<int> normal = bands({{Cold, 20}, {Medium, 20}, {Hot, 20}, {Medium, 20}, {Cold, 20}});
    for (const int turn : {100, 300, 460}) CHECK(rows_on(turn) == normal);
    CHECK(rows_on(370) == rows_on(10));  // the next year repeats
}

TEST_CASE("tolerance spares an animal one direction, and plants none") {
    const Bitzkey tender(0, encode_dna(Species::Grazer), 1, 1);
    const Bitzkey hardy(1, encode_dna(Species::Grazer, {{"cold_tolerance", 1}}), 1, 1);
    const Bitzkey warm(2, encode_dna(Species::Grazer, {{"heat_tolerance", 1}}), 1, 1);
    const Bitzkey plant(3, encode_dna(Species::Plant, {{"heat_tolerance", 1}, {"cold_tolerance", 1}}), 1, 1);
    const auto steps = [](const Bitzkey& agent) {
        std::vector<int> found;
        for (const int here : {Hot, Medium, Cold, VeryCold}) found.push_back(agent.climate_steps(here));
        return found;
    };
    CHECK(steps(tender) == std::vector<int>{1, 0, 1, 2});
    CHECK(steps(hardy) == std::vector<int>{1, 0, 0, 0});
    CHECK(steps(warm) == std::vector<int>{0, 0, 1, 2});
    CHECK(steps(plant) == std::vector<int>{1, 0, 1, 2});
}

TEST_CASE("a step from home doubles an animal's food, and two steps also hurt") {
    Simulation simulation = world_at(119);  // the next turn is in the second quarter: no winter
    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Medium}}), 10, 30);
    Bitzkey& one_away = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Cold}}), 30, 30);
    Bitzkey& two_away = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", VeryCold}}), 50, 30);
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    // With nothing to eat, resting gives back at most half a turn's burn.
    CHECK(at_home.hunger <= at_home.burn + 1e-9);
    CHECK(one_away.hunger >= 1.5 * one_away.burn - 1e-9);
    CHECK(two_away.hunger >= 1.5 * two_away.burn - 1e-9);
    CHECK((at_home.health == at_home.max_health && one_away.health == one_away.max_health));
    CHECK(two_away.health < 0.93 * two_away.max_health);
}

TEST_CASE("winter turns a home zone hostile, and the cold can kill") {
    Simulation simulation = world_at(0);  // turn 1, next, is the north's winter
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"temperature", Hot}}), 50, 10);
    grazer.health = 5;
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    simulation.take_turn();
    CHECK(!grazer.alive());
    CHECK(grazer.last_action == "froze");
}

TEST_CASE("a plant out of its temperature withers without growing or seeding, and two steps hurt") {
    Simulation simulation = world_at(119);
    Bitzkey& one_away = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Hot}, {"fertility", 7}}), 50, 30);
    Bitzkey& two_away = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", VeryCold}, {"fertility", 7}}), 20, 30);
    for (Bitzkey* plant : {&one_away, &two_away}) {
        plant->health = plant->max_health / 2;
        plant->hunger = 70;  // too hungry to heal
    }
    simulation.add_agent(encode_dna(Species::Predator), 90, 90);
    for (int turn = 0; turn < 3; ++turn) simulation.take_turn();
    CHECK((one_away.last_action == "withering" && two_away.last_action == "withering"));
    CHECK(one_away.health == doctest::Approx(0.5 * one_away.max_health));  // a step away doesn't hurt
    CHECK(two_away.health == doctest::Approx(0.2 * two_away.max_health));
    CHECK((simulation.agents().size() == 3 && simulation.seeds().empty()));
}

TEST_CASE("a plant out of its temperature gives half the food") {
    Simulation simulation = world_at(120);
    Bitzkey& at_home = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Medium}}), 50, 30);
    Bitzkey& away = simulation.add_agent(encode_dna(Species::Plant, {{"temperature", Cold}}), 60, 30);
    CHECK((simulation.plant_yield(at_home) == 1.0 && simulation.plant_yield(away) == 0.5));
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
    Simulation simulation = world_at(120);
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

TEST_CASE("grass grows only where it is medium now") {
    Simulation simulation = world_at(10);  // the north's winter: its temperate zone is cold, the equator medium
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
    Simulation simulation = world_at(120);
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
    CHECK((seed.generation == 1 && seed.sown == 120 && plant.children == 1));

    while (simulation.turn() < 370) simulation.take_turn();  // next year's northern winter makes its zone cold
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
