// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAlignRequests.h"

#include "model/JPLength.h"
#include "pipeline/JPStageUtil.h"
#include "setup/JPVisionPipelines.h"

inline namespace jf {

bool JPAlignRequests::forPart(const JPConfiguration& config, const JPVisionConfig& vision, const JPPart& part, double partHeightMm,
                              double placeAngle, JPJobMachine::AlignRequest& rq,
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
    // OpenPnP's findOffsets: pre-rotated to the placement's angle, else looked at 0°.
    JPBottomVision::Settings& o = rq.offsets;
    o.partId = part.id;
    o.preRotate = pre;
    o.wantedAngle = placeAngle;
    o.maxVisionPasses = vision.maxVisionPasses;
    o.maxLinearOffsetMm = vision.maxLinearOffsetMm;
    o.maxAngularOffset = vision.maxAngularOffset;
    o.fullRotation = v->text("max-rotation", "Adjust") == "Full";
    o.visionOffset = v->locationOf("vision-offset").convertToUnits(JPLengthUnit::Millimeters);
    o.partCheckSizeMm = JPBottomVision::partCheckSize(*v, f);
    o.checkSizeTolerancePercent = v->number("check-size-tolerance-percent", 20);
    rq.imageAngle = pre ? JPStageUtil::angleNorm(placeAngle, 180) : 0;
    rq.angleRange = o.fullRotation ? 180 : vision.maxAngularOffset;
    // By its OpenPnP pipeline, when the machine finds parts so.
    if (vision.bottomPipeline) {
        rq.pipeline = std::make_shared<JPPipeline>(JPVisionPipelines::of(*v));
        rq.pipeline->context().configurationDirectory = config.directory();
        rq.pipeline->context().label = "bottom vision";
        rq.partId = part.id;
        rq.settingsId = v->id;
    }
    return true;
}

} // inline namespace jf
