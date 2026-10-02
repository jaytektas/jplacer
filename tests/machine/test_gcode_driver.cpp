// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The G-code driver against a simulated grblHAL controller: it identifies the
// firmware and its plugin, moves, keeps the position live from status reports,
// reads stored settings, takes a plugin reading, and reports refusals.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPGcodeDriver.h"

#include <chrono>
#include <regex>
#include <string>
#include <thread>

using namespace jf;

static std::vector<JPFirmwareProfile> profiles() {
    std::vector<JPFirmwareProfile> out;
    for (const char* f : { "grblhal.json", "grbl.json", "generic.json" }) {
        JPFirmwareProfile p;
        std::string error;
        const bool ok = p.load(std::string(JPLACER_PROFILES_DIR) + "/" + f, error);
        assert(ok);
        out.push_back(std::move(p));
    }
    return out;
}

static JPDriverConfig config(const char* identity) {
    JJson sim = JJson::object();
    sim["identity"] = JJson::array();
    sim["identity"].push("[VER:1.1f.20250101:]");
    sim["identity"].push(identity);
    sim["identity"].push("[PLUGIN:JayTEK v0.01]");
    sim["axisLetters"] = JJson::array();
    for (const char* l : { "X", "Y", "Z", "A" }) sim["axisLetters"].push(l);
    sim["settings"] = JJson::object();
    sim["settings"]["100"] = "80.000";
    sim["settings"]["110"] = "5000.000";
    sim["replies"] = JJson::object();
    sim["replies"]["M1000 P0"] = "-12000";

    JPDriverConfig c;
    c.id   = "gantry";
    c.name = "Gantry";
    c.link = JJson::object();
    c.link["type"]      = "simulated";
    c.link["simulator"] = sim;
    c.statusIntervalMs  = 10;
    c.commandTimeoutMs  = 500;
    c.identifyTimeoutMs = 200;
    return c;
}

int main() {
    {
        JPGcodeDriver driver(config("[FIRMWARE:grblHAL]"), profiles());
        assert(!driver.send("G0 X1").get().ok);          // not connected yet

        std::string error;
        const bool connected = driver.connect(error);
        assert(connected && error.empty());
        assert(driver.profile() && driver.profile()->id() == "grblhal");
        assert(driver.plugins().size() == 1 && driver.plugins()[0]->name == "JayTEK");

        int statusSeen = 0;
        driver.onStatus.connect([&](JPFirmwareProfile::Status) { ++statusSeen; });

        const JPReply moved = driver.sendCommand("move", { { "axes", "X10 Y5" }, { "feed", "3000" } }).get();
        assert(moved.ok);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (driver.status().positions["X"] != 10.0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const JPFirmwareProfile::Status st = driver.status();
        assert(st.state == "Idle" && st.positions.at("X") == 10.0 && st.positions.at("Y") == 5.0);
        assert(statusSeen > 0);

        assert(driver.readSettings(error));
        assert(driver.axisSetting("stepsPerMm", "X") == 80.0);
        assert(driver.axisSetting("maxRate", "X") == 5000.0);
        assert(!driver.axisSetting("travel", "X"));       // not stored by this controller

        const JPFirmwareProfile::Reading& vacuum = driver.plugins()[0]->readings[0];
        const JPReply read = driver.send(JPFirmwareProfile::fill(vacuum.command, { { "sensor", "0" } })).get();
        assert(read.ok && read.lines.size() == 1);
        std::smatch m;
        assert(std::regex_search(read.lines[0], m, vacuum.pattern) && m[1] == "-12000");

        const JPReply refused = driver.send("M9999").get();
        assert(!refused.ok && refused.error == "error:20");
        assert(!driver.sendCommand("no-such-command").get().ok);

        driver.disconnect();
        assert(!driver.isConnected() && !driver.send("G0 X0").get().ok);
    }
    {
        // A port where nothing answers is not a connection.
        JPDriverConfig c = config("[FIRMWARE:grblHAL]");
        c.link["simulator"]["silent"] = true;
        JPGcodeDriver driver(c, profiles());
        std::string error;
        assert(!driver.connect(error) && error.find("nothing answered") != std::string::npos);
        assert(!driver.isConnected());
    }
    {
        // Bytes left on the line spoil the first command; identification asks again.
        JPDriverConfig c = config("[FIRMWARE:grblHAL]");
        c.link["simulator"]["garbleFirstLine"] = true;
        JPGcodeDriver driver(c, profiles());
        std::string error;
        assert(driver.connect(error) && driver.profile()->id() == "grblhal");
    }
    {
        // A Grbl that is not grblHAL falls to the Grbl profile.
        JPGcodeDriver driver(config("[OPT:V,15,128]"), profiles());
        std::string error;
        assert(driver.connect(error));
        assert(driver.profile()->id() == "grbl" && driver.plugins().empty());
    }
    return 0;
}
