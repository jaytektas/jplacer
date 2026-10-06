// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's JobProcessorTest: its two-nozzle, three-tip test machine (no tip loaded at the start) running its
// panelized job of four boards (96 placements of eight parts), each plan the job processor makes kept: how many tip
// changes, how many nozzles each cycle uses, how many cycles, the planning cost (its TravelCost to the second
// nozzle's placement, times 100 as OpenPnP's test reports it), how many part changes, and the rank rule (nothing of
// rank R+10 planned before rank R), for each job order and planner strategy OpenPnP's test runs, and with ranks.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "OpenPnpJobBench.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>

using namespace jf;

namespace {

using JobOrder = JPJobProcessorConfig::JobOrder;
using Strategy = JPJobProcessorConfig::Strategy;
const std::filesystem::path kConfig = std::filesystem::path(JPLACER_TESTDATA_DIR) / "openpnp/job-processor";

struct Run {
    OpenPnpJobBench bench { kConfig, "pnp-test-panelized.job.xml" };
    const std::vector<std::vector<OpenPnpJobBench::PlannedPlacement>>& results = bench.results;

    // Each placement of the job's boards, once for each board (its board locations share it, as OpenPnP's).
    void forEachPlacement(const std::function<void(JPPlacement&)>& fn) {
        std::set<JPBoard*> boards;
        for (JPBoardLocation* l : bench.job->boardLocations())
            if (JPBoard* b = l->board(); b && boards.insert(b).second)
                for (JPPlacement& p : b->placements) fn(p);
    }
    void run(const char* name) {
        JPJobProcessor::Failure failure;
        const bool ok = bench.run(failure);
        if (!ok) std::fprintf(stderr, "%s: %s\n", name, failure.message.c_str());
        assert(ok);
        std::fprintf(stderr, "%s: tip changes %d, utilisation %.2f, cycles %d, planning cost %.2f, part changes %d\n", name,
                     tipChanges(), utilisation(), cycleCount(), averagePlanningCost(), partChanges());
        checkRanks();
    }

