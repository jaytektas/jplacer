// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPEntry.h"
#include "JPPartsStore.h"

#include <string>
#include <vector>

inline namespace jf {

// A job's copies and the main library. The library is a source to pull
// from: nothing here changes it except copyToLibrary, which the person asks
// for. A job's copy remembers the library entry it came from and that
// entry's revision then (JPOrigin).
class JPLibrarySync {
public:
    // The library entry a job entry is the same as: the one it came from,
    // else for a part the one with its MPN or a supplier number, for a
    // package or footprint the one of its name. Empty id: none.
    static JPEntry counterpart(const JPPartsStore& job, const JPEntry& e, const JPPartsStore& library);

    // The library's entry has changed since the job's was copied from it, and
    // the person has not chosen to keep the job's (keep).
    static bool libraryNewer(const JPPartsStore& job, const JPEntry& e, const JPPartsStore& library);
    // The job's copy has been changed since it was copied.
    static bool changedInJob(const JPPartsStore& job, const JPEntry& e);

    // The job's copy takes the library's version (its id and its placements
    // stay); a part's package and a package's footprint come along.
    static void update(JPPartsStore& job, const JPEntry& e, const JPPartsStore& library);
    // The job's copy stays as it is, and stops saying the library is newer
    // until the library changes again.
    static void keep(JPPartsStore& job, const JPEntry& e, const JPPartsStore& library);

    // The library entry the job's entry would replace (counterpart), and what
    // would change in it; none: copyToLibrary adds it, no questions asked.
    static std::vector<std::string> wouldChange(const JPPartsStore& job, const JPEntry& e, const JPPartsStore& library);
    // The job's entry put into the library: added when the library has no
    // counterpart, else replacing it (its revision raised). A part brings its
    // package, and a package its footprint, where the library lacks them.
    // The job's copy then remembers the library entry as where it came from.
    static void copyToLibrary(JPPartsStore& job, const JPEntry& e, JPPartsStore& library);

    // A library entry brought into the job (once: a second time is the copy
    // already there). Its id in the job.
    static std::string bringIntoJob(JPPartsStore& job, const JPEntry& e, const JPPartsStore& library);
};

} // inline namespace jf
