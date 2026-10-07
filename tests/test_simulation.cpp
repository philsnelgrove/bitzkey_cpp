// The simulation's rules, ported from the Python tests' key cases.

#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <set>

#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"

using namespace bitzkey;

namespace {

Simulation world(int size = 12, int seed = 3) {
    Simulation simulation(Simulation::Settings{size, size, 0, static_cast<std::uint64_t>(seed)});
    simulation.clear_grass();
    return simulation;
}

}  // namespace

TEST_CASE("a seeded world replays exactly, and seeking repeats the recorded run") {
    Simulation first(Simulation::Settings{40, 40, 60, 11});
    Simulation second(Simulation::Settings{40, 40, 60, 11});
    for (int turn = 0; turn < 40; ++turn) {
        first.advance();
        second.advance();
    }
    REQUIRE(first.agents().size() == second.agents().size());
    for (std::size_t index = 0; index < first.agents().size(); ++index) {
        CHECK(first.agents()[index]->hunger == second.agents()[index]->hunger);
        CHECK(first.agents()[index]->x == second.agents()[index]->x);
    }
    std::vector<double> hunger_at_25;
    first.seek(25);
    for (const auto& agent : first.agents()) hunger_at_25.push_back(agent->hunger);
    first.seek(40);
    first.seek(25);
    std::vector<double> again;
    for (const auto& agent : first.agents()) again.push_back(agent->hunger);
    CHECK(hunger_at_25 == again);
    first.seek(40);
    CHECK(first.agents().size() == second.agents().size());
    CHECK(first.agents().back()->hunger == second.agents().back()->hunger);
}

TEST_CASE("random populations spawn in groups of three to five sharing a genome") {
    for (const int population : {3, 7, 90, 101}) {
        Simulation simulation(Simulation::Settings{50, 50, population, static_cast<std::uint64_t>(population)});
        std::map<Dna, int> groups;
        for (const auto& agent : simulation.agents()) groups[agent->dna] += 1;
        CHECK(static_cast<int>(simulation.agents().size()) == population);
        for (const auto& [dna, count] : groups) {
            if (population >= 3) CHECK(count >= 3);
            CHECK(count <= 5);
        }
    }
}

TEST_CASE("bodies set health pools, metabolism, stomachs and lifespans") {
    Simulation simulation = world();
    const Bitzkey& small = simulation.add_agent(encode_dna(Species::Grazer), 1, 1);
    const Bitzkey& large = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 2, 2);
    CHECK(small.max_health == 60);
    CHECK(large.max_health == 150);
    CHECK(small.metabolism == doctest::Approx(1.6));
    CHECK(large.metabolism == doctest::Approx(0.6));
    CHECK(small.stomach == doctest::Approx(0.5));
    CHECK(large.stomach == doctest::Approx(2.0));
    CHECK(small.lifespan == doctest::Approx(250));
    CHECK(large.lifespan == doctest::Approx(900));
    CHECK(small.steps == 2);  // grazers move half as far again, rounded up
    const Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"speed", 7}}), 3, 3);
    CHECK(predator.steps == 4);
}

TEST_CASE("litters: small bodies and a high litter trait make more young, predators half as many") {
    const std::array<std::pair<int, int>, 2> fecund = {std::pair{7, 0}, std::pair{7, 0}};
    const std::array<std::pair<int, int>, 2> sparse = {std::pair{0, 7}, std::pair{0, 7}};
    CHECK(litter_size(fecund, Species::Grazer) == 6);
    CHECK(litter_size(sparse, Species::Grazer) == 1);
    CHECK(litter_size(fecund, Species::Predator) == 3);
}

TEST_CASE("size counts strongly in strength and toughness, and plants' thorns come from weapons") {
    Simulation simulation = world();
    const Bitzkey& target = simulation.add_agent(encode_dna(Species::Grazer), 1, 1);
    const Bitzkey& brute = simulation.add_agent(encode_dna(Species::Predator, {{"strength", 7}}), 2, 2);
    const Bitzkey& giant = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 3, 3);
    CHECK(Simulation::damage(giant, target) == doctest::Approx(Simulation::damage(brute, target)));
    const Bitzkey& thorny = simulation.add_agent(encode_dna(Species::Plant, {{"weapons", 4}, {"strength", 0}}), 5, 5);
    const Bitzkey& strong = simulation.add_agent(encode_dna(Species::Plant, {{"weapons", 4}, {"strength", 7}}), 6, 6);
    CHECK(Simulation::damage(thorny, target) == Simulation::damage(strong, target));
    const Bitzkey& full = simulation.add_agent(encode_dna(Species::Plant, {{"weapons", 7}, {"size", 7}}), 7, 7);
    CHECK(Simulation::damage(full, target) == doctest::Approx(15));
}

