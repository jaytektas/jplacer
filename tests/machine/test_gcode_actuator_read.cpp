// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's GcodeDriverTest: a GcodeDriver connected over TCP to a G-code server, an actuator read by its
// Actuator Read Command, the value the Actuator Read Regex's "Value" group finds in the reply ("read:a1:497"
// gives 497); and the read refused without a regex, without a command, and with a regex the reply does not match.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"
#include "machine/JPFirmwareProfile.h"
#include "machine/JPValueRegex.h"

#include <j/config/Json.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace jf;

namespace {

// OpenPnP's GcodeServer: "ok" to every line, "read:a1:497" before it to READ A1.
struct GcodeServer {
    int               listener = -1;
    int               port = 0;
    std::atomic<bool> stop { false };
    std::thread       thread;

    GcodeServer() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(::bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::listen(listener, 4) == 0);
        socklen_t len = sizeof a;
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
        thread = std::thread([this] {
            while (!stop) {
                const int c = ::accept(listener, nullptr, nullptr);
                if (c < 0) return;
                std::string pending;
                char buf[512];
                ssize_t n;
                while ((n = ::recv(c, buf, sizeof buf, 0)) > 0) {
                    pending.append(buf, size_t(n));
                    for (size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
                        std::string line = pending.substr(0, nl);
                        pending.erase(0, nl + 1);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.empty()) continue;
                        const std::string reply = line == "READ A1" ? "read:a1:497\nok\n" : "ok\n";
                        ::send(c, reply.data(), reply.size(), MSG_NOSIGNAL);
                    }
                }
                ::close(c);
            }
        });
    }
    ~GcodeServer() {
        stop = true;
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        thread.join();
    }
};

std::vector<JPFirmwareProfile> profiles() {
    JPFirmwareProfile p;
    std::string error;
    assert(p.load(std::string(JPLACER_PROFILES_DIR) + "/generic.json", error));
    return { p };
}

// The machine with one GcodeDriver over TCP and actuator A1 read by `command` and `regex` (empty: none set).
bool read(int port, const std::string& command, const std::string& regex, std::string& value, std::string& why) {
    JJson j = JJson::parse(R"({ "name": "GcodeDriverTest",
        "drivers": [ { "id": "D", "name": "GcodeDriver", "openpnpClass": "GcodeDriver", "profile": "generic",
                       "connectWaitMs": 0, "commandTimeoutMs": 500, "link": { "type": "tcp", "host": "127.0.0.1" } } ],
        "actuators": [ { "id": "A1", "name": "A1", "driver": "D" } ] })");
    j["drivers"][0]["link"]["port"] = port;
    if (!command.empty()) j["actuators"][0]["readCommand"] = command;
    if (!regex.empty()) j["actuators"][0]["readPattern"] = regex;
    JPCellConfig config;
    std::string error;
    assert(config.fromJson(j, error));
    JPCell cell(config, profiles());
    std::atomic<int> connected { 0 };
    cell.onConnection.connect([&](bool ok, std::string) { connected = ok ? 1 : -1; });
    cell.connect();
    for (int i = 0; i < 200 && connected == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(connected == 1);
    const bool ok = cell.readActuatorAndWait("A1", std::nullopt, value, why);
    std::fprintf(stderr, "read by \"%s\", \"%s\": %s\n", command.c_str(), regex.c_str(), ok ? value.c_str() : why.c_str());
    cell.disconnect();
    return ok;
}

} // namespace

int main() {
    GcodeServer server;
    std::string value, why;
    // testActuatorRead
    assert(read(server.port, "READ A1", "read:a1:(?<Value>-?\\d+)", value, why) && value == "497");
    // testActuatorReadNoRegex
    assert(!read(server.port, "READ A1", "", value, why));
    // testActuatorReadNoCommand
    assert(!read(server.port, "", "read:a1:(?<Value>-?\\d+)", value, why));
    // testActuatorReadBadRegex
    assert(!read(server.port, "READ A1", "reXXad:a1:(?<Value>-?\\d+)", value, why));

    // The Value group found among others: named, plain, non-capturing, a look-ahead, a bracket in a class.
    JPValueRegex re;
    std::smatch m;
    const std::string reply = "T:21.5 /0.0 B:60.2 /60.0";
    assert(re.compile("T:(?<Tool>[0-9.]+) /[0-9.]+ B:(?<Value>[0-9.]+)") && re.valueGroup() == 2u);
    assert(std::regex_search(reply, m, re.regex()) && m[*re.valueGroup()] == "60.2");
    assert(re.compile("(?:T):([(0-9.]+) (?=/)/([0-9.]+) B:(?<Value>[0-9.]+)") && re.valueGroup() == 3u);
    assert(std::regex_search(reply, m, re.regex()) && m[*re.valueGroup()] == "60.2");
    assert(re.compile("^(-?\\d+)$") && !re.valueGroup());
    assert(!re.compile("(?<Value>"));
    return 0;
}
