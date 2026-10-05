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

std::shared_ptr<JPVisionComposite> JPVisionPipelinePrep::composite(const JPPackage& pkg, const JPVisionSettings& settings,
                                                                   const JPCameraConfig* camera, double cameraWidthMm,
                                                                   double cameraHeightMm, const JPNozzleTipConfig* tip) {
    const JPNozzleTipConfig defaults;
    JPVisionComposite::Input in;
    in.packageId = pkg.id;
    in.footprint = pkg.footprint.inMillimeters();
    in.compositing = pkg.visionCompositing.value_or(JPVisionCompositing {});
    in.toleranceMm = (tip ? *tip : defaults).maxPickToleranceMm;
    in.maxPartDiameterMm = (tip ? *tip : defaults).maxPartDiameterMm;
    in.cameraWidthMm = cameraWidthMm;
    in.cameraHeightMm = cameraHeightMm;
    in.roamingRadiusMm = camera ? camera->roamingRadiusMm : 0;
    in.cameraName = camera ? camera->name : std::string();
    const JPLocation offset = settings.locationOf("vision-offset");
    in.visionOffsets = offset.x() != 0 || offset.y() != 0 || offset.z() != 0 || offset.rotation() != 0;
    in.settingsName = settings.name;
    return std::make_shared<JPVisionComposite>(in);
}

bool JPVisionPipelinePrep::bottom(JPPipeline& pipeline, const JPConfiguration& config, const JPVisionSettings& settings,
                                  const std::string& partId, const std::string& packageId, double rotation,
                                  const JPNozzleTipConfig* tip, const JPCameraConfig* camera, std::string& why,
                                  std::shared_ptr<JPVisionComposite>* made) {
    const JPPackage* pkg = packageFor(config, partId, packageId);
    if (!pkg) {
        why = "A package must be designated to configure the pipeline. Please select a single part or package on the "
              "Parts or Packages tab.";
        return false;
    }
    const auto& ctx = pipeline.context();
    const double cameraWidthMm = ctx.pixelsPerMmX > 0 ? ctx.cameraWidth / ctx.pixelsPerMmX : 0;
    const double cameraHeightMm = ctx.pixelsPerMmY > 0 ? ctx.cameraHeight / ctx.pixelsPerMmY : 0;
    auto c = composite(*pkg, settings, camera, cameraWidthMm, cameraHeightMm, tip);
    if (JPVisionCompositing::isEnforced(pkg->visionCompositing.value_or(JPVisionCompositing {}).compositingMethod)
        && JPVisionComposite::isInvalid(c->solution())) {
        why = "Vision Compositing has not found a valid solution for package " + pkg->id + ". Status: "
            + JPVisionComposite::solutionName(c->solution()) + ", " + c->diagnostics()
            + ". For more diagnostic information go to the Vision Compositing tab on package " + pkg->id + ". ";
        return false;
    }
    const JPNozzleTipConfig defaults;
    const double tipTolerance = (tip ? *tip : defaults).maxPickToleranceMm;
    pipeline.setProperty("part", JPPipelineValue { partOf(partId, pkg) });
    pipeline.setProperty("footprint", JPPipelineValue { footprintOf(pkg->footprint) });
    pipeline.setProperty("footprint.rotation", JPPipelineValue { rotation });
    // The part where it should be: over the camera's centre.
    const JPPipelineValue centre { JPPipelineValue::Pixel { ctx.cameraWidth / 2.0, ctx.cameraHeight / 2.0 } };
    pipeline.setProperty("MinAreaRect.center", centre);
    pipeline.setProperty("MinAreaRect.expectedAngle", JPPipelineValue { rotation });
    pipeline.setProperty("DetectRectlinearSymmetry.center", centre);
    pipeline.setProperty("DetectRectlinearSymmetry.expectedAngle", JPPipelineValue { rotation });
    pipeline.setProperty("DetectRectlinearSymmetry.searchDistance",
                         JPPipelineValue { JPPipelineValue::LengthMm { tipTolerance * kSearchMargin } });
    const std::vector<const JPVisionComposite::Shot*> travel = c->travel(0, 0);
    shot(pipeline, *c, *travel.front(), tip, ctx.cameraWidth / 2.0, ctx.cameraHeight / 2.0);
    assignParameters(pipeline, settings);
    if (made) *made = c;
    return true;
}

