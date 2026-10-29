#pragma once
// Where a world report reads its world from: a probe port, the app's (/probe) or the daemon's (/api/probe), or a
// saved world, loaded here and served by a probe of its own on a port of this computer. Either way it is read through
// the same routes, so a save and a running world report alike.

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "core/Simulation.hpp"
#include "net/Probe.hpp"

namespace httplib {
class Server;
}

namespace bitzkey::report {

using Json = nlohmann::json;

class Source {
public:
    // A probe at "host:port", "port" alone (on this computer) or a whole "http://host:port" address. Its routes are
    // found under /probe or /api/probe, whichever answers. Throws std::runtime_error if neither does.
    static std::unique_ptr<Source> port(const std::string& address);
    // A saved world, loaded. Throws std::runtime_error, naming the problem, if it can't be read.
    static std::unique_ptr<Source> save(const std::filesystem::path& path);
    ~Source();
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    // A route's answer, such as "state" or "herds?species=grazer". Throws std::runtime_error when it fails.
    Json get(const std::string& route);
    std::string text(const std::string& route);
    // What it reads, for the panel's header: the address, or the save's name.
    const std::string& name() const { return name_; }
    bool live() const { return !world_; }  // a running world, worth looking at again

private:
    Source() = default;
    std::string fetch(const std::string& route);

    std::string name_, host_, prefix_;
    int port_ = 0;
    // A save's world, and the probe serving it.
    std::unique_ptr<Simulation> world_;
    std::mutex world_mutex_;
    std::unique_ptr<net::Probe> probe_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
};

}  // namespace bitzkey::report
