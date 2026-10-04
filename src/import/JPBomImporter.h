// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "job/JPPlacement.h"

#include <string>
#include <vector>

inline namespace jf {

// A bill of materials from a PCB tool, as CSV: one line per part with the
// designators it is placed at ("C4,C5,C6", or spaces or semicolons between
// them). Only the designators are sure to be there; whatever else a column
// says about the part is read as a pick-and-place file's is (JPCsvTable).
class JPBomImporter {
public:
    struct Line {
        std::vector<std::string> designators;
        JPPlacement              part;   // what the line says about the part (no position)
        int                      line = 0;
    };

    static bool read(const std::string& path, std::vector<Line>& out, std::vector<std::string>& notes, std::string& error);
    static bool parse(const std::string& text, std::vector<Line>& out, std::vector<std::string>& notes, std::string& error);
};

} // inline namespace jf
