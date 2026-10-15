// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A measurement recorded (JPCell::record): in the running cell's setup, and the setup written to the cell file, at
// once; so a later save from the cell's setup (a tip change's) keeps it. None written without a path.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace jf;

namespace {

JPCellConfig cellConfig() {
    const std::string json = R"({
      "name": "Record",
      "drivers": [ { "id": "D", "name": "Gantry", "statusIntervalMs": 10, "commandTimeoutMs": 500, "connectWaitMs": 0,
                     "link": { "type": "simulated", "simulator": {
                         "identity": [ "[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]" ], "axisLetters": [ "X", "Y", "Z" ] } } } ],
      "heads": [ { "id": "H", "name": "Head" } ],
      "axes": [ { "id": "X", "name": "x", "kind": "controller", "type": "x", "driver": "D", "letter": "X", "feedratePerSecond": 100 } ],
      "nozzles": [ { "id": "N", "name": "N1", "mount": { "head": "H", "axisX": "X" } } ]
    })";
    JPCellConfig c;
    std::string error;
    const bool ok = c.fromJson(JJson::parse(json), error);
    assert(ok);
    return c;
}

} // namespace

int main() {
    const std::string path = (std::filesystem::temp_directory_path() / "jplacer-test-record.json").string();
    std::filesystem::remove(path);
    JPCell cell(cellConfig(), {});
    JPPlacementCheck check;
    check.when = "2026-10-15 18:30";
    check.places.push_back({ 0, 0.01, 0.02, -0.03, 0.04, 0, 0 });
    cell.record([check](JPCellConfig& c) { c.nozzles[0].placementCheck = check; }, path);
    for (int i = 0; i < 300 && !std::filesystem::exists(path); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // In the running cell's setup, and in the file.
    assert(cell.config().nozzles[0].placementCheck && cell.config().nozzles[0].placementCheck->when == "2026-10-15 18:30");
    JPCellConfig back;
    std::string error;
    assert(back.load(path, error) && back.nozzles[0].placementCheck && back.nozzles[0].placementCheck->places.size() == 1);
    // A save from the cell's setup keeps it.
    assert(cell.config().save(path, error));
    JPCellConfig again;
    assert(again.load(path, error) && again.nozzles[0].placementCheck);
    std::filesystem::remove(path);
    // No path: in the cell's setup, nothing written.
    cell.record([](JPCellConfig& c) { c.nozzles[0].placementCheck.reset(); }, "");
    for (int i = 0; i < 300 && cell.config().nozzles[0].placementCheck; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!cell.config().nozzles[0].placementCheck && !std::filesystem::exists(path));
    return 0;
}