TEST_CASE("animals shun mates whose ring or dot differ too far, and tolerance widens that") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"size", 2}, {"fear", 3}});
    Bitzkey& animal = simulation.add_agent(dna, 1, 1);
    const Bitzkey& close = simulation.add_agent(dna ^ (0x08ULL << 56) ^ 0x10, 2, 2);
    const Bitzkey& ringed = simulation.add_agent(dna ^ (0x40ULL << 56), 3, 3);
    Bitzkey& strange = simulation.add_agent(dna ^ (0xFFULL << 56) ^ 0xFF, 4, 4);
    CHECK(Simulation::compatible(animal, close));
    CHECK_FALSE(Simulation::compatible(animal, ringed));
    animal.mind[Tolerance] = 7;
    CHECK(Simulation::willing(animal, strange));
    CHECK_FALSE(Simulation::compatible(animal, strange));  // but the stranger shuns it
    strange.mind[Tolerance] = 2;
    CHECK(Simulation::compatible(animal, strange));
}

TEST_CASE("predators hunt other kinds freely, and their own kind only past their reluctance") {
    Simulation simulation = world();
    Bitzkey& gentle = simulation.add_agent(encode_dna(Species::Predator, {{"empathy", 7}, {"tolerance", 7}, {"patience", 7}}), 1, 1);
    const Bitzkey& kin = simulation.add_agent(gentle.dna, 2, 1);
    const Bitzkey& stranger = simulation.add_agent(gentle.dna ^ (0xFFULL << 56), 3, 1);
    gentle.hunger = 30;
    CHECK(Simulation::is_prey(gentle, stranger));
    CHECK_FALSE(Simulation::is_prey(gentle, kin));
    gentle.own_rivalry = 0.8;  // reluctance worn down to a fifth
    CHECK(Simulation::is_prey(gentle, kin));
}

TEST_CASE("omnivores fight their own only in an overcrowded territory") {
    Simulation simulation = world();
    Bitzkey& ruthless = simulation.add_agent(encode_dna(Species::Omnivore), 1, 1);
    const Bitzkey& other = simulation.add_agent(encode_dna(Species::Omnivore), 2, 1);
    ruthless.hunger = 50;
    CHECK_FALSE(Simulation::is_prey(ruthless, other));
    ruthless.own_rivalry = 0.5;
    CHECK(Simulation::is_prey(ruthless, other));
}

TEST_CASE("territory: the less empathetic tolerate fewer rivals, and omnivores crowd predators") {
    Simulation simulation = world(30);
    Bitzkey& callous = simulation.add_agent(encode_dna(Species::Predator, {{"empathy", 0}}), 15, 15);
    for (const int x : {11, 12, 13, 17, 18, 19}) simulation.add_agent(encode_dna(Species::Omnivore), x, 15);
    simulation.index_cells();
    const auto [rivalry, own] = simulation.territory(callous);
    CHECK(rivalry == 1.0);
    CHECK(own == 0.0);
}

TEST_CASE("the herd action moves toward the middle of the herd, fear setting how close it keeps") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}});
    Bitzkey& fearful = simulation.add_agent(encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}, {"fear", 7}}), 2, 5);
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{5, 4}, {5, 6}, {6, 5}}) simulation.add_agent(dna, x, y);
    simulation.index_cells();
    auto sensed = simulation.sense(fearful);
    REQUIRE(std::find(sensed.options.begin(), sensed.options.end(), Herd) != sensed.options.end());
    CHECK(sensed.situation.herd_middle == Cell{5, 5});
    simulation.act(fearful, Herd, sensed.situation);
    CHECK(fearful.x == 4);
    CHECK(fearful.last_action == "herded");
}

TEST_CASE("striking a herd-mate alarms the herd and rouses those beside the attacker") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}, {"sight", 7}});
    Bitzkey& victim = simulation.add_agent(dna, 5, 5);
    const Bitzkey& beside = simulation.add_agent(dna, 6, 6);
    const Bitzkey& near = simulation.add_agent(dna, 3, 5);
    const Bitzkey& far = simulation.add_agent(dna, 9, 9);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 6, 5);
    simulation.index_cells();
    simulation.attack(predator, victim);
    CHECK(beside.alarm.value_or(-1) == predator.identifier);
    CHECK(near.alarm.value_or(-1) == predator.identifier);
    CHECK_FALSE(far.alarm.has_value());
    CHECK(beside.herd_assailant.value_or(-1) == predator.identifier);
    CHECK_FALSE(near.herd_assailant.has_value());
}

