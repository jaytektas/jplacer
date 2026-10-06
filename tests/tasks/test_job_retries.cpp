// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's ReferenceJobProcessorRetryTests: one nozzle (N1, tip NT1), a board of R0402-1k placements, and feeders
// holding a few parts each (OpenPnP's TestFeeder: an auto feeder whose feed fails once its parts are used, or, for one
// that says it is empty, a tray of one). The feed retried as the feeder's Feed Retry Count says and the feeder then
// turned off; the pick retried as its Pick Retry Count says when the part-on check fails; the part's Pick Retry Count
// feeding again, the next feeder for the part taken when one is off; an empty feeder failed over at once; and with
// Defer, placements tried again later, the feeders' faults counted (turned off past the fault limit, by priority).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "FakeJobMachine.h"
#include "model/JPBoard.h"
#include "model/JPBoardLocation.h"
#include "model/JPConfiguration.h"
#include "model/JPJob.h"
#include "tasks/JPJobProcessor.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>

using namespace jf;
namespace fs = std::filesystem;

namespace {

JPLocation at(double x, double y, double z = 0, double r = 0) { return JPLocation(JPLengthUnit::Millimeters, x, y, z, r); }

// OpenPnP's test machine: TestNozzle (picks succeeding at its ratio, counted), TestActuator (the vacuum read), and
// the TestFeeders' feeds (each auto feeder's actuator, named after it, refused once its parts are used).
class Machine : public FakeJobMachine {
public:
    std::vector<Nozzle>          heads { { "N1", "N1", "", { "NT1" } } };
    std::map<std::string, int>&  feedCount;
    std::map<std::string, int>   partCount;
    double                       pickSuccessRatio = 1.0, pickSuccessAccumulator = 0;
    int                          pickCount = 0;
    std::string                  vacuum = "0.5";   // N1_VAC's reading

    explicit Machine(std::map<std::string, int>& feeds) : feedCount(feeds) {}
    std::vector<Nozzle> nozzles() const override { return heads; }
    std::vector<std::pair<std::string, std::string>> tips() const override { return { { "NT1", "NT1" } }; }
    bool changeTip(const std::string& nozzleId, const std::string& tipId, std::string&) override {
        for (Nozzle& n : heads)
            if (n.id == nozzleId) n.tipId = tipId;
        return true;
    }
    bool actuate(const std::string& feeder, double, std::string& why) override {
        if (feedCount[feeder] > partCount[feeder]) {
            why = "No parts.";
            return false;
        }
        return true;
    }
    bool pick(const std::string&, const JPLocation&, std::string& why) override {
        ++pickCount;
        pickSuccessAccumulator += pickSuccessRatio;
        if (pickSuccessAccumulator >= 1.0) {
            pickSuccessAccumulator -= 1.0;
            return true;
        }
        why = "Refusing to pick";
        return false;
    }
    // NT1's part-on check, Absolute, 0.5 to 1.0 (OpenPnP's test tip).
    bool vacuumChecked(const std::string&, VacuumStep step) const override { return step == VacuumStep::AfterPick; }
    bool partOn(const std::string&, bool& on, std::string&) override {
        const double v = std::stod(vacuum);
        on = v >= 0.5 && v <= 1.0;
        return true;
    }
};

// OpenPnP's MachineBuilder and JobBuilder: a board B1 at 10, 10, 10, -10, package R0402 for NT1, part R0402-1k.
struct Bench {
    fs::path                             dir;
    JPConfiguration                      config;
    std::map<std::string, int>           feedCount;   // each feeder's feeds, as OpenPnP's TestFeeder counts them
    Machine                              machine { feedCount };
    JPJob                                job;
    std::shared_ptr<JPBoard>             board = std::make_shared<JPBoard>();
    JPJobProcessor::Settings             settings;
    std::unique_ptr<JPJobProcessor>      processor;

    Bench() : dir(fs::temp_directory_path() / ("jplacer-job-retries-" + std::to_string(::getpid()))), config(dir.string()) {
        fs::remove_all(dir);
        fs::create_directories(dir);
        auto pkg = std::make_shared<JPPackage>();
        pkg->id = "R0402";
        pkg->compatibleNozzleTipIds = { "NT1" };
        config.addPackage(pkg);
        auto part = std::make_shared<JPPart>();
        part->id = "R0402-1k";
        part->packageId = "R0402";
        part->height = JPLength(1, JPLengthUnit::Millimeters);
        config.addPart(part);
        board->file = (dir / "B1.board.xml").string();
        auto l = std::make_unique<JPBoardLocation>();
        l->id = "B1";
        l->holder = board;
        l->setLocation(at(10, 10, 10, -10));
        job.addBoardOrPanelLocation(std::move(l));
    }
    ~Bench() { fs::remove_all(dir); }

