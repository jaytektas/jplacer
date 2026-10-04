// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLibrary.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"
#include "common/JPWholeFile.h"

#include <j/core/Log.h>

#include <filesystem>
#include <system_error>

inline namespace jf {

namespace fs = std::filesystem;

std::string JPLibrary::defaultFolder() {
    const std::string data = JPlacerPaths::dataDir();
    return data.empty() ? std::string() : (fs::path(data) / "library").string();
}

std::string JPLibrary::path() const {
    return (fs::path(m_folder) / kFileName).string();
}

bool JPLibrary::open(const std::string& folder) {
    m_folder = folder;
    m_problem.clear();
    m_store = JPPartsStore();
    std::error_code ec;
    if (!fs::exists(path(), ec)) {
        JLOGC(JPlacerLog::kLibrary, JLogLevel::Info) << "no library in " << folder << " yet: starting an empty one";
        return true;
    }
    const std::optional<JJson> doc = JJson::tryParseFile(path());
    std::string why;
    if (!doc) why = "not valid JSON";
    else if (!JPPartsStore::fromJson(*doc, m_store, why)) m_store = JPPartsStore();
    if (!why.empty()) {
        m_problem = path() + ": " + why + "; the library is read-only and the file is left as it is";
        JLOGC(JPlacerLog::kLibrary, JLogLevel::Warn) << m_problem;
        return false;
    }
    JLOGC(JPlacerLog::kLibrary, JLogLevel::Info) << path() << ": " << m_store.parts.size() << " part(s), "
                                                 << m_store.packages.size() << " package(s), "
                                                 << m_store.footprints.size() << " footprint(s)";
    return true;
}

bool JPLibrary::save(std::string& error) const {
    if (readOnly()) {
        error = m_problem;
        return false;
    }
    return JPWholeFile::write(path(), m_store.toJson().dump(2) + "\n", error);
}

} // inline namespace jf
