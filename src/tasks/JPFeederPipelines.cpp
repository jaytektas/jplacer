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
const std::string* defaultOf(const JPFeeder& f, const std::string& element) {
    const std::string kind = JPFeeder::simpleName(f.className());
    if (element == "pipeline") {
        if (kind == "ReferenceStripFeeder") return &JPDefaultPipelines::stripFeeder();
        if (kind == "ReferenceLoosePartFeeder") return &JPDefaultPipelines::loosePartFeeder();
        if (kind == "AdvancedLoosePartFeeder") return &JPDefaultPipelines::advancedLoosePartFeeder();
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

std::optional<JPPipeline> JPFeederPipelines::of(const JPFeeder& feeder, const std::string& element) {
    const std::string* def = defaultOf(feeder, element);
    if (!def) return std::nullopt;
    if (const JPXmlNode* p = feeder.pipeline(element))
        if (auto held = parse(JPXmlWriter::text(*p))) return held;
    return parse(*def);
}

bool JPFeederPipelines::reset(JPFeeder& feeder, const std::string& element) {
    const std::string* def = defaultOf(feeder, element);
    if (!def) return false;
    const std::optional<JPPipeline> fresh = parse(*def);
    if (!fresh) return false;
    feeder.setPipeline(fresh->toXml(), element);
    return true;
}

void JPFeederPipelines::configureForEditing(const JPConfiguration& config, const JPFeeder& feeder, JPPipeline& pipeline) {
    const std::string kind = JPFeeder::simpleName(feeder.className());
    const auto& ctx = pipeline.context();
    if (kind == "ReferenceLoosePartFeeder" || kind == "AdvancedLoosePartFeeder") {
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

} // inline namespace jf
