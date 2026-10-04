// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSupplierNumber.h"

#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// One designator on a board, as the CAD placed it: where (board millimetres,
// seen from the side it is on as the CAD shows that side), turned how far,
// which side, and what the files said it is. Only the designator is sure to
// be there (a CPL adds the position, rotation and side); everything else is
// kept where a file gives it, empty where none does.
struct JPPlacement {
    enum class Side { Top, Bottom };
    enum class Mounting { Unknown, Smd, ThroughHole };

    std::string designator;
    double      x = 0, y = 0;
    double      rotationDeg = 0;
    Side        side = Side::Top;
    bool        fiducial = false;   // a mark to find the board by, not a part to place

    // What identifies the part exactly.
    std::vector<JPSupplierNumber> supplierNumbers;
    std::string mpn;
    std::string manufacturer;

    // What describes it.
    std::string value;
    std::string tolerance, voltage, power, dielectric, temperature;   // ratings, as the file wrote them
    Mounting    mounting = Mounting::Unknown;
    bool        doNotPlace = false;

    // What points at its package.
    std::string footprint;          // the CAD footprint name
    std::string supplierPackage;    // the supplier's name for the package ("0805", "PowerSSO-16")
    int         pins = 0;           // as the file counts them (0: not said)

    // Shown only.
    std::string description;
    std::string device;             // the CAD's device name
    std::string datasheet;
    std::string cadId;              // the CAD's own id for it

    // Every other column, heading and text, as read.
    std::vector<std::pair<std::string, std::string>> other;

    // Its part among the job's (JPPartsStore): empty when it has none yet.
    // `partGuessed`: given by its value, ratings and package alone, for the
    // person to confirm.
    std::string partId;
    bool        partGuessed = false;
};

} // inline namespace jf
