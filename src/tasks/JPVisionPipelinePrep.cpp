// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionPipelinePrep.h"

#include "model/JPLength.h"
#include "pipeline/JPPipelineAssignments.h"

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

// The stand-in fiducial's diameter (VisionUtils.readyHomingFiducialWithDiameter).
constexpr double kStandInFiducialMm = 1.0;
// ReferenceNozzleTip's maxPartDiameter and maxPickTolerance.
constexpr double kMaxPartDiameterMm = 20.0, kMaxPickToleranceMm = 1.0;
// The bottom vision's sampling size when the nozzle tip has none, and the
// least it may be in pixels.
constexpr double kSamplingMm = 0.1, kLeastSamplingPx = 2.0;
// A search a little wider than the pick may be off.
constexpr double kSearchMargin = 1.2;

JPPipelineValue::Outline outline(const JPFootprint::Outline& o, double mm) {
    JPPipelineValue::Outline out;
    for (const JPFootprint::Point& p : o) out.push_back({ p.x * mm, p.y * mm });
    return out;
}

JPPipelineValue::Footprint footprintOf(const JPFootprint& f) {
    const double mm = JPLength(1, f.units).convertToUnits(JPLengthUnit::Millimeters).value();
    JPPipelineValue::Footprint out;
    for (const JPFootprint::Outline& o : f.padsOutlines()) out.pads.push_back(outline(o, mm));
    out.body = outline(f.bodyOutline(), mm);
    return out;
}

JPPipelineValue::Part partOf(const std::string& partId, const JPPackage* pkg) {
    JPPipelineValue::Part p;
    p.id = partId;
    if (pkg) {
        p.packageId = pkg->id;
        p.hasFootprint = true;
        const double mm = JPLength(1, pkg->footprint.units).convertToUnits(JPLengthUnit::Millimeters).value();
        p.bodyWidthMm = pkg->footprint.bodyWidth * mm;
        p.bodyHeightMm = pkg->footprint.bodyHeight * mm;
    }
    return p;
}

// The pads' bounds, in millimetres.
void padBounds(const JPPipelineValue::Footprint& f, double& w, double& h) {
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (const auto& o : f.pads)
        for (const auto& p : o) x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
    w = x0 <= x1 ? x1 - x0 : 0;
    h = y0 <= y1 ? y1 - y0 : 0;
}

const JPPackage* packageFor(const JPConfiguration& config, const std::string& partId, const std::string& packageId) {
    if (!packageId.empty())
        if (const JPPackage* p = config.package(packageId)) return p;
    if (!partId.empty())
        if (const JPPart* part = config.part(partId)) return config.package(part->packageId);
    return nullptr;
}

void assignParameters(JPPipeline& pipeline, const JPVisionSettings& settings) {
    for (const auto& [name, value] : JPPipelineAssignments::fromXml(settings.parameterAssignments())) pipeline.setProperty(name, value);
}

} // namespace

void JPVisionPipelinePrep::fiducial(JPPipeline& pipeline, const JPConfiguration& config, const JPVisionSettings& settings,
                                    const std::string& partId, const std::string& packageId, double rotation, double maxDistanceMm) {
    const JPPackage* pkg = packageFor(config, partId, packageId);
    JPPipelineValue::Footprint footprint;
    if (pkg && !pkg->footprint.pads.empty()) {
        footprint = footprintOf(pkg->footprint);
    } else {
        // A round 1 mm pad.
        JPFootprint stand;
        JPFootprint::Pad pad;
        pad.width = pad.height = kStandInFiducialMm;
        pad.roundness = 100;
        stand.pads.push_back(pad);
        footprint = footprintOf(stand);
    }
    pipeline.setProperty("part", JPPipelineValue { partOf(partId, pkg) });
    pipeline.setProperty("footprint", JPPipelineValue { footprint });
    pipeline.setProperty("footprint.rotation", JPPipelineValue { rotation });
    double w = 0, h = 0;
    padBounds(footprint, w, h);
    pipeline.setProperty("fiducial.diameter", JPPipelineValue { JPPipelineValue::LengthMm { std::max(w, h) } });
    if (!pipeline.stage("maxDistance"))
        pipeline.setProperty("fiducial.maxDistance", JPPipelineValue { JPPipelineValue::LengthMm { maxDistanceMm } });
    assignParameters(pipeline, settings);
}

