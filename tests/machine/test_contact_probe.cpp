// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's ContactProbeNozzle, as the cell runs it: the contact sense
// actuator's probe meets the surface, the nozzle is where the controller
// says it stopped, plus the final adjustment; its retract. A nozzle tip's Z
// calibration at its touch location: the offset (where it was met, from
// where it should) moves every Z of the nozzle; one too large is refused; a
// probe meeting nothing fails.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <cmath>
#include <thread>

using namespace jf;

namespace {

JPCellConfig cellConfig(const char* surface) {
    const std::string json = R"({
      "name": "Probe",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z" ])"
                         + std::string(surface) + R"( } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 100 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y", "feedratePerSecond": 100 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 50,
                  "safeZone": { "low": 0, "high": 0, "lowEnabled": true, "highEnabled": false } } ],
      "actuators": [ { "id": "P", "name": "Probe", "driver": "D", "onCommand": "G38.2 Z-20 F100", "offCommand": "G0 Z0" } ],
      "nozzleTips": [ { "id": "T", "name": "Tip", "touchLocation": { "x": 10, "y": 10, "z": -6, "rotation": 0 } } ],
      "nozzles": [ { "id": "N", "name": "N1", "tip": "T", "tips": [ "T" ],
                     "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "Z" },
                     "contactProbe": { "method": "ContactSenseActuator", "actuator": "P", "startOffset": 1, "depth": 2,
                                       "adjust": -0.2, "maxZOffset": 2 } } ]
    })";
    JPCellConfig c;
    std::string error;
    const bool ok = c.fromJson(JJson::parse(json), error);
    assert(ok && c.problems().empty());
    return c;
}

std::vector<JPFirmwareProfile> profiles() {
    JPFirmwareProfile p;
    std::string error;
    const bool ok = p.load(std::string(JPLACER_PROFILES_DIR) + "/grblhal.json", error);
    assert(ok);
    return { p };
}

void start(JPCell& cell) {
    cell.connect();
    for (int i = 0; i < 300 && !cell.isConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isConnected());
    cell.home();
    for (int i = 0; i < 300 && !cell.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isHomed());
}

bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

void waitFor(const std::function<bool()>& done) {
    for (int i = 0; i < 300 && !done(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

} // namespace

int main() {
    {
        JPCell cell(cellConfig(R"(, "probeSurface": { "Z": -6.5 })"), profiles());
        start(cell);
        std::string why;
        assert(cell.moveAxesAndWait({ { "X", 10 }, { "Y", 10 }, { "Z", -5 } }, 1.0, why));
        double z = 0;
        // Met at -6.5, the final adjustment (0.2 down) after.
        assert(cell.contactProbeAndWait("N", true, 2, z, why));
        assert(near(z, -6.7) && near(cell.jogBase().at("Z"), -6.7));
        assert(cell.contactProbeAndWait("N", false, 2, z, why) && near(z, 0));
        // The tip met at -6.5 (with the final adjustment, -6.7) where it should be at -6: every Z of the nozzle 0.7 lower.
        assert(!cell.zCalibration("N"));
        cell.calibrateZ("N", false);
        waitFor([&] { return cell.zCalibration("N").has_value(); });
        assert(cell.zCalibration("N") && near(*cell.zCalibration("N"), 0.7));
        const JPMountConfig& mount = cell.config().nozzles[0].mount;
        assert(cell.moveToolAndWait(mount, { 10.0, 10.0, -6.0, std::nullopt }, 1.0, why));
        assert(near(cell.jogBase().at("Z"), -6.7));
        // Discard probing: the probe sent on the way down to the discard place, the part let go there.
        {
            JPCellConfig config = cell.config();
            config.discardLocation = JPMachineLocation { 20, 20, -5, 0 };
            config.nozzles[0].contactProbe.discardProbing = true;
            config.nozzles[0].vacuumActuatorId = "P";   // something to switch for the let-go
            assert(cell.reconfigure(config, why));
            bool probed = false;
            cell.onTraffic.connect([&probed](const std::string&, bool out, const std::string& line) {
                if (out && line.rfind("G38.2", 0) == 0) probed = true;
            });
            assert(cell.discardAndWait("N", 1.0, why) && probed);
        }
        // Reset: as it was.
        cell.calibrateZ("N", true);
        waitFor([&] { return !cell.zCalibration("N").has_value(); });
        assert(!cell.zCalibration("N"));
        // Calibrated and waited for; then another touch location probed with it (OpenPnP's
        // Calibrate all Touch Locations' Z to Template): met where the calibrated nozzle says,
        // the same surface, so at the template's Z.
        assert(cell.calibrateZAndWait("N", why) && near(*cell.zCalibration("N"), 0.7));
        assert(cell.contactProbeCycleAndWait("N", JPMachineLocation { 30, 10, -5, 0 }, false, z, why));
        assert(near(z, -6.0) && cell.zCalibration("N"));
        // A reference probe: the calibration forgotten first, the Z as the machine has it.
        assert(cell.contactProbeCycleAndWait("N", JPMachineLocation { 30, 10, -5, 0 }, true, z, why));
        assert(near(z, -6.7) && !cell.zCalibration("N"));
        cell.disconnect();
    }
    {
        // Met 6.5 below where the tip should be: more than the largest offset, refused.
        JPCellConfig config = cellConfig(R"(, "probeSurface": { "Z": -6.5 })");
        config.nozzleTips[0].touchLocation->z = 0;
        JPCell cell(config, profiles());
        start(cell);
        std::string alarm;
        cell.onAlarm.connect([&alarm](std::string w) { alarm = w; });
        cell.calibrateZ("N", false);
        waitFor([&] { return !alarm.empty(); });
        assert(alarm.find("unexpectedly large") != std::string::npos && !cell.zCalibration("N"));
        cell.disconnect();
    }
    {
        // Nothing to meet: the probe fails.
        JPCell cell(cellConfig(""), profiles());
        start(cell);
        std::string why;
        double z = 0;
        assert(!cell.contactProbeAndWait("N", true, 2, z, why));
        cell.disconnect();
    }
    return 0;
}
