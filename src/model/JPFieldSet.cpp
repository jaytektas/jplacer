// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFieldSet.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

inline namespace jf {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && uint8_t(s[b]) <= ' ') ++b;
    while (e > b && uint8_t(s[e - 1]) <= ' ') --e;
    return s.substr(b, e - b);
}

// A built-in: its key, its name, whether it is required, the import's name for what it reads (none: not
// imported), the headings it is matched by, and what it is.
JPField builtIn(const char* key, const char* name, bool required, const char* importAs = "",
                std::vector<std::string> matches = {}, const char* about = "") {
    JPField f;
    f.key = key;
    f.name = name;
    f.builtIn = true;
    f.required = required;
    f.importAs = importAs;
    f.matches = std::move(matches);
    f.about = about;
    return f;
}

// The built-ins of each kind, in its tables' order (the headings an import's columns are matched by as the
// CAD tools and suppliers name them).
std::vector<JPField> builtIns(JPFieldSet::Of of) {
    using O = JPFieldSet::Of;
    switch (of) {
        case O::Parts:
            return {
                builtIn("source", "Source", false, "", {}, "Where it lives: the library, or the open board named (its icon)"),
                builtIn("id", "ID", true, "", {}, "What placements, feeders and jobs know it by"),
                builtIn("name", "Name", false, "name", { "PARTNAME", "LIBREF", "LIBRARYREF", "LIBRARYREFERENCE", "LIBRARYPART" },
                        "What it is called where it is shown"),
                builtIn("description", "Description", false, "description", { "DESCRIPTION", "DESC", "DESCR", "PARTDESCRIPTION" }),
                builtIn("value", "Value", false, "value", { "VALUE", "VAL", "COMMENT", "COMPVALUE", "PARTVALUE" }),
                builtIn("mpn", "MPN", false, "mpn",
                        { "MPN", "MFRPN", "MFRPART", "MFRPARTNUMBER", "MFGPN", "MFGPARTNUMBER", "MANUFACTURERPN",
                          "MANUFACTURERPART", "MANUFACTURERPARTNUMBER", "PARTNUMBER", "PN", "MANUFACTURERPARTNUMBER1" }),
                builtIn("height", "Height", true, "height", { "HEIGHT", "PARTHEIGHT", "COMPONENTHEIGHT" }),
                builtIn("depth", "Through-Board Depth", false),
                builtIn("package", "Package", true, "package", { "PACKAGE", "CASE", "CASEPACKAGE", "PACKAGECASE" }),
                builtIn("speed", "Speed %", false),
                builtIn("bottomVision", "BottomVision", false),
                builtIn("fiducialVision", "FiducialVision", false),
                builtIn("placements", "Placements", false, "", {}, "How many placements use it"),
                builtIn("feeders", "Feeders", false, "", {}, "How many feeders hold it"),
                builtIn("packaging", "Packaging", false),
                builtIn("suppliers", "Suppliers", false),
                builtIn("manufacturer", "Manufacturer", false, "manufacturer",
                        { "MANUFACTURER", "MFR", "MFG", "MANUFACTURERNAME", "MAKER", "BRAND", "MANUFACTURER1" }),
                builtIn("stock", "Stock", false),
                builtIn("datasheet", "Datasheet", false, "datasheet", { "DATASHEET", "DATASHEETURL", "DATASHEETLINK" }),
                // Read by an import, shown by no table: what the part is matched and made by.
                builtIn("footprint", "Footprint", false, "footprint",
                        { "FOOTPRINT", "PATTERN", "COMPPACKAGE", "LANDPATTERN", "PCBFOOTPRINT", "FOOTPRINTNAME" },
                        "Its CAD footprint, as the files name it: what its package is found or made by"),
                builtIn("supplier", "Supplier", false, "supplier", { "SUPPLIER", "DISTRIBUTOR", "VENDOR", "SUPPLIER1" },
                        "Who sells it, for its offer"),
                builtIn("supplierPn", "Supplier PN", false, "supplierPn",
                        { "SUPPLIERPN", "SUPPLIERPART", "SUPPLIERPARTNUMBER", "LCSC", "LCSCPART", "LCSCPARTNUMBER", "LCSCPARTNO",
                          "JLCPCBPART", "JLCPCBPARTNUMBER", "DIGIKEY", "DIGIKEYPN", "DIGIKEYPARTNUMBER", "MOUSER", "MOUSERPN",
                          "MOUSERPARTNUMBER", "FARNELL", "FARNELLPN", "SKU", "ORDERCODE", "DISTRIBUTORPN",
                          "DISTRIBUTORPARTNUMBER", "SUPPLIERPARTNUMBER1" },
                        "The supplier's number for it, for its offer"),
                builtIn("quantity", "Quantity", false, "quantity", { "QUANTITY", "QTY", "QTYPERBOARD", "COUNT", "AMOUNT" },
                        "How many a board uses, as a BOM says"),
            };
        case O::Packages:
            return {
                builtIn("source", "Source", false, "", {}, "Where it lives: the library, or the open board named (its icon)"),
                builtIn("id", "ID", true, "", {}, "What parts know it by"),
                builtIn("name", "Name", false, "", {}, "What it is called where it is shown"),
                builtIn("description", "Description", false),
                builtIn("tape", "Tape Specification", false),
                builtIn("bottomVision", "BottomVision", false),
                builtIn("fiducialVision", "FiducialVision", false),
            };
        case O::Pads:
            return {
                builtIn("name", "Name", true), builtIn("mark", "Mark", false), builtIn("x", "X", true), builtIn("y", "Y", true),
                builtIn("width", "Width", true), builtIn("length", "Length", true), builtIn("rotation", "Rot.", true),
                builtIn("round", "% Round", false),
            };
        case O::Placements:
            return {
                builtIn("enabled", "Enabled", false),
                builtIn("id", "ID", true, "designator",
                        { "DESIGNATOR", "DESIGNATORS", "REF", "REFS", "REFDES", "REFERENCE", "REFERENCES", "COMPONENT",
                          "PARTREFERENCE" },
                        "Its reference designator (R1, C12)"),
                builtIn("part", "Part", true),
                builtIn("side", "Side", true, "side", { "SIDE", "LAYER", "TB", "SYMMIRROR", "MIRROR", "TOPBOTTOM", "BOARDSIDE" }),
                builtIn("x", "X", true, "x", { "X", "POSX", "MIDX", "CENTERX", "CENTREX", "REFX", "SYMX", "LOCATIONX", "CX" }),
                builtIn("y", "Y", true, "y", { "Y", "POSY", "MIDY", "CENTERY", "CENTREY", "REFY", "SYMY", "LOCATIONY", "CY" }),
                builtIn("rotation", "Rot.", true, "rotation", { "ROTATION", "ROT", "ROTATE", "SYMROTATE", "ANGLE", "ORIENTATION" }),
                builtIn("type", "Type", false),
                builtIn("placed", "Placed", false),
                builtIn("status", "Status", false),
                builtIn("errorHandling", "Error Handling", false),
                builtIn("rank", "Rank", false),
                builtIn("comments", "Comments", false),
                builtIn("verified", "Verified", false),
                // Read by an import, shown by no table: whether it is placed at all (Enabled).
                builtIn("doNotPlace", "Do Not Place", false, "doNotPlace",
                        { "DNP", "DNF", "DONOTPLACE", "DONOTPOPULATE", "DONOTFIT", "NOPLACE", "POPULATE", "FITTED", "MOUNT",
                          "MOUNTED", "PLACE" },
                        "Whether the part is fitted: DNP, or a Populate / Fitted column's no (its Enabled)"),
            };
        case O::Locations:
            return {
                builtIn("id", "Board/Panel Id", true), builtIn("name", "Name", false), builtIn("width", "Width", false),
                builtIn("length", "Length", false), builtIn("side", "Side", true), builtIn("x", "X", true), builtIn("y", "Y", true),
                builtIn("z", "Z", true), builtIn("rotation", "Rot.", true), builtIn("enabled", "Enabled?", true),
                builtIn("checkFids", "Check Fids?", false),
            };
        case O::Boards:
            return { builtIn("name", "Board Name", true), builtIn("width", "Width", false), builtIn("length", "Length", false) };
        case O::Panels:
            return { builtIn("name", "Panel Name", true), builtIn("width", "Width", false), builtIn("length", "Length", false) };
        case O::Feeders:
            return {
                builtIn("id", "ID", true, "", {}, "What the machine, the stock and the runs know it by"),
                builtIn("name", "Name", true), builtIn("part", "Part", true), builtIn("type", "Type", true),
                builtIn("priority", "Priority", false), builtIn("faults", "Faults", false), builtIn("enabled", "Enabled", true),
                builtIn("feed", "Feed", false),
            };
        case O::Vision:
            return { builtIn("name", "Name", true), builtIn("assignedTo", "Assigned To", false) };
        case O::Job:
            return {};
    }
    return {};
}

} // namespace

