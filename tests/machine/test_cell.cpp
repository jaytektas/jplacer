// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A cell over a simulated controller: it connects, follows controller axes from
// status reports and a mapped axis through its map, switches and reads an
// actuator, takes new settings while it runs (never letting its controllers
// go), keeps a camera's calibrations a picture size each, and reports a cell
// whose controller cannot connect.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <atomic>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <string>

using namespace jf;

namespace {

std::vector<JPFirmwareProfile> profiles() {
    JPFirmwareProfile p;
    std::string error;
    const bool ok = p.load(std::string(JPLACER_PROFILES_DIR) + "/grblhal.json", error);
    assert(ok);
    return { p };
}

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Test",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ],
                         "axisLetters": [ "X", "Y", "Z", "A" ],
                         "replies": { "M1000 P1": "-31000", "M1000 P3": "25" } } } } ],
      "heads": [ { "id": "H", "name": "Head", "pump": { "actuator": "P", "control": "PartOn", "onWaitMs": 0 } } ],
      "axes": [ { "id": "X",  "name": "x",  "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "homeCoordinate": 390, "feedratePerSecond": 100,
                  "backlash": "oneSidedOptimized", "backlashOffset": 0.1, "backlashSpeedFactor": 0.25,
                  "softLimits": { "low": 0, "high": 400, "lowEnabled": true, "highEnabled": true } },
                { "id": "Z",  "name": "z",  "kind": "controller", "type": "z", "driver": "D", "letter": "Z",
                  "feedratePerSecond": 50, "resolution": 0.25 },
                { "id": "C",  "name": "c",  "kind": "controller", "type": "rotation", "driver": "D", "letter": "A",
                  "feedratePerSecond": 360, "wrapAroundRotation": true, "limitRotation": true },
                { "id": "ZR", "name": "zr", "kind": "mapped", "type": "z", "inputAxis": "Z",
                  "map": { "input0": -1, "output0": 1, "input1": 0, "output1": 0 } } ],
      "nozzles": [ { "id": "N", "name": "Right", "mount": { "head": "H", "axisX": "X", "axisZ": "ZR" }, "vacuumActuator": "V",
                     "tips": [ "T" ], "tip": "T" } ],
      "nozzleTips": [ { "id": "T", "name": "503", "partOn": { "method": "Absolute", "low": -32000, "high": -30000 } } ],
      "actuators": [ { "id": "V", "name": "Vacuum", "driver": "D", "index": "1",
                       "onCommand": "M64 P{index}", "offCommand": "M65 P{index}",
                       "readCommand": "M1000 P{index}", "readPattern": "^(-?\\d+)$" },
                     { "id": "P", "name": "Pump", "driver": "D", "index": "2",
                       "onCommand": "M64 P{index}", "offCommand": "M65 P{index}", "disabledActuation": "ActuateOff" },
                     { "id": "L", "name": "Light", "driver": "D", "valueType": "number", "valueCommand": "M3 S{value}",
                       "onValue": "255", "offValue": "0" },
                     { "id": "TH", "name": "Heat", "driver": "D", "index": "3", "readCommand": "M1000 P{index}",
                       "readPattern": "^(-?\\d+)$", "thermistor": { "scale": 2, "offset": 1 } } ]
    })";
    JPCellConfig c;
    std::string error;
    const bool ok = c.fromJson(JJson::parse(json), error);
    assert(ok && c.problems().empty());
    return c;
}

// Waits for a signal's value, set from another thread.
template <class T>
struct Latch {
    std::mutex m;
    std::condition_variable cv;
    std::optional<T> value;
    void set(T v) { { std::lock_guard lk(m); value = std::move(v); } cv.notify_all(); }
    T take() {
        std::unique_lock lk(m);
        const bool got = cv.wait_for(lk, std::chrono::seconds(2), [&] { return value.has_value(); });
        assert(got);
        T v = std::move(*value);
        value.reset();
        return v;
    }
};

} // namespace

