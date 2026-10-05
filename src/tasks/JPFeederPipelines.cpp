// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederPipelines.h"

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

bool isStrip(const JPFeeder& f) { return JPFeeder::simpleName(f.className()) == "ReferenceStripFeeder"; }

std::optional<JPPipeline> parse(const std::string& xml) {
    JPXmlElement root;
    std::string error;
    if (!JPXmlReader::parse(xml, root, error)) return std::nullopt;
    return JPPipeline::fromXml(root);
}

} // namespace

std::optional<JPPipeline> JPFeederPipelines::of(const JPFeeder& feeder) {
    if (!isStrip(feeder)) return std::nullopt;
    if (const JPXmlNode* p = feeder.pipeline())
        if (auto held = parse(JPXmlWriter::text(*p))) return held;
    return parse(JPDefaultPipelines::stripFeeder());
}

bool JPFeederPipelines::reset(JPFeeder& feeder) {
    if (!isStrip(feeder)) return false;
    const std::optional<JPPipeline> fresh = parse(JPDefaultPipelines::stripFeeder());
    if (!fresh) return false;
    feeder.setPipeline(fresh->toXml());
    return true;
}

void JPFeederPipelines::configureForEditing(const JPFeeder& feeder, JPPipeline& pipeline) {
    if (!isStrip(feeder)) return;
    const auto& ctx = pipeline.context();
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
