// Bitzkey Reports: a save read through a probe of its own, and the world's report made from it.

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "core/Rules.hpp"
#include "core/Simulation.hpp"
#include "core/WorldFile.hpp"
#include "net/Probe.hpp"
#include "report/Analysis.hpp"
#include "report/Source.hpp"

using namespace bitzkey;

TEST_CASE("a save is reported on through the same routes as a running world") {
    rules::apply(rules::current());
    Simulation world(Simulation::Settings{30, 30, 60, 7});
    for (int turn = 0; turn < 30; ++turn) world.advance();
    const auto path = std::filesystem::temp_directory_path() / "bitzkey-report-test.bzworld";
    WorldFile::save(world, path);
    {
        const auto source = report::Source::save(path);
        CHECK_FALSE(source->live());
        CHECK(source->name() == "bitzkey-report-test.bzworld");
        const report::World read = report::gather(*source);
        CHECK(read.missing.empty());
        CHECK(read.state["turn"] == world.turn());
        CHECK(read.minds["minds"].contains("grazer"));
        CHECK(read.herds.size() == 3);

        const report::Analysis analysis = report::analyse(read);
        REQUIRE(analysis.sections.size() == 10);
        CHECK(analysis.sections.front().title == "Summary");
        const std::string text = report::as_text(analysis);
        CHECK(text.find("== Herds & migration ==") != std::string::npos);
        CHECK(text.find("BITZKEY BRAIN REPORT") != std::string::npos);
        CHECK(text.find('\t') == std::string::npos);  // its tables lined up in spaces
        CHECK_THROWS_AS(source->get("animal/999999"), std::runtime_error);
    }
    std::filesystem::remove(path);
}

TEST_CASE("the minds route counts runaway values and jammed neurons") {
    Simulation world(Simulation::Settings{30, 30, 40, 3});
    for (int turn = 0; turn < 10; ++turn) world.advance();
    net::Probe probe([&world](const std::function<void(Simulation&)>& look) { look(world); });
    const auto minds = probe.minds(world);
    int thinking = 0;
    for (const char* species : {"grazer", "omnivore", "predator"}) {
        const auto& mind = minds["minds"][species];
        thinking += mind["thinking"].get<int>();
        CHECK(mind["jammed"].get<int>() <= mind["neurons"].get<int>());
        CHECK(mind["runaway"].get<int>() <= mind["valued"].get<int>());
        CHECK(mind["value_median"].get<double>() <= mind["value_max"].get<double>());
    }
    CHECK(thinking > 0);
}

TEST_CASE("a probe's address is a port, host:port or a whole address") {
    CHECK_THROWS_WITH_AS(report::Source::port("not a port"), "give the probe as host:port, or a port alone", std::runtime_error);
    CHECK_THROWS_WITH_AS(report::Source::port("127.0.0.1:1"), "no probe answers at 127.0.0.1:1", std::runtime_error);
}
