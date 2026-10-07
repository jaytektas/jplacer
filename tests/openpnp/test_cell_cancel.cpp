// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// JPCell::setCancelled, OpenPnP's Cancel for a camera's calibration: once set, a move waited for fails at once
// with "cancelled" (nothing sent), while up to safe Z still goes, as a cancelled procedure does last; cleared,
// moves go again. OpenPnP's basic test machine, its simulated controller.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"
#include "machine/JPFirmwareProfile.h"
#include "openpnp/JPOpenPnpMachineImporter.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    JPCellConfig config;
    std::vector<std::string> notes;
    std::string error;
    const bool ok = JPOpenPnpMachineImporter::import((fs::path(JPLACER_TESTDATA_DIR) / "openpnp/basic-job/machine.xml").string(),
                                                     config, notes, error);
    assert(ok);
    std::vector<JPFirmwareProfile> profiles;
    for (const auto& e : fs::directory_iterator(JPLACER_PROFILES_DIR)) {
        JPFirmwareProfile p;
        std::string why;
        if (p.load(e.path().string(), why)) profiles.push_back(p);
    }
    JPCell cell(config, profiles);
    std::mutex m;
    std::vector<std::string> sent;
    cell.onTraffic.connect([&](std::string, bool out, std::string line) {
        if (!out) return;
        std::lock_guard lk(m);
        sent.push_back(line);
    });
    std::atomic<int> connected { 0 };
    cell.onConnection.connect([&](bool c, std::string) { connected = c ? 1 : -1; });
    cell.connect();
    for (int i = 0; i < 500 && connected == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isConnected());
    std::atomic<int> homed { 0 };
    {
        auto c = cell.onMotion.connect([&](bool ok, std::string) { homed = ok ? 1 : -1; });
        cell.home();
        for (int i = 0; i < 500 && homed == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(homed == 1);

    const JPAxisConfig* x = nullptr;
    for (const JPAxisConfig& a : config.axes)
        if (!x && a.kind == JPAxisConfig::Kind::Controller && a.letter == "X") x = &a;
    assert(x);
    std::string why;
    assert(cell.moveAxesAndWait({ { x->id, 5 } }, 1.0, why));

    // Cancelled: the next move is refused, and nothing is sent for it.
    cell.setCancelled(true);
    assert(cell.isCancelled());
    size_t before;
    {
        std::lock_guard lk(m);
        before = sent.size();
    }
    assert(!cell.moveAxesAndWait({ { x->id, 10 } }, 1.0, why) && why == "cancelled");
    {
        std::lock_guard lk(m);
        assert(sent.size() == before);
    }
    // Up to safe Z still goes.
    assert(cell.safeZAndWait(config.heads.front().id, 1.0, why));

    // Cleared: moves go again.
    cell.setCancelled(false);
    assert(cell.moveAxesAndWait({ { x->id, 10 } }, 1.0, why));
    cell.disconnectAndWait();
    return 0;
}
