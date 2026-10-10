// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPEagleBoardImporter.h"

#include "common/JPlacerLog.h"
#include "openpnp/JPXmlReader.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <strings.h>

inline namespace jf {

namespace {

constexpr double kMilToMm = 0.0254;

bool same(const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) == 0; }

// "R180" as 180, "MR90" as 90: the letters (and spaces) taken out.
std::string digits(const std::string& s) {
    std::string out;
    for (char c : s)
        if (!std::isalpha(uint8_t(c)) && c != ' ') out += c;
    return out;
}

std::string orDefault(const JPXmlElement& e, const char* name, const char* fallback) {
    const auto it = e.attributes.find(name);
    return it == e.attributes.end() ? fallback : it->second;
}

const JPXmlElement* path(const JPXmlElement* e, std::initializer_list<const char*> names) {
    for (const char* n : names) {
        if (!e) return nullptr;
        e = e->child(n);
    }
    return e;
}

struct Point {
    double x, y;
};

// OpenPnP's Utils2D.rotateTranslateCenterPoint with no translation: `p`
// turned `degrees` about `centre`.
Point rotateAbout(Point p, double degrees, Point centre) {
    const double c = degrees * M_PI / 180, x = p.x - centre.x, y = p.y - centre.y;
    return { x * std::cos(c) - y * std::sin(c) + centre.x, x * std::sin(c) + y * std::cos(c) + centre.y };
}

} // namespace

std::vector<JPBoardImporter::Option> JPEagleBoardImporter::options() const {
    return { { "Create Missing Parts", "", true },
             { "Update Existing Parts", "", false },
             { "Add Library Prefix to Part Names", "", false },
             { "Import Parts on the Top of the board", "", true },
             { "Import Parts on the Bottom of the board", "", true } };
}

