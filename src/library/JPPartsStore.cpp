// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartsStore.h"

#include <j/core/Uuid.h>

#include <algorithm>
#include <cctype>

inline namespace jf {

namespace {

template <typename T>
T* byId(std::vector<T>& v, const std::string& id) {
    if (id.empty()) return nullptr;
    for (T& x : v)
        if (x.id == id) return &x;
    return nullptr;
}

template <typename T>
const T* byOrigin(const std::vector<T>& v, const std::string& libraryId) {
    if (libraryId.empty()) return nullptr;
    for (const T& x : v)
        if (x.origin.libraryId == libraryId) return &x;
    return nullptr;
}

bool sameText(const std::string& a, const std::string& b) {
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

template <typename T>
std::string addTo(std::vector<T>& v, T x) {
    if (x.id.empty()) x.id = makeUuid();
    v.push_back(std::move(x));
    return v.back().id;
}

template <typename T>
JPOrigin originOf(const T& libraryEntry) {
    return JPOrigin { libraryEntry.id, libraryEntry.revision, 1 };
}

} // namespace

const JPFootprint* JPPartsStore::footprint(const std::string& id) const { return const_cast<JPPartsStore*>(this)->footprint(id); }
const JPPackage*   JPPartsStore::package(const std::string& id) const   { return const_cast<JPPartsStore*>(this)->package(id); }
const JPPart*      JPPartsStore::part(const std::string& id) const      { return const_cast<JPPartsStore*>(this)->part(id); }
JPFootprint*       JPPartsStore::footprint(const std::string& id)       { return byId(footprints, id); }
JPPackage*         JPPartsStore::package(const std::string& id)         { return byId(packages, id); }
JPPart*            JPPartsStore::part(const std::string& id)            { return byId(parts, id); }

const JPPackage* JPPartsStore::packageNamed(const std::string& name) const {
    if (name.empty()) return nullptr;
    for (const JPPackage& p : packages)
        if (p.hasName(name)) return &p;
    return nullptr;
}

const JPPart* JPPartsStore::partByNumber(const JPSupplierNumber& n) const {
    if (n.number.empty()) return nullptr;
    for (const JPPart& p : parts)
        for (const JPSupplierNumber& m : p.supplierNumbers)
            if (sameText(m.number, n.number) && (m.supplier.empty() || n.supplier.empty() || sameText(m.supplier, n.supplier)))
                return &p;
    return nullptr;
}

const JPPart* JPPartsStore::partByMpn(const std::string& mpn) const {
    const std::string key = mpnKey(mpn);
    if (key.empty()) return nullptr;
    for (const JPPart& p : parts)
        if (mpnKey(p.mpn) == key) return &p;
    return nullptr;
}

const JPPart*      JPPartsStore::partFrom(const std::string& libraryId) const      { return byOrigin(parts, libraryId); }
const JPPackage*   JPPartsStore::packageFrom(const std::string& libraryId) const   { return byOrigin(packages, libraryId); }
const JPFootprint* JPPartsStore::footprintFrom(const std::string& libraryId) const { return byOrigin(footprints, libraryId); }

std::string JPPartsStore::mpnKey(const std::string& mpn) {
    std::string out;
    for (const char c : mpn)
        if (!std::isspace(static_cast<unsigned char>(c)) && c != '-' && c != '.')
            out += char(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string JPPartsStore::add(JPFootprint f) { return addTo(footprints, std::move(f)); }
std::string JPPartsStore::add(JPPackage p)   { return addTo(packages, std::move(p)); }
std::string JPPartsStore::add(JPPart p)      { return addTo(parts, std::move(p)); }

bool JPPartsStore::addName(const std::string& packageId, const std::string& name) {
    JPPackage* p = package(packageId);
    if (!p || name.empty()) return false;
    if (const JPPackage* owner = packageNamed(name)) return owner == p;
    p->names.push_back(name);
    return true;
}

std::string JPPartsStore::copyPackage(const JPPartsStore& from, const std::string& id) {
    const JPPackage* src = from.package(id);
    if (!src) return {};
    if (const JPPackage* have = packageFrom(src->id)) return have->id;
    JPPackage p = *src;
    p.id.clear();
    p.revision = 1;
    p.origin = originOf(*src);
    p.footprintId.clear();
    if (const JPFootprint* f = from.footprint(src->footprintId)) {
        if (const JPFootprint* have = footprintFrom(f->id)) {
            p.footprintId = have->id;
        } else {
            JPFootprint copy = *f;
            copy.id.clear();
            copy.revision = 1;
            copy.origin = originOf(*f);
            p.footprintId = add(std::move(copy));
        }
    }
    // A name already given to another package here stays with that one.
    p.names.erase(std::remove_if(p.names.begin(), p.names.end(), [this](const std::string& n) { return packageNamed(n) != nullptr; }),
                  p.names.end());
    return add(std::move(p));
}

std::string JPPartsStore::copyPart(const JPPartsStore& from, const std::string& id) {
    const JPPart* src = from.part(id);
    if (!src) return {};
    if (const JPPart* have = partFrom(src->id)) return have->id;
    JPPart p = *src;
    p.id.clear();
    p.revision = 1;
    p.origin = originOf(*src);
    p.packageId = src->packageId.empty() ? std::string() : copyPackage(from, src->packageId);
    return add(std::move(p));
}

JJson JPPartsStore::toJson() const {
    JJson j = JJson::object();
    j["version"] = 1;
    JJson f = JJson::array(), k = JJson::array(), p = JJson::array();
    for (const JPFootprint& x : footprints) f.push(x.toJson());
    for (const JPPackage& x : packages) k.push(x.toJson());
    for (const JPPart& x : parts) p.push(x.toJson());
    j["footprints"] = std::move(f);
    j["packages"]   = std::move(k);
    j["parts"]      = std::move(p);
    return j;
}

bool JPPartsStore::fromJson(const JJson& j, JPPartsStore& out, std::string& error) {
    if (!j.isObject()) {
        error = "not a JSON object";
        return false;
    }
    if (const int v = int(j["version"].number()); v != 1) {
        error = "written by another version of jplacer (format " + std::to_string(v) + ")";
        return false;
    }
    JPPartsStore s;
    for (const JJson& x : j["footprints"].arr()) s.footprints.push_back(JPFootprint::fromJson(x));
    for (const JJson& x : j["packages"].arr()) s.packages.push_back(JPPackage::fromJson(x));
    for (const JJson& x : j["parts"].arr()) s.parts.push_back(JPPart::fromJson(x));
    out = std::move(s);
    return true;
}

} // inline namespace jf
