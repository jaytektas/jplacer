// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPEagleMountsmdUlpImporter.h"

#include <optional>

inline namespace jf {

void JPEagleMountsmdUlpImporter::parse(const std::vector<std::string>& files, const std::vector<bool>& options,
                                       JPConfiguration& config, JPBoard& out) const {
    if (exists(files[0])) parseFile(files[0], JPSide::Top, options[0], config, out);
    if (exists(files[1])) parseFile(files[1], JPSide::Bottom, options[0], config, out);
}

void JPEagleMountsmdUlpImporter::parseFile(const std::string& path, JPSide side, bool createMissingParts,
                                           JPConfiguration& config, JPBoard& out) {
    // C1 41.91 34.93 180 0.1uF C0805
    // T10 21.59 14.22 90 SOT23-BEC
    for (std::string line : lines(path)) {
        line = trim(line);
        if (line.empty()) continue;
        const std::vector<std::string> f = splitWhitespace(line);
        JPPlacement p;
        p.id = at(f, 0);
        p.location = JPLocation(JPLengthUnit::Millimeters, number(at(f, 1)), number(at(f, 2)), 0, number(at(f, 3)));
        if (createMissingParts) {
            std::optional<std::string> value, packageId;
            if (f.size() > 4) value = trim(f[4]);
            if (f.size() > 5) packageId = trim(f[5]);
            if (!packageId || packageId->empty()) {
                packageId = value;
                value.reset();
            }
            // OpenPnP fails here on a line of four fields (no package or value).
            if (!packageId) throw Failure("Cannot invoke \"String.isEmpty()\" because \"packageId\" is null");
            std::string partId = *packageId;
            if (value && !value->empty()) partId += "-" + *value;
            assign(out, p, boardPart(config, out, partId, *packageId, value.value_or(""), true));
        }
        p.side = side;
        out.placements.push_back(p);
    }
}

} // inline namespace jf