    // OpenPnP's TestFeeder: an auto feeder fed by its actuator, or (`explicitEmpty`) a tray of `parts` that says it
    // is empty once they are used.
    JPFeeder& feeder(const std::string& name, double x, double y, double z, int parts = 0, bool explicitEmpty = false) {
        JPFeeder f = JPFeeder::create(explicitEmpty ? "org.openpnp.machine.reference.feeder.ReferenceTrayFeeder"
                                                    : "org.openpnp.machine.reference.feeder.ReferenceAutoFeeder",
                                      "R0402-1k");
        const std::string id = f.id();
        f.setName(name);
        f.setLocation(at(x, y, z, 0));
        f.setEnabled(true);
        if (explicitEmpty) {
            f.setNumber("tray-count-x", parts);
            f.setNumber("tray-count-y", 1);
        } else {
            f.setText("actuator-name", id);
        }
        machine.partCount[id] = parts;
        config.addFeeder(std::move(f));
        return *config.feeder(id);
    }
    void placement(const std::string& id, double x, double y, double rotation) {
        JPPlacement p;
        p.id = id;
        p.partId = "R0402-1k";
        p.location = at(x, y, 0, rotation);
        board->placements.push_back(p);
    }
    int feeds(const JPFeeder& f) { return feedCount[f.id()]; }
    // OpenPnP's runJob: run to its end (an error ends it).
    void run() {
        JPJobProcessor::Hooks hooks;
        hooks.event = [this](const std::string& event, const JJson& globals, std::string&) {
            if (event == "Feeder.BeforeFeed") ++feedCount[globals["feeder"].str()];
            return true;
        };
        processor = std::make_unique<JPJobProcessor>(config, job, machine, settings, hooks);
        JPJobProcessor::Failure failure;
        for (int steps = 0; steps < 10000; ++steps) {
            const auto r = processor->next(failure);
            if (r == JPJobProcessor::Result::Finished) return;
            if (r == JPJobProcessor::Result::Failed) {
                std::fprintf(stderr, "Exception %s\n", failure.message.c_str());
                return;
            }
        }
        assert(false && "the job did not end");
    }
    // OpenPnP's getActivePlacements: those not placed.
    int activePlacements() const {
        int n = 0;
        for (const auto& j : processor->jobPlacements())
            if (j.status != JPJobProcessor::Status::Complete) ++n;
        return n;
    }
};

void testFeederFeedRetry() {
    Bench b;
    JPFeeder& f1 = b.feeder("F1", 100, 20, -5, 1);
    b.placement("R1", 10, 10, 0);
    b.placement("R2", 20, 20, 0);
    f1.setNumber("feed-retry-count", 3);
    b.run();
    // The feeder holds 1 part: the first feed succeeds, the second fails, then three retries.
    assert(b.feeds(*b.config.feeder(f1.id())) == 5);
}

void testFeederDisable() {
    Bench b;
    const std::string id = b.feeder("F1", 100, 20, -5, 0).id();
    b.placement("R1", 10, 10, 0);
    b.config.feeder(id)->setNumber("feed-retry-count", 3);
    assert(b.config.feeder(id)->enabled());
    b.run();
    assert(!b.config.feeder(id)->enabled());
}

void testFeederPickRetry() {
    Bench b;
    const std::string id = b.feeder("F1", 100, 20, -5, 1).id();
    b.placement("R1", 10, 10, 0);
    b.config.feeder(id)->setNumber("pick-retry-count", 3);
    b.machine.vacuum = "0";   // the part-on check fails
    b.run();
    assert(b.machine.pickCount == 4);
}

void testPartPickRetry() {
    Bench b;
    std::vector<std::string> ids;
    for (const auto& [name, x, parts] : { std::tuple { "F1", 100.0, 1 }, std::tuple { "F2", 110.0, 1 }, std::tuple { "F3", 120.0, 1 },
                                          std::tuple { "F4", 130.0, 0 }, std::tuple { "F5", 140.0, 0 } }) {
        ids.push_back(b.feeder(name, x, 20, -5, parts).id());
        b.config.feeder(ids.back())->setNumber("feed-retry-count", 0);
    }
    b.placement("R1", 10, 10, 0);
    b.config.part("R0402-1k")->pickRetryCount = 3;
    b.machine.vacuum = "0";
    b.run();
    assert(b.feedCount[ids[0]] == 2 && b.feedCount[ids[1]] == 2 && b.feedCount[ids[2]] == 0);
}

void testPartFailover() {
    // Three feeders of one part each, saying when they are empty: the first fills R1, then says it is empty for R2,
    // which the second fills.
    Bench b;
    std::vector<std::string> ids;
    for (const auto& [name, x] : { std::pair { "F1", 100.0 }, std::pair { "F2", 110.0 }, std::pair { "F3", 120.0 } }) {
        ids.push_back(b.feeder(name, x, 20, -5, 1, true).id());
        b.config.feeder(ids.back())->setNumber("feed-retry-count", 0);
    }
    for (const auto& [name, x] : { std::pair { "F4", 130.0 }, std::pair { "F5", 140.0 } }) b.feeder(name, x, 20, -5, 0);
    b.placement("R1", 10, 10, 0);
    b.placement("R2", 20, 10, 0);
    b.run();
    assert(b.feedCount[ids[0]] == 2 && b.feedCount[ids[1]] == 1 && b.feedCount[ids[2]] == 0);
}

void testPlacementRetry() {
    // Defer: a nozzle that fails some picks still places everything.
    Bench b;
    const std::string id = b.feeder("F1", 100, 20, -5, 20).id();
    b.placement("R1", 10, 10, 0);
    b.placement("R2", 20, 20, 0);
    b.job.errorHandling = JPJob::ErrorHandling::Defer;
    b.machine.pickSuccessRatio = 0.6;
    b.run();
    assert(b.feedCount[id] == 4);   // both placements need 2 feeds
    assert(b.config.feeder(id)->summariseJobFaults() == "-X-X");
    assert(b.activePlacements() == 0);   // nothing left unplaced
}

void testPlacementRetryDisablesOneFeeder() {
    // Defer, a nozzle that mostly fails: the high priority feeder faults until it is turned off, the next carries on,
    // the low priority one is never used.
    Bench b;
    const std::string f1 = b.feeder("F1", 100, 20, -5, 20).id(), f2 = b.feeder("F2", 110, 20, -5, 20).id(),
                      f3 = b.feeder("F3", 120, 20, -5, 20).id();
    b.config.feeder(f1)->setText("priority", "High");
    b.config.feeder(f3)->setText("priority", "Low");
    b.placement("R1", 10, 10, 0);
    b.placement("R2", 20, 20, 0);
    b.job.errorHandling = JPJob::ErrorHandling::Defer;
    b.machine.pickSuccessRatio = 0.35;
    b.run();
    assert(!b.config.feeder(f1)->enabled() && b.config.feeder(f1)->summariseJobFaults() == "X-XX");
    assert(b.config.feeder(f2)->enabled() && b.config.feeder(f2)->summariseJobFaults() == "-X");
    assert(b.config.feeder(f3)->enabled() && b.config.feeder(f3)->summariseJobFaults().empty());
    assert(b.activePlacements() == 0);
}

void testPlacementRetryNeverDisablesAnyFeeder() {
    // A fault limit of 0: no feeder turned off for its faults; each placement tried 5 times, then left.
    Bench b;
    const std::string id = b.feeder("F1", 100, 20, -5, 50).id();
    b.placement("R1", 10, 10, 0);
    b.placement("R2", 20, 20, 0);
    b.job.errorHandling = JPJob::ErrorHandling::Defer;
    b.machine.pickSuccessRatio = 0.0;
    b.settings.feederFaultLimit = 0;
    b.run();
    assert(b.config.feeder(id)->enabled());
    assert(b.config.feeder(id)->summariseJobFaults() == "XXXXXX");   // 6 of 6 feeds bad (its window)
    assert(b.activePlacements() == 2);   // nothing placed
    assert(b.feedCount[id] == 10);   // 5 tries of each of 2 placements
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> tests {
        { "testFeederFeedRetry", testFeederFeedRetry }, { "testFeederDisable", testFeederDisable },
        { "testFeederPickRetry", testFeederPickRetry }, { "testPartPickRetry", testPartPickRetry },
        { "testPartFailover", testPartFailover }, { "testPlacementRetry", testPlacementRetry },
        { "testPlacementRetryDisablesOneFeeder", testPlacementRetryDisablesOneFeeder },
        { "testPlacementRetryNeverDisablesAnyFeeder", testPlacementRetryNeverDisablesAnyFeeder } };
    if (argc > 1) {
        tests.at(argv[1])();
        return 0;
    }
    for (const auto& [name, test] : tests) test();
    return 0;
}
