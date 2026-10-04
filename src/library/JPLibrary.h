// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPartsStore.h"

#include <string>

inline namespace jf {

// The main parts library: the parts, packages and footprints kept from job
// to job, in `library.json` in a folder the person chooses (by default in
// jplacer's data folder; it can be a git repository or a shared drive). Jobs
// pull copies from it; nothing in a job changes it.
//
// A library file that cannot be read (damaged, or from a newer jplacer) is
// never overwritten: the library opens empty and read-only, and says why.
// A save writes a new file and puts it in place only once it is whole, so a
// crash part way leaves the last good library.
class JPLibrary {
public:
    static constexpr const char* kFileName = "library.json";

    // jplacer's data folder's "library".
    static std::string defaultFolder();

    // The library in `folder`: read if there is one, else a new one holding
    // the standard packages (JPStarterLibrary), saved at the first save.
    // False when there is one that cannot be read (then read-only, `problem`).
    bool open(const std::string& folder);
    bool save(std::string& error) const;

    const std::string&  folder() const { return m_folder; }
    std::string         path() const;
    bool                readOnly() const { return !m_problem.empty(); }
    const std::string&  problem() const { return m_problem; }
    JPPartsStore&       store() { return m_store; }
    const JPPartsStore& store() const { return m_store; }

private:
    std::string  m_folder;
    std::string  m_problem;
    JPPartsStore m_store;
};

} // inline namespace jf
