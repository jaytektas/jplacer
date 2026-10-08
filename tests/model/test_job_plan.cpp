// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The job's plan (DESIGN.md, Job execution): its placements left to place in groups of one part, ordered by the
// job's sort rule (height, package size, most first, name), groups moved by hand kept first in their order,
// each saying where its part comes from; no plan (the machine's job order) gives the run no group order; the
// plan kept in the job's file.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBoardLocation.h"
#include "model/JPJobPlan.h"
#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> ids(const std::vector<JPJobPlan::Group>& g) {
    std::vector<std::string> out;
    for (const auto& x : g) out.push_back(x.partId);
    return out;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-job-plan";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    // Three parts: a tall big capacitor, a small resistor, a mid-size IC; heights 2.5, 0.5, 1.2.
    for (const auto& [id, w, l] : { std::tuple { "CAP", 6.6, 6.6 }, std::tuple { "R0402", 1.0, 0.5 }, std::tuple { "SOIC8", 5.0, 4.0 } }) {
        auto k = std::make_shared<JPPackage>();
        k->id = id;
        k->footprint.bodyWidth = w;
        k->footprint.bodyHeight = l;
        config.addPackage(k);
    }
    for (const auto& [id, pkg, h] : { std::tuple { "C100u", "CAP", 2.5 }, std::tuple { "R10k", "R0402", 0.5 },
                                      std::tuple { "LM358", "SOIC8", 1.2 } }) {
        auto p = std::make_shared<JPPart>();
        p->id = id;
        p->packageId = pkg;
        p->height = JPLength(h, JPLengthUnit::Millimeters);
        config.addPart(p);
    }
    JPFeeder f = JPFeeder::create(JPFeeder::classNames().front(), "R10k");
    f.setEnabled(true);
    f.setName("Lane 1");
    config.addFeeder(f);

    auto board = std::make_shared<JPBoard>();
    auto add = [&board](const std::string& id, const std::string& part) {
        JPPlacement p;
        p.id = id;
        p.partId = part;
        board->placements.push_back(p);
    };
    add("C1", "C100u");
    for (int i = 1; i <= 3; ++i) add("R" + std::to_string(i), "R10k");
    add("U1", "LM358");
    add("U2", "LM358");
    JPJob job;
    auto l = std::make_unique<JPBoardLocation>();
    l->id = "Brd1";
    l->holder = board;
    JPPlacementsHolderLocation* where = job.addBoardOrPanelLocation(std::move(l));
    job.storePlacedStatus(*where, "R3", true);

    // No plan: listed by name, and no group order for the run.
    assert(JPJobPlan::sortOf(job) == JPJobPlan::kMachine && JPJobPlan::order(config, job).empty());
    auto g = JPJobPlan::groups(config, job);
    assert((ids(g) == std::vector<std::string> { "C100u", "LM358", "R10k" }));
    assert(g[2].left == 2 && g[2].feederName == "Lane 1" && g[0].feederName.empty() && g[1].left == 2);
    assert(g[1].heightMm == 1.2 && g[1].bodyMm2 == 20.0 && g[1].packageId == "SOIC8");

    job.planSort = JPJobPlan::kHeight;
    assert((ids(JPJobPlan::groups(config, job)) == std::vector<std::string> { "R10k", "LM358", "C100u" }));
    job.planSort = JPJobPlan::kPackage;
    assert((ids(JPJobPlan::groups(config, job)) == std::vector<std::string> { "R10k", "LM358", "C100u" }));
    job.planSort = JPJobPlan::kMost;
    assert((ids(JPJobPlan::groups(config, job)) == std::vector<std::string> { "LM358", "R10k", "C100u" }));
    job.planSort = JPJobPlan::kName;
    const auto order = JPJobPlan::order(config, job);
    assert(order.at("C100u") == 0 && order.at("LM358") == 1 && order.at("R10k") == 2);

    // Moved by hand: R10k to the top; kept first, the rest sorted after.
    job.planSort = JPJobPlan::kHeight;
    g = JPJobPlan::groups(config, job);   // R10k, LM358, C100u
    JPJobPlan::move(job, g, 2, 0);        // C100u first
    assert((ids(JPJobPlan::groups(config, job)) == std::vector<std::string> { "C100u", "R10k", "LM358" }));
    // A hand order alone (the machine's sort) still orders the run.
    job.planSort.clear();
    assert(!JPJobPlan::order(config, job).empty() && JPJobPlan::order(config, job).at("C100u") == 0);

    // Kept in the job's file.
    job.planSort = JPJobPlan::kMost;
    std::string error;
    const std::string file = (dir / "plan.job.xml").string();
    assert(JPXmlWriter::write(file, job.toXml(), error, false));
    JPXmlElement root;
    assert(JPXmlReader::read(file, root, error));
    const auto back = JPJob::fromXml(root);
    assert(back->planSort == JPJobPlan::kMost && back->planOrder == job.planOrder);
    fs::remove_all(dir);
    return 0;
}
