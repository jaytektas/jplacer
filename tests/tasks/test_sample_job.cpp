// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's SampleJobTest and SamplePanelizedJobTest: its imperfect simulated machine (SampleJobTest's machine.xml:
// Simulation Mode's dynamic imperfections, the machine off from home by 1, 2 mm, non-square by 0.02, a 0.4 mm nozzle
// runout, camera lag, noise and vibration, Pick & Place Checking against the table's picture) with OpenPnP's own
// parts and packages, homed (visually, on the homing fiducial), and OpenPnP's sample pnp-test job (one board, or
// the panelized one) run on it to its end by jplacer's job processor on the cell, through its simulated cameras:
// fiducials, feeders, bottom vision and runout calibration all looked at, each pick and place checked where the
// simulated machine has the nozzle. It must finish without a failure.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "CellJobHost.h"
#include "machine/JPCell.h"
#include "machine/JPFirmwareProfile.h"
#include "model/JPConfiguration.h"
#include "model/JPJob.h"
#include "openpnp/JPOpenPnpMachineImporter.h"
#include "setup/JPVisionPipelines.h"
#include "tasks/JPCellJobMachine.h"
#include "tasks/JPJobProcessor.h"
#include "tasks/JPVisualHoming.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace jf;
namespace fs = std::filesystem;

namespace {

const fs::path kMachine = fs::path(JPLACER_TESTDATA_DIR) / "openpnp/sample-job/machine.xml";
const fs::path kDefaults = fs::path(JPLACER_TESTDATA_DIR) / "../../openpnp-defaults";

std::vector<JPFirmwareProfile> profiles() {
    std::vector<JPFirmwareProfile> all;
    for (const auto& e : fs::directory_iterator(JPLACER_PROFILES_DIR)) {
        JPFirmwareProfile p;
        std::string error;
        if (p.load(e.path().string(), error)) all.push_back(p);
    }
    // Highest priority first, as the app has them.
    std::stable_sort(all.begin(), all.end(), [](const JPFirmwareProfile& a, const JPFirmwareProfile& b) { return a.priority() > b.priority(); });
    return all;
}

// Waits for `done` (true or false set); false when it did not come within `ms`.
bool waitFor(const std::atomic<int>& done, int ms) {
    for (int i = 0; i < ms / 10 && done == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return done != 0;
}

void runSampleJob(const char* name, const char* jobFile) {
    std::fprintf(stderr, "%s runs with imperfect machine in real-time, please be patient...\n", name);
    const fs::path dir = fs::temp_directory_path() / ("jplacer-sample-job-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    // As OpenPnP starts a configuration with only its machine.xml (as the app loads one): its own parts, packages
    // and vision settings for what is not there, the stock vision settings with their stock pipelines.
    JPConfiguration config(dir.string());
    config.setDefaults((kDefaults / "config").string());
    std::vector<std::string> problems, notes;
    std::string error;
    bool ok = config.load(problems, error);
    if (ok) JPVisionPipelines::ensureStock(config);
    ok = ok && config.importFeeders(kMachine.string(), error) >= 0;
    JPCellConfig cellConfig;
    ok = ok && JPOpenPnpMachineImporter::import(kMachine.string(), cellConfig, notes, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    assert(ok);
    for (const std::string& n : notes) std::fprintf(stderr, "note: %s\n", n.c_str());

    JPCell cell(cellConfig, profiles());
    std::atomic<int> connected { 0 };
    cell.onConnection.connect([&](bool c, std::string) { connected = c ? 1 : -1; });
    cell.connect();
    assert(waitFor(connected, 10000) && connected == 1);
    CellJobHost host(cell, config);
    host.startCameras();

    // Homed: the controllers, then the head's visual homing on its homing fiducial.
    std::atomic<int> homed { 0 };
    std::string why;
    {
        auto done = cell.onMotion.connect([&](bool ok, std::string w) {
            why = w;
            homed = ok ? 1 : -1;
        });
        cell.home();
        waitFor(homed, 60000);
    }
    if (homed != 1) std::fprintf(stderr, "not homed: %s\n", why.c_str());
    assert(homed == 1);
    for (const JPHeadConfig& h : cell.config().heads)
        if (h.visualHoming) {
            JPCameraFeed* feed = host.headCameraFeed();
            assert(feed);
            const auto look = JPVisualHoming::homeLook(config, cell.config().vision);
            const JPVisualHoming::Result r = JPVisualHoming::run(cell, *feed, h, 1.0, look ? &*look : nullptr);
            if (!r.ok) std::fprintf(stderr, "visual homing: %s\n", r.why.c_str());
            assert(r.ok);
            std::fprintf(stderr, "visual homing corrected %+.3f, %+.3f mm\n", r.correctedX, r.correctedY);
        }

    // OpenPnP's sample job, a copy of it (loading keeps the old form beside it).
    fs::copy(kDefaults / "samples/pnp-test", dir / "pnp-test", fs::copy_options::recursive);
    std::unique_ptr<JPJob> job = config.loadJob((dir / "pnp-test" / jobFile).string(), error);
    if (!job) std::fprintf(stderr, "%s\n", error.c_str());
    assert(job);
    JPCellJobMachine machine(host, config, [](const std::function<void()>& fn) { fn(); }, [](const std::string&) { return true; },
                             [](const std::string&) {});
    JPJobProcessor::Hooks hooks;
    hooks.status = [](const std::string& text) { std::fprintf(stderr, "%s\n", text.c_str()); };
    std::atomic<int> placed { 0 };
    hooks.placed = [&] { ++placed; };
    // Material: a feed for each part taken, a place for each placed, each naming its feeder.
    int fedParts = 0, placedParts = 0;
    hooks.material = [&](bool isPlace, const std::string& feederId, const JPJobProcessor::JobPlacement&) {
        assert(!feederId.empty());
        ++(isPlace ? placedParts : fedParts);
    };
    JPJobProcessor processor(config, *job, machine, cellConfig.jobProcessor, hooks);
    processor.setVision(cellConfig.vision);
    JPJobProcessor::Failure failure;
    JPJobProcessor::Result r = JPJobProcessor::Result::More;
    while ((r = processor.next(failure)) == JPJobProcessor::Result::More) {}
    if (r == JPJobProcessor::Result::Failed) std::fprintf(stderr, "%s: %s\n", name, failure.message.c_str());
    assert(r == JPJobProcessor::Result::Finished);
    // Every pick and place checked against the table's picture where the simulated machine had the nozzle.
    std::fprintf(stderr, "%d placed; %d picks and %d places checked\n", placed.load(), host.picksChecked(), host.placesChecked());
    assert(placed > 0 && host.picksChecked() == placed && host.placesChecked() == placed);
    assert(placedParts == placed && fedParts >= placed);
    cell.disconnect();
    fs::remove_all(dir);
}

} // namespace

int main(int argc, char** argv) {
    // One by name (OpenPnP's test class), else both.
    const std::string only = argc > 1 ? argv[1] : "";
    if (only.empty() || only == "SampleJobTest") runSampleJob("SampleJobTest", "pnp-test.job.xml");
    if (only.empty() || only == "SamplePanelizedJobTest") runSampleJob("SamplePanelizedJobTest", "pnp-test-panelized.job.xml");
    return 0;
}
