// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImportField.h"

#include <cctype>
#include <map>

inline namespace jf {

namespace {

struct Info {
    JPImportField::Id        id;
    const char*              label;
    const char*              key;
    std::vector<const char*> names;   // normalised (JPImportField::normalise)
};

const std::vector<Info>& infos() {
    using I = JPImportField::Id;
    static const std::vector<Info> v {
        { I::Designator,   "Designator",    "designator",  { "DESIGNATOR", "DESIGNATORS", "REF", "REFS", "REFDES", "REFERENCE",
                                                             "REFERENCES", "COMPONENT", "PARTREFERENCE" } },
        { I::X,            "X",             "x",           { "X", "POSX", "MIDX", "CENTERX", "CENTREX", "REFX", "SYMX", "LOCATIONX", "CX" } },
        { I::Y,            "Y",             "y",           { "Y", "POSY", "MIDY", "CENTERY", "CENTREY", "REFY", "SYMY", "LOCATIONY", "CY" } },
        { I::Rotation,     "Rotation",      "rotation",    { "ROTATION", "ROT", "ROTATE", "SYMROTATE", "ANGLE", "ORIENTATION" } },
        { I::Side,         "Side",          "side",        { "SIDE", "LAYER", "TB", "SYMMIRROR", "MIRROR", "TOPBOTTOM", "BOARDSIDE" } },
        { I::DoNotPlace,   "Do Not Place",  "doNotPlace",  { "DNP", "DNF", "DONOTPLACE", "DONOTPOPULATE", "DONOTFIT", "NOPLACE",
                                                             "POPULATE", "FITTED", "MOUNT", "MOUNTED", "PLACE" } },
        { I::Value,        "Value",         "value",       { "VALUE", "VAL", "COMMENT", "COMPVALUE", "PARTVALUE" } },
        { I::Footprint,    "Footprint",     "footprint",   { "FOOTPRINT", "PATTERN", "COMPPACKAGE", "LANDPATTERN", "PCBFOOTPRINT",
                                                             "FOOTPRINTNAME" } },
        { I::Package,      "Package",       "package",     { "PACKAGE", "CASE", "CASEPACKAGE", "PACKAGECASE" } },
        { I::Description,  "Description",   "description", { "DESCRIPTION", "DESC", "DESCR", "PARTDESCRIPTION" } },
        { I::Manufacturer, "Manufacturer",  "manufacturer",{ "MANUFACTURER", "MFR", "MFG", "MANUFACTURERNAME", "MAKER", "BRAND",
                                                             "MANUFACTURER1" } },
        { I::Mpn,          "MPN",           "mpn",         { "MPN", "MFRPN", "MFRPART", "MFRPARTNUMBER", "MFGPN", "MFGPARTNUMBER",
                                                             "MANUFACTURERPN", "MANUFACTURERPART", "MANUFACTURERPARTNUMBER",
                                                             "PARTNUMBER", "PN", "MANUFACTURERPARTNUMBER1" } },
        { I::Supplier,     "Supplier",      "supplier",    { "SUPPLIER", "DISTRIBUTOR", "VENDOR", "SUPPLIER1" } },
        { I::SupplierPn,   "Supplier PN",   "supplierPn",  { "SUPPLIERPN", "SUPPLIERPART", "SUPPLIERPARTNUMBER", "LCSC", "LCSCPART",
                                                             "LCSCPARTNUMBER", "LCSCPARTNO", "JLCPCBPART", "JLCPCBPARTNUMBER",
                                                             "DIGIKEY", "DIGIKEYPN", "DIGIKEYPARTNUMBER", "MOUSER", "MOUSERPN",
                                                             "MOUSERPARTNUMBER", "FARNELL", "FARNELLPN", "SKU", "ORDERCODE",
                                                             "DISTRIBUTORPN", "DISTRIBUTORPARTNUMBER", "SUPPLIERPARTNUMBER1" } },
        { I::Height,       "Height",        "height",      { "HEIGHT", "PARTHEIGHT", "COMPONENTHEIGHT" } },
        { I::Datasheet,    "Datasheet",     "datasheet",   { "DATASHEET", "DATASHEETURL", "DATASHEETLINK" } },
        { I::Quantity,     "Quantity",      "quantity",    { "QUANTITY", "QTY", "QTYPERBOARD", "COUNT", "AMOUNT" } },
        { I::Extra,        "Keep as extra", "extra",       { } },
        { I::Ignore,       "Ignore",        "ignore",      { } },
    };
    return v;
}

const Info& info(JPImportField::Id id) {
    for (const Info& i : infos())
        if (i.id == id) return i;
    return infos().back();
}

bool endsWith(const std::string& s, const std::string& end) {
    return s.size() > end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

} // namespace

const std::vector<JPImportField::Id>& JPImportField::all() {
    static const std::vector<Id> v = [] {
        std::vector<Id> out;
        for (const Info& i : infos()) out.push_back(i.id);
        return out;
    }();
    return v;
}

const char* JPImportField::label(Id id) { return info(id).label; }
const char* JPImportField::key(Id id) { return info(id).key; }

JPImportField::Id JPImportField::fromKey(const std::string& key) {
    for (const Info& i : infos())
        if (key == i.key) return i.id;
    return Id::Ignore;
}

bool JPImportField::isPlacement(Id id) {
    return id == Id::Designator || id == Id::X || id == Id::Y || id == Id::Rotation || id == Id::Side || id == Id::DoNotPlace;
}

std::string JPImportField::normalise(const std::string& header) {
    std::string s;
    for (const unsigned char c : header)
        if (std::isalnum(c)) s += char(std::toupper(c));
        else if (c == '#') s += "NUMBER";   // "Mfr Part #" is "MFRPARTNUMBER"
    // A units suffix ("X(mm)", "Height (mil)", "Y in") names how it is measured, not what.
    for (const char* unit : { "MILS", "MIL", "MM", "INCH", "IN" })
        if (endsWith(s, unit)) {
            const std::string bare = s.substr(0, s.size() - std::string(unit).size());
            // Not when the unit is part of a word ("MOUNTIN" is not MOUNT + IN): only if what is left is known.
            for (const Info& i : infos())
                for (const char* n : i.names)
                    if (bare == n) return bare;
        }
    return s;
}

JPImportField::Id JPImportField::guess(const std::string& header) {
    const std::string n = normalise(header);
    if (n.empty()) return Id::Ignore;
    for (const Info& i : infos())
        for (const char* name : i.names)
            if (n == name) return i.id;
    return Id::Extra;
}

} // inline namespace jf
