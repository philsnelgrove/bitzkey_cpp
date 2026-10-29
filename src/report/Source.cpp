#include "report/Source.hpp"

#include <regex>
#include <stdexcept>

#include <httplib.h>

#include "core/WorldFile.hpp"

namespace bitzkey::report {

std::unique_ptr<Source> Source::port(const std::string& address) {
    static const std::regex form(R"(^\s*(?:https?://)?(?:([^:/\s]+):)?(\d{1,5})/?\s*$)");
    std::smatch parts;
    if (!std::regex_match(address, parts, form)) throw std::runtime_error("give the probe as host:port, or a port alone");
    std::unique_ptr<Source> source(new Source);
    source->host_ = parts[1].matched ? parts[1].str() : "127.0.0.1";
    source->port_ = std::stoi(parts[2].str());
    source->name_ = source->host_ + ":" + std::to_string(source->port_);
    for (const char* prefix : {"/probe", "/api/probe"}) {
        source->prefix_ = prefix;
        try {
            source->get("state");
            return source;
        } catch (const std::runtime_error&) {
        }
    }
    throw std::runtime_error("no probe answers at " + source->name_);
}

std::unique_ptr<Source> Source::save(const std::filesystem::path& path) {
    std::unique_ptr<Source> source(new Source);
    source->world_ = WorldFile::load(path);
    Source* self = source.get();
    source->probe_ = std::make_unique<net::Probe>([self](const std::function<void(Simulation&)>& look) {
        std::lock_guard lock(self->world_mutex_);
        look(*self->world_);
    });
    source->server_ = std::make_unique<httplib::Server>();
    source->probe_->install(*source->server_, "/probe");
    source->port_ = source->server_->bind_to_any_port("127.0.0.1");
    if (source->port_ <= 0) throw std::runtime_error("no port to read the save through");
    source->thread_ = std::thread([self] { self->server_->listen_after_bind(); });
    source->host_ = "127.0.0.1";
    source->prefix_ = "/probe";
    const std::u8string named = path.filename().u8string();
    source->name_ = std::string(named.begin(), named.end());
    return source;
}

Source::~Source() {
    if (server_) server_->stop();
    if (thread_.joinable()) thread_.join();
}

std::string Source::fetch(const std::string& route) {
    httplib::Client client(host_, port_);
    client.set_connection_timeout(3);
    client.set_read_timeout(120);  // a large world's brain report takes a while
    const auto answer = client.Get(prefix_ + "/" + route);
    if (!answer) throw std::runtime_error("no answer from " + name_ + " for " + route);
    if (answer->status != 200) {
        std::string why = answer->body;
        try {
            why = Json::parse(answer->body).value("error", why);
        } catch (const std::exception&) {
        }
        throw std::runtime_error(route + ": " + why);
    }
    return answer->body;
}

Json Source::get(const std::string& route) {
    const std::string body = fetch(route);
    try {
        return Json::parse(body);
    } catch (const std::exception&) {
        throw std::runtime_error(route + ": not JSON");
    }
}

std::string Source::text(const std::string& route) { return fetch(route); }

}  // namespace bitzkey::report
