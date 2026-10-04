// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPEntry.h"
#include "JPFootprint.h"
#include "JPPackage.h"
#include "JPPart.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// A set of parts, packages and footprints, each stored on its own and linked
// by id: a part to its one package, a package to its one footprint. The main
// library is one; each job holds another, its own copies (copyPart), so a
// job never changes under the person when the library does.
class JPPartsStore {
public:
    std::vector<JPFootprint> footprints;
    std::vector<JPPackage>   packages;
    std::vector<JPPart>      parts;

    const JPFootprint* footprint(const std::string& id) const;
    const JPPackage*   package(const std::string& id) const;
    const JPPart*      part(const std::string& id) const;
    JPFootprint*       footprint(const std::string& id);
    JPPackage*         package(const std::string& id);
    JPPart*            part(const std::string& id);

    // The package a CAD footprint name or supplier package name belongs to.
    const JPPackage* packageNamed(const std::string& name) const;
    // The part a supplier number orders (an unnamed supplier on either side
    // matches any), or with this MPN (compared as mpnKey does).
    const JPPart* partByNumber(const JPSupplierNumber& n) const;
    const JPPart* partByMpn(const std::string& mpn) const;
    // The copy in this store of a library entry.
    const JPPart*      partFrom(const std::string& libraryId) const;
    const JPPackage*   packageFrom(const std::string& libraryId) const;
    const JPFootprint* footprintFrom(const std::string& libraryId) const;

    // An MPN as compared: upper case, without spaces, dashes or dots.
    static std::string mpnKey(const std::string& mpn);

    // Added with a new id when it has none; the id it has.
    std::string add(JPFootprint f);
    std::string add(JPPackage p);
    std::string add(JPPart p);
    // Taken out. What pointed at it (placements at a part, parts at a
    // package, packages at a footprint) is left pointing at nothing, and so
    // shows what it lacks until it is linked again.
    bool remove(const JPEntry& e);

    // A name given to a package; false (and nothing changed) when another
    // package has it: a name belongs to one package only.
    bool addName(const std::string& packageId, const std::string& name);

    // Library part `id` copied into this store with its package and
    // footprint (each copied once: a second part in the same package uses
    // the copy already here), each remembering where it came from. The
    // copy's id; empty when `from` has no such part.
    std::string copyPart(const JPPartsStore& from, const std::string& id);
    std::string copyPackage(const JPPartsStore& from, const std::string& id);
    std::string copyFootprint(const JPPartsStore& from, const std::string& id);

    JJson toJson() const;
    static bool fromJson(const JJson& j, JPPartsStore& out, std::string& error);
};

} // inline namespace jf
