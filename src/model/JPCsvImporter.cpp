// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCsvImporter.h"

#include "JPCsvLine.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cctype>

inline namespace jf {

namespace {

constexpr int         kMaxHeaderLines = 50;
constexpr size_t      kMinColumns     = 6;
constexpr const char* kMil            = "(MIL)";
constexpr double      kMilToMm        = 0.0254;

// The column of the first of `names` the header has; -1 when none.
int column(const std::vector<std::string>& names, const std::vector<std::string>& header) {
    for (const std::string& n : names)
        for (size_t j = 0; j < header.size(); ++j)
            if (header[j] == n) return int(j);
    return -1;
}

bool endsWith(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

std::string without(std::string s, const std::string& what) {
    for (size_t at; (at = s.find(what)) != std::string::npos;) s.erase(at, what.size());
    return s;
}

std::string replaced(std::string s, char from, char to) {
    std::replace(s.begin(), s.end(), from, to);
    return s;
}

double angleNorm(double v, double lim) {
    while (std::abs(v) > lim) v += v < 0 ? 2 * lim : -2 * lim;
    return v;
}

} // namespace

std::vector<JPBoardImporter::File> JPCsvImporter::files() const {
    return { { "Centroid File (.csv)", { "csv", "txt", "dat" } } };
}

std::vector<JPBoardImporter::Option> JPCsvImporter::options() const {
    return { { "Create Missing Parts", "", true }, { "Update Existing Part Heights", "", true } };
}

void JPCsvImporter::parse(const std::vector<std::string>& files, const std::vector<bool>& options,
                          JPConfiguration& config, JPBoard& out) const {
    if (!exists(files[0])) return;
    const Patterns pat = patterns();
    const std::vector<std::string> text = splitLines(csvText(files[0]));

    int ref = -1, value = -1, package = -1, x = -1, y = -1, rotation = -1, side = -1, height = -1, comment = -1;
    bool xMil = false, yMil = false, heightMil = false;
    int len = 0;
    char separator = ',';
    size_t line = 0;
    // The header: the first line (of the first 50) naming the columns.
    auto isHeader = [&](const std::vector<std::string>& h) {
        if ((ref = column(pat.reference, h)) < 0 || (value = column(pat.value, h)) < 0 ||
            (package = column(pat.package, h)) < 0 || (x = column(pat.x, h)) < 0 || (y = column(pat.y, h)) < 0 ||
            (rotation = column(pat.rotation, h)) < 0) {
            len = 0;
            return false;
        }
        height = column(pat.height, h);
        side = column(pat.side, h);
        comment = column(pat.comment, h);
        xMil = endsWith(h[x], kMil);
        yMil = endsWith(h[y], kMil);
        heightMil = height >= 0 && endsWith(h[height], kMil);
        len = std::max({ ref, value, package, x, y, rotation, side, height, comment });
        return true;
    };
    for (int i = 0; i < kMaxHeaderLines && line < text.size(); ++i) {
        std::string h = upper(trim(text[line++]));
        if (h.empty()) continue;
        if (h[0] == '#') h.erase(0, 1);
        separator = ',';
        std::vector<std::string> fields = JPCsvLine::split(h, separator);
        if (fields.size() >= kMinColumns && isHeader(fields)) break;
        separator = '\t';
        fields = JPCsvLine::split(h, separator);
        if (fields.size() >= kMinColumns && isHeader(fields)) break;
    }
    if (len <= 0)
        throw Failure("Unable to find relevant headers' names.\n See "
                      "https://github.com/openpnp/openpnp/wiki/Importing-Centroid-Data for more.");

    auto length = [](const std::string& s, bool mil) {
        const double v = number(without(without(without(replaced(s, ',', '.'), " "), "mm"), "mil"));
        return mil ? v * kMilToMm : v;
    };
    for (; line < text.size(); ++line) {
        if (trim(text[line]).empty()) continue;
        const std::vector<std::string> as = JPCsvLine::split(text[line], separator);
        if (int(as.size()) <= len) continue;
        const std::string id = as[ref];
        const double px = length(as[x], xMil), py = length(as[y], yMil);
        const double z = height >= 0 ? length(as[height], heightMil) : 0.0;
        const double r = angleNorm(number(without(replaced(as[rotation], ',', '.'), " ")), 180);

        const std::string partId = as[package] + "-" + as[value];
        JPPart* part = config.part(partId);
        if (!part && options[CreateMissingParts]) {
            part = findOrMakePart(config, partId, as[package]);
            part->height = JPLength(z, JPLengthUnit::Millimeters);
        }
        if (!part) {
            JLOGC(JPlacerLog::kBoardImport, JLogLevel::Warn)
                << "no part for placement " << id << " (" << partId << ") found, skipped.";
            continue;
        }
        if (options[UpdateExistingPartHeights] && height >= 0) part->height = JPLength(z, JPLengthUnit::Millimeters);

        JPPlacement p;
        p.id = id;
        const std::string u = upper(id);
        if (u.rfind("FID", 0) == 0 || u.rfind("REF", 0) == 0) {
            if (u.size() < 4) throw Failure("Index 3 out of bounds for length " + std::to_string(u.size()));
            if (std::isdigit(uint8_t(u[3]))) p.type = JPPlacement::Type::Fiducial;
        }
        p.location = JPLocation(JPLengthUnit::Millimeters, px, py, 0, r);
        p.partId = part->id;
        if (comment >= 0) p.comments = as[comment];
        const char c = side >= 0 ? first(upper(as[side])) : 0;
        p.side = c == 'B' || c == 'Y' ? JPSide::Bottom : JPSide::Top;
        out.placements.push_back(p);
    }
}

} // inline namespace jf
