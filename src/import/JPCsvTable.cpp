// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCsvTable.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

inline namespace jf {

namespace {

using Column = JPCsvTable::Column;
using Heading = JPCsvTable::Heading;

constexpr double kMilPerMm  = 1000.0 / 25.4;
constexpr double kInchPerMm = 1.0 / 25.4;

std::string lower(const std::string& s) { return JPCsvTable::lower(s); }
std::string trim(const std::string& s) { return JPCsvTable::trim(s); }

// One CSV record: fields split on `sep`, double quotes around a field (with
// "" for a quote inside).
std::vector<std::string> splitRecord(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
            else if (c == '"') quoted = false;
            else field += c;
        } else if (c == '"') {
            quoted = true;
        } else if (c == sep) {
            out.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    out.push_back(trim(field));
    return out;
}


Heading classify(const std::string& raw) {
    std::string h = lower(trim(raw));
    Heading out;
    if (h.find("(mil)") != std::string::npos || h.find("[mil]") != std::string::npos) out.unitsPerMm = kMilPerMm;
    if (h.find("(mm)") != std::string::npos || h.find("[mm]") != std::string::npos) out.unitsPerMm = 1;
    if (h.find("(in)") != std::string::npos || h.find("(inch)") != std::string::npos) out.unitsPerMm = kInchPerMm;
    if (const size_t p = h.find_first_of("(["); p != std::string::npos) h = trim(h.substr(0, p));
    auto is = [&h](std::initializer_list<const char*> names) {
        return std::any_of(names.begin(), names.end(), [&h](const char* n) { return h == n; });
    };
    // A part's centre is what a placer wants; a footprint's reference point or
    // pad 1 is a second choice.
    if (is({ "designator", "ref", "refdes", "reference", "ref des", "part", "name" }))
        out.column = Column::Designator, out.rank = h == "name" ? 1 : 2;
    else if (is({ "mid x", "center-x", "center x", "centerx", "posx", "pos x", "x" }))
        out.column = Column::X, out.rank = 3;
    else if (is({ "mid y", "center-y", "center y", "centery", "posy", "pos y", "y" }))
        out.column = Column::Y, out.rank = 3;
    else if (is({ "ref x", "ref-x", "refx" }))
        out.column = Column::X, out.rank = 2;
    else if (is({ "ref y", "ref-y", "refy" }))
        out.column = Column::Y, out.rank = 2;
    else if (is({ "pad x", "pad-x" }))
        out.column = Column::PadX, out.rank = 1;
    else if (is({ "pad y", "pad-y" }))
        out.column = Column::PadY, out.rank = 1;
    else if (is({ "rotation", "rot", "angle", "orientation" }))
        out.column = Column::Rotation, out.rank = 1;
    else if (is({ "layer", "side", "tb", "top/bottom" }))
        out.column = Column::Side, out.rank = 1;
    else if (is({ "supplier footprint", "supplier package" }))
        out.column = Column::SupplierPackage, out.rank = 1;
    else if (is({ "supplier part", "supplier part number", "supplier pn", "supplier #" }))
        out.column = Column::SupplierNumber, out.rank = 2;
    else if (is({ "lcsc", "lcsc part", "lcsc part #", "lcsc part number", "lcsc#", "jlcpcb part #", "jlcpcb part" }))
        out.column = Column::SupplierNumber, out.rank = 1, out.supplier = "LCSC";
    else if (is({ "digikey", "digi-key", "digikey part number", "digi-key part number", "digikey pn" }))
        out.column = Column::SupplierNumber, out.rank = 1, out.supplier = "Digi-Key";
    else if (is({ "mouser", "mouser part number", "mouser pn" }))
        out.column = Column::SupplierNumber, out.rank = 1, out.supplier = "Mouser";
    else if (is({ "supplier", "supplier name", "vendor" }))
        out.column = Column::Supplier, out.rank = 1;
    else if (is({ "manufacturer part", "manufacturer part number", "mpn", "mfr part", "mfr. part #", "mfr part number",
                  "part number" }))
        out.column = Column::Mpn, out.rank = 1;
    else if (is({ "manufacturer", "mfr", "mfr.", "manufacturer name" }))
        out.column = Column::Manufacturer, out.rank = 1;
    else if (is({ "tolerance" }))
        out.column = Column::Tolerance, out.rank = 1;
    else if (is({ "voltage", "voltage rating", "rated voltage" }))
        out.column = Column::Voltage, out.rank = 1;
    else if (is({ "power", "power rating", "rated power" }))
        out.column = Column::Power, out.rank = 1;
    else if (is({ "dielectric", "temperature coefficient", "tempco" }))
        out.column = Column::Dielectric, out.rank = 1;
    else if (is({ "temperature", "operating temperature", "temperature range" }))
        out.column = Column::Temperature, out.rank = 1;
    else if (is({ "smd" }))
        out.column = Column::Smd, out.rank = 1;
    else if (is({ "mounting style", "mounting", "mount", "type" }))
        out.column = Column::Mounting, out.rank = 1;
    else if (is({ "add into bom", "populate", "fitted", "fit" }))
        out.column = Column::Fitted, out.rank = 1;
    else if (is({ "dnp", "do not place", "do not populate", "dnf" }))
        out.column = Column::DoNotPlace, out.rank = 1;
    else if (is({ "pins", "pin count", "pad count" }))
        out.column = Column::Pins, out.rank = 1;
    else if (is({ "description", "desc" }))
        out.column = Column::Description, out.rank = 1;
    else if (is({ "device" }))
        out.column = Column::Device, out.rank = 1;
    else if (is({ "datasheet" }))
        out.column = Column::Datasheet, out.rank = 1;
    else if (is({ "unique id", "uuid" }))
        out.column = Column::CadId, out.rank = 1;
    else if (is({ "footprint", "package", "pattern" }))
        out.column = Column::Footprint, out.rank = h == "footprint" ? 2 : 1;
    else if (is({ "value", "val", "comment" }))
        out.column = Column::Value, out.rank = h == "comment" ? 1 : 2;
    return out;
}

// From an SMD column (Yes / No) or a mounting column (SMD, THT, Through Hole).
JPPlacement::Mounting mountingOf(const std::string& smd, const std::string& mounting) {
    if (JPCsvTable::isYes(smd)) return JPPlacement::Mounting::Smd;
    if (JPCsvTable::isNo(smd)) return JPPlacement::Mounting::ThroughHole;
    const std::string m = lower(trim(mounting));
    if (m.find("smd") != std::string::npos || m.find("smt") != std::string::npos || m.find("surface") != std::string::npos)
        return JPPlacement::Mounting::Smd;
    if (m.find("tht") != std::string::npos || m.find("through") != std::string::npos || m == "th" || m == "dip")
        return JPPlacement::Mounting::ThroughHole;
    return JPPlacement::Mounting::Unknown;
}

} // namespace

std::string JPCsvTable::lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string JPCsvTable::trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool JPCsvTable::length(const std::string& text, double headingUnitsPerMm, double& mm) {
    const std::string t = trim(text);
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return false;
    const std::string unit = lower(trim(std::string(end)));
    double perMm = headingUnitsPerMm > 0 ? headingUnitsPerMm : 1;
    if (unit == "mm") perMm = 1;
    else if (unit == "mil") perMm = kMilPerMm;
    else if (unit == "in" || unit == "inch" || unit == "\"") perMm = kInchPerMm;
    else if (!unit.empty()) return false;
    mm = v / perMm;
    return true;
}

bool JPCsvTable::sideOf(const std::string& text, JPPlacement::Side& side) {
    const std::string t = lower(trim(text));
    if (t == "t" || t == "top" || t == "f.cu" || t == "toplayer" || t == "f") { side = JPPlacement::Side::Top; return true; }
    if (t == "b" || t == "bottom" || t == "b.cu" || t == "bottomlayer") { side = JPPlacement::Side::Bottom; return true; }
    return false;
}

bool JPCsvTable::isYes(const std::string& text) {
    const std::string t = lower(trim(text));
    return t == "yes" || t == "y" || t == "true" || t == "1" || t == "x" || t == "dnp";
}

bool JPCsvTable::isNo(const std::string& text) {
    const std::string t = lower(trim(text));
    return t == "no" || t == "n" || t == "false" || t == "0";
}

bool JPCsvTable::parse(const std::string& textIn, const std::function<bool(const JPCsvTable&)>& isHeading, std::string& error) {
    std::string text = textIn;
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);   // UTF-8 byte-order mark
    std::vector<std::string> lines;
    std::vector<int> numbers;
    {
        std::string line;
        std::istringstream is(text);
        int n = 0;
        while (std::getline(is, line)) {
            ++n;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!trim(line).empty()) {
                lines.push_back(line);
                numbers.push_back(n);
            }
        }
    }
    // The heading row: the first `isHeading` accepts, with whichever
    // separator makes it so (tools write comma, semicolon or tab).
    for (size_t i = 0; i < lines.size(); ++i)
        for (const char sep : { ',', ';', '\t' }) {
            m_names = splitRecord(lines[i], sep);
            m_heads.clear();
            for (const std::string& f : m_names) m_heads.push_back(classify(f));
            std::fill(std::begin(m_col), std::end(m_col), -1);
            m_numberCols.clear();
            for (size_t c = 0; c < m_heads.size(); ++c) {
                const int k = int(m_heads[c].column);
                if (m_heads[c].column == Column::Other) continue;
                if (m_heads[c].column == Column::SupplierNumber) m_numberCols.push_back(c);
                if (m_col[k] < 0 || m_heads[c].rank > m_heads[size_t(m_col[k])].rank) m_col[k] = int(c);
            }
            if (!isHeading(*this)) continue;
            m_records.clear();
            m_lines.clear();
            for (size_t r = i + 1; r < lines.size(); ++r) {
                m_records.push_back(splitRecord(lines[r], sep));
                m_lines.push_back(numbers[r]);
            }
            return true;
        }
    error = "no heading row naming the columns needed";
    return false;
}