void JPEagleBoardImporter::parse(const std::vector<std::string>& files, const std::vector<bool>& options,
                                 JPConfiguration& config, JPBoard& out) const {
    if (!exists(files[0])) return;
    // Neither side: nothing read; one: only that side.
    std::optional<JPSide> only;
    if (options[ImportTop] && options[ImportBottom]) {
    } else if (options[ImportTop]) {
        only = JPSide::Top;
    } else if (options[ImportBottom]) {
        only = JPSide::Bottom;
    } else {
        return;
    }
    const bool create = options[CreateMissingParts], update = options[UpdateExistingParts];

    JPXmlElement eagle;
    std::string error;
    if (!JPXmlReader::read(files[0], eagle, error)) throw Failure(error);
    const JPXmlElement* drawing = eagle.child("drawing");
    const JPXmlElement* board = path(drawing, { "board" });
    if (!board) return;

    // The layers by name, in case a board numbers them its own way.
    std::string dimensionLayer, topLayer, bottomLayer, tCreamLayer, bCreamLayer;
    if (const JPXmlElement* layers = path(drawing, { "layers" }))
        for (const JPXmlElement& l : layers->children) {
            const std::string& n = l.attr("name");
            if (same(n, "Dimension")) dimensionLayer = l.attr("number");
            else if (same(n, "Top")) topLayer = l.attr("number");
            else if (same(n, "Bottom")) bottomLayer = l.attr("number");
            else if (same(n, "tCream")) tCreamLayer = l.attr("number");
            else if (same(n, "bCream")) bCreamLayer = l.attr("number");
        }

    // The board's width (its outline's furthest X), for turning bottom pads over.
    double xBoundary = 0;
    if (const JPXmlElement* plain = board->child("plain"))
        for (const JPXmlElement& w : plain->children)
            if (w.name == "wire" && same(w.attr("layer"), dimensionLayer))
                xBoundary = std::max({ xBoundary, number(w.attr("x1")), number(w.attr("x2")) });
    const Point centre { xBoundary / 2, 0 };

    // How much smaller than its pad the cream is, from the design rules.
    double minCream = 0, maxCream = 0;
    if (const JPXmlElement* rules = board->child("designrules"))
        for (const JPXmlElement& p : rules->children) {
            const bool isMin = same(p.attr("name"), "mlMinCreamFrame"), isMax = same(p.attr("name"), "mlMaxCreamFrame");
            if (!isMin && !isMax) continue;
            const std::string v = upper(p.attr("value"));
            double n;
            if (v.size() >= 3 && v.compare(v.size() - 3, 3, "MIL") == 0) n = number(digits(p.attr("value"))) * kMilToMm;
            else if (v.size() >= 2 && v.compare(v.size() - 2, 2, "MM") == 0) n = number(digits(p.attr("value")));
            else throw Failure(std::string(isMin ? "mlMinCream" : "mlMaxCream") + " must either be in mil or mm");
            (isMin ? minCream : maxCream) = n;
        }

    std::map<std::string, std::string> seen;   // board parts made (or updated) once, at their first element: their keys
    const JPXmlElement* elements = board->child("elements");
    if (!elements) return;
    for (const JPXmlElement& element : elements->children) {
        if (element.name != "element") continue;
        const std::string rot = orDefault(element, "rot", "R0");
        const JPSide side = !rot.empty() && std::toupper(uint8_t(rot[0])) == 'M' ? JPSide::Bottom : JPSide::Top;
        if (only && *only != side) continue;
        const std::string rotNumber = digits(rot);
        const double rotation = number(rotNumber), x = number(element.attr("x")), y = number(element.attr("y"));
        JPPlacement placement;
        placement.id = element.attr("name");
        placement.location = JPLocation(JPLengthUnit::Millimeters, x, y, 0, rotation);

        // The package's SMD pads, pads and polygons, from the board's libraries.
        const std::string packageId = element.attr("package"), libraryId = element.attr("library");
        std::vector<const JPXmlElement*> polys;
        if (const JPXmlElement* libraries = board->child("libraries"))
            for (const JPXmlElement& library : libraries->children) {
                if (!same(library.attr("name"), libraryId)) continue;
                if (const JPXmlElement* packages = library.child("packages"))
                    for (const JPXmlElement& pak : packages->children)
                        if (same(pak.attr("name"), packageId))
                            for (const JPXmlElement& e : pak.children)
                                if (e.name == "smd" || e.name == "pad" || e.name == "polygon") polys.push_back(&e);
            }

        {
            const std::string value = element.attr("value");
            const std::string pkgId = options[AddLibraryPrefix] ? libraryId + "-" + packageId : packageId;
            const std::string partId = !trim(value).empty() ? pkgId + "-" + value : pkgId;
            std::string key;
            if (auto s = seen.find(partId); s != seen.end()) {
                key = s->second;
            } else {
                // The board's part: the library's where it has one; else, Create Missing Parts, one made for the
                // library, its package the library's or (none) one made with the pads the board's library draws.
                JPPart* part = nullptr;
                bool made = false;
                JPPackage* madePackage = nullptr;
                key = boardPart(config, out, partId, pkgId, value, create, &part, &made, &madePackage);
                JPFootprint fp;
                for (const JPXmlElement* e : polys) {
                    if (e->name != "smd") continue;
                    JPFootprint::Pad p;
                    p.name = e->attr("name");
                    p.x = number(e->attr("x"));
                    p.y = number(e->attr("y"));
                    p.width = number(e->attr("dx"));
                    p.height = number(e->attr("dy"));
                    p.rotation = number(digits(orDefault(*e, "rot", "R0")));
                    p.roundness = number(orDefault(*e, "roundness", "0"));
                    fp.pads.push_back(p);
                }
                if (made) {
                    // A package made for it (the library has none of that name) takes the pads the board draws.
                    if (madePackage) madePackage->footprint = fp;
                } else if (part && update) {
                    // Asked for (Update Existing Parts): the library's part and package from the board.
                    if (JPPackage* pkg = config.libraryPackage(pkgId)) {
                        pkg->footprint = fp;
                        part->packageId = pkg->id;
                    }
                }
                seen[partId] = key;
            }
            assign(out, placement, key);
        }

        // Solder paste: each SMD pad that takes cream, sized halfway between
        // the design rules' least and most frame, and where it lies on the
        // board (a bottom part's turned over); a polygon on a cream layer
        // as the rectangle around it.
        for (const JPXmlElement* e : polys) {
            if (e->name == "smd") {
                if (same(orDefault(*e, "cream", "yes"), "No")) continue;
                JPBoardPad pad;
                pad.pad.units = JPLengthUnit::Millimeters;
                pad.pad.height = number(e->attr("dx")) - (maxCream - minCream) / 2;
                pad.pad.width = number(e->attr("dy")) - (maxCream - minCream) / 2;
                pad.pad.roundness = number(orDefault(*e, "roundness", "0"));
                const double padRotation = number(rotNumber) + std::fmod(number(digits(orDefault(*e, "rot", "R0"))), 360);
                Point a { number(e->attr("x")) + x, number(e->attr("y")) + y };
                const Point partCentre { x, y };
                if (side == JPSide::Top) {
                    a = rotateAbout(a, rotation > 180 ? rotation : -rotation, partCentre);
                } else {
                    a = rotateAbout(a, rotation > 180 ? rotation : -(180 - rotation), partCentre);
                    a.x = 2 * centre.x - a.x;   // across the board's middle
                    a.y = 2 * y - a.y;          // across the part's
                }
                pad.location = JPLocation(JPLengthUnit::Millimeters, a.x, a.y, 0, padRotation);
                pad.name = element.attr("name") + "-" + e->attr("name");
                if (same(e->attr("layer"), topLayer)) {
                    pad.side = side == JPSide::Top ? JPSide::Top : JPSide::Bottom;
                } else if (same(e->attr("layer"), bottomLayer)) {
                    pad.side = side == JPSide::Top ? JPSide::Bottom : JPSide::Top;
                } else {
                    JLOGC(JPlacerLog::kBoardImport, JLogLevel::Info)
                        << "Warning: " << files[0] << "contains a SMD pad that is not on a topLayer or bottomLayer";
                }
                out.solderPastePads.push_back(pad);
            } else if (e->name == "polygon" &&
                       (same(e->attr("layer"), tCreamLayer) || same(e->attr("layer"), bCreamLayer))) {
                JLOGC(JPlacerLog::kBoardImport, JLogLevel::Info)
                    << "Warning: " << files[0] << " contains a Polygon pad - this functionality has been implmented as "
                    << "the smallest bounded rectangle and may over paste the area";
                double xMin = 0, xMax = 0, yMin = 0, yMax = 0;
                for (const JPXmlElement& v : e->children) {
                    if (v.name != "vertex") continue;
                    xMin = std::min(xMin, number(v.attr("x")));
                    xMax = std::max(xMax, number(v.attr("x")));
                    yMin = std::min(yMin, number(v.attr("y")));
                    yMax = std::max(yMax, number(v.attr("y")));
                }
                JPBoardPad pad;
                pad.pad.units = JPLengthUnit::Millimeters;
                pad.pad.height = yMax - yMin;
                pad.pad.width = xMax - xMin;
                pad.location = JPLocation(JPLengthUnit::Millimeters, x + (xMax + xMin) / 2, y + (yMax + yMin) / 2, 0, 0);
                pad.name = element.attr("name") + "-Polygon ";
                pad.side = same(e->attr("layer"), tCreamLayer) ? JPSide::Top : JPSide::Bottom;
                out.solderPastePads.push_back(pad);
            }
        }
        placement.side = side;
        out.placements.push_back(placement);
    }
}

} // inline namespace jf