void JPVisionPipelinePrep::shot(JPPipeline& pipeline, const JPVisionComposite& composite, const JPVisionComposite::Shot& shot,
                                const JPNozzleTipConfig* tip, double partX, double partY) {
    const auto& ctx = pipeline.context();
    const double pxPerMm = (ctx.pixelsPerMmX + ctx.pixelsPerMmY) / 2;
    const JPNozzleTipConfig defaults;
    const double tipTolerance = (tip ? *tip : defaults).maxPickToleranceMm;
    auto length = [](double mm) { return JPPipelineValue { JPPipelineValue::LengthMm { mm } }; };
    // The footprint moved to the shot, cropped to fit in its mask.
    pipeline.setProperty("footprint.xOffset", length(shot.x));
    pipeline.setProperty("footprint.yOffset", length(shot.y));
    const double maxDim = std::sqrt(2.0) * shot.maxMaskRadius - tipTolerance * kSearchMargin;
    pipeline.setProperty("footprint.maxWidth", length(maxDim));
    pipeline.setProperty("footprint.maxHeight", length(maxDim));
    // The corner masked.
    pipeline.setProperty("MaskCircle.diameter", length(shot.maxMaskRadius * 2));
    // The background masked as the tip's background calibration found it (each range widened by its
    // tolerance; saturation and value open where OpenPnP leaves them open), sampled at half its smallest detail.
    double sampling = kSamplingMm;
    if (tip && tip->background.method != "None") {
        const JPNozzleTipConfig::Background& b = tip->background;
        sampling = b.minimumDetailSizeMm * 0.5;
        auto value = [](int v) { return JPPipelineValue { double(v) }; };
        pipeline.setProperty("MaskHsv.hueMin", value(std::max(0, b.minHue - b.tolHue)));
        pipeline.setProperty("MaskHsv.hueMax", value(std::min(255, b.maxHue + b.tolHue)));
        pipeline.setProperty("MaskHsv.saturationMin", value(std::max(0, b.minSaturation - b.tolSaturation)));
        pipeline.setProperty("MaskHsv.saturationMax", value(255));
        pipeline.setProperty("MaskHsv.valueMin", value(0));
        pipeline.setProperty("MaskHsv.valueMax", value(std::min(255, b.maxValue + b.tolValue)));
    }
    // At least two pixels a sample, or sub-sampling costs too much.
    if (pxPerMm > 0) sampling = std::max(sampling, kLeastSamplingPx / pxPerMm);
    pipeline.setProperty("BlurGaussian.kernelSize", length(sampling));
    pipeline.setProperty("DetectRectlinearSymmetry.subSampling", length(sampling));
    // A margin for finding the edges.
    pipeline.setProperty("DetectRectlinearSymmetry.maxWidth", length(shot.width + 2 * sampling));
    pipeline.setProperty("DetectRectlinearSymmetry.maxHeight", length(shot.height + 2 * sampling));
    if (JPVisionComposite::isAdvanced(composite.solution())) {
        // The whole part masked, and the edges this shot sees.
        pipeline.setProperty("partmask.diameter", length((composite.maxPadRadius() + tipTolerance) * 2));
        pipeline.setProperty("partmask.center", JPPipelineValue { JPPipelineValue::Pixel { partX, partY } });
        pipeline.setProperty("MinAreaRect.leftEdge", JPPipelineValue { shot.hasLeftEdge() });
        pipeline.setProperty("MinAreaRect.rightEdge", JPPipelineValue { shot.hasRightEdge() });
        pipeline.setProperty("MinAreaRect.topEdge", JPPipelineValue { shot.hasTopEdge() });
        pipeline.setProperty("MinAreaRect.bottomEdge", JPPipelineValue { shot.hasBottomEdge() });
        pipeline.setProperty("MinAreaRect.searchAngle",
                             JPPipelineValue { std::atan2(composite.tolerance(), composite.maxCornerRadius()) * 180 / M_PI });
    }
}

} // inline namespace jf