std::string JPCsvTable::field(const Record& r, Column c) const {
    const int i = m_col[int(c)];
    return i >= 0 && size_t(i) < r.size() ? r[size_t(i)] : std::string();
}

void JPCsvTable::partFields(const Record& rec, JPPlacement& p) const {
    for (const size_t c : m_numberCols) {
        const std::string number = c < rec.size() ? rec[c] : std::string();
        if (number.empty()) continue;
        JPSupplierNumber n { m_heads[c].supplier.empty() ? field(rec, Column::Supplier) : m_heads[c].supplier, number };
        if (std::find(p.supplierNumbers.begin(), p.supplierNumbers.end(), n) == p.supplierNumbers.end())
            p.supplierNumbers.push_back(std::move(n));
    }
    p.mpn             = field(rec, Column::Mpn);
    p.manufacturer    = field(rec, Column::Manufacturer);
    p.value           = field(rec, Column::Value);
    p.tolerance       = field(rec, Column::Tolerance);
    p.voltage         = field(rec, Column::Voltage);
    p.power           = field(rec, Column::Power);
    p.dielectric      = field(rec, Column::Dielectric);
    p.temperature     = field(rec, Column::Temperature);
    p.mounting        = mountingOf(field(rec, Column::Smd), field(rec, Column::Mounting));
    p.doNotPlace      = isNo(field(rec, Column::Fitted)) || isYes(field(rec, Column::DoNotPlace));
    p.footprint       = field(rec, Column::Footprint);
    p.supplierPackage = field(rec, Column::SupplierPackage);
    p.pins            = std::atoi(field(rec, Column::Pins).c_str());
    p.description     = field(rec, Column::Description);
    p.device          = field(rec, Column::Device);
    p.datasheet       = field(rec, Column::Datasheet);
    p.cadId           = field(rec, Column::CadId);
    for (size_t c = 0; c < m_heads.size() && c < rec.size(); ++c)
        if (m_heads[c].column == Column::Other && !rec[c].empty()) p.other.emplace_back(trim(m_names[c]), rec[c]);
}

} // inline namespace jf
