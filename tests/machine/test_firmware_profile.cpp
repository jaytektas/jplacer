// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The bundled firmware profiles load, and the grblHAL one reads identification,
// plugins, status reports and settings the way a grblHAL controller writes them.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPFirmwareProfile.h"

#include <string>

using namespace jf;

static JPFirmwareProfile load(const char* file) {
    JPFirmwareProfile p;
    std::string error;
    const bool ok = p.load(std::string(JPLACER_PROFILES_DIR) + "/" + file, error);
    assert(ok && error.empty());
    return p;
}

int main() {
    // A value left out goes with its letter (OpenPnP's send on change only).
    assert(JPFirmwareProfile::fill("G1 {axes} F{feed}", { { "axes", "X1" }, { "feed", JPFirmwareProfile::kLeaveOut } }) == "G1 X1");
    assert(JPFirmwareProfile::fill("M204 S{acceleration} P1", { { "acceleration", JPFirmwareProfile::kLeaveOut } }) == "M204 P1");
    const JPFirmwareProfile generic = load("generic.json");
    const JPFirmwareProfile grbl    = load("grbl.json");
    const JPFirmwareProfile hal     = load("grblhal.json");
    assert(generic.identifyCommand().empty());
    assert(hal.priority() > grbl.priority() && grbl.priority() > generic.priority());

    const std::vector<std::string> identity = {
        "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]", "[PLUGIN:JayTEK v0.01]"
    };
    assert(hal.identifies(identity));
    assert(grbl.identifies(identity));                 // why grblHAL is tried first
    assert(!hal.identifies({ "[VER:1.1h.20190825:]" }));
    const auto plugins = hal.pluginsIn(identity);
    assert(plugins.size() == 1 && plugins[0]->name == "JayTEK");
    assert(plugins[0]->readings.size() == 2);
    assert(JPFirmwareProfile::fill(plugins[0]->passThroughCommand, { { "payload", "0A01" } }) == "M485 0A01");

    assert(hal.isOk("ok") && !hal.isOk("okay?"));
    assert(hal.errorIn("error:20") && hal.errorIn("ALARM:1") && !hal.errorIn("ok"));

    const auto st = hal.parseStatus("<Idle|MPos:10.000,-5.500,1.250,90.000|FS:0,0>");
    assert(st && st->state == "Idle");
    assert(st->positions.at("X") == 10.0 && st->positions.at("Y") == -5.5);
    assert(st->positions.at("Z") == 1.25 && st->positions.at("A") == 90.0);
    assert(!st->positions.count("B"));
    assert(!hal.parseStatus("[MSG:Caution: Unlocked]"));

    assert(hal.command("move", { { "axes", "X1 Y2" }, { "feed", "3000" } }) == "G1 X1 Y2 F3000");
    assert(!hal.command("no-such-command"));

    assert(hal.axisSettingId("stepsPerMm", "X") == "100");
    assert(hal.axisSettingId("acceleration", "A") == "123");
    assert(!hal.axisSettingId("stepsPerMm", "Q"));
    const auto set = hal.parseSetting("$110=5000.000");
    assert(set && set->first == "110" && set->second == "5000.000");
    assert(hal.settingWriteCommand("100", "80") == "$100=80");

    // OpenPnP's other firmwares, as its GcodeDriverSolutions sets them up: each
    // known by its M115 reply, its position read from M114 (an extruder's E, and
    // Marlin's step counts, left out), moves waited for with M400.
    {
        const JPFirmwareProfile smoothie = load("smoothieware.json"), marlin = load("marlin.json");
        const JPFirmwareProfile duet = load("reprapfirmware.json"), tinyg = load("tinyg.json");
        assert(smoothie.identifies({ "ok", "FIRMWARE_NAME:Smoothieware, FIRMWARE_URL:http%3A//smoothieware.org, X-SOURCE_CODE_URL:https://github.com/Smoothieware/Smoothieware" }));
        assert(marlin.identifies({ "FIRMWARE_NAME:Marlin 2.0.9.3 (Nov 10 2021) SOURCE_CODE_URL:github.com/MarlinFirmware/Marlin" }));
        assert(duet.identifies({ "FIRMWARE_NAME: RepRapFirmware for Duet 3 MB6HC FIRMWARE_VERSION: 3.4.5" }));
        assert(!marlin.identifies({ "FIRMWARE_NAME:Smoothieware" }) && !smoothie.identifies({ "[VER:1.1f.20250101:]" }));
        const auto s = smoothie.parseStatus("ok C: X:12.5000 Y:-3.2500 Z:0.0000 A:90.0000");
        assert(s && s->positions.at("X") == 12.5 && s->positions.at("Y") == -3.25 && s->positions.at("A") == 90);
        const auto m = marlin.parseStatus("X:10.00 Y:20.00 Z:-1.50 E:0.00 Count X:800 Y:1600 Z:-120");
        assert(m && m->positions.size() == 3 && m->positions.at("X") == 10 && m->positions.at("Z") == -1.5);
        const auto d = duet.parseStatus("X:1.000 Y:2.000 Z:3.000 U:4.000 E:0.000 Count 80 160 400 Machine 1.000 2.000 3.000 Bed comp 0.000");
        assert(d && d->positions.at("U") == 4 && !d->positions.count("E"));
        assert(tinyg.isOk("tinyg [mm] ok>") && tinyg.errorIn("tinyg [mm] err: Unrecognized command"));
        // Their properties, as OpenPnP reads them: a URL's colons kept, a comma ending one, %3A a colon.
        const std::string say = "FIRMWARE_NAME:Smoothieware, FIRMWARE_URL:http%3A//smoothieware.org, "
                                "X-SOURCE_CODE_URL:https://github.com/openpnp/Smoothieware-best-for-pnp, X-AXES:6, X-PAXES:6";
        assert(JPFirmwareProfile::property(say, "FIRMWARE_NAME") == "Smoothieware");
        assert(JPFirmwareProfile::property(say, "FIRMWARE_URL") == "http://smoothieware.org");
        assert(JPFirmwareProfile::property(say, "X-SOURCE_CODE_URL") == "https://github.com/openpnp/Smoothieware-best-for-pnp");
        assert(JPFirmwareProfile::property(say, "X-PAXES") == "6" && JPFirmwareProfile::property(say, "NONE", "x") == "x");
        assert(JPFirmwareProfile::property("FIRMWARE_NAME:Marlin 2.1 SOURCE_CODE_URL:github.com/x AXIS_COUNT:4 UUID:abc", "AXIS_COUNT") == "4");
        for (const JPFirmwareProfile* p : { &smoothie, &marlin, &duet, &tinyg })
            assert(p->identifyCommand() == "M115" && p->statusCommand() == "M114" && p->commands().at("waitMotion") == "M400");
    }

    JPFirmwareProfile broken;
    std::string error;
    assert(!broken.load(std::string(JPLACER_PROFILES_DIR) + "/missing.json", error) && !error.empty());
    return 0;
}