const std::vector<JPFieldSet::Of>& JPFieldSet::kinds() {
    static const std::vector<Of> v { Of::Parts, Of::Packages, Of::Pads, Of::Placements, Of::Locations, Of::Boards,
                                     Of::Panels, Of::Feeders, Of::Vision, Of::Job };
    return v;
}

const char* JPFieldSet::key(Of of) {
    switch (of) {
        case Of::Parts:      return "parts";
        case Of::Packages:   return "packages";
        case Of::Pads:       return "pads";
        case Of::Placements: return "placements";
        case Of::Locations:  return "locations";
        case Of::Boards:     return "boards";
        case Of::Panels:     return "panels";
        case Of::Feeders:    return "feeders";
        case Of::Vision:     return "vision";
        case Of::Job:        return "job";
    }
    return "";
}

const char* JPFieldSet::label(Of of) {
    switch (of) {
        case Of::Parts:      return "Part";
        case Of::Packages:   return "Package";
        case Of::Pads:       return "Pad";
        case Of::Placements: return "Placement";
        case Of::Locations:  return "Board Location";
        case Of::Boards:     return "Board";
        case Of::Panels:     return "Panel";
        case Of::Feeders:    return "Feeder";
        case Of::Vision:     return "Vision Settings";
        case Of::Job:        return "Job";
    }
    return "";
}

