// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// The CAD tool a file came from, as the person says on each import (never
// guessed from the file): it decides how the file's conventions become the
// board's (one frame for both sides, seen from the top).
//
//   EasyEDA:         bottom parts as seen from the top already;
//   KiCad:           as KiCad 6 onward write it by default, likewise;
//   KiCad, bottom X negated: KiCad's "Use negative X coordinates for
//                    footprints on bottom layer" (and KiCad 5's files), turned
//                    back;
//   Other:           any other CSV, taken as seen from the top.
struct JPCadTool {
    enum class Kind { EasyEda, KiCad, KiCadNegativeX, Other };

    static const std::vector<Kind>& all() {
        static const std::vector<Kind> k = { Kind::EasyEda, Kind::KiCad, Kind::KiCadNegativeX, Kind::Other };
        return k;
    }
    static const char* name(Kind k) {
        switch (k) {
            case Kind::EasyEda:        return "EasyEDA";
            case Kind::KiCad:          return "KiCad";
            case Kind::KiCadNegativeX: return "KiCad, bottom X negated";
            case Kind::Other:          return "Other (CSV)";
        }
        return "";
    }
    static const char* key(Kind k) {
        switch (k) {
            case Kind::EasyEda:        return "easyeda";
            case Kind::KiCad:          return "kicad";
            case Kind::KiCadNegativeX: return "kicad-negative-x";
            case Kind::Other:          return "other";
        }
        return "";
    }
    static Kind fromKey(const std::string& s) {
        for (const Kind k : all())
            if (s == key(k)) return k;
        return Kind::Other;
    }
};

} // inline namespace jf
