// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLabcenterProteusImporter.h"

#include <regex>

inline namespace jf {

namespace {
// A field with the quotes around it taken off.
std::string unquoted(std::string s) {
    if (!s.empty() && s.front() == '"') s.erase(0, 1);
    if (!s.empty() && s.back() == '"') s.pop_back();
    return s;
}
}

void JPLabcenterProteusImporter::parse(const std::vector<std::string>& files, const std::vector<bool>& options,
                                       JPConfiguration& config, JPBoard& out) const {
    if (!exists(files[0])) return;
    static const std::regex kThou(R"re(^.*?\bUnits\b.*?\bthou\b.*?$)re");
    // "R1","10k","0402",[Stock Code,]TOP,270,15.1678,15.24
    const int plain[] = { 0, 1, 2, 3, 4, 5, 6 }, stock[] = { 0, 1, 2, 4, 5, 6, 7 };
    const int* ind = options[1] ? stock : plain;
    double mul = 1.0;
    for (std::string line : lines(files[0])) {
        if (line.empty()) continue;
        if (std::regex_match(line, kThou)) {
            mul = .0254;
            continue;
        }
        if (line[0] != '"') continue;
        line = trim(line);
        const std::vector<std::string> t = split(line, ',');
        JPPlacement p;
        p.id = unquoted(at(t, ind[0]));
        const std::string value = unquoted(at(t, ind[1])), packageName = unquoted(at(t, ind[2]));
        const double x = number(at(t, ind[5])) * mul, y = number(at(t, ind[6])) * mul;
        const double rotation = number(at(t, ind[4]));
        const std::string& layer = at(t, ind[3]);
        p.location = JPLocation(JPLengthUnit::Millimeters, x, y, 0, rotation);
        if (options[0]) p.partId = findOrMakePart(config, packageName + "-" + value, packageName)->id;
        p.side = first(layer) == 'T' ? JPSide::Top : JPSide::Bottom;
        out.placements.push_back(p);
    }
}

} // inline namespace jf
