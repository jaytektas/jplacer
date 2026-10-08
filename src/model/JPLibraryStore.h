// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLibraryFootprint.h"
#include "JPManufacturer.h"
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
    static constexpr int         kSchema = 3;   // 2: packagings, offers, manufacturers; 3: footprints

    bool open(const std::string& path, std::string& error);
    bool isOpen() const { return m_db.isOpen(); }
    // What the library holds.
    struct Contents {
        std::vector<std::shared_ptr<JPPart>>             parts;
        std::vector<std::shared_ptr<JPPackage>>          packages;
        std::vector<std::shared_ptr<JPLibraryFootprint>> footprints;
        std::vector<JPManufacturer>                      manufacturers;
        // Read from a library of schema 2: the CAD footprint names its packages were known by (package uuid,
        // name), which are footprints' names now (JPConfiguration moves them). Never written.
        std::vector<std::pair<std::string, std::string>> packageNames;
    };
    bool load(Contents& out, std::string& error);
    bool save(const Contents& in, std::string& error);
    std::string meta(const std::string& key);
    bool        setMeta(const std::string& key, const std::string& value);

private:
    mutable JDatabase m_db;
};

} // inline namespace jf
