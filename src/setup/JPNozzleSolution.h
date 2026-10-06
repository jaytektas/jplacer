// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPCellConfig.h"

#include <string>

inline namespace jf {

// OpenPnP's nozzle solution (HeadSolutions' Create nozzles for this head): a
// head's nozzles made as so many units of one kind, each a nozzle with a Z
// motor of its own (Standalone), or a pair on one Z motor, the second's Z the
// first's negated (DualNegated) or both pushed down by a cam (DualCam); each
// nozzle with a rotation axis of its own and its vacuum valve and sense
// actuators. The head's nozzles, axes and actuators are reused in order, so
// their settings stay where they remain alike; those left over are removed.
class JPNozzleSolution {
public:
    enum class Kind { Standalone, DualNegated, DualCam };
    static const char* name(Kind k);
    static bool parse(const std::string& name, Kind& kind);

    // What the head's nozzles are now (OpenPnP's first look): the kind and
    // the number of units (at least one).
    static void current(const JPCellConfig& cell, const std::string& headId, Kind& kind, int& units);
    // OpenPnP's createNozzleSolution, for the head whose camera is `cameraId`
    // (the nozzles move on its X and Y axes; new axes on its X axis's controller).
    static void apply(JPCellConfig& cell, const std::string& headId, const std::string& cameraId, Kind kind, int units);
};

} // inline namespace jf
