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
#include <chrono>
#include <condition_variable>
#include <mutex>
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
                         "replies": { "M1000 P1": "-31000" } } } } ],
      "heads": [ { "id": "H", "name": "Head", "pump": { "actuator": "P", "control": "PartOn", "onWaitMs": 0 } } ],
      "axes": [ { "id": "X",  "name": "x",  "kind": "controller", "type": "x", "driver": "D", "letter": "X",
                  "homeCoordinate": 390, "feedratePerSecond": 100,
                  "backlash": "oneSided", "backlashOffset": 0.1, "backlashSpeedFactor": 0.25,
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
                       "onCommand": "M64 P{index}", "offCommand": "M65 P{index}", "disabledActuation": "ActuateOff" } ]
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
        // Part detection: the vacuum read after the pick (-31000) must be in
        // the tip's range; out of it, the pick fails, and says so.
        {
            Latch<std::string> alarm;
            auto watch = cell.onAlarm.connect([&](std::string what) { alarm.set(what); });
            JPCellConfig next = cell.config();
            next.nozzleTips[0].partOn.high = -31500;
            std::string why;
            assert(cell.reconfigure(next, why));
            cell.pick("N");
            const std::string what = alarm.take();
            assert(what.find("pick failed") != std::string::npos && what.find("-31000.0") != std::string::npos);
            cell.place("N");
            watch();
        }

        // Motion: refused until homed, then a tool jogs along its own axes.
        Latch<std::pair<bool, std::string>> motion;
        cell.onMotion.connect([&](bool ok, std::string why) { motion.set({ ok, why }); });
        cell.jog("N", 1, 0, 0, 0, 0.5);
        const auto [unhomedOk, unhomedWhy] = motion.take();
        assert(!unhomedOk && unhomedWhy.find("not homed") != std::string::npos);

        cell.home();
        assert(motion.take().first && cell.isHomed());
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
            next.discardLocation = JPLocation{ 20, 0, -1, 0 };
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

        // Backlash, one-sided: X ends travelling -X (opposite to its +0.1
        // offset). Arriving +X, it goes past by 0.1 first and comes back slowly;
        // arriving -X, it is one move.
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
            next.axes[0].backlash = JPAxisConfig::Backlash::OneSided;
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
    return 0;
}
