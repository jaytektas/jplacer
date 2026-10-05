// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAlignRequests.h"

#include "model/JPLength.h"
#include "setup/JPVisionPipelines.h"

inline namespace jf {

bool JPAlignRequests::forPart(const JPConfiguration& config, const JPVisionConfig& vision, const JPPart& part, double partHeightMm,
                              double placeAngle, double pickAngle, JPJobMachine::AlignRequest& rq,
                              const JPVisionSettings** settings) {
    if (settings) *settings = nullptr;
    if (!vision.bottomVisionEnabled) return false;
    const JPVisionSettings* v = config.inheritedVision(part, JPVisionSettings::Kind::Bottom, vision.bottomVisionId);
    if (settings) *settings = v;
    if (!v || !v->enabled) return false;
    const JPPackage* pkg = config.package(part.packageId);
    if (!pkg) return false;
    rq = {};
    // Its shape: the footprint's pads, else its body.
    const JPFootprint& f = pkg->footprint;
    auto mmOf = [&f](double d) { return JPLength(d, f.units).convertToUnits(JPLengthUnit::Millimeters).value(); };
    for (const JPFootprint::Pad& pad : f.pads)
        rq.shape.push_back({ mmOf(pad.x), mmOf(pad.y), mmOf(pad.width), mmOf(pad.height), pad.rotation });
    if (rq.shape.empty() && f.bodyWidth > 0 && f.bodyHeight > 0) rq.shape.push_back({ 0, 0, mmOf(f.bodyWidth), mmOf(f.bodyHeight), 0 });
    rq.partHeightMm = partHeightMm;
    const std::string preRotate = v->text("pre-rotate-usage", "Default");
    const bool pre = preRotate == "AlwaysOn" || (preRotate == "Default" && vision.preRotate);
    rq.imageAngle = pre ? placeAngle : pickAngle;
    rq.angleRange = v->text("max-rotation", "Adjust") == "Full" ? 180 : vision.maxAngularOffset;
    rq.passes = pre ? vision.maxVisionPasses : 1;
    rq.maxLinearOffsetMm = vision.maxLinearOffsetMm;
    // By its OpenPnP pipeline, when the machine finds parts so.
    if (vision.bottomPipeline) {
        rq.pipeline = std::make_shared<JPPipeline>(JPVisionPipelines::of(*v));
        rq.pipeline->context().configurationDirectory = config.directory();
        rq.partId = part.id;
        rq.settingsId = v->id;
    }
    return true;
}

} // inline namespace jf
