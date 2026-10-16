// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A drive's periodic error (JPPeriodicErrorConfig, an eccentric pulley): a head sent to a place is sent the axis
// coordinates that put it there (Y less its wave there, X less the twist the wave makes across), a Y move alone
// moving X too; read back from the controller's report, it is where it was sent; an X move alone leaves Y as it was;
// none set, the axes go where they are sent. The cell's file keeps it, the last scan too.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace jf;

namespace {

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Wave",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 1000 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y", "feedratePerSecond": 1000 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 500 } ],
      "cameras": [ { "id": "C", "name": "Top", "looking": "down", "mount": { "head": "H", "axisX": "X", "axisY": "Y" },
                     "device": { "backend": "simulated" } } ],
      "periodicError": { "axis": "Y", "crossAxis": "X", "period": 45, "along": { "sin": 0.03, "cos": -0.01 },
                         "across": { "sin": 0.0, "cos": 0.02 } }
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

bool near(double a, double b, double within = 2e-4) { return std::abs(a - b) < within; }

// The X and Y of a move's G-code line ("G1 X.. Y.. F.."): what is in it.
void axesOf(const std::string& line, std::optional<double>& x, std::optional<double>& y) {
    x.reset();
    y.reset();
    if (const auto i = line.find(" X"); i != std::string::npos) x = std::stod(line.substr(i + 2));
    if (const auto i = line.find(" Y"); i != std::string::npos) y = std::stod(line.substr(i + 2));
}

} // namespace

int main() {
    const JPCellConfig config = cellConfig();
    const JPPeriodicErrorConfig& w = config.periodicError;
    assert(w.active() && near(w.alongSize(), std::hypot(0.03, 0.01), 1e-12));
    // The axis coordinate that puts the head at a place: there, less its wave there.
    for (double place : { 0.0, 12.3, 80.0, 151.7 }) {
        const double a = w.axisFor(place);
        assert(std::abs(a + w.along(a) - place) < 1e-9);
    }
    // The file keeps it, and the last scan.
    JPPeriodicErrorConfig kept = w;
    JPPeriodicErrorConfig::Scan scan;
    scan.when = "2026-10-16 14:00:00";
    scan.board = "Brd1";
    scan.periodMm = 45;
    scan.alongSin = 0.001;
    scan.rmsBefore = 0.03;
    scan.rmsAfter = 0.008;
    scan.points.push_back({ 80, 0.01, -0.02, 0.011, -0.019, false });
    scan.points.push_back({ 85, 0.2, 0.0, 0.01, 0.0, true });
    kept.lastScan = scan;
    const JPPeriodicErrorConfig back = JPPeriodicErrorConfig::fromJson(kept.toJson());
    assert(back.axis == "Y" && back.crossAxis == "X" && back.periodMm == 45 && back.alongSin == 0.03 && back.acrossCos == 0.02);
    assert(back.lastScan && back.lastScan->board == "Brd1" && back.lastScan->points.size() == 2);
    assert(back.lastScan->points[1].outlier && back.lastScan->points[0].acrossFit == -0.019 && back.lastScan->rmsAfter == 0.008);
    JPCellConfig none = config;
    none.periodicError = {};
    const JJson noneJson = none.toJson();
    assert(noneJson["periodicError"].isNull());   // nothing written

    JPCell cell(config, profiles());
    cell.connect();
    for (int i = 0; i < 300 && !cell.isConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cell.home();
    for (int i = 0; i < 300 && !cell.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isHomed());
    std::mutex m;
    std::vector<std::string> sent;
    auto watch = cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        if (!out || line.rfind("G1 ", 0) != 0) return;
        std::lock_guard lk(m);
        sent.push_back(line);
    });
    auto last = [&] {
        std::lock_guard lk(m);
        assert(!sent.empty());
        return sent.back();
    };
    auto settled = [&](double x, double y) {
        for (int i = 0; i < 300; ++i) {
            const auto p = cell.positions();
            // The controller reports to a thousandth.
            if (near(p.at("X"), x, 1e-3) && near(p.at("Y"), y, 1e-3)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    std::string why;
    const JPMountConfig& camera = cell.config().cameras[0].mount;
    // To (100, 80): Y sent less its wave there, X less the twist; read back at (100, 80).
    assert(cell.moveToolAndWait(camera, { 100.0, 80.0, std::nullopt, std::nullopt }, 1.0, why));
    std::optional<double> x, y;
    axesOf(last(), x, y);
    const double ay = w.axisFor(80);
    assert(x && y && near(*y, ay) && near(*x, 100 - w.across(ay)));
    assert(!near(*y, 80, 0.005));   // the wave is there
    assert(settled(100, 80));
    // Y alone: X moves too (the twist is different there).
    assert(cell.moveAxesAndWait({ { "Y", 97.0 } }, 1.0, why));
    axesOf(last(), x, y);
    const double by = w.axisFor(97);
    assert(x && y && near(*y, by) && near(*x, 100 - w.across(by)));
    assert(settled(100, 97));
    // X alone: Y stays as it was sent.
    assert(cell.moveAxesAndWait({ { "X", 120.0 } }, 1.0, why));
    axesOf(last(), x, y);
    assert(x && !y && near(*x, 120 - w.across(by)));
    assert(settled(120, 97));
    watch();
    cell.disconnect();

    // None set: sent as asked.
    JPCell plain(none, profiles());
    plain.connect();
    for (int i = 0; i < 300 && !plain.isConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    plain.home();
    for (int i = 0; i < 300 && !plain.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    sent.clear();
    auto watch2 = plain.onTraffic.connect([&](std::string, bool out, std::string line) {
        if (!out || line.rfind("G1 ", 0) != 0) return;
        std::lock_guard lk(m);
        sent.push_back(line);
    });
    assert(plain.moveToolAndWait(plain.config().cameras[0].mount, { 100.0, 80.0, std::nullopt, std::nullopt }, 1.0, why));
    axesOf(last(), x, y);
    assert(x && y && near(*x, 100) && near(*y, 80));
    watch2();
    plain.disconnect();
    return 0;
}
