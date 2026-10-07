// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Machine Setup's search finds a part by the settings on its page: OpenPnP's basic test machine imported, each
// node's page made by JPSetupProperties::forNode and its words taken as the search takes them
// (JPSetupProperties::words). "motion" finds the controller (Motion Control Type) and the machine (its Motion
// Planner tab) and not an axis.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPOpenPnpMachineImporter.h"
#include "setup/JPSetupProperties.h"
#include "setup/JPSetupTree.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace jf;

namespace {

bool finds(JPCellConfig& cell, const std::string& path, const std::string& what) {
    for (const JPSetupProperties::Tab& t : JPSetupProperties::forNode(cell, path, {}).tabs)
        if (JPSetupProperties::words(t).find(what) != std::string::npos) return true;
    return false;
}

void paths(const JPSetupTree::Node& n, std::vector<std::string>& out) {
    if (!n.path.empty()) out.push_back(n.path);
    for (const JPSetupTree::Node& c : n.children) paths(c, out);
}

} // namespace

int main() {
    JPCellConfig cell;
    std::vector<std::string> notes;
    std::string error;
    const bool ok = JPOpenPnpMachineImporter::import(
        (std::filesystem::path(JPLACER_TESTDATA_DIR) / "openpnp/basic-job/machine.xml").string(), cell, notes, error);
    assert(ok);
    std::vector<std::string> all;
    paths(JPSetupTree::build(cell, nullptr), all);
    std::vector<std::string> motion;
    for (const std::string& p : all)
        if (finds(cell, p, "motion")) motion.push_back(p);
    auto has = [&motion](const std::string& prefix) {
        for (const std::string& p : motion)
            if (p.rfind(prefix, 0) == 0) return true;
        return false;
    };
    assert(has("driver:") && has("machine"));
    assert(!has("axis:"));
    // Lower-cased, as the search's filter is.
    for (const JPSetupProperties::Tab& t : JPSetupProperties::forNode(cell, "machine", {}).tabs)
        if (t.title == "Motion Planner") assert(JPSetupProperties::words(t).find("allow continous motion?") != std::string::npos);
    return 0;
}
