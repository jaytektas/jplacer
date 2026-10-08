// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"
#include "JPJob.h"

#include <map>
#include <string>
#include <vector>

inline namespace jf {

// The job's plan (DESIGN.md, Job execution, Load as you go): its placements left to place (enabled, not
// placed, their side up on an enabled board) in groups of one part, in the order a run places them: as the
// job's sort rule says (the lowest first, so tall parts are not in the nozzle's way; the smallest package
// first; the most placements first; by name), then as moved by hand (JPJob::planOrder). With neither (the
// Machine's job order) the run orders placements as Machine Setup's Job Order says, OpenPnP's way, and the
// groups are listed by name. Each group says where its part comes from: the feeder holding it, or a load
// the run will ask for.
class JPJobPlan {
public:
    static constexpr const char* kMachine = "Machine's job order";
    static constexpr const char* kHeight  = "Height";
    static constexpr const char* kPackage = "Package size";
    static constexpr const char* kMost    = "Most first";
    static constexpr const char* kName    = "Name";
    static constexpr const char* kSorts[] = { kMachine, kHeight, kPackage, kMost, kName };

    struct Group {
        std::string partId, packageId;
        int         left = 0;          // placements left to place
        double      heightMm = 0;      // 0: not known
        double      bodyMm2 = 0;       // the package's body, width by length; 0: not known
        std::string feederName;        // the feeder holding it; empty: to be loaded
    };

    // The groups in run order.
    static std::vector<Group> groups(JPConfiguration& config, const JPJob& job);
    // Each part's place in that order (for the run: a group's placements before the next's); empty when the job
    // has no plan (the machine's job order).
    static std::map<std::string, size_t> order(JPConfiguration& config, const JPJob& job);
    // The job's sort rule (the machine's job order when not set or not known).
    static std::string sortOf(const JPJob& job);
    // A group moved by hand from `from` to `to` in `shown` (the order as shown): the job keeps that order.
    static void move(JPJob& job, const std::vector<Group>& shown, size_t from, size_t to);
};

} // inline namespace jf
