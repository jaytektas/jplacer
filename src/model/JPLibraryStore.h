// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPackage.h"
#include "JPPart.h"

#include <j/db/Database.h>

#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The library's file (DESIGN.md, Storage): its parts and packages in a
// SQLite database beside the configuration. A part's searched fields have
// columns of their own (id, name, value, datasheet, package);
// its OpenPnP fields are kept as JSON in the shape of OpenPnP's part (as a
// board keeps one); its identifiers and AKAs are rows of their own, indexed,
// for matching. Written whole, in one transaction: a library half written is
// never left. A few named values (meta) record what it was made from.
class JPLibraryStore {
public:
    static constexpr const char* kFile = "library.db";
    static constexpr int         kSchema = 1;

    bool open(const std::string& path, std::string& error);
    bool isOpen() const { return m_db.isOpen(); }
    bool load(std::vector<std::shared_ptr<JPPart>>& parts, std::vector<std::shared_ptr<JPPackage>>& packages,
              std::string& error);
    bool save(const std::vector<std::shared_ptr<JPPart>>& parts, const std::vector<std::shared_ptr<JPPackage>>& packages,
              std::string& error);
    std::string meta(const std::string& key);
    bool        setMeta(const std::string& key, const std::string& value);

private:
    mutable JDatabase m_db;
};

} // inline namespace jf
