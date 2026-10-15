// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The Jog panel's Position Nozzle and Position Camera with a part aligned on the nozzle (JPJogPanel): Position
// Nozzle puts the part's centre where the camera looks, and Position Camera after it (the part held, or gone since)
// brings the camera back there, the part's centre, not the nozzle's. The nozzle leans and its tip runs out, as on
// bench. Before, the camera came back off the place by the part's offset on the nozzle (0.31 mm on bench).
// Moved since, the nozzle's own centre again. With no part, Park Z on the camera after (its virtual Z up to safe Z,
// the nozzle's with it), the camera comes back where it was, the lean taken where the nozzle was down; with the camera's Z at
// safe Z, the nozzle stays up.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPJogPanel.h"

#include <j/core/SceneGraph.h>

#include <chrono>
#include <cmath>
#include <thread>

using namespace jf;

namespace {

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Head",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z", "A" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 1000 },
                { "id": "Y", "name": "y", "kind": "controller", "type": "y", "driver": "D", "letter": "Y", "feedratePerSecond": 1000 },
                { "id": "Z", "name": "z", "kind": "controller", "type": "z", "driver": "D", "letter": "Z", "feedratePerSecond": 500,
                  "safeZone": { "low": 0, "high": 0, "lowEnabled": true, "highEnabled": false } },
                { "id": "C", "name": "c", "kind": "controller", "type": "rotation", "driver": "D", "letter": "A",
                  "feedratePerSecond": 3600 } ],
      "cameras": [ { "id": "CAM", "name": "Top", "looking": "down", "mount": { "head": "H", "axisX": "X", "axisY": "Y" },
                     "device": { "backend": "simulated" } } ],
      "nozzleTips": [ { "id": "T", "name": "Tip" } ],
      "nozzles": [ { "id": "N", "name": "N1", "tip": "T", "tips": [ "T" ],
                     "mount": { "head": "H", "axisX": "X", "axisY": "Y", "axisZ": "Z", "axisRotation": "C",
                                "offset": { "x": -20, "y": -60, "z": 0 } } } ]
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

bool near(double a, double b) { return std::abs(a - b) < 1e-4; }

} // namespace

int main() {
    JPCellConfig config = cellConfig();
    JPNozzleTipConfig& tip = config.nozzleTips[0];
    tip.runoutCalibration.enabled = true;
    JPRunout r;
    r.algorithm = "ModelNoOffset";
    r.radius = 0.05;
    r.phaseDeg = 30;
    tip.runout["N"] = r;
    // The camera's Z a virtual axis, as OpenPnP's: at the board's height (-24), and taken up with the head to safe Z.
    JPAxisConfig zc;
    zc.id = "ZC";
    zc.name = "zc";
    zc.kind = JPAxisConfig::Kind::Virtual;
    zc.type = JPAxisConfig::Type::Z;
    config.axes.push_back(zc);
    config.cameras[0].mount.axisZ = "ZC";
    JPMountConfig& m = config.nozzles[0].mount;
    m.leanX = -0.04;
    m.leanY = 0.017;
    m.leanZ = -24;
    JPCell cell(config, profiles());
    cell.connect();
    for (int i = 0; i < 300 && !cell.isConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cell.home();
    for (int i = 0; i < 300 && !cell.isHomed(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isHomed());

    JSceneGraph graph;
    JPJogPanel::Choices choices;
    choices.tool = "N";
    JPJogPanel jog(graph, cell, choices);
    // The part sits 0.05, -0.31 mm off the nozzle (as bottom vision found it on bench), until it goes.
    bool aligned = true;
    jog.partCentredAt = [&](const std::string&, double x, double y) -> std::optional<std::array<double, 2>> {
        if (!aligned) return std::nullopt;
        return std::array<double, 2> { x - 0.05, y + 0.31 };
    };
    auto settle = [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        for (int i = 0; i < 500 && cell.isMoving(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    };
    const JPMountConfig& camera = cell.config().cameras[0].mount;
    const JPMountConfig& nozzle = cell.config().nozzles[0].mount;
    std::string why;
    assert(cell.moveToolAndWait(camera, { 100.0, 80.0, std::nullopt, std::nullopt }, 1.0, why));
    assert(cell.moveAxesAndWait({ { "C", 37.0 } }, 1.0, why));
    auto boardHeight = [&] { assert(cell.moveAxesAndWait({ { "ZC", -24.0 } }, 1.0, why)); };
    auto cameraAt = [&] {
        const auto p = cell.jogBase();
        return std::pair { p.at("X"), p.at("Y") };
    };
    const auto start = cameraAt();

    // Held: there and back, the camera where it was.
    boardHeight();
    assert(jog.act("positionNozzle"));
    settle();
    const auto [nx, ny] = cell.offsetsNow(nozzle, -24.0);
    const auto axes = cell.jogBase();
    assert(near(axes.at("X") + nx, 99.95) && near(axes.at("Y") + ny, 80.31));   // the nozzle off by the part's offset
    assert(jog.act("positionCamera"));
    settle();
    auto back = cameraAt();
    assert(near(back.first, start.first) && near(back.second, start.second));

    // Put down there (the part gone from the nozzle): the camera back on it all the same.
    boardHeight();
    assert(jog.act("positionNozzle"));
    settle();
    aligned = false;
    assert(jog.act("positionCamera"));
    settle();
    back = cameraAt();
    assert(near(back.first, start.first) && near(back.second, start.second));

    // The nozzle moved since: the camera to the nozzle's own centre.
    aligned = true;
    boardHeight();
    assert(jog.act("positionNozzle"));
    settle();
    aligned = false;
    assert(cell.moveAxesAndWait({ { "X", cell.jogBase().at("X") + 1.0 } }, 1.0, why));
    assert(jog.act("positionCamera"));
    settle();
    back = cameraAt();
    assert(near(back.first, start.first + 1.0 - 0.05) && near(back.second, start.second + 0.31));

    // No part, Park Z with the camera chosen after (the camera's Z up to safe Z, the nozzle's too): the camera
    // back where it was, the lean taken where the nozzle was down, not at safe Z (0.97 mm off on bench).
    assert(cell.moveToolAndWait(camera, { 100.0, 80.0, std::nullopt, std::nullopt }, 1.0, why));
    const auto here = cameraAt();
    boardHeight();
    assert(jog.act("positionNozzle"));
    settle();
    assert(near(cell.jogBase().at("Z"), -24));
    jog.selectTool("CAM");
    assert(jog.act("parkZ"));
    settle();
    jog.selectTool("N");
    assert(near(cell.jogBase().at("ZC"), 0) && near(cell.jogBase().at("Z"), 0));
    assert(jog.act("positionCamera"));
    settle();
    back = cameraAt();
    assert(near(back.first, here.first) && near(back.second, here.second));

    // The camera's Z at safe Z: the nozzle stays up there.
    assert(jog.act("positionNozzle"));
    settle();
    assert(near(cell.jogBase().at("Z"), 0));
    cell.disconnect();
    return 0;
}
