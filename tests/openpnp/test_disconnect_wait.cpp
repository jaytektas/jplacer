// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// JPCell::disconnectAndWait: the controllers let go of by the time it returns (as an update needs before it
// starts the new version, which connects to them), where disconnect() only asks the cell's thread to.
// OpenPnP's basic test machine, its simulated controller.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"
#include "machine/JPFirmwareProfile.h"
#include "openpnp/JPOpenPnpMachineImporter.h"

#include <atomic>
#include <chrono>
#include <filesystem>
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
    std::atomic<int> connected { 0 };
    cell.onConnection.connect([&](bool c, std::string) { connected = c ? 1 : -1; });
    cell.connect();
    for (int i = 0; i < 500 && connected == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cell.isConnected());
    cell.disconnectAndWait();
    assert(!cell.isConnected() && connected == -1);   // let go of, and said so, as it returns
    cell.disconnectAndWait();                          // again: nothing to let go of, and back at once
    assert(!cell.isConnected());
    return 0;
}