const char* JPFieldSet::plural(Of of) {
    switch (of) {
        case Of::Parts:      return "Parts";
        case Of::Packages:   return "Packages";
        case Of::Pads:       return "Pads";
        case Of::Placements: return "Placements";
        case Of::Locations:  return "Board Locations";
        case Of::Boards:     return "Boards";
        case Of::Panels:     return "Panels";
        case Of::Feeders:    return "Feeders";
        case Of::Vision:     return "Vision Settings";
        case Of::Job:        return "Jobs";
    }
    return "";
}

JPFieldSet::JPFieldSet(Of of) : m_of(of), m_fields(builtIns(of)), m_builtIns(m_fields) {}

std::vector<const JPField*> JPFieldSet::present() const {
    std::vector<const JPField*> out;
    for (const JPField& f : m_fields)
        if (!f.removed) out.push_back(&f);
    return out;
}

std::vector<const JPField*> JPFieldSet::added() const {
    std::vector<const JPField*> out;
    for (const JPField& f : m_fields)
        if (!f.builtIn && !f.removed) out.push_back(&f);
    return out;
}

const JPField* JPFieldSet::field(const std::string& key) const {
    for (const JPField& f : m_fields)
        if (f.key == key) return &f;
    return nullptr;
}

const JPField* JPFieldSet::named(const std::string& name) const {
    const std::string u = upper(trim(name));
    for (const JPField& f : m_fields)
        if (!f.removed && upper(f.name) == u) return &f;
    return nullptr;
}

