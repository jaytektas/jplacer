// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPKicadPosImporter.h"

#include <regex>

inline namespace jf {

std::vector<JPBoardImporter::File> JPKicadPosImporter::files() const {
    return { { "Top File (.pos)", { "pos" } }, { "Bottom File (.pos)", { "pos" } } };
}

std::vector<JPBoardImporter::Option> JPKicadPosImporter::options() const {
    return { { "Assign Parts", "", true },
             { "Create Missing Parts", "PartId = 'Package'-'Value'", false },
             { "Use only Value as PartId", "Check this, if Value is unique (e.g. company internal part number)", false } };
}

void JPKicadPosImporter::parse(const std::vector<std::string>& files, const std::vector<bool>& options,
                               JPConfiguration& config, JPBoard& out) const {
    if (exists(files[0])) parseFile(files[0], JPSide::Top, options, config, out);
    if (exists(files[1])) parseFile(files[1], JPSide::Bottom, options, config, out);
}

void JPKicadPosImporter::parseFile(const std::string& path, JPSide side, const std::vector<bool>& options,
                                   JPConfiguration& config, JPBoard& out) {
    // ## Unit = mm, Angle = deg.
    // # Ref Val Package PosX PosY Rot Side
    // C1 100u Capacitors_SMD:c 128.9050 -52.0700 0.0 F.Cu
    static const std::regex kLine(
        R"re((\S+)\s+(.*?)\s+(.*?)\s+(-?\d+\.\d+)\s+(-?\d+\.\d+)\s+(-?\d+\.\d+)\s(.*?))re");
    for (std::string line : lines(path)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::smatch m;
        if (!std::regex_match(line, m, kLine)) throw Failure("No match found");
        const std::string value = m[2], packageName = m[3];
        double x = number(m[4]), y = number(m[5]), rotation = number(m[6]);
        if (std::string(m[7]).find("bottom") != std::string::npos) {
            // KiCad gives a bottom part's X from the board's other edge, and
            // its rotation as seen through the board.
            x = -x;
            rotation = 180 - rotation;
        }
        if (rotation == 0.0) rotation = 0.0;   // no -0.0
        JPPlacement p;
        p.id = m[1];
        p.location = JPLocation(JPLengthUnit::Millimeters, x, y, 0, rotation);
        if (options[AssignParts]) {
            const std::string partId = options[UseOnlyValueAsPartId] ? value : packageName + "-" + value;
            assign(out, p, boardPart(config, out, partId, packageName, value, options[CreateMissingParts]));
        }
        p.side = side;
        out.placements.push_back(p);
    }
}

} // inline namespace jf