TEST_CASE("young start with their parents' current minds, which their dots show") {
    Simulation simulation = world();
    simulation.set_mutation(0, 50);
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"fear", 2}, {"curiosity", 5}, {"libido", 6}});
    Bitzkey& first = simulation.add_agent(dna, 5, 5);
    Bitzkey& second = simulation.add_agent(dna, 5, 6);
    first.mind[Fear] = 4.0;
    first.mind[Curiosity] = 3.0;
    first.mind[Libido] = 7.0;
    second.mind[Fear] = 3.0;
    second.mind[Curiosity] = 5.0;
    second.mind[Libido] = 7.0;
    simulation.index_cells();
    simulation.breed(first, second, {{5, 4}});
    const Bitzkey& child = *simulation.agents().back();
    CHECK(child.mind[Fear] == 3.5);
    CHECK(child.mind[Curiosity] == 4.0);
    CHECK(child.mind[Libido] == 7.0);
    CHECK((child.inborn[Fear] == 3 || child.inborn[Fear] == 4));
    CHECK(child.inborn[Curiosity] == 4);
}

TEST_CASE("each species weighs the food it eats against the mouths sharing it") {
    Simulation simulation = world(30);
    simulation.grass_cell(15, 13) = 4;
    simulation.grass_cell(16, 15) = 2;
    const Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 15, 15);
    const Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore), 14, 15);
    const Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 17, 15);
    simulation.add_agent(encode_dna(Species::Plant), 15, 17);
    simulation.add_agent(encode_dna(Species::Grazer), 15, 18);
    simulation.index_cells();
    simulation.set_grass_regrowth(0);  // judge the standing grass alone
    CHECK(simulation.food_supply(grazer) == doctest::Approx(1.25 * (0.25 * 6 + 2) / (1 + 1 + 0.5)));
    CHECK(simulation.food_supply(omnivore) == doctest::Approx((0.5 * 0.25 * 6 + 0.75 * 2 + 2 * 2) / (1 + 0.5)));
    CHECK(simulation.food_supply(predator) == doctest::Approx(3 * 2));
    simulation.add_agent(encode_dna(Species::Predator) ^ (0xFFULL << 56), 18, 16);  // a rival, not food
    simulation.index_cells();
    CHECK(simulation.food_supply(predator) == doctest::Approx(3 * 2 / 2.0));
}

TEST_CASE("scarce food stops breeding, and plenty allows full litters") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"fertility", 7}, {"litter", 7}});
    Bitzkey& first = simulation.add_agent(dna, 5, 5);
    simulation.add_agent(dna, 5, 6);
    simulation.index_cells();
    simulation.set_grass_regrowth(0);  // a barren world
    auto has_breed = [&] {
        const auto options = simulation.sense(first).options;
        return std::find(options.begin(), options.end(), Breed) != options.end();
    };
    CHECK_FALSE(has_breed());
    for (int x = 0; x < 12; ++x) {
        for (int y = 0; y < 12; ++y) simulation.grass_cell(x, y) = 3;
    }
    CHECK(has_breed());
    simulation.breed(first, *simulation.agents()[1], simulation.free_cells_near(first));
    CHECK(simulation.agents().size() == 2 + 6);
}

TEST_CASE("a distant hunter is watched but not fled, and a herd flees only hunters closer still") {
    Simulation simulation = world();
    const Dna dna = encode_dna(Species::Grazer, {{"empathy", 7}, {"patience", 7}, {"sight", 7}});
    Bitzkey& grazer = simulation.add_agent(dna, 5, 6);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 5, 2);
    simulation.index_cells();
    const auto offers_flight = [&] {
        const auto sensed = simulation.sense(grazer);
        return std::find(sensed.options.begin(), sensed.options.end(), Flee) != sensed.options.end();
    };
    CHECK(simulation.sense(grazer).situation.hunter == &predator);
    CHECK_FALSE(offers_flight());  // four steps off
    predator.y = 3;
    simulation.index_cells();
    CHECK(offers_flight());  // three steps off
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{4, 6}, {6, 6}, {5, 7}}) simulation.add_agent(dna, x, y);
    simulation.index_cells();
    CHECK(Simulation::flee_range(grazer, 3) == doctest::Approx(1.5));
    CHECK_FALSE(offers_flight());  // its herd watches with it
}

TEST_CASE("grazers are the plant and grass specialists, and omnivores live on meat") {
    Simulation simulation = world();
    const Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 1, 1);
    const Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore), 2, 2);
    const Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 3, 3);
    CHECK(grazer.grazing == 1.25);
    CHECK(omnivore.grazing == 0.5);
    CHECK(omnivore.browsing == 0.75);
    CHECK(grazer.browsing == 1.25);
    CHECK(predator.browsing == 0.0);
}

