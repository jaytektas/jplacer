// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCplBomImport.h"

#include "JPDesignators.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>

inline namespace jf {

namespace {

using I = JPImportField::Id;

// The part's fields, in the order a board part keeps them.
const std::vector<I> kPartFields { I::Value, I::Footprint, I::Package, I::Description, I::Manufacturer, I::Mpn,
                                   I::Supplier, I::SupplierPn, I::Height, I::Datasheet, I::Quantity };

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

bool same(const std::string& a, const std::string& b) { return upper(trim(a)) == upper(trim(b)); }

// A fiducial by OpenPnP's rule: a designator FIDn or REFn.
bool fiducial(const std::string& designator) {
    const std::string u = upper(designator);
    return (u.rfind("FID", 0) == 0 || u.rfind("REF", 0) == 0) && u.size() > 3 && std::isdigit(uint8_t(u[3]));
}

bool number(std::string s, double& v) {
    s = trim(s);
    if (s.find('.') == std::string::npos)
        for (char& c : s)
            if (c == ',') c = '.';
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return !s.empty() && end && *end == '\0';
}

// The supplier a supplier PN column's header names, where it names one ("LCSC Part #": LCSC).
std::string supplierOf(const std::string& header) {
    const std::string n = JPImportField::normalise(header);
    for (const char* s : { "LCSC", "JLCPCB", "DIGIKEY", "MOUSER", "FARNELL", "ARROW", "TME", "RS" })
        if (n.rfind(s, 0) == 0) {
            if (std::string(s) == "DIGIKEY") return "Digi-Key";
            if (std::string(s) == "JLCPCB") return "JLCPCB";
            if (std::string(s) == "LCSC") return "LCSC";
            std::string name = s;
            for (size_t i = 1; i < name.size(); ++i) name[i] = char(std::tolower(uint8_t(name[i])));
            return name;
        }
    return "";
}

} // namespace

bool JPCplBomImport::doNotPlace(const std::string& header, const std::string& cell) {
    const std::string v = JPImportField::normalise(cell);
    const std::string h = JPImportField::normalise(header);
    const bool says = h == "POPULATE" || h == "FITTED" || h == "MOUNT" || h == "MOUNTED" || h == "PLACE";
    if (says) return v == "N" || v == "NO" || v == "FALSE" || v == "0" || v == "DNP" || v == "DNF" || v == "NOTFITTED"
                     || v == "NF" || v == "NOFIT";
    return !v.empty() && v != "N" && v != "NO" && v != "FALSE" && v != "0";
}

bool JPCplBomImport::bottom(const std::string& cell) {
    const std::string v = upper(trim(cell));
    return !v.empty() && (v[0] == 'B' || v == "Y" || v == "YES" || v == "MIRROR" || v == "MIRRORED");
}

int JPCplBomImport::winner(JPImportField::Id field) const {
    if (const auto w = winners.find(field); w != winners.end() && w->second >= 0 && size_t(w->second) < sources.size())
        return w->second;
    if (!JPImportField::isPlacement(field))
        for (size_t i = 1; i < sources.size(); ++i)
            if (sources[i].column(field) >= 0) return int(i);
    return 0;
}

std::map<std::string, std::vector<int>> JPCplBomImport::join(Report& report) const {
    std::map<std::string, std::vector<int>> rows;
    if (sources.empty()) return rows;
    const size_t n = sources.size();
    const JPImportSource& cpl = sources[0];
    for (size_t r = 0; r < cpl.table.rows.size(); ++r) {
        const std::string d = trim(cpl.cell(cpl.table.rows[r], I::Designator));
        if (d.empty()) continue;
        auto& at = rows[d];
        if (at.empty()) at.assign(n, -1);
        if (at[0] >= 0) report.problems.push_back("Placement file: " + d + " is in it twice; the first is used");
        else at[0] = int(r);
    }
    for (size_t s = 1; s < n; ++s) {
        const JPImportSource& src = sources[s];
        const std::string name = std::filesystem::path(src.table.path).filename().string();
        if (src.column(I::Designator) < 0) {
            report.problems.push_back(name + ": no Designator column, so it cannot be joined to the placements");
            continue;
        }
        for (size_t r = 0; r < src.table.rows.size(); ++r)
            for (const std::string& d : JPDesignators::expand(src.cell(src.table.rows[r], I::Designator))) {
                auto& at = rows[d];
                if (at.empty()) at.assign(n, -1);
                if (at[s] >= 0) report.problems.push_back(name + ": " + d + " is on two lines; the first is used");
                else at[s] = int(r);
            }
    }
    report.cplOnly.clear();
    report.otherOnly.clear();
    bool others = false;
    for (size_t s = 1; s < n; ++s) others = others || sources[s].column(I::Designator) >= 0;
    for (const auto& [d, at] : rows) {
        const bool inOther = std::any_of(at.begin() + 1, at.end(), [](int r) { return r >= 0; });
        if (at[0] < 0) report.otherOnly.push_back(d);
        else if (others && !inOther && !fiducial(d)) report.cplOnly.push_back(d);
    }
    return rows;
}

std::string JPCplBomImport::value(const std::map<std::string, std::vector<int>>& rows, const std::string& designator,
                                  JPImportField::Id field, Report* report) const {
    const auto it = rows.find(designator);
    if (it == rows.end()) return "";
    std::vector<std::pair<int, std::string>> said;
    for (size_t s = 0; s < sources.size(); ++s) {
        const int r = it->second[s];
        if (r < 0) continue;
        const std::string v = trim(sources[s].cell(sources[s].table.rows[size_t(r)], field));
        if (!v.empty()) said.emplace_back(int(s), v);
    }
    if (said.empty()) return "";
    if (report && said.size() > 1
        && std::any_of(said.begin() + 1, said.end(), [&said](const auto& p) { return !same(p.second, said[0].second); }))
        report->conflicts.push_back({ field, designator, said });
    const int w = winner(field);
    for (const auto& [s, v] : said)
        if (s == w) return v;
    return said.front().second;
}

JPCplBomImport::Report JPCplBomImport::check() const {
    Report report;
    const auto rows = join(report);
    for (const auto& [d, at] : rows) {
        if (at.empty() || at[0] < 0) continue;
        ++report.placements;
        for (const I f : kPartFields) value(rows, d, f, &report);
    }
    return report;
}

bool JPCplBomImport::build(const JPConfiguration& config, const std::string& when, JPBoard& out, Report& report,
                           std::string& error) const {
    if (sources.empty() || sources.front().role != JPImportSource::Role::Cpl) {
        error = "Choose the placement file (CPL) first";
        return false;
    }
    const JPImportSource& cpl = sources[0];
    for (const I need : { I::Designator, I::X, I::Y })
        if (cpl.column(need) < 0) {
            error = std::string("The placement file has no ") + JPImportField::label(need) + " column: choose which it is";
            return false;
        }
    report = Report();
    const auto rows = join(report);

    // The extras: every column kept as one, by its header, from whichever file has it.
    struct ExtraColumn { size_t source; int column; std::string key; };
    std::vector<ExtraColumn> extras;
    for (size_t s = 0; s < sources.size(); ++s)
        for (size_t c = 0; c < sources[s].mapping.size(); ++c)
            if (sources[s].mapping[c] == I::Extra && !sources[s].table.header[c].empty())
                extras.push_back({ s, int(c), "extra:" + sources[s].table.header[c] });
    // A supplier PN column naming its supplier ("LCSC Part #"), where no column gives the supplier.
    std::string impliedSupplier;
    for (const JPImportSource& s : sources)
        if (const int c = s.column(I::SupplierPn); c >= 0 && s.column(I::Supplier) < 0 && impliedSupplier.empty())
            impliedSupplier = supplierOf(s.table.header[size_t(c)]);

    std::map<std::string, std::string> keyOfGroup;   // a board part's grouping → its key
    for (const auto& [d, at] : rows) {
        if (at.empty() || at[0] < 0) continue;
        const auto& row = cpl.table.rows[size_t(at[0])];
        double x = 0, y = 0, rotation = 0;
        if (!JPImportSource::length(cpl.cell(row, I::X), cpl.units, x) || !JPImportSource::length(cpl.cell(row, I::Y), cpl.units, y)) {
            report.problems.push_back(d + ": its X or Y is not a number; passed over");
            continue;
        }
        if (cpl.column(I::Rotation) >= 0 && !trim(cpl.cell(row, I::Rotation)).empty()
            && !number(cpl.cell(row, I::Rotation), rotation))
            report.problems.push_back(d + ": its rotation is not a number; 0 taken");
        JPPlacement p;
        p.id = d;
        p.location = JPLocation(JPLengthUnit::Millimeters, x, y, 0, rotation);
        p.side = bottom(cpl.cell(row, I::Side)) ? JPSide::Bottom : JPSide::Top;
        if (fiducial(d)) p.type = JPPlacement::Type::Fiducial;

        // The part's fields: each from its winning file (another's where that says nothing).
        std::map<std::string, std::string> fields;
        for (const I f : kPartFields)
            if (std::string v = value(rows, d, f, &report); !v.empty()) fields[JPImportField::key(f)] = v;
        if (!impliedSupplier.empty() && fields.count("supplierPn") && !fields.count("supplier")) fields["supplier"] = impliedSupplier;
        for (const ExtraColumn& e : extras) {
            const int r = at[e.source];
            if (r < 0) continue;
            const std::string v = trim(sources[e.source].table.rows[size_t(r)][size_t(e.column)]);
            if (!v.empty() && !fields.count(e.key)) fields[e.key] = v;
        }
        // Not placed: a do-not-place column says so (any file's), or the value is "DNP".
        bool dnp = false;
        for (size_t s = 0; s < sources.size(); ++s)
            if (const int c = sources[s].column(I::DoNotPlace); c >= 0 && at[s] >= 0)
                dnp = dnp || doNotPlace(sources[s].table.header[size_t(c)], sources[s].table.rows[size_t(at[s])][size_t(c)]);
        if (const auto v = fields.find("value"); v != fields.end() && JPImportField::normalise(v->second) == "DNP") dnp = true;
        if (dnp) {
            p.enabled = false;
            ++report.doNotPlace;
        }

        // Its board part: one per manufacturer + MPN, else per value + footprint; none when no file names one.
        const std::string val = fields.count("value") ? fields["value"] : "";
        const std::string footprint = fields.count("footprint") ? fields["footprint"]
                                    : fields.count("package")   ? fields["package"] : "";
        const std::string mpn = fields.count("mpn") ? fields["mpn"] : "";
        if (val.empty() && footprint.empty() && mpn.empty()) {
            if (p.type == JPPlacement::Type::Fiducial) ++report.fiducials;
            else ++report.noPart;
            out.placements.push_back(p);
            ++report.placements;
            continue;
        }
        const std::string group = !mpn.empty() ? "mpn:" + upper(fields.count("manufacturer") ? fields["manufacturer"] : "") + "|" + upper(mpn)
                                               : "vf:" + val + "|" + footprint;
        std::string& key = keyOfGroup[group];
        if (key.empty()) {
            JPBoardPart bp;
            bp.key = key = out.newPartKey();
            bp.fields = fields;
            const std::string name = !footprint.empty() && !val.empty() ? footprint + "-" + val
                                   : !val.empty() ? val : !mpn.empty() ? mpn : footprint;
            bp.fields["part"] = !mpn.empty() ? mpn : name;
            // The library's part, by the names it would have: the MPN, OpenPnP's footprint-value, the value.
            const JPPart* found = nullptr;
            for (const std::string& id : { mpn, footprint.empty() || val.empty() ? std::string() : footprint + "-" + val, val })
                if (!found && !id.empty()) found = config.libraryPart(id);
            if (found) {
                bp.state = JPBoardPart::State::Matched;
                bp.libraryPartId = found->id;
                ++report.matched;
            } else if (createMissing && !bp.fields["part"].empty()) {
                bp.state = JPBoardPart::State::Local;
                bp.localPart = std::make_shared<JPPart>();
                bp.localPart->id = bp.fields["part"];
                if (const auto h = fields.find("height"); h != fields.end()) {
                    double mm = 0;
                    if (JPImportSource::length(h->second, cpl.units, mm)) bp.localPart->height = JPLength(mm, JPLengthUnit::Millimeters);
                }
                if (const JPPackage* k = config.libraryPackage(footprint)) {
                    bp.localPart->packageId = k->id;
                } else if (!footprint.empty()) {
                    bp.localPackage = std::make_shared<JPPackage>();
                    bp.localPackage->id = footprint;
                    bp.localPart->packageId = footprint;
                }
                ++report.local;
            } else {
                ++report.unmatched;
            }
            out.parts().push_back(bp);
            ++report.parts;
        }
        p.boardPart = key;
        p.partId = out.part(key)->partId();
        out.placements.push_back(p);
        ++report.placements;
    }
    for (const JPImportSource& s : sources) out.provenance.push_back(s.provenance(when));
    return true;
}

} // inline namespace jf