int main() {
    {
        JPCell cell(cellConfig(), profiles());
        Latch<std::pair<bool, std::string>> connection;
        Latch<std::pair<bool, std::string>> actuator;
        cell.onConnection.connect([&](bool ok, std::string why) { connection.set({ ok, why }); });
        cell.onActuator.connect([&](std::string, bool ok, std::string v) { actuator.set({ ok, v }); });

        cell.connect();
        const auto [ok, why] = connection.take();
        assert(ok && why.empty() && cell.isConnected());
        assert(cell.firmware().at("D") == "grblHAL");

        cell.sendLine("D", "G0 X12 Z-2");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (cell.positions()["ZR"] != 2.0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto pos = cell.positions();
        assert(pos.at("X") == 12.0 && pos.at("Z") == -2.0 && pos.at("ZR") == 2.0);

        cell.switchActuator("V", true);
        assert(actuator.take() == std::make_pair(true, std::string("on")));
        cell.readActuator("V");
        assert(actuator.take() == std::make_pair(true, std::string("-31000")));
        // OpenPnP's ThermistorToLinearSensorActuator: 25 degrees C read, a linear sensor's 3.152 V, times 2 plus 1.
        cell.readActuator("TH");
        const auto heat = actuator.take();
        assert(heat.first && std::abs(std::stod(heat.second) - 7.3037249283667665) < 1e-9);

        // A Number actuator: set to a value by its value command; on and off,
        // with no commands of their own, set it to its on and off values.
        {
            std::mutex m;
            std::vector<std::string> lines;
            auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
                std::lock_guard lk(m);
                if (out && line.rfind("M3", 0) == 0) lines.push_back(line);
            });
            cell.setActuator("L", "128");
            assert(actuator.take() == std::make_pair(true, std::string("128")));
            cell.switchActuator("L", true);
            assert(actuator.take() == std::make_pair(true, std::string("on")));
            watch();
            std::lock_guard lk(m);
            assert(lines.size() == 2 && lines[0] == "M3 S128" && lines[1] == "M3 S255");
        }
        // A Profile actuator (OpenPnP's actuator profiles): set to a profile,
        // each of its actuators to its value there (one left empty, left);
        // switched on or off, its Default ON or Default OFF profile.
        {
            JPCellConfig next = cell.config();
            JPActuatorConfig pr;
            pr.id = "PR";
            pr.name = "Lights";
            pr.valueType = JPActuatorConfig::ValueType::Profile;
            pr.profileActuators[0] = "L";
            pr.profileActuators[1] = "V";
            JPActuatorConfig::Profile dim, bright;
            dim.name = "Dim";
            dim.defaultOff = true;
            dim.values[0] = "10";
            bright.name = "Bright";
            bright.defaultOn = true;
            bright.values[0] = "200";
            bright.values[1] = "false";
            pr.profiles = { dim, bright };
            next.actuators.push_back(pr);
            std::string why;
            assert(cell.reconfigure(next, why));
            std::mutex m;
            std::vector<std::string> lines;
            auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
                std::lock_guard lk(m);
                if (out && (line.rfind("M3", 0) == 0 || line.rfind("M6", 0) == 0 || line.rfind("M7", 0) == 0)) lines.push_back(line);
            });
            assert(cell.setActuatorAndWait("PR", "Dim", why));
            assert(cell.switchActuatorAndWait("PR", true, why));
            assert(!cell.setActuatorAndWait("PR", "Nowhere", why) && why.find("not found") != std::string::npos);
            watch();
            std::lock_guard lk(m);
            assert(lines.size() >= 2 && lines[0] == "M3 S10" && lines[1] == "M3 S200");
        }

        // Pick and place: the head's pump comes on with the first part, then
        // the vacuum; placing, the vacuum goes off, then the pump (PartOn).
        {
            std::mutex m;
            std::vector<std::string> seen;
            auto watch = cell.onActuator.connect([&](std::string id, bool ok, std::string v) {
                std::lock_guard lk(m);
                seen.push_back(id + (ok ? " " : " failed ") + v);
            });
            cell.pick("N");
            cell.place("N");
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            auto count = [&] { std::lock_guard lk(m); return seen.size(); };
            while (count() < 4 && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            std::lock_guard lk(m);
            assert((seen == std::vector<std::string>{ "P on", "V on", "V off", "P off" }));
            watch();
        }
        // OpenPnP's place blow-off: none without a level (the tip's, else the
        // part's package's); with one, the blow-off actuator switched with the place.
        {
            JPCellConfig next = cell.config();
            JPActuatorConfig b;
            b.id = "B";
            b.name = "Blow";
            b.driverId = "D";
            b.onCommand = "M64 P7";
            b.offCommand = "M65 P7";
            next.actuators.push_back(b);
            next.nozzles[0].blowOffActuatorId = "B";
            std::string why;
            assert(cell.reconfigure(next, why));
            std::mutex m;
            std::vector<std::string> lines;
            auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
                std::lock_guard lk(m);
                if (out && line == "M64 P7") lines.push_back(line);
            });
            std::atomic<int> placed { 0 };
            auto watchPlace = cell.onActuator.connect([&](std::string id, bool ok, std::string v) {
                if (id == "V" && ok && v == "off") ++placed;
            });
            for (const double level : { 0.0, 1.0 }) {
                cell.setNozzlePart("N", { "", 0, 0, level });
                assert(cell.pickAndWait("N", why));
                const int before = placed;
                cell.place("N");
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (placed == before && std::chrono::steady_clock::now() < until)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                assert(placed > before);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));   // the place's last switches
            }
            watchPlace();
            watch();
            std::lock_guard lk(m);
            assert(lines.size() == 1);
            next.nozzles[0].blowOffActuatorId.clear();
            assert(cell.reconfigure(next, why));
            cell.setNozzlePart("N", {});
        }
        // Simulation Mode's Pick & Place Checking: with an image camera on the
        // head, a pick and a place of a part are checked where the simulated
        // machine has the nozzle (here off by the homing error); one not
        // recognized fails and says so; OpenPnP's test object, and a place
        // near the discard location, are not checked.
        {
            JPCellConfig next = cell.config();
            JPCameraConfig cam;
            cam.id = "CAM";
            cam.name = "Top";
            cam.mount.headId = "H";
            cam.device = JJson::object();
            cam.device["backend"] = std::string("image");
            cam.device["source"] = std::string("table.png");
            next.cameras.push_back(cam);
            next.simulation.mode = JPSimulationConfig::Mode::StaticImperfectionsMachine;
            next.simulation.replaceDrivers = false;
            next.simulation.homingErrorX = 0.5;
            next.simulation.pickAndPlaceChecking = true;
            std::string why;
            assert(cell.reconfigure(next, why));
            std::mutex m;
            std::vector<JPCell::PnpCheck> checks;
            bool recognize = true;
            cell.setPnpChecker([&](const JPCell::PnpCheck& c, std::string& detail) {
                std::lock_guard lk(m);
                checks.push_back(c);
                detail = recognize ? "recognized" : "nothing there";
                return recognize;
            });
            cell.setNozzlePart("N", { "R1", 0, 0, 0 });
            assert(cell.pickAndWait("N", why));
            {
                std::lock_guard lk(m);
                assert(checks.size() == 1 && checks[0].pick && checks[0].partId == "R1" && checks[0].nozzleId == "N");
                assert(checks[0].camera["source"].str() == "table.png");
                assert(std::abs(checks[0].x - (cell.positions().at("X") - 0.5)) < 1e-9);
                recognize = false;
            }
            assert(!cell.placeAtAndWait("N", {}, 1.0, why) && why == "Nozzle Right part R1 place location not recognized.");
            cell.setNozzlePart("N", { "TEST-OBJECT", 0, 0, 0 });   // not checked
            assert(cell.placeAtAndWait("N", {}, 1.0, why));
            next.discardLocation = JPMachineLocation { cell.positions().at("X") - 0.5 + 1, 0, 0, 0 };   // within 4 mm
            assert(cell.reconfigure(next, why));
            cell.setNozzlePart("N", { "R1", 0, 0, 0 });
            assert(cell.pickAndWait("N", why));   // not recognized, but not checked there
            {
                std::lock_guard lk(m);
                assert(checks.size() == 2);   // the pick and the place before the test object
            }
            next.cameras.pop_back();
            next.simulation = {};
            next.discardLocation.reset();
            assert(cell.reconfigure(next, why));
            cell.setPnpChecker(nullptr);
            cell.setNozzlePart("N", {});
        }
        // Part detection, as OpenPnP's: checked at the steps the tip says,
        // with the nozzle's vacuum sense actuator (none: nothing checked). The
        // level read (-31000) must be in the tip's range: out of it, no part.
        {
            std::string why;
            assert(!cell.vacuumChecked("N", JPCell::VacuumStep::AfterPick));
            JPCellConfig next = cell.config();
            next.nozzles[0].vacuumSenseActuatorId = "V";
            assert(cell.reconfigure(next, why));
            assert(cell.vacuumChecked("N", JPCell::VacuumStep::AfterPick) && cell.vacuumChecked("N", JPCell::VacuumStep::Align)
                   && !cell.vacuumChecked("N", JPCell::VacuumStep::AfterPlace));   // no part-off method
            assert(cell.pickAndWait("N", why));
            bool on = false;
            assert(cell.partOnAndWait("N", on, why) && on);
            JPNozzleTipConfig::Sensing readOn, readOff;
            cell.vacuumReadings("T", readOn, readOff);
            assert(readOn.lastReading && *readOn.lastReading == -31000 && !readOn.lastDifference && !readOff.lastReading);
            next.nozzleTips[0].partOn.high = -31500;
            assert(cell.reconfigure(next, why));
            assert(cell.partOnAndWait("N", on, why) && !on);
            // Perform Checks? off after the pick: not checked there.
            next.nozzleTips[0].partOnCheckAfterPick = false;
            assert(cell.reconfigure(next, why));
            assert(!cell.vacuumChecked("N", JPCell::VacuumStep::AfterPick) && cell.vacuumChecked("N", JPCell::VacuumStep::BeforePlace));
            // Difference: from the level as the pick's dwell ended (graphed), to the level now.
            next.nozzleTips[0].partOn = {};
            next.nozzleTips[0].partOn.method = "Difference";
            next.nozzleTips[0].partOn.low = -32000;
            next.nozzleTips[0].partOn.high = -30000;
            next.nozzleTips[0].partOn.diffLow = -10;
            next.nozzleTips[0].partOn.diffHigh = 10;
            assert(cell.reconfigure(next, why));
            assert(cell.placeAtAndWait("N", { std::nullopt, std::nullopt, std::nullopt, std::nullopt }, 1.0, why));
            assert(cell.pickAndWait("N", why));
            assert(cell.partOnAndWait("N", on, why) && on);
            cell.vacuumReadings("T", readOn, readOff);
            assert(readOn.lastReading && *readOn.lastReading == -31000 && readOn.lastDifference && *readOn.lastDifference == 0);
            assert(!readOn.vacuumGraph.empty() && !readOn.valveGraph.empty());
            assert(cell.placeAtAndWait("N", { std::nullopt, std::nullopt, std::nullopt, std::nullopt }, 1.0, why));
            next.nozzles[0].vacuumSenseActuatorId.clear();
            next.nozzleTips[0].partOn = {};
            next.nozzleTips[0].partOn.method = "Absolute";
            next.nozzleTips[0].partOn.low = -32000;
            next.nozzleTips[0].partOn.high = -30000;
            next.nozzleTips[0].partOnCheckAfterPick = true;
            assert(cell.reconfigure(next, why));
        }

        // Motion: refused until homed, then a tool jogs along its own axes.
        Latch<std::pair<bool, std::string>> motion;
        cell.onMotion.connect([&](bool ok, std::string why) { motion.set({ ok, why }); });
        cell.jog("N", 1, 0, 0, 0, 0.5);
        const auto [unhomedOk, unhomedWhy] = motion.take();
        assert(!unhomedOk && unhomedWhy.find("not homed") != std::string::npos);

        cell.home();
        assert(motion.take().first && cell.isHomed());
        // OpenPnP's linear transform axes: a skewed X (X + 0.01 Y + 1) moved with
        // Y, and a pair turned 30 degrees moved together, solved back onto the
        // controller's X and Y; their coordinates read back from them.
        {
            JPCellConfig next = cell.config();
            JPAxisConfig y;
            y.id = "Y";
            y.name = "y";
            y.type = JPAxisConfig::Type::Y;
            y.driverId = "D";
            y.letter = "Y";
            y.feedratePerSecond = 100;
            next.axes.push_back(y);
            JPAxisConfig skew;
            skew.id = "XS";
            skew.name = "xs";
            skew.kind = JPAxisConfig::Kind::Linear;
            skew.type = JPAxisConfig::Type::X;
            skew.linearInputs = { "X", "Y", "", "" };
            skew.linearFactors = { 1, 0.01, 0, 0 };
            skew.linearOffset = 1;
            next.axes.push_back(skew);
            {
                std::string e;
                const auto back = JPAxisConfig::fromJson(skew.toJson(), e);
                assert(back && back->kind == JPAxisConfig::Kind::Linear && back->linearInputs[1] == "Y"
                       && back->linearFactors[1] == 0.01 && back->linearOffset == 1);
            }
            const double c30 = std::cos(M_PI / 6), s30 = std::sin(M_PI / 6);
            JPAxisConfig rx = skew, ry = skew;
            rx.id = "XR";
            rx.name = "xr";
            rx.linearFactors = { c30, -s30, 0, 0 };
            rx.linearOffset = 0;
            ry.id = "YR";
            ry.name = "yr";
            ry.type = JPAxisConfig::Type::Y;
            ry.linearFactors = { s30, c30, 0, 0 };
            ry.linearOffset = 0;
            next.axes.push_back(rx);
            next.axes.push_back(ry);
            std::string why;
            // Homed, waited for until it is (not by a motion event, which an earlier move may have left).
            auto homeNow = [&cell] {
                cell.home();
                for (int i = 0; i < 500 && (!cell.isHomed() || cell.isHoming() || cell.isMoving()); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                assert(cell.isHomed());
            };
            assert(next.problems().empty());
            assert(cell.reconfigure(next, why));
            homeNow();
            assert(cell.moveAxesAndWait({ { "XS", 50 }, { "Y", 20 } }, 1.0, why));
            auto settled = [&cell](const char* id, double want) {
                for (int i = 0; i < 200; ++i) {
                    if (std::abs(cell.positions().at(id) - want) < 1e-3) return true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                return false;
            };
            assert(settled("X", 48.8) && settled("XS", 50));
            assert(cell.moveAxesAndWait({ { "XR", 100 }, { "YR", 40 } }, 1.0, why));
            // X = cos 30 * 100 + sin 30 * 40, Y = -sin 30 * 100 + cos 30 * 40 (the inverse turn).
            assert(settled("X", c30 * 100 + s30 * 40) && settled("Y", -s30 * 100 + c30 * 40));
            assert(settled("XR", 100) && settled("YR", 40));
            next.axes.resize(next.axes.size() - 4);
            assert(cell.reconfigure(next, why));
            homeNow();
        }
        // OpenPnP's rotation mode offset: the nozzle sent to a rotation turns
        // its axis that much less, reads that much more, and has none again
        // once its part is gone.
        {
            JPCellConfig next = cell.config();
            next.nozzles[0].mount.axisRotation = "C";
            std::string why;
            const bool back = cell.reconfigure(next, why);
            if (!back) std::fprintf(stderr, "why: %s\n", why.c_str());
            assert(back);
            const JPMountConfig mount = cell.config().nozzles[0].mount;
            cell.setNozzlePart("N", { "R1", 0, 0, 0 });
            cell.setRotationModeOffset("N", 30.0);
            assert(cell.moveToolAndWait(mount, { std::nullopt, std::nullopt, std::nullopt, 40.0 }, 1.0, why));
            assert(std::abs(cell.jogBase().at("C") - 10) < 1e-6);
            assert(cell.rotationModeOffsetOf(mount) == 30);
            cell.setNozzlePart("N", {});
            assert(cell.moveToolAndWait(mount, { std::nullopt, std::nullopt, std::nullopt, 40.0 }, 1.0, why));
            assert(cell.rotationModeOffsetOf(mount) == 0 && std::abs(cell.jogBase().at("C") - 40) < 1e-6);
            next.nozzles[0].mount.axisRotation.clear();
            assert(cell.reconfigure(next, why));
        }
        // An axis interlock (OpenPnP's ActuatorInterlockMonitor): signalling
        // X moving, switched on before X moves and off after; another axis's move alone
        // leaves it (C here); a confirmation out of range stops the move.
        {
            JPCellConfig next = cell.config();
            JPActuatorConfig il;
            il.id = "IL";
            il.name = "Brake";
            il.driverId = "D";
            il.onCommand = "M64 P5";
            il.offCommand = "M65 P5";
            il.interlock.enabled = true;
            il.interlock.type = "SignalAxesMoving";
            il.interlock.axes[0] = "X";
            next.actuators.push_back(il);
            std::string why;
            assert(cell.reconfigure(next, why));
            std::mutex m;
            std::vector<std::string> lines;
            auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
                std::lock_guard lk(m);
                if (out && (line == "M64 P5" || line == "M65 P5" || line.rfind("G1", 0) == 0 || line.rfind("G0", 0) == 0))
                    lines.push_back(line.substr(0, 3));
            });
            const double x = cell.jogBase().at("X");
            assert(cell.moveAxesAndWait({ { "X", x - 1 } }, 1.0, why));
            motion.take();
            {
                std::lock_guard lk(m);
                assert(lines.size() >= 3 && lines.front() == "M64" && lines.back() == "M65");
                lines.clear();
            }
            const double c = cell.jogBase().at("C");
            assert(cell.moveAxesAndWait({ { "C", c + 10 } }, 1.0, why));
            motion.take();
            {
                std::lock_guard lk(m);
                for (const std::string& l : lines) assert(l != "M64" && l != "M65");
            }
            watch();
            // A confirmation before X moves: Vacuum reads -31000, out of 0..10: refused, X stays.
            next = cell.config();
            for (JPActuatorConfig& a : next.actuators)
                if (a.id == "V") {
                    a.interlock.enabled = true;
                    a.interlock.type = "ConfirmInRangeBeforeAxesMove";
                    a.interlock.axes[0] = "X";
                    a.interlock.goodMin = 0;
                    a.interlock.goodMax = 10;
                }
            assert(cell.reconfigure(next, why));
            const double x2 = cell.jogBase().at("X");
            assert(!cell.moveAxesAndWait({ { "X", x2 - 1 } }, 1.0, why) && why.find("below good range") != std::string::npos);
            motion.take();
            assert(std::abs(cell.jogBase().at("X") - x2) < 1e-6);
            for (JPActuatorConfig& a : next.actuators) a.interlock.enabled = false;
            assert(cell.reconfigure(next, why));
            // Back where the rest of the test has them.
            assert(cell.moveAxesAndWait({ { "X", x }, { "C", c } }, 1.0, why));
            motion.take();
        }
        // Send FeedRate On Change Only: the second move at the same speed goes without its F.
        {
            JPCellConfig next = cell.config();
            next.drivers[0].sendOnChangeFeed.on = true;
            std::string why;
            assert(cell.reconfigure(next, why));
            std::mutex m;
            std::vector<std::string> lines;
            auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
                std::lock_guard lk(m);
                if (out && line.rfind("G1", 0) == 0) lines.push_back(line);
            });
            // (C: X takes a backlash approach at its own speed.)
            const double c = cell.jogBase().at("C");
            assert(cell.moveAxesAndWait({ { "C", c + 10 } }, 1.0, why));
            motion.take();
            assert(cell.moveAxesAndWait({ { "C", c } }, 1.0, why));
            motion.take();
            watch();
            std::lock_guard lk(m);
            assert(lines.size() >= 2 && lines[lines.size() - 2].find('F') != std::string::npos
                   && lines.back().find('F') == std::string::npos);
            next.drivers[0].sendOnChangeFeed.on = false;
            assert(cell.reconfigure(next, why));
        }
        // There, to a hair (a backlash offset taken off leaves rounding dust).
        auto settle = [&](const char* axis, double want) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            auto at = [&] { return std::abs(cell.positions()[axis] - want) < 1e-9; };
            while (!at() && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return at();
        };
        assert(settle("X", 390.0));                        // G92 made the home coordinate true

        cell.jog("N", 5, 0, 0, 0, 0.5);
        assert(motion.take().first && settle("X", 395.0));
        cell.jog("N", 0, 0, 1.5, 0, 1.0);                  // the nozzle's Z is mapped: its input goes the other way
        assert(motion.take().first && settle("ZR", 1.5) && settle("Z", -1.5));
        // Whole steps: Z moves in quarters.
        cell.moveAxes({ { "Z", -1.6 } }, 1.0);
        assert(motion.take().first && settle("Z", -1.5));
        // A whole step never takes an axis past a soft limit: to the limit
        // itself, the step on this side of it (X 400 in steps of 0.3 is 399.9).
        {
            JPCellConfig next = cell.config();
            next.axes[0].resolution = 0.3;
            std::string why;
            assert(cell.reconfigure(next, why));
            cell.home();                                   // the axes changed: homed again
            assert(motion.take().first);
            cell.moveAxes({ { "X", 400.0 } }, 1.0);
            const auto [ok, whyNot] = motion.take();
            assert(ok && std::abs(cell.jogBase().at("X") - 399.9) < 1e-6);
            next.axes[0].resolution = 0;
            assert(cell.reconfigure(next, why));
            cell.home();
            assert(motion.take().first);
            cell.moveAxes({ { "X", 395.0 } }, 1.0);
            assert(motion.take().first && settle("X", 395.0));
        }
        // A rotation that wraps and is limited goes the short way, past 180,
        // and is then told where it is within -180..180.
        cell.moveAxes({ { "C", 170 } }, 1.0);
        assert(motion.take().first && settle("C", 170.0));
        cell.moveAxes({ { "C", -170 } }, 1.0);
        assert(motion.take().first && settle("C", -170.0));
        // Discard: up, across to the discard location, down, the part let go
        // (its check passes: nothing was set for part off), and up again.
        {
            JPCellConfig next = cell.config();
            next.discardLocation = JPMachineLocation{ 20, 0, -1, 0 };
            std::string why;
            assert(cell.reconfigure(next, why));
            cell.discard("N", 1.0);
            const auto [ok, whyNot] = motion.take();
            assert(ok && settle("X", 20.0));
            cell.moveAxes({ { "X", 395.0 } }, 1.0);   // where the tests after expect it
            assert(motion.take().first && settle("X", 395.0));
        }

        // The machine's speed scales every move: X's 100 mm/s (6000 a minute)
        // at half its own speed with the machine at half goes at F1500.
        {
            std::mutex m;
            std::string sent;
            auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
                std::lock_guard lk(m);
                if (out && line.rfind("G1 ", 0) == 0) sent = line;
            });
            cell.setSpeed(0.5);
            cell.moveAxes({ { "X", 394.0 } }, 0.5);   // travelling -X: no backlash approach
            assert(motion.take().first && settle("X", 394.0));
            cell.setSpeed(1.0);
            {
                std::lock_guard lk(m);
                assert(sent.find("F1500") != std::string::npos);
            }
            watch();
            cell.moveAxes({ { "X", 395.0 } }, 1.0);
            assert(motion.take().first && settle("X", 395.0));
        }

        // A step from where the axis was sent, not from where it reports: back
        // to 390 exactly, the limit itself, even when the report is 389.999.
        cell.sendLine("D", "G92 X395.001");                // the report now reads a hair high
        assert(settle("X", 395.001));
        cell.jog("N", -5, 0, 0, 0, 0.5);
        assert(motion.take().first && settle("X", 390.0));
        cell.jog("N", 5, 0, 0, 0, 0.5);
        assert(motion.take().first && settle("X", 395.0));

        // Backlash, one-sided optimized: X ends travelling -X (opposite to its
        // +0.1 offset). Arriving +X, it goes past by 0.1 first and comes back
        // slowly; arriving -X, it is one move.
        std::vector<std::string> sent;
        std::mutex sentMutex;
        auto unwatch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
            if (!out || line.rfind("G1", 0) != 0) return;
            std::lock_guard lk(sentMutex);
            sent.push_back(line);
        });
        cell.jog("N", -2, 0, 0, 0, 1.0);                  // 395 -> 393, the right way
        assert(motion.take().first && settle("X", 393.0));
        {
            std::lock_guard lk(sentMutex);
            assert(sent.size() == 1 && sent[0] == "G1 X393.0000 F6000");
            sent.clear();
        }
        cell.jog("N", 2, 0, 0, 0, 1.0);                   // 393 -> 395, the wrong way
        assert(motion.take().first && settle("X", 395.0));
        {
            std::lock_guard lk(sentMutex);
            assert(sent.size() == 2 && sent[0] == "G1 X395.1000 F6000" && sent[1] == "G1 X395.0000 F1500");
            sent.clear();
        }
        // One-sided: arriving -X too, by way of the place plus the offset, so
        // the last stretch is always the same.
        {
            JPCellConfig next = cell.config();
            next.axes[0].backlash = JPAxisConfig::Backlash::OneSided;
            std::string why;
            assert(cell.reconfigure(next, why));
            cell.jog("N", -2, 0, 0, 0, 1.0);              // 395 -> 393, the right way
            assert(motion.take().first && settle("X", 393.0));
            cell.jog("N", 2, 0, 0, 0, 1.0);               // and back
            assert(motion.take().first && settle("X", 395.0));
            std::lock_guard lk(sentMutex);
            assert(sent.size() == 4 && sent[0] == "G1 X393.1000 F6000" && sent[1] == "G1 X393.0000 F1500");
            sent.clear();
        }
        // Directional: travelling the way the +0.05 offset points, X goes 0.05
        // further; the other way, to the place itself. Its position is told
        // without the offset.
        {
            JPCellConfig next = cell.config();
            next.axes[0].backlash = JPAxisConfig::Backlash::Directional;
            next.axes[0].backlashOffset = 0.05;
            std::string why;
            assert(cell.reconfigure(next, why) && cell.isHomed());
            cell.jog("N", 2, 0, 0, 0, 1.0);               // 395 -> 397
            assert(motion.take().first && settle("X", 397.0));
            cell.jog("N", -2, 0, 0, 0, 1.0);              // 397 -> 395
            assert(motion.take().first && settle("X", 395.0));
            {
                std::lock_guard lk(sentMutex);
                assert(sent.size() == 2 && sent[0] == "G1 X397.0500 F6000" && sent[1] == "G1 X395.0000 F6000");
                sent.clear();
            }
            // Sneaking up: the last 0.5 mm at a quarter of the speed.
            next.axes[0].backlash = JPAxisConfig::Backlash::DirectionalSneakUp;
            next.axes[0].sneakUpMm = 0.5;
            assert(cell.reconfigure(next, why));
            cell.jog("N", 2, 0, 0, 0, 1.0);               // 395 -> 397
            assert(motion.take().first && settle("X", 397.0));
            {
                std::lock_guard lk(sentMutex);
                assert(sent.size() == 2 && sent[0] == "G1 X396.5500 F6000" && sent[1] == "G1 X397.0500 F1500");
                sent.clear();
            }
            // Back as the tests after expect it: one-sided, at 395.
            cell.jog("N", -2, 0, 0, 0, 1.0);
            assert(motion.take().first && settle("X", 395.0));
            next.axes[0].backlash = JPAxisConfig::Backlash::OneSidedOptimized;
            next.axes[0].backlashOffset = 0.1;
            assert(cell.reconfigure(next, why));
        }
        unwatch();

        cell.jog("N", 10, 0, 0, 0, 0.5);                   // 405 is past the soft limit
        const auto [limitOk, limitWhy] = motion.take();
        assert(!limitOk && limitWhy.find("soft limits") != std::string::npos && settle("X", 395.0));

        // New settings while it runs: nothing lets go. A nozzle renamed and a
        // timeout changed are taken as they are; the machine stays connected
        // and homed, and the controller's next line goes under its new name.
        {
            JPCellConfig next = cell.config();
            next.nozzles[0].name = "Left";
            next.drivers[0].name = "Main";
            next.drivers[0].commandTimeoutMs = 700;
            std::string why;
            assert(cell.reconfigure(next, why) && cell.isConnected() && cell.isHomed());
            assert(cell.config().nozzles[0].name == "Left");
            Latch<std::string> name;
            auto watch = cell.onTraffic.connect([&](std::string who, bool, std::string) { name.set(who); });
            cell.sendLine("D", "G4 P0");
            assert(name.take() == "Main");
            watch();

            // How it connects changed: the open link stays open, still homed;
            // the new settings are for the next connect.
            next = cell.config();
            next.drivers[0].link["simulator"]["replies"]["M1000 P2"] = "7";
            assert(cell.reconfigure(next, why) && cell.isConnected() && cell.isHomed());
            assert(cell.config().drivers[0].link.dump() == next.drivers[0].link.dump());

            // An axis's limits (or speed, or backlash) changed: its coordinates
            // mean what they did, so still homed.
            next = cell.config();
            next.axes[0].softLimitHigh = 380;
            assert(cell.reconfigure(next, why) && cell.isConnected() && cell.isHomed());
            // Where it is changed (its home coordinate): still connected, homed no longer.
            next.axes[0].homeCoordinate = 385;
            assert(cell.reconfigure(next, why) && cell.isConnected() && !cell.isHomed());
        }

        // Let go, the pump is switched off first, as its Disabled actuation says.
        std::atomic<bool> pumpOff{ false };
        auto watchPump = cell.onActuator.connect([&](std::string id, bool ok, std::string v) {
            if (id == "P" && ok && v == "off") pumpOff = true;
        });
        cell.disconnect();
        assert(!connection.take().first && !cell.isHomed() && pumpOff);
        watchPump();
    }
    {
        // A camera keeps a calibration for each picture size: a new one at a
        // size replaces the old, and they go through the cell file.
        JPCellConfig c = cellConfig();
        JPCameraConfig cam;
        cam.id = "C";
        cam.name = "top";
        c.cameras.push_back(cam);
        JPCell cell(c, profiles());
        auto calibrated = [](int width, int height, double rms) {
            JPCameraCalibration k;
            k.valid = true;
            k.width = width;
            k.height = height;
            k.rmsPx = rms;
            return k;
        };
        cell.setCameraCalibration("C", calibrated(1280, 720, 0.5));
        cell.setCameraCalibration("C", calibrated(640, 480, 0.3));
        cell.setCameraCalibration("C", calibrated(1280, 720, 0.2));
        assert(cell.cameraCalibrations("C").size() == 2);
        assert(cell.cameraCalibration("C", 1280, 720).rmsPx == 0.2);
        assert(cell.cameraCalibration("C", 640, 480).rmsPx == 0.3);
        assert(!cell.cameraCalibration("C", 800, 600).valid);
        const JPCameraConfig back = JPCameraConfig::fromJson(cell.config().cameras[0].toJson());
        assert(back.calibrations.size() == 2 && back.calibrationFor(640, 480) && back.calibrationFor(640, 480)->rmsPx == 0.3);
    }
    {
        // OpenPnP's Keep Alive: disconnected, the controller's connection is left
        // open, and connecting again takes it up as it is (not identified again).
        JPCellConfig c = cellConfig();
        c.drivers[0].keepAlive = true;
        JPCell cell(c, profiles());
        Latch<std::pair<bool, std::string>> connection;
        cell.onConnection.connect([&](bool ok, std::string why) { connection.set({ ok, why }); });
        std::mutex m;
        int identified = 0;
        auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
            std::lock_guard lk(m);
            if (out && line == "$I") ++identified;
        });
        cell.connect();
        assert(connection.take().first && cell.isConnected());
        int first = 0;
        {
            std::lock_guard lk(m);
            first = identified;
        }
        assert(first > 0);
        cell.disconnect();
        assert(!connection.take().first && !cell.isConnected());
        cell.connect();
        assert(connection.take().first && cell.isConnected());
        {
            std::lock_guard lk(m);
            assert(identified == first);   // taken up as it was
        }
        watch();
    }
    {
        // A controller on a link that does not exist: the cell says why.
        JPCellConfig c = cellConfig();
        c.drivers[0].link = JJson::object();
        c.drivers[0].link["type"] = "carrier-pigeon";
        JPCell cell(c, profiles());
        Latch<std::pair<bool, std::string>> connection;
        cell.onConnection.connect([&](bool ok, std::string why) { connection.set({ ok, why }); });
        cell.connect();
        const auto [ok, why] = connection.take();
        assert(!ok && why.find("carrier-pigeon") != std::string::npos && !cell.isConnected());
    }
    // A simulated controller added without its axes' letters (Machine Setup's NullDriver): given its axes'
    // letters, it identifies and moves.
    {
        JPCellConfig c = cellConfig();
        c.drivers[0].link = JJson::object();
        c.drivers[0].link["type"] = std::string("simulated");
        JPCell cell(c, profiles());
        Latch<std::pair<bool, std::string>> connection;
        cell.onConnection.connect([&](bool ok, std::string why) { connection.set({ ok, why }); });
        cell.connect();
        const auto [ok, why] = connection.take();
        assert(ok && cell.firmware().at("D") == "grblHAL");
        cell.sendLine("D", "G0 X7");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (cell.positions()["X"] != 7.0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(cell.positions().at("X") == 7.0);
        cell.disconnect();
    }
    // OpenPnP's Sync Initial Location and Allow Unhomed Motion: unhomed, a jog only with the first, any other
    // move only with both; without, each refused saying so.
    for (const int mode : { 0, 1, 2 }) {
        JPCellConfig c = cellConfig();
        c.drivers[0].syncInitialLocation = mode >= 1;
        c.drivers[0].allowUnhomedMotion = mode >= 2;
        JPCell cell(c, profiles());
        Latch<std::pair<bool, std::string>> connection, motion;
        cell.onConnection.connect([&](bool ok, std::string why) { connection.set({ ok, why }); });
        cell.onMotion.connect([&](bool ok, std::string why) { motion.set({ ok, why }); });
        cell.connect();
        assert(connection.take().first && !cell.isHomed());
        cell.jog("N", 1, 0, 0, 0, 1.0);
        const auto jogged = motion.take();
        assert(jogged.first == (mode >= 1));
        if (mode == 0) assert(jogged.second.find("Sync. Initial Location") != std::string::npos);
        cell.moveAxes({ { "X", 5.0 } }, 1.0);
        const auto moved = motion.take();
        assert(moved.first == (mode >= 2));
        if (mode == 1) assert(moved.second.find("Allow Unhomed Motion") != std::string::npos);
        cell.disconnect();
    }
    return 0;
}