    double utilisation() const {
        double r = 0;
        for (const auto& step : results) r += double(step.size());
        return r / double(results.size());
    }
    int cycleCount() const { return int(results.size()); }
    double averagePlanningCost() const {
        double r = 0;
        int count = 0;
        for (const auto& step : results)
            for (const auto& p : step)
                if (p.planningCost) {
                    r += *p.planningCost;
                    ++count;
                }
        // makeMachineFastest makes it about 100 times faster: as OpenPnP's test, so the numbers read naturally.
        return count == 0 ? 0 : r * 100 / count;
    }
    int partChanges() const {
        int r = 0;
        std::set<std::string> prev;
        for (const auto& step : results) {
            std::set<std::string> next;
            for (const auto& p : step) {
                if (!prev.count(p.partId) && !next.count(p.partId)) ++r;
                next.insert(p.partId);
            }
            prev = next;
        }
        return r;
    }
    int tipChanges() const {
        int r = 0;
        std::map<std::string, std::string> tips;
        for (const auto& step : results)
            for (const auto& p : step)
                if (tips[p.nozzleId] != p.tipId) {
                    ++r;
                    tips[p.nozzleId] = p.tipId;
                }
        return r;
    }
    void checkRanks() const {
        int working = -1000;
        for (const auto& step : results) {
            int next = working;
            for (const auto& p : step) {
                if (p.rank > next) next = p.rank;
                assert(p.rank > working - 10);   // rank error
            }
            working = next;
        }
    }
    int firstPlacementPosition(const std::string& id) const {
        for (size_t c = 0; c < results.size(); ++c)
            for (const auto& p : results[c])
                if (p.placementId == id) return int(c + 1);
        assert(false && "placement not found");
        return -1;
    }
    int lastPlacementPosition(const std::string& id) const {
        int r = -1;
        for (size_t c = 0; c < results.size(); ++c)
            for (const auto& p : results[c])
                if (p.placementId == id) r = int(c + 1);
        assert(r != -1);
        return r;
    }
};

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
int rankOf(const JPPlacement& p) { return std::stoi(p.id.substr(1)); }

void testNozzleTips() {
    // The baseline, the default configuration: Nozzle Tips with Minimize.
    Run r;
    assert(r.bench.settings.jobOrder == JobOrder::NozzleTips);
    r.run("testNozzleTips");
    assert(r.tipChanges() == 3);
    assert(near(r.utilisation(), 1.6, 0.01));
    assert(near(r.cycleCount(), 60, 1));
    assert(near(r.averagePlanningCost(), 1.18, 0.01));
    assert(near(r.partChanges(), 20, 5));
}

void testStartAsPlanned() {
    // As Nozzle Tips: this machine starts with no tips loaded.
    Run r;
    r.bench.settings.strategy = Strategy::StartAsPlanned;
    r.run("testStartAsPlanned");
    assert(r.tipChanges() == 3);
    assert(near(r.utilisation(), 1.6, 0.01));
    assert(near(r.cycleCount(), 60, 1));
    assert(near(r.averagePlanningCost(), 1.18, 0.01));
    assert(near(r.partChanges(), 20, 5));
}

void testBoardPart() {
    Run r;
    r.bench.settings.jobOrder = JobOrder::BoardPart;
    r.run("testBoardPart");
    assert(r.tipChanges() == 3);
    assert(near(r.utilisation(), 1.6, 0.01));
    assert(near(r.cycleCount(), 60, 1));
    assert(near(r.averagePlanningCost(), 1.24, 0.01));
    assert(near(r.partChanges(), 46, 5));
}

void testUnsorted() {
    // The panel's and boards' own order kept.
    Run r;
    r.bench.settings.jobOrder = JobOrder::Unsorted;
    r.bench.settings.strategy = Strategy::FullyAsPlanned;
    r.run("Unsorted");
    assert(r.tipChanges() == 33);
    assert(near(r.utilisation(), 1.95, 0.01));
    assert(near(r.cycleCount(), 49, 1));
    assert(r.averagePlanningCost() == 0);   // no planning
    assert(near(r.partChanges(), 72, 5));
}

void testFlexibility() {
    Run r;
    r.bench.settings.jobOrder = JobOrder::NozzleTipsByFlexibility;
    r.run("testFlexibility");
    assert(r.tipChanges() == 3);
    assert(r.utilisation() == 2.0);   // both nozzles used every cycle
    assert(r.cycleCount() == 48);
    assert(near(r.averagePlanningCost(), 1.49, 0.01));
    assert(near(r.partChanges(), 34, 5));
}

void testAllOnePart() {
    Run r;
    r.forEachPlacement([](JPPlacement& p) { p.partId = "R0201-1K"; });
    r.run("testAllOnePart");
    assert(r.tipChanges() == 2);
    assert(near(r.utilisation(), 2.0, 0.01));
    assert(near(r.cycleCount(), 48, 1));
    assert(near(r.averagePlanningCost(), 1.41, 0.01));
}

void testRank() {
    Run r;
    r.forEachPlacement([](JPPlacement& p) { p.rank = rankOf(p); });
    r.run("testRank");
    assert(r.tipChanges() == 4);
    assert(near(r.utilisation(), 1.84, 0.01));
    assert(near(r.cycleCount(), 52, 1));
    assert(near(r.averagePlanningCost(), 1.41, 0.01));
    assert(near(r.partChanges(), 31, 3));
}

void testRank2() {
    Run r;
    r.forEachPlacement([](JPPlacement& p) { p.rank = -rankOf(p); });
    r.run("testRank2");
    assert(r.tipChanges() == 4);
    assert(near(r.utilisation(), 1.77, 0.01));
    assert(near(r.cycleCount(), 54, 1));
    assert(near(r.averagePlanningCost(), 1.49, 0.01));
    assert(near(r.partChanges(), 37, 3));
}

void testRankWeak() {
    // R10 overshadows R11: R11 first, R10 after; the rest in between as it suits.
    Run r;
    r.forEachPlacement([](JPPlacement& p) { p.rank = p.id == "R10" ? 5 : p.id == "R11" ? -5 : 0; });
    r.run("testRankWeak");
    assert(r.tipChanges() == 3);
    assert(near(r.utilisation(), 1.6, 0.01));
    assert(near(r.cycleCount(), 60, 1));
    assert(r.lastPlacementPosition("R11") == 4);
    assert(r.firstPlacementPosition("R10") == 32);
    assert(r.lastPlacementPosition("R10") == 36);
    assert(near(r.averagePlanningCost(), 1.18, 0.01));
    assert(near(r.partChanges(), 19, 3));
}

void testRankRounded() {
    Run r;
    r.forEachPlacement([](JPPlacement& p) { p.rank = rankOf(p) - rankOf(p) % 5; });
    r.run("testRankRounded");
    assert(r.tipChanges() == 5);
    assert(near(r.utilisation(), 1.65, 0.01));
    assert(near(r.cycleCount(), 58, 1));
    assert(near(r.averagePlanningCost(), 1.44, 0.01));
    assert(near(r.partChanges(), 31, 3));
}

void testRankRoundedFlexibility() {
    Run r;
    r.bench.settings.jobOrder = JobOrder::NozzleTipsByFlexibility;
    r.forEachPlacement([](JPPlacement& p) { p.rank = rankOf(p) - rankOf(p) % 5; });
    r.run("testRankRoundedFlexibility");
    assert(r.tipChanges() == 4);
    assert(near(r.utilisation(), 1.84, 0.01));
    assert(near(r.cycleCount(), 52, 1));
    assert(near(r.averagePlanningCost(), 1.50, 0.01));
    assert(near(r.partChanges(), 27, 3));
}

} // namespace

int main(int argc, char** argv) {
    // One by name (OpenPnP's -Dtest=JobProcessorTest#name), else all.
    const std::map<std::string, void (*)()> tests {
        { "testNozzleTips", testNozzleTips }, { "testStartAsPlanned", testStartAsPlanned }, { "testBoardPart", testBoardPart },
        { "testUnsorted", testUnsorted }, { "testFlexibility", testFlexibility }, { "testAllOnePart", testAllOnePart },
        { "testRank", testRank }, { "testRank2", testRank2 }, { "testRankWeak", testRankWeak }, { "testRankRounded", testRankRounded },
        { "testRankRoundedFlexibility", testRankRoundedFlexibility } };
    if (argc > 1) {
        tests.at(argv[1])();
        return 0;
    }
    for (const auto& [name, test] : tests) test();
    return 0;
}
