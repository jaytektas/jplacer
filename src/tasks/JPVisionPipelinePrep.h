// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPVisionComposite.h"

#include "machine/JPCameraConfig.h"
#include "machine/JPNozzleTipConfig.h"
#include "model/JPConfiguration.h"
#include "pipeline/JPPipeline.h"

#include <memory>
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
    // OpenPnP's VisionCompositing.Composite for a package: its shots, for
    // the camera looking up (`camera`: its roaming radius and name; none:
    // no roaming, so one shot) seeing `cameraWidthMm` x `cameraHeightMm`,
    // on a nozzle with `tip` (its largest part and pick tolerance; none:
    // OpenPnP's defaults); `settings` with Vision Offsets allow one shot only.
    static std::shared_ptr<JPVisionComposite> composite(const JPPackage& pkg, const JPVisionSettings& settings,
                                                        const JPCameraConfig* camera, double cameraWidthMm,
                                                        double cameraHeightMm, const JPNozzleTipConfig* tip);
    // ReferenceBottomVision's preparePipeline for the part over the
    // camera's centre, turned `rotation`, on a nozzle with `tip`: what the
    // whole run shares, then the first shot of its composite (`made`, when
    // given, the composite). False (and why) without a package to go by,
    // or when the package's compositing is enforced and finds no solution.
    static bool bottom(JPPipeline& pipeline, const JPConfiguration& config, const JPVisionSettings& settings,
                       const std::string& partId, const std::string& packageId, double rotation, const JPNozzleTipConfig* tip,
                       const JPCameraConfig* camera, std::string& why, std::shared_ptr<JPVisionComposite>* made = nullptr);
    // What preparePipeline sets for one shot of `composite`: the footprint
    // moved to the shot, the masks and sizes it allows, and for a composite
    // of corners the part's mask (round the part's centre, at `partX`,
    // `partY` in the picture) and which of the part's edges it sees.
    static void shot(JPPipeline& pipeline, const JPVisionComposite& composite, const JPVisionComposite::Shot& shot,
                     const JPNozzleTipConfig* tip, double partX, double partY);
};

} // inline namespace jf