bool JPFieldSet::has(const std::string& key) const {
    const JPField* f = field(key);
    return f && !f->removed;
}

bool JPFieldSet::renamed(const std::string& key) const {
    const JPField* f = field(key);
    if (!f || !f->builtIn) return false;
    for (const JPField& b : m_builtIns)
        if (b.key == key) return b.name != f->name;
    return false;
}

bool JPFieldSet::nameFree(const std::string& name, const std::string& key, std::string& error) const {
    const std::string n = trim(name);
    if (n.empty()) {
        error = "A field needs a name.";
        return false;
    }
    if (n.find(':') != std::string::npos) {
        error = "A field's name cannot have a colon in it (a search names a field before one).";
        return false;
    }
    const JPField* other = named(n);
    if (other && other->key != key) {
        error = std::string(plural(m_of)) + " already have a field named " + other->name + ".";
        return false;
    }
    return true;
}

std::string JPFieldSet::add(JPField f) {
    f.key = "u" + std::to_string(m_next++);
    f.name = trim(f.name);
    f.builtIn = f.required = f.removed = false;
    f.importAs.clear();
    m_fields.push_back(std::move(f));
    return m_fields.back().key;
}

bool JPFieldSet::update(const JPField& f, std::string& error) {
    auto it = std::find_if(m_fields.begin(), m_fields.end(), [&f](const JPField& x) { return x.key == f.key; });
    if (it == m_fields.end() || !nameFree(f.name, f.key, error)) return false;
    it->name = trim(f.name);
    it->matches = f.matches;
    if (!it->builtIn) {
        it->type = f.type;
        it->choices = f.choices;
    }
    return true;
}

bool JPFieldSet::remove(const std::string& key) {
    auto it = std::find_if(m_fields.begin(), m_fields.end(), [&key](const JPField& x) { return x.key == key; });
    if (it == m_fields.end() || it->required) return false;
    if (it->builtIn) it->removed = true;
    else m_fields.erase(it);
    return true;
}

void JPFieldSet::restore(const std::string& key) {
    for (JPField& f : m_fields)
        if (f.key == key && f.builtIn) f.removed = false;
}

JJson JPFieldSet::toJson() const {
    JJson j = JJson::object();
    j["next"] = m_next;
    JJson builtIns = JJson::array();
    JJson added = JJson::array();
    for (const JPField& f : m_fields) {
        if (!f.builtIn) {
            added.push(f.toJson());
            continue;
        }
        // A built-in as jplacer has it is not kept.
        const auto shipped = std::find_if(m_builtIns.begin(), m_builtIns.end(), [&f](const JPField& b) { return b.key == f.key; });
        if (shipped != m_builtIns.end() && shipped->name == f.name && shipped->matches == f.matches && !f.removed) continue;
        builtIns.push(f.toJson());
    }
    j["builtIn"] = builtIns;
    j["added"] = added;
    return j;
}

void JPFieldSet::fromJson(const JJson& j) {
    m_fields = m_builtIns;
    m_next = std::max(1, int(j["next"].number(1)));
    for (const JJson& b : j["builtIn"].arr()) {
        const JPField kept = JPField::fromJson(b);
        for (JPField& f : m_fields)
            if (f.key == kept.key) {
                if (!kept.name.empty()) f.name = kept.name;
                f.matches = kept.matches;
                f.removed = kept.removed && !f.required;
            }
    }
    for (const JJson& a : j["added"].arr()) {
        JPField f = JPField::fromJson(a);
        if (f.key.empty() || f.name.empty() || field(f.key)) continue;
        f.builtIn = false;
        m_fields.push_back(std::move(f));
        // Never a key given again, even where the file's count says less.
        if (m_fields.back().key.size() > 1 && m_fields.back().key[0] == 'u')
            m_next = std::max(m_next, std::atoi(m_fields.back().key.c_str() + 1) + 1);
    }
}

} // inline namespace jf
