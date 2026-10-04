// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPDipTraceImporter.h"

inline namespace jf {

void JPDipTraceImporter::parse(const std::vector<std::string>& files, const std::vector<bool>& options,
                               JPConfiguration& config, JPBoard& out) const {
    if (!exists(files[0])) return;
    // RefDes,Name,X (mm),Y (mm),Side,Rotate,Value
    // C1,C0603,8.6,7.2,Top,0,1nF
    int count = 0;
    for (std::string line : lines(files[0])) {
        if (count++ == 0 || line.empty()) continue;   // the first is the header
        line = trim(line);
        const std::vector<std::string> t = split(line, ',');
        JPPlacement p;
        p.id = at(t, 0);
        const std::string& value = at(t, 6);
        const std::string& packageName = at(t, 1);
        const double x = number(at(t, 2)), y = number(at(t, 3)), rotation = number(at(t, 5));
        const std::string& layer = at(t, 4);
        p.location = JPLocation(JPLengthUnit::Millimeters, x, y, 0, rotation);
        if (options[0]) p.partId = findOrMakePart(config, packageName + "-" + value, packageName)->id;
        p.side = first(layer) == 'T' ? JPSide::Top : JPSide::Bottom;
        out.placements.push_back(p);
    }
}

} // inline namespace jf