TEST_CASE("plants seed less the more plants shade them") {
    Simulation simulation = world(30);
    Bitzkey& plant = simulation.add_agent(encode_dna(Species::Plant, {{"fertility", 7}}), 15, 15);
    for (int x = 16; x <= 18; ++x) simulation.add_agent(encode_dna(Species::Plant), x, 15);  // three within three cells
    const std::size_t before = simulation.agents().size();
    const double damage = CLIMATE_DAMAGE;
    CLIMATE_DAMAGE = 0;  // out of their temperature through the winters, the plants wither but live
    for (int turn = 0; turn < 500; ++turn) {
        plant.hunger = 0;
        simulation.take_turn();
    }
    CLIMATE_DAMAGE = damage;
    // Shaded by three plants, the first never seeds; its neighbours, less shaded, may.
    CHECK(plant.children == 0);
    CHECK(simulation.agents().size() >= before);
}

TEST_CASE("hunters switch away from prey that is scarce nearby") {
    Simulation simulation = world(30);
    Bitzkey& omnivore = simulation.add_agent(encode_dna(Species::Omnivore, {{"sight", 7}}), 15, 15);
    const Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer), 15, 16);
    for (int x = 12; x < 16; ++x) simulation.add_agent(encode_dna(Species::Plant), x, 13);  // within six steps
    simulation.index_cells();
    CHECK(simulation.prey_switching(omnivore).first == false);  // a grazer against four plants
    simulation.sense(omnivore);
    CHECK_FALSE(Simulation::is_prey(omnivore, grazer));
    simulation.add_agent(encode_dna(Species::Grazer), 16, 16);
    simulation.index_cells();
    simulation.sense(omnivore);
    CHECK(Simulation::is_prey(omnivore, grazer));  // two in six: common enough

    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator), 25, 25);
    for (int x = 20; x < 25; ++x) simulation.add_agent(encode_dna(Species::Omnivore), x, 22);
    simulation.index_cells();
    CHECK(simulation.prey_switching(predator) == std::pair{false, true});  // omnivores near, and no grazers
}

TEST_CASE("a blow that exhausts the striker kills it, and no body is left without a recorded death") {
    Simulation simulation = world();
    Bitzkey& grazer = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}, {"strength", 7}, {"weapons", 7}}), 5, 5);
    Bitzkey& predator = simulation.add_agent(encode_dna(Species::Predator, {{"size", 7}}), 5, 6);
    simulation.index_cells();
    grazer.hunger = 99.9;
    simulation.defend(grazer, predator);
    CHECK_FALSE(grazer.alive());
    CHECK(grazer.died_turn == simulation.turn());
    CHECK(grazer.last_action == "exhausted");

    Simulation busy(Simulation::Settings{60, 60, 90, 1});
    for (int turn = 0; turn < 300; ++turn) busy.advance();
    for (const auto& agent : busy.agents()) CHECK((agent->alive() || agent->died_turn.has_value()));
}

TEST_CASE("a dead Bitzkey leaves meat by how nourished it was, and the brain is gone") {
    Simulation simulation = world();
    Bitzkey& fed = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 2, 2);
    Bitzkey& starved = simulation.add_agent(encode_dna(Species::Grazer, {{"size", 7}}), 4, 4);
    starved.hunger = 100;
    simulation.die(fed, "wounded");
    simulation.die(starved, "starved");
    CHECK(fed.meat == doctest::Approx(80));
    CHECK(starved.meat == 0);
    CHECK(fed.brain == nullptr);
    CHECK_FALSE(fed.alive());
}

TEST_CASE("a world with no animals left has ended") {
    Simulation simulation = world();
    simulation.add_agent(encode_dna(Species::Plant), 2, 2);
    CHECK(simulation.end_reason() == EndReason::Extinct);
    simulation.add_agent(encode_dna(Species::Grazer), 3, 3);
    CHECK(simulation.end_reason() == EndReason::None);
}

TEST_CASE("grass grows twice as fast on and around corpses") {
    Simulation simulation = world();
    const auto& rows = simulation.medium_rows();  // grass grows only where it is medium
    for (const int y : {6, 7, 8}) CHECK(std::find(rows.begin(), rows.end(), y) != rows.end());
    Bitzkey& corpse = simulation.add_agent(encode_dna(Species::Grazer), 0, 7);
    corpse.health = 0;
    simulation.die(corpse, "wounded");
    simulation.clear_grass();
    simulation.sow(20000);
    double fertile = 0;
    int cells = 0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            fertile += simulation.grass_at(py_mod(dx, 12), 7 + dy);
            ++cells;
        }
    }
    const double elsewhere = (static_cast<double>(simulation.grass_tufts()) - fertile) / (12 * static_cast<int>(rows.size()) - cells);
    CHECK(fertile / cells / elsewhere == doctest::Approx(2).epsilon(0.2));
}
