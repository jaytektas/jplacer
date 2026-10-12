// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCplBomImport.h"

#include "model/JPLibraryLearning.h"
#include "common/JPUuid.h"
#include "common/JPWhen.h"

#include "JPDesignators.h"

#include "model/JPAngles.h"
#include "model/JPPartMatcher.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>

inline namespace jf {

namespace {

using I = JPImportField::Id;

// The part's fields, in the order a board part keeps them.
const std::vector<I> kPartFields { I::Name, I::Value, I::Footprint, I::Package, I::Description, I::Manufacturer, I::Mpn,
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

int JPCplBomImport::winner(const JPImportField::Target& field) const {
    if (const auto w = winners.find(field.text()); w != winners.end() && w->second >= 0 && size_t(w->second) < sources.size())
        return w->second;
    // The footprint a board file draws, where one does (it is what is placed); else as the other fields.
    if (field == JPImportField::target(I::Footprint))
        for (size_t i = 0; i < sources.size(); ++i)
            if (!sources[i].footprints.empty()) return int(i);
    // A placement's from the placement file where it has it; anything else from another file first.
    if (field.of == JPFieldSet::Of::Placements) {
        if (!sources.empty() && sources[0].column(field) >= 0) return 0;
        for (size_t i = 1; i < sources.size(); ++i)
            if (sources[i].column(field) >= 0) return int(i);
        return 0;
    }
    for (size_t i = 1; i < sources.size(); ++i)
        if (sources[i].column(field) >= 0) return int(i);
    return 0;
}

std::string JPCplBomImport::single(const JPFields& fields, const JPImportField::Target& field) const {
    // A column's: the winner's first row that says something, else any other file's.
    const int w = winner(field);
    for (int pass = 0; pass < 2; ++pass)
        for (size_t s = 0; s < sources.size(); ++s) {
            if ((pass == 0) != (int(s) == w)) continue;
            const int c = sources[s].column(field);
            if (c < 0) continue;
            for (const auto& r : sources[s].table.rows)
                if (size_t(c) < r.size() && !trim(r[size_t(c)]).empty()) return trim(r[size_t(c)]);
        }
    // What a file says of itself, by the field's name or its matches.
    const JPField* f = fields.of(field.of).field(field.key);
    if (!f) return "";
    for (const JPImportSource& s : sources)
        for (const auto& [what, said] : s.about)
            if (f->matchesHeading(what)) return said;
    return "";
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
        else if (others && !inOther && !fiducial(d) && !leftOut(cpl.table.rows[size_t(at[0])])) report.cplOnly.push_back(d);
    }
    return rows;
}

std::string JPCplBomImport::value(const std::map<std::string, std::vector<int>>& rows, const std::string& designator,
                                  const JPImportField::Target& field, Report* report) const {
    const auto it = rows.find(designator);
    if (it == rows.end()) return "";
    std::vector<std::pair<int, std::string>> said;
    for (size_t s = 0; s < sources.size(); ++s) {
        const int r = it->second[s], c = sources[s].column(field);
        if (r < 0 || c < 0) continue;
        const auto& row = sources[s].table.rows[size_t(r)];
        const std::string v = size_t(c) < row.size() ? trim(row[size_t(c)]) : std::string();
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

bool JPCplBomImport::leftOut(const std::vector<std::string>& cplRow) const {
    if (sides == Sides::Both) return false;
    return bottom(sources.front().cell(cplRow, I::Side)) != (sides == Sides::Bottom);
}

JPCplBomImport::Report JPCplBomImport::check() const {
    Report report;
    for (const JPImportSource& s : sources) report.problems.insert(report.problems.end(), s.notes.begin(), s.notes.end());
    const auto rows = join(report);
    for (const auto& [d, at] : rows) {
        if (at.empty() || at[0] < 0) continue;
        if (leftOut(sources.front().table.rows[size_t(at[0])])) {
            ++report.otherSide;
            continue;
        }
        ++report.placements;
        for (const I f : kPartFields) value(rows, d, f, &report);
        for (const JPImportSource& s : sources)
            for (const JPImportField::Target& t : s.mapping)
                if (!t.empty() && JPImportField::idOf(t) == I::Ignore && !JPImportField::single(t.of)) value(rows, d, t, &report);
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
            error = std::string("The placement file has no ") + JPImportField::name(need) + " column: choose which it is";
            return false;
        }
    report = Report();
    for (const JPImportSource& s : sources) report.problems.insert(report.problems.end(), s.notes.begin(), s.notes.end());
    const auto rows = join(report);
    // The footprint a board file (a KiCad board) draws at a placement (its rows in the files).
    auto drawn = [this](const std::vector<int>& at) -> const JPFootprint* {
        for (size_t s = 0; s < sources.size(); ++s) {
            if (sources[s].footprints.empty() || at[s] < 0) continue;
            const auto f = sources[s].footprints.find(sources[s].cell(sources[s].table.rows[size_t(at[s])], I::Footprint));
            if (f != sources[s].footprints.end()) return &f->second;
        }
        return nullptr;
    };
    std::set<std::string> drawnUsed;

    // The fields added that columns fill: a row's (a placement's, a part's, a package's), and the board's.
    const JPFields& defs = config.fields();
    std::vector<JPImportField::Target> added;
    for (const JPImportSource& src : sources)
        for (const JPImportField::Target& t : src.mapping)
            if (!t.empty() && JPImportField::idOf(t) == I::Ignore && defs.of(t.of).has(t.key)
                && std::find(added.begin(), added.end(), t) == added.end())
                added.push_back(t);
    // A value as its field reads it (lengths in the placement file's units where they say none); a note when
    // it cannot, saying whose it was.
    auto read = [&](const JPImportField::Target& t, const std::string& text, const std::string& whose, JJson& v) {
        const JPField* f = defs.of(t.of).field(t.key);
        std::string why;
        if (f && f->parse(text, v, why, cpl.units)) return true;
        report.fieldNotes.push_back(whose + "'s " + JPImportField::label(defs, t) + ": " + why + "; not taken");
        return false;
    };
    // A supplier PN column naming its supplier ("LCSC Part #"), where no column gives the supplier.
    std::string impliedSupplier;
    for (const JPImportSource& s : sources)
        if (const int c = s.column(I::SupplierPn); c >= 0 && s.column(I::Supplier) < 0 && impliedSupplier.empty())
            impliedSupplier = supplierOf(s.table.header[size_t(c)]);

    std::map<std::string, std::string> keyOfGroup;   // a board part's grouping → its key
    for (const auto& [d, at] : rows) {
        if (at.empty() || at[0] < 0) continue;
        const auto& row = cpl.table.rows[size_t(at[0])];
        if (leftOut(row)) {
            ++report.otherSide;
            continue;
        }
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
        p.cadRotation = rotation;   // the CAD's, kept apart from the one placed with
        p.side = bottom(cpl.cell(row, I::Side)) ? JPSide::Bottom : JPSide::Top;
        if (fiducial(d)) p.type = JPPlacement::Type::Fiducial;

        // The part's fields: each from its winning file (another's where that says nothing).
        std::map<std::string, std::string> fields;
        for (const I f : kPartFields)
            if (std::string v = value(rows, d, f, &report); !v.empty()) fields[JPImportField::key(f)] = v;
        if (!impliedSupplier.empty() && fields.count("supplierPn") && !fields.count("supplier")) fields["supplier"] = impliedSupplier;
        // The fields added: a placement's its own; a part's and its package's kept with its board part.
        for (const JPImportField::Target& t : added) {
            if (JPImportField::single(t.of)) continue;
            const std::string v = value(rows, d, t, &report);
            if (v.empty()) continue;
            if (t.of == JPFieldSet::Of::Placements) {
                JJson parsed;
                if (read(t, v, d, parsed)) p.custom.set(t.key, parsed);
            } else {
                // As its field reads it (a length in millimetres), for the library's part and package.
                JJson parsed;
                if (read(t, v, d, parsed))
                    fields[std::string(t.of == JPFieldSet::Of::Parts ? "part:" : "package:") + t.key] =
                        defs.of(t.of).field(t.key)->text(parsed);
            }
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
        const std::string named = fields.count("name") ? fields["name"] : "";   // the part's Name, as a file gives it
        if (val.empty() && footprint.empty() && mpn.empty() && named.empty()) {
            if (p.type == JPPlacement::Type::Fiducial) ++report.fiducials;
            else ++report.noPart;
            out.placements.push_back(p);
            ++report.placements;
            continue;
        }
        // One board part for the rows that are one part, by the surest they say: its maker and MPN, else its
        // supplier's part number, else its Name, value and footprint together (a Name alone is not enough:
        // EasyEDA's CPL names a part by its value, "100nF" an 0402 and an 0603 alike).
        const std::string supplierPn = fields.count("supplierPn") ? fields["supplierPn"] : "";
        const std::string group = !mpn.empty() ? "mpn:" + upper(fields.count("manufacturer") ? fields["manufacturer"] : "") + "|" + upper(mpn)
                                : !supplierPn.empty() ? "spn:" + upper(supplierPn)
                                                      : "nvf:" + upper(named) + "|" + val + "|" + footprint;
        std::string& key = keyOfGroup[group];
        // Another placement of a part already made: what its rows say of the part's fields added, against the first's.
        if (!key.empty())
            if (const JPBoardPart* first = out.part(key))
                for (const auto& [k, v] : fields) {
                    if (k.rfind("part:", 0) != 0 && k.rfind("package:", 0) != 0) continue;
                    const std::string& had = first->field(k);
                    if (!had.empty() && !same(had, v)) {
                        const bool part = k.rfind("part:", 0) == 0;
                        const JPImportField::Target t { part ? JPFieldSet::Of::Parts : JPFieldSet::Of::Packages,
                                                        k.substr(k.find(':') + 1) };
                        report.fieldNotes.push_back(JPImportField::label(defs, t) + ": " + d + " says \"" + v + "\" where "
                                                    + first->field("part") + "'s first placement says \"" + had + "\"; that is taken");
                    }
                }
        if (key.empty()) {
            JPBoardPart bp;
            bp.key = key = out.newPartKey();
            bp.fields = fields;
            // The library's part, where the evidence is strong (its MPN, a supplier's part number, a name it learned,
            // OpenPnP's footprint-value): JPPartMatcher. The rest are left to be chosen, with their candidates.
            const JPPart* found = JPPartMatcher::automatic(config, bp);
            // Its Name: the file's, else made up of its MPN, footprint and value. One another part has already
            // (two parts the file names alike: another of this import's, or, not this one, a library part) told
            // apart by its footprint, else made up.
            std::string name = !named.empty() ? named : JPLibraryLearning::madeName(mpn, footprint, val);
            auto taken = [&](const std::string& n) {
                if (std::any_of(out.parts().begin(), out.parts().end(), [&](const JPBoardPart& o) { return upper(o.field("part")) == upper(n); }))
                    return true;
                const JPPart* library = config.partNamed(n);
                return library && library != found;
            };
            if (!named.empty() && taken(name)) {
                name = footprint.empty() ? JPLibraryLearning::madeName(mpn, footprint, val) : named + "-" + footprint;
                if (taken(name)) name = JPLibraryLearning::madeName(mpn, footprint, val);
            }
            bp.fields["part"] = name;
            if (found) {
                bp.state = JPBoardPart::State::Matched;
                bp.libraryPartId = found->id;
                ++report.matched;
                // What the files say of its fields added, where the library's part (or its package) has another
                // value: the library's kept, said.
                auto differs = [&](JPFieldSet::Of of, const JPFieldValues& library, const std::string& whose) {
                    const JPFieldValues said = bp.added(defs, of);
                    for (const auto& [k, v] : said.all()) {
                        const JJson& had = library.get(k);
                        if (had.isNull() || had.dump() == v.dump()) continue;
                        const JPField* f = defs.of(of).field(k);
                        report.fieldNotes.push_back(whose + "'s " + JPImportField::label(defs, { of, k }) + ": the library has \"" +
                                                    f->text(had) + "\", the files say \"" + f->text(v) + "\"; the library's is kept");
                    }
                };
                differs(JPFieldSet::Of::Parts, found->custom, found->shownName());
                // Its description likewise.
                if (const std::string& said = bp.field("description"); !said.empty() && found->name && !same(*found->name, said))
                    report.fieldNotes.push_back(found->shownName() + "'s Description: the library has \"" + *found->name +
                                                "\", the files say \"" + said + "\"; the library's is kept");
                if (const JPPackage* k = config.package(found->packageId)) differs(JPFieldSet::Of::Packages, k->custom, k->shownName());
            } else if (createMissing && !bp.fields["part"].empty()) {
                // A part made for the library (put there as the import is taken), from what the files said.
                // Named as the files name it, known for good by a GUID; one made already for that name, that one.
                const std::string name = bp.fields["part"];
                std::string id;
                for (const auto& m : out.madeParts)
                    if (m->label == name) id = m->id;
                if (id.empty()) {
                    auto made = std::make_shared<JPPart>();
                    made->id = id = JPUuid::make();
                    made->label = name;
                    made->value = val;   // its value as the files wrote it
                    made->custom = bp.added(defs, JPFieldSet::Of::Parts);
                    JPLibraryLearning::describe(*made, bp);
                    JPLibraryLearning::learnName(*made, bp, out.scopeName(), JPWhen::now());
                    if (const auto h = fields.find("height"); h != fields.end()) {
                        double mm = 0;
                        if (JPImportSource::length(h->second, cpl.units, mm)) made->height = JPLength(mm, JPLengthUnit::Millimeters);
                    }
                    // Its package: the library's of that name (or a name its footprints are known by); else one
                    // made (named so, known by a GUID), with the pads the board file draws where it does (a KiCad
                    // board); one made already for that name, that one.
                    if (const JPPackage* k = config.packageNamed(footprint)) {
                        made->packageId = k->id;
                    } else if (!footprint.empty()) {
                        for (const auto& k : out.madePackages)
                            if (upper(k->label) == upper(footprint)) made->packageId = k->id;
                        if (made->packageId.empty()) {
                            auto k = std::make_shared<JPPackage>();
                            k->id = JPUuid::make();
                            k->label = footprint;
                            k->custom = bp.added(defs, JPFieldSet::Of::Packages);
                            if (const JPFootprint* pads = drawn(at)) {
                                k->footprint = *pads;
                                drawnUsed.insert(footprint);
                            }
                            made->packageId = k->id;
                            out.madePackages.push_back(std::move(k));
                        }
                    }
                    out.madeParts.push_back(std::move(made));
                }
                bp.state = JPBoardPart::State::Matched;
                bp.libraryPartId = id;
                ++report.made;
            } else {
                ++report.unmatched;
            }
            out.parts().push_back(bp);
            ++report.parts;
        }
        p.boardPart = key;
        p.partId = out.part(key)->partId();
        // The one placed with turned by its footprint's zero rotation, where the library's footprint for it
        // says (CAD tools disagree about which way 0° faces).
        const JPBoardPart* placed = out.part(key);
        if (placed->state == JPBoardPart::State::Matched)
            if (const JPPart* part = config.libraryPartFor(*placed))
                if (const JPLibraryFootprint* f = config.footprintFor(*placed, *part); f && f->zeroRotationDeg != 0) {
                    const double r = JPAngles::normalise(rotation + f->zeroRotationDeg);
                    p.location = p.location.derive(std::nullopt, std::nullopt, std::nullopt, r);
                }
        out.placements.push_back(p);
        ++report.placements;
    }
    report.footprints = int(drawnUsed.size());
    // The board's and the job's fields: one value for the whole import, typed over what the files say.
    for (const JPFieldSet::Of of : { JPFieldSet::Of::Boards, JPFieldSet::Of::Job }) {
        const auto& typed = of == JPFieldSet::Of::Boards ? typedBoard : typedJob;
        for (const JPField* f : defs.of(of).added()) {
            const JPImportField::Target t { of, f->key };
            std::string v;
            if (const auto at = typed.find(f->key); at != typed.end()) {
                v = trim(at->second);
            } else {
                v = single(defs, t);
                // A column's rows that say otherwise: the first taken, said.
                std::set<std::string> said;
                for (const JPImportSource& src : sources)
                    if (const int c = src.column(t); c >= 0)
                        for (const auto& r : src.table.rows)
                            if (size_t(c) < r.size() && !trim(r[size_t(c)]).empty()) said.insert(trim(r[size_t(c)]));
                if (said.size() > 1)
                    report.fieldNotes.push_back(JPImportField::label(defs, t) + ": the rows say " + std::to_string(said.size()) +
                                                " different things; \"" + v + "\" is taken");
            }
            if (v.empty()) continue;
            JJson parsed;
            if (read(t, v, of == JPFieldSet::Of::Boards ? std::string("The board") : std::string("The job"), parsed))
                (of == JPFieldSet::Of::Boards ? out.custom : report.job).set(f->key, parsed);
        }
    }
    for (const JPImportSource& s : sources) out.provenance.push_back(s.provenance(when));
    return true;
}

} // inline namespace jf
