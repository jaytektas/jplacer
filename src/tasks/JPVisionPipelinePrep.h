// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPNozzleTipConfig.h"
#include "model/JPConfiguration.h"
#include "pipeline/JPPipeline.h"

#include <string>

inline namespace jf {

// What OpenPnP sets on a vision setting's pipeline before it runs (its
// preparePipeline), for a part or package (or neither): the footprint, the
// sizes and places the stages are told, then the values the setting assigns
// to its parameters. The camera's scale and size are in its context.
class JPVisionPipelinePrep {
public:
    // ReferenceFiducialLocator: the package's footprint (none: a round 1 mm
    // fiducial, as OpenPnP's FIDUCIAL-HOME stands in), its diameter, its
    // rotation; `maxDistanceMm` for pipelines without a maxDistance stage.
    static void fiducial(JPPipeline& pipeline, const JPConfiguration& config, const JPVisionSettings& settings,
                         const std::string& partId, const std::string& packageId, double rotation, double maxDistanceMm);
    // ReferenceBottomVision for the part over the camera's centre, turned
    // `rotation`, in one shot, on a nozzle with `tip` (its largest part and
    // pick tolerance; none: OpenPnP's defaults): false (and why) without a
    // package to go by.
    static bool bottom(JPPipeline& pipeline, const JPConfiguration& config, const JPVisionSettings& settings,
                       const std::string& partId, const std::string& packageId, double rotation, const JPNozzleTipConfig* tip,
                       std::string& why);
};

} // inline namespace jf
