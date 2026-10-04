// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// How a job is run, as OpenPnP's ReferencePnpJobProcessor keeps it in
// machine.xml (<pnp-job-processor>), with its defaults; and its fiducial
// locator's tolerances (<fiducial-locator><tolerances>). Kept in the cell.
struct JPJobProcessorConfig {
    // OpenPnP's JobOrderHint and PnpJobPlanner.Strategy, in their order.
    enum class JobOrder { Part, PartHeight, PartBoard, HeightPartBoard, BoardPart, PickLocation, PickPlaceLocation,
                          NozzleTips, NozzleTipsByFlexibility, Unsorted };
    enum class Strategy { Minimize, StartAsPlanned, FullyAsPlanned };

    JobOrder jobOrder                = JobOrder::NozzleTips;
    Strategy strategy                = Strategy::Minimize;
    int      maxVisionRetries        = 3;
    int      maxPlacementRetries     = 5;
    int      feederFaultLimit        = 3;
    int      feederFaultWindowSize   = 6;
    bool     steppingToNextMotion    = true;
    bool     optimizeMultipleNozzles = true;
    bool     preRotateAllNozzles     = true;
    int      fiducialLevel           = 1;
    double   scalingTolerance        = 0.05;
    double   shearingTolerance       = 0.05;
    double   boardLocationToleranceMm = 5.0;

    // As OpenPnP's file names them ("PartHeight") and its settings show them ("Height:Part").
    static const std::vector<std::string>& jobOrderKeys();
    static const std::vector<std::string>& jobOrderNames();
    static const std::vector<std::string>& strategyKeys();
    static const std::vector<std::string>& strategyNames();

    JJson toJson() const;
    static JPJobProcessorConfig fromJson(const JJson& j);
};

} // inline namespace jf
