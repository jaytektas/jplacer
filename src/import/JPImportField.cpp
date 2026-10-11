// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImportField.h"

#include <algorithm>
#include <cctype>

inline namespace jf {

namespace {

using O = JPFieldSet::Of;

struct Info {
    JPImportField::Id id;
    const char*       name;   // as the import says it
    const char*       key;    // the built-in's (JPField::importAs), and its field's in its kind
    O                 of;
    const char*       field;
};

const std::vector<Info>& infos() {
    using I = JPImportField::Id;
    static const std::vector<Info> v {
        { I::Designator,   "Designator",   "designator",   O::Placements, "id" },
        { I::X,            "X",            "x",            O::Placements, "x" },
        { I::Y,            "Y",            "y",            O::Placements, "y" },
        { I::Rotation,     "Rotation",     "rotation",     O::Placements, "rotation" },
        { I::Side,         "Side",         "side",         O::Placements, "side" },
        { I::DoNotPlace,   "Do Not Place", "doNotPlace",   O::Placements, "doNotPlace" },
        { I::Value,        "Value",        "value",        O::Parts,      "value" },
        { I::Footprint,    "Footprint",    "footprint",    O::Parts,      "footprint" },
        { I::Package,      "Package",      "package",      O::Parts,      "package" },
        { I::Description,  "Description",  "description",  O::Parts,      "description" },
        { I::Manufacturer, "Manufacturer", "manufacturer", O::Parts,      "manufacturer" },
        { I::Mpn,          "MPN",          "mpn",          O::Parts,      "mpn" },
        { I::Supplier,     "Supplier",     "supplier",     O::Parts,      "supplier" },
        { I::SupplierPn,   "Supplier PN",  "supplierPn",   O::Parts,      "supplierPn" },
        { I::Height,       "Height",       "height",       O::Parts,      "height" },
        { I::Datasheet,    "Datasheet",    "datasheet",    O::Parts,      "datasheet" },
        { I::Quantity,     "Quantity",     "quantity",     O::Parts,      "quantity" },
    };
    return v;
}

const Info* info(JPImportField::Id id) {
    for (const Info& i : infos())
        if (i.id == id) return &i;
    return nullptr;
}

// jplacer's own fields as shipped: what a header is known by before any field is added or renamed.
const JPFieldSet& shipped(O of) {
    static const JPFieldSet placements(O::Placements), parts(O::Parts);
    return of == O::Placements ? placements : parts;
}

bool endsWith(const std::string& s, const std::string& end) {
    return s.size() > end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

// A header made comparable, units and all.
std::string bare(const std::string& header) {
    std::string s;
    for (const unsigned char c : header)
        if (std::isalnum(c)) s += char(std::toupper(c));
        else if (c == '#') s += "NUMBER";   // "Mfr Part #" is "MFRPARTNUMBER"
    return s;
}

} // namespace

std::string JPImportField::Target::text() const {
    return key.empty() ? std::string() : std::string(JPFieldSet::key(of)) + ":" + key;
}

JPImportField::Target JPImportField::Target::fromText(const std::string& text) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos) return {};
    const std::string kind = text.substr(0, colon);
    for (const O of : JPFieldSet::kinds())
        if (kind == JPFieldSet::key(of)) return { of, text.substr(colon + 1) };
    return {};
}

const std::vector<JPFieldSet::Of>& JPImportField::kinds() {
    static const std::vector<O> v { O::Placements, O::Parts, O::Packages, O::Boards, O::Job };
    return v;
}

std::vector<JPImportField::Target> JPImportField::offered(const JPFields& fields) {
    std::vector<Target> out;
    for (const O of : kinds())
        for (const JPField* f : fields.of(of).present())
            if (f->imported()) out.push_back({ of, f->key });
    return out;
}

std::string JPImportField::label(const JPFields& fields, const Target& t) {
    if (t.empty()) return "Ignore";
    const JPFieldSet& set = fields.of(t.of);
    const JPField* f = set.field(t.key);
    std::string name = f ? f->name : t.key;
    // jplacer's own, as shipped: by the import's word for it ("Designator", not the table's "ID").
    if (f && f->builtIn && !set.renamed(f->key))
        if (const Info* i = info(idOf(t))) name = i->name;
    return std::string(JPFieldSet::label(t.of)) + " \xE2\x96\xB8 " + name;
}

JPImportField::Target JPImportField::guess(const JPFields& fields, const std::string& header, const std::vector<Target>& taken) {
    if (bare(header).empty()) return {};
    // Fields of your own first: one made for a heading ("LCSC") takes it over jplacer's that also matches it.
    for (const bool own : { true, false })
        for (const Target& t : offered(fields)) {
            if (std::find(taken.begin(), taken.end(), t) != taken.end()) continue;
            const JPField* f = fields.of(t.of).field(t.key);
            if (f && f->builtIn != own && f->matchesHeading(header)) return t;
        }
    return {};
}

JPImportField::Target JPImportField::target(Id id) {
    const Info* i = info(id);
    return i ? Target { i->of, i->field } : Target {};
}

JPImportField::Id JPImportField::idOf(const Target& t) {
    for (const Info& i : infos())
        if (i.of == t.of && t.key == i.field) return i.id;
    return Id::Ignore;
}

const char* JPImportField::name(Id id) {
    const Info* i = info(id);
    return i ? i->name : "Ignore";
}

const char* JPImportField::key(Id id) {
    const Info* i = info(id);
    return i ? i->key : "ignore";
}

bool JPImportField::isPlacement(Id id) {
    const Info* i = info(id);
    return i && i->of == O::Placements;
}

bool JPImportField::known(const std::string& header) {
    for (const O of : { O::Placements, O::Parts })
        for (const JPField* f : shipped(of).present())
            if (!f->importAs.empty() && f->matchesHeading(header)) return true;
    return false;
}

std::string JPImportField::normalise(const std::string& header) {
    const std::string s = bare(header);
    // A units suffix ("X(mm)", "Height (mil)", "Y in") names how it is measured, not what.
    for (const char* unit : { "MILS", "MIL", "MM", "INCH", "IN" })
        if (endsWith(s, unit)) {
            const std::string stripped = s.substr(0, s.size() - std::string(unit).size());
            // Not when the unit is part of a word ("MOUNTIN" is not MOUNT + IN): only if what is left is known.
            if (known(stripped)) return stripped;
        }
    return s;
}

} // inline namespace jf
