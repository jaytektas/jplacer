// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederPipelines.h"

#include "model/JPLength.h"
#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"
#include "pipeline/JPDefaultPipelines.h"

#include <algorithm>

inline namespace jf {

namespace {

// EIA-481 tape's sprocket holes, as OpenPnP's ReferenceStripFeeder has them.
constexpr double kHoleDiameterMm = 1.5, kHolePitchMm = 4.0;
// OpenPnP's tolerances on them: the least and most a hole may measure, the least pitch.
constexpr double kLeast = 0.9, kMost = 1.1;

// The default of a kind's pipeline element; null when it has none.
const std::string* defaultOf(const JPFeeder& f, const std::string& element, const JPDropBoxes* boxes) {
    const std::string kind = JPFeeder::simpleName(f.className());
    if (kind == "ReferenceHeapFeeder") {
        std::string colour = "GREEN";
        if (boxes)
            if (const auto box = boxes->box(JPFeederPipelines::dropBoxOf(f, *boxes))) colour = JPDropBoxes::colour(box->name);
        if (element == "feeder-pipeline") return JPDefaultPipelines::heapFeeder("Part", colour);
        if (element == "training-pipeline") return JPDefaultPipelines::heapFeeder("Training", colour);
        return nullptr;
    }
    if (element == "pipeline") {
        if (kind == "ReferenceStripFeeder") return &JPDefaultPipelines::stripFeeder();
        if (kind == "ReferenceLoosePartFeeder") return &JPDefaultPipelines::loosePartFeeder();
        if (kind == "AdvancedLoosePartFeeder") return &JPDefaultPipelines::advancedLoosePartFeeder();
        // By its Vision Type.
        if (kind == "BambooFeederAutoVision" || kind == "ReferencePushPullFeeder") {
            const std::string fallback = kind == "BambooFeederAutoVision" ? "CircularSymmetry" : "ColorKeyed";
            return f.text("pipeline-type", fallback) == "ColorKeyed" ? &JPDefaultPipelines::feederVisionColorKeyed()
                                                                     : &JPDefaultPipelines::feederVisionCircularSymmetry();
        }
    }
    if (element == "training-pipeline" && kind == "AdvancedLoosePartFeeder") return &JPDefaultPipelines::advancedLoosePartFeederTraining();
    return nullptr;
}

std::optional<JPPipeline> parse(const std::string& xml) {
    JPXmlElement root;
    std::string error;
    if (!JPXmlReader::parse(xml, root, error)) return std::nullopt;
    return JPPipeline::fromXml(root);
}

} // namespace

std::optional<JPPipeline> JPFeederPipelines::of(const JPFeeder& feeder, const std::string& element, const JPDropBoxes* boxes) {
    const std::string* def = defaultOf(feeder, element, boxes);
    if (!def) return std::nullopt;
    if (const JPXmlNode* p = feeder.pipeline(element))
        if (auto held = parse(JPXmlWriter::text(*p))) return held;
    return parse(*def);
}

bool JPFeederPipelines::reset(JPFeeder& feeder, const std::string& element, const JPDropBoxes* boxes) {
    const std::string* def = defaultOf(feeder, element, boxes);
    if (!def) return false;
    const std::optional<JPPipeline> fresh = parse(*def);
    if (!fresh) return false;
    feeder.setPipeline(fresh->toXml(), element);
    return true;
}

std::string JPFeederPipelines::dropBoxOf(const JPFeeder& feeder, const JPDropBoxes& boxes) {
    const std::string id = feeder.text("drop-box-id");
    if (boxes.box(id)) return id;
    const std::vector<JPDropBoxes::Box> all = boxes.boxes();
    return all.empty() ? std::string() : all.back().id;
}

std::optional<JPPipeline> JPFeederPipelines::ofDropBox(const JPDropBoxes& boxes, const std::string& boxId) {
    const auto box = boxes.box(boxId);
    if (!box) return std::nullopt;
    if (const JPXmlNode* p = boxes.partPipeline(boxId))
        if (auto held = parse(JPXmlWriter::text(*p))) return held;
    const std::string* def = JPDefaultPipelines::heapFeeder("DropBox", JPDropBoxes::colour(box->name));
    return def ? parse(*def) : std::nullopt;
}

bool JPFeederPipelines::resetDropBox(JPDropBoxes& boxes, const std::string& boxId) {
    const auto box = boxes.box(boxId);
    const std::string* def = box ? JPDefaultPipelines::heapFeeder("DropBox", JPDropBoxes::colour(box->name)) : nullptr;
    const std::optional<JPPipeline> fresh = def ? parse(*def) : std::nullopt;
    if (!fresh) return false;
    boxes.setPartPipeline(boxId, fresh->toXml());
    return true;
}

void JPFeederPipelines::configureForEditing(const JPConfiguration& config, const JPFeeder& feeder, JPPipeline& pipeline) {
    const std::string kind = JPFeeder::simpleName(feeder.className());
    const auto& ctx = pipeline.context();
    if (kind == "ReferenceLoosePartFeeder" || kind == "AdvancedLoosePartFeeder" || kind == "ReferenceHeapFeeder") {
        // Its part: what its template is named after, and its package's body.
        JPPipelineValue::Part part;
        part.id = feeder.partId();
        if (const JPPart* p = config.part(part.id))
            if (const JPPackage* pkg = config.package(p->packageId)) {
                part.packageId = pkg->id;
                part.hasFootprint = true;
                const double mm = JPLength(1, pkg->footprint.units).convertToUnits(JPLengthUnit::Millimeters).value();
                part.bodyWidthMm = pkg->footprint.bodyWidth * mm;
                part.bodyHeightMm = pkg->footprint.bodyHeight * mm;
            }
        pipeline.setProperty("part", JPPipelineValue { part });
        return;
    }
    if (kind == "BambooFeederAutoVision" || kind == "ReferencePushPullFeeder") {
        if (ctx.cameraWidth > 0 && ctx.pixelsPerMmX > 0 && ctx.pixelsPerMmY > 0)
            configureTape(feeder, pipeline, true, ctx.cameraWidth, ctx.cameraHeight, 1 / ctx.pixelsPerMmX, 1 / ctx.pixelsPerMmY);
        return;
    }
    if (kind != "ReferenceStripFeeder") return;
    const double px = (ctx.pixelsPerMmX + ctx.pixelsPerMmY) / 2;
    if (px > 0) {
        pipeline.setProperty("DetectFixedCirclesHough.minDistance", JPPipelineValue { long(kHolePitchMm * kLeast * px) });
        pipeline.setProperty("DetectFixedCirclesHough.minDiameter", JPPipelineValue { long(kHoleDiameterMm * kLeast * px) });
        pipeline.setProperty("DetectFixedCirclesHough.maxDiameter", JPPipelineValue { long(kHoleDiameterMm * kMost * px) });
    }
    pipeline.setProperty("sprocketHole.diameter", JPPipelineValue { JPPipelineValue::LengthMm { kHoleDiameterMm } });
    // Searched over half the camera.
    if (ctx.cameraWidth > 0 && ctx.pixelsPerMmX > 0 && ctx.pixelsPerMmY > 0) {
        const double range = ctx.cameraWidth > ctx.cameraHeight ? ctx.cameraHeight / 2.0 / ctx.pixelsPerMmY
                                                                : ctx.cameraWidth / 2.0 / ctx.pixelsPerMmX;
        pipeline.setProperty("sprocketHole.maxDistance", JPPipelineValue { JPPipelineValue::LengthMm { range } });
    }
}

void JPFeederPipelines::configureTape(const JPFeeder& feeder, JPPipeline& pipeline, bool autoSetup, int width, int height,
                                      double mmPerPixelX, double mmPerPixelY) {
    pipeline.setProperty("sprocketHole.diameter", JPPipelineValue { JPPipelineValue::LengthMm { kHoleDiameterMm } });
    double range = 0;
    if (autoSetup) {
        // The whole picture (its larger size as the radius about its middle), to find holes at its edge.
        range = width > height ? mmPerPixelX * height : mmPerPixelY * width;
    } else {
        // Half the distance between the holes, and a pitch.
        const JPLocation h1 = feeder.locationOf("hole-1-location").convertToUnits(JPLengthUnit::Millimeters);
        range = h1.linearDistanceTo(feeder.locationOf("hole-2-location")) * 0.5 + kHolePitchMm;
    }
    pipeline.setProperty("sprocketHole.maxDistance", JPPipelineValue { JPPipelineValue::LengthMm { range } });
}

} // inline namespace jf