bool JPVisionPipelinePrep::bottom(JPPipeline& pipeline, const JPConfiguration& config, const JPVisionSettings& settings,
                                  const std::string& partId, const std::string& packageId, double rotation, std::string& why) {
    const JPPackage* pkg = packageFor(config, partId, packageId);
    if (!pkg) {
        why = "A package must be designated to configure the pipeline. Please select a single part or package on the "
              "Parts or Packages tab.";
        return false;
    }
    const auto& ctx = pipeline.context();
    const double pxPerMm = (ctx.pixelsPerMmX + ctx.pixelsPerMmY) / 2;
    const JPPipelineValue::Footprint footprint = footprintOf(pkg->footprint);
    pipeline.setProperty("part", JPPipelineValue { partOf(partId, pkg) });
    pipeline.setProperty("footprint", JPPipelineValue { footprint });
    pipeline.setProperty("footprint.rotation", JPPipelineValue { rotation });
    // The part where it should be: over the camera's centre.
    const JPPipelineValue centre { JPPipelineValue::Pixel { ctx.cameraWidth / 2.0, ctx.cameraHeight / 2.0 } };
    pipeline.setProperty("MinAreaRect.center", centre);
    pipeline.setProperty("MinAreaRect.expectedAngle", JPPipelineValue { rotation });
    pipeline.setProperty("DetectRectlinearSymmetry.center", centre);
    pipeline.setProperty("DetectRectlinearSymmetry.expectedAngle", JPPipelineValue { rotation });
    pipeline.setProperty("DetectRectlinearSymmetry.searchDistance", JPPipelineValue { JPPipelineValue::LengthMm { kMaxPickToleranceMm * kSearchMargin } });
    // One shot, the whole part: the mask as wide as the camera sees, up to the largest part.
    double viewMm = kMaxPartDiameterMm;
    if (pxPerMm > 0 && ctx.cameraWidth > 0)
        viewMm = std::min(kMaxPartDiameterMm, std::min(ctx.cameraWidth / ctx.pixelsPerMmX, ctx.cameraHeight / ctx.pixelsPerMmY));
    pipeline.setProperty("MaskCircle.diameter", JPPipelineValue { JPPipelineValue::LengthMm { viewMm } });
    const double maxDim = std::sqrt(2.0) * viewMm / 2 - kMaxPickToleranceMm * kSearchMargin;
    pipeline.setProperty("footprint.maxWidth", JPPipelineValue { JPPipelineValue::LengthMm { maxDim } });
    pipeline.setProperty("footprint.maxHeight", JPPipelineValue { JPPipelineValue::LengthMm { maxDim } });
    double sampling = kSamplingMm;
    if (pxPerMm > 0) sampling = std::max(sampling, kLeastSamplingPx / pxPerMm);
    pipeline.setProperty("BlurGaussian.kernelSize", JPPipelineValue { JPPipelineValue::LengthMm { sampling } });
    pipeline.setProperty("DetectRectlinearSymmetry.subSampling", JPPipelineValue { JPPipelineValue::LengthMm { sampling } });
    // The shot's size, the pick's tolerance round it, a margin for the edges.
    double w = 0, h = 0;
    padBounds(footprint, w, h);
    const double mm = JPLength(1, pkg->footprint.units).convertToUnits(JPLengthUnit::Millimeters).value();
    w = std::max(w, pkg->footprint.bodyWidth * mm) + 2 * kMaxPickToleranceMm;
    h = std::max(h, pkg->footprint.bodyHeight * mm) + 2 * kMaxPickToleranceMm;
    pipeline.setProperty("DetectRectlinearSymmetry.maxWidth", JPPipelineValue { JPPipelineValue::LengthMm { w + 2 * sampling } });
    pipeline.setProperty("DetectRectlinearSymmetry.maxHeight", JPPipelineValue { JPPipelineValue::LengthMm { h + 2 * sampling } });
    assignParameters(pipeline, settings);
    return true;
}

} // inline namespace jf
