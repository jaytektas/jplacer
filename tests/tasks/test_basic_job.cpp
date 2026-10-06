// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's BasicJobTest: its basic test machine (a head of two nozzles, offset 10 and 20 mm, two tips with changer
// locations, a tube feeder at the origin brought down to Z -10), imported as OpenPnP migrates it (its old single
// driver, OpenPnP's test driver, as the NullDriver whose migration makes the same axes), and a job of two 0805
// resistors run on it by jplacer's job processor and job machine on the cell itself: every move the controller is
// sent, and every switching, as OpenPnP's test expects them, in order. Each tip loaded, each part picked from the
// feeder at safe Z, down, vacuum, up, each placed (as high as the part, turned to the placement), then parked.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPCell.h"
#include "machine/JPFirmwareProfile.h"
#include "model/JPBoard.h"
#include "model/JPBoardLocation.h"
#include "model/JPConfiguration.h"
#include "model/JPJob.h"
#include "openpnp/JPOpenPnpMachineImporter.h"
#include "tasks/JPCellJobMachine.h"
#include "tasks/JPJobProcessor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace jf;
namespace fs = std::filesystem;

namespace {

const fs::path kConfig = fs::path(JPLACER_TESTDATA_DIR) / "openpnp/basic-job";

std::vector<JPFirmwareProfile> profiles() {
    std::vector<JPFirmwareProfile> all;
    for (const auto& e : fs::directory_iterator(JPLACER_PROFILES_DIR)) {
        JPFirmwareProfile p;
        std::string error;
        if (p.load(e.path().string(), error)) all.push_back(p);
    }
    // Highest priority first, as the app has them (a grblHAL answers as a Grbl 1.1 too).
    std::stable_sort(all.begin(), all.end(), [](const JPFirmwareProfile& a, const JPFirmwareProfile& b) { return a.priority() > b.priority(); });
    return all;
}

// What the job machine takes from where it runs: the cell, no cameras to look through, the tips it puts on.
class Host : public JPJobMachineHost {
public:
    explicit Host(JPCell& cell) : m_cell(cell) {}
    JPCell* cell() const override { return &m_cell; }
    std::optional<JPLocation> cameraLocation() const override { return std::nullopt; }
    JPCameraFeed* headCameraFeed() const override { return nullptr; }
    JPCameraFeed* upCameraFeed() const override { return nullptr; }
    JPCameraFeed* cameraFeed(const std::string&) const override { return nullptr; }
    void showPicture(const JPCameraFeed*, const JPFrame&, const std::string&, int) override {}
    void showCamera(const std::string&) override {}
    std::string nozzlePart(const std::string& nozzleId) const override {
        const auto p = m_parts.find(nozzleId);
        return p == m_parts.end() ? std::string() : p->second;
    }
    void setNozzlePart(const std::string& nozzleId, const std::string& partId) override { m_parts[nozzleId] = partId; }
    std::string chosenNozzleId() const override { return {}; }
    std::string tipChangeRefusal(const std::string&, const std::string&) const override { return {}; }
    void setTipOn(const std::string& nozzleId, const std::string& tipId) override {
        JPCellConfig c = m_cell.config();
        for (JPNozzleConfig& n : c.nozzles)
            if (n.id == nozzleId) n.tipId = tipId;
        std::string why;
        const bool ok = m_cell.reconfigure(c, why);
        assert(ok);
    }
    std::string cellPath() const override { return {}; }
    void slotScored(const std::string&, double) override {}
    std::optional<JPRunout> measureRunout(JPCell&, JPCameraFeed&, const JPNozzleConfig&, const JPNozzleTipConfig&, std::string& words,
                                          std::optional<JPBackgroundCalibration::Result>&) override {
        words = "no camera looking up";
        return std::nullopt;
    }
    void keepRunout(const std::string&, const std::string&, const std::optional<JPRunout>&) override {}
    void keepBackground(const std::string&, const JPBackgroundCalibration::Result&) override {}

private:
    JPCell&                            m_cell;
    std::map<std::string, std::string> m_parts;
};

// One of OpenPnP's expected operations: a move of a tool to a location (in the head's coordinates, as OpenPnP's
// test gives them: X, Y, Z, rotation), or an actuation.
struct Expected {
    std::string                description;
    std::string                tool;   // the nozzle or camera moved; empty: an actuation
    std::array<double, 4>      at {};
};

// The controller axes (by letter) of a tool, as X, Y, Z, rotation; a virtual one (a camera's Z) none.
std::array<std::string, 4> lettersOf(const JPCellConfig& c, const JPMountConfig& m) {
    std::array<std::string, 4> out;
    const std::array<const std::string*, 4> ids { &m.axisX, &m.axisY, &m.axisZ, &m.axisRotation };
    for (size_t i = 0; i < 4; ++i)
        if (const JPAxisConfig* a = c.axis(*ids[i]); a && a->kind == JPAxisConfig::Kind::Controller) out[i] = a->letter;
    return out;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("jplacer-basic-job-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const char* f : { "packages.xml", "parts.xml" }) fs::copy_file(kConfig / f, dir / f);
    JPConfiguration config(dir.string());
    std::vector<std::string> problems, notes;
    std::string error;
    bool ok = config.load(problems, error) && config.importFeeders((kConfig / "machine.xml").string(), error) >= 0;
    JPCellConfig cellConfig;
    ok = ok && JPOpenPnpMachineImporter::import((kConfig / "machine.xml").string(), cellConfig, notes, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    assert(ok);
    // The feeder brought down to Z -10.
    JPFeeder& feeder = *config.feeder("F1");
    feeder.setLocation(feeder.location().derive(std::nullopt, std::nullopt, -10.0, std::nullopt));

    // The vacuum switched by outputs the controller is sent (OpenPnP's test driver records each actuation).
    for (JPActuatorConfig& a : cellConfig.actuators)
        if (a.name == "N1VAC" || a.name == "N2VAC") {
            a.onCommand = "M64 P" + a.name.substr(1, 1);
            a.offCommand = "M65 P" + a.name.substr(1, 1);
        }

    JPCell cell(cellConfig, profiles());
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
    assert(connected == 1);
    // Homed: what the homing says when it is done.
    std::atomic<int> homed { 0 };
    std::string homingWhy;
    {
        auto done = cell.onMotion.connect([&](bool ok, std::string w) {
            homingWhy = w;
            homed = ok ? 1 : -1;
        });
        cell.home();
        for (int i = 0; i < 500 && homed == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (homed != 1) std::fprintf(stderr, "not homed: %s\n", homingWhy.c_str());
    assert(homed == 1);
    {
        std::lock_guard lk(m);
        sent.clear();
    }

    // OpenPnP's createSimpleJob: a board at 0, 0, -10 with R1 (10, 10, 45) and R2 (20, 20, 90).
    auto board = std::make_shared<JPBoard>();
    board->file = (dir / "test.board.xml").string();
    for (const auto& [id, x, y, r] : { std::tuple { "R1", 10.0, 10.0, 45.0 }, std::tuple { "R2", 20.0, 20.0, 90.0 } }) {
        JPPlacement p;
        p.id = id;
        p.partId = "R-0805-10K";
        p.location = JPLocation(JPLengthUnit::Millimeters, x, y, 0, r);
        board->placements.push_back(p);
    }
    JPJob job;
    auto l = std::make_unique<JPBoardLocation>();
    l->id = "test";
    l->holder = board;
    l->setLocation(JPLocation(JPLengthUnit::Millimeters, 0, 0, -10, 0));
    job.addBoardOrPanelLocation(std::move(l));

    Host host(cell);
    JPCellJobMachine machine(host, config, [](const std::function<void()>& fn) { fn(); }, [](const std::string&) { return true; },
                             [](const std::string&) {});
    JPJobProcessor::Hooks hooks;
    JPJobProcessor processor(config, job, machine, cellConfig.jobProcessor, hooks);
    processor.setVision(cellConfig.vision);
    JPJobProcessor::Failure failure;
    JPJobProcessor::Result r = JPJobProcessor::Result::More;
    while ((r = processor.next(failure)) == JPJobProcessor::Result::More) {}
    if (r == JPJobProcessor::Result::Failed) std::fprintf(stderr, "job: %s\n", failure.message.c_str());
    assert(r == JPJobProcessor::Result::Finished);

    // OpenPnP's expected operations, in order: each tip loaded, each part picked from F1 (at -10, -20 for the nozzles'
    // offsets) at safe Z, down, the vacuum, up; each placed (as high as the 0805, 0.8255 mm, on the board at -10),
    // turned to its placement; then parked by the camera.
    const double placeZ = 0.825500 - 10;
    const std::vector<Expected> expected {
        { "Move N1 Nozzle Change Load", "N1", { 40, 0, 0, 0 } },
        { "Move N2 Nozzle Change Load", "N2", { 50, 0, 0, 0 } },
        { "Move N1 to F1, Safe Z", "N1", { -10, 0, 0, 0 } },
        { "Move N1 to F1, Feeder Z", "N1", { -10, 0, -10, 0 } },
        { "Actuate", "", {} },
        { "Move N1 to F1, Safe Z", "N1", { -10, 0, 0, 0 } },
        { "Move N2 to F1, Safe Z", "N2", { -20, 0, 0, 0 } },
        { "Move N2 to F1, Feeder Z", "N2", { -20, 0, -10, 0 } },
        { "Actuate", "", {} },
        { "Move N2 to F1, Safe Z", "N2", { -20, 0, 0, 0 } },
        { "Move N1 to R1, Safe-Z", "N1", { 0, 10, 0, 45 } },
        { "Move N1 to R1, Z", "N1", { 0, 10, placeZ, 45 } },
        { "Actuate", "", {} },
        { "Move N1 to R1, Safe-Z", "N1", { 0, 10, 0, 45 } },
        { "Move N2 to R2, Safe-Z", "N2", { 0, 20, 0, 90 } },
        { "Move N2 to R2, Z", "N2", { 0, 20, placeZ, 90 } },
        { "Actuate", "", {} },
        { "Move N2 to R2, Safe-Z", "N2", { 0, 20, 0, 90 } },
        { "Park", "Top", { 0, 0, 0, 90 } } };
    std::map<std::string, std::array<std::string, 4>> letters;
    for (const JPNozzleConfig& n : cellConfig.nozzles) letters[n.name] = lettersOf(cellConfig, n.mount);
    for (const JPCameraConfig& c : cellConfig.cameras) letters[c.name] = lettersOf(cellConfig, c.mount);

    // What the controller was sent, each move (G1) or switching (M64, M65) an operation, the axes where it left them.
    std::lock_guard lk(m);
    std::map<std::string, double> axes;
    size_t next = 0;
    for (const std::string& line : sent) {
        const bool move = line.rfind("G1 ", 0) == 0, actuate = line.rfind("M64 ", 0) == 0 || line.rfind("M65 ", 0) == 0;
        if (!move && !actuate) continue;
        std::fprintf(stderr, "sent: %s\n", line.c_str());
        if (next >= expected.size()) std::fprintf(stderr, "unexpected: %s\n", line.c_str());
        assert(next < expected.size());
        const Expected& e = expected[next++];
        if (actuate) {
            assert(e.tool.empty());
            continue;
        }
        for (size_t at = 3; at < line.size();) {
            const size_t end = line.find(' ', at);
            const std::string word = line.substr(at, end == std::string::npos ? std::string::npos : end - at);
            if (!word.empty() && word[0] != 'F') axes[word.substr(0, 1)] = std::stod(word.substr(1));
            at = end == std::string::npos ? line.size() : end + 1;
        }
        // The tool's controller axes where OpenPnP expects them.
        if (e.tool.empty()) std::fprintf(stderr, "expected %s, moved: %s\n", e.description.c_str(), line.c_str());
        assert(!e.tool.empty());
        for (size_t i = 0; i < 4; ++i) {
            const std::string& l = letters.at(e.tool)[i];
            if (l.empty()) continue;
            const double now = axes.count(l) ? axes.at(l) : 0;
            if (std::abs(now - e.at[i]) > 1e-3) std::fprintf(stderr, "%s: %s at %f, expected %f\n", e.description.c_str(), l.c_str(), now, e.at[i]);
            assert(std::abs(now - e.at[i]) <= 1e-3);
        }
    }
    assert(next == expected.size());
    cell.disconnect();
    fs::remove_all(dir);
    return 0;
}
