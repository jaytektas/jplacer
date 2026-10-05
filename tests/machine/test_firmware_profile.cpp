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

    JPFirmwareProfile broken;
    std::string error;
    assert(!broken.load(std::string(JPLACER_PROFILES_DIR) + "/missing.json", error) && !error.empty());
    return 0;
}
