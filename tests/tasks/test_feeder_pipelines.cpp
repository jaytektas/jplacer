// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A strip feeder's pipeline: OpenPnP's default when it holds none (its old
// ImageCapture "settle-first" read as settling), the one it holds once
// kept, the default back on reset; what the editor sets on it, in pixels
// through the camera's scale; and no pipeline for a kind without one.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPFeederPipelines.h"

#include <cmath>

using namespace jf;

int main() {
    JPFeeder strip = JPFeeder::create("org.openpnp.machine.reference.feeder.ReferenceStripFeeder", "R1");
    std::optional<JPPipeline> p = JPFeederPipelines::of(strip);
    assert(p && p->stages().size() == 7);
    assert(p->stage("results")->typeName() == "DetectCircularSymmetry");
    // settle-first="true" in OpenPnP's default: now to settle, the old setting gone.
    JPPipelineStage& capture = p->stages().front();
    assert(capture.text("settle-option") == "Settle" && !capture.toXml().get("settle-first"));

    // Kept, then read back as kept.
    p->stages().pop_back();
    strip.setPipeline(p->toXml());
    assert(strip.pipeline() && strip.pipeline()->name == "pipeline");
    assert(JPFeederPipelines::of(strip)->stages().size() == 6);
    // Reset: the default again.
    assert(JPFeederPipelines::reset(strip));
    assert(JPFeederPipelines::of(strip)->stages().size() == 7);

    // At 20 px/mm on a 640 x 480 camera: holes 1.5 mm (27 to 33 px), pitch 4 mm (72 px least), searched half the picture's height (12 mm) about.
    JPPipeline edited = *JPFeederPipelines::of(strip);
    edited.context().pixelsPerMmX = edited.context().pixelsPerMmY = 20;
    edited.context().cameraWidth = 640;
    edited.context().cameraHeight = 480;
    JPFeederPipelines::configureForEditing(strip, edited);
    assert(std::get<long>(edited.property("DetectFixedCirclesHough.minDiameter")->value) == 27);
    assert(std::get<long>(edited.property("DetectFixedCirclesHough.maxDiameter")->value) == 33);
    assert(std::get<long>(edited.property("DetectFixedCirclesHough.minDistance")->value) == 72);
    assert(std::abs(std::get<JPPipelineValue::LengthMm>(edited.property("sprocketHole.diameter")->value).mm - 1.5) < 1e-9);
    assert(std::abs(std::get<JPPipelineValue::LengthMm>(edited.property("sprocketHole.maxDistance")->value).mm - 12) < 1e-9);

    // A drag feeder has no pipeline here.
    JPFeeder drag = JPFeeder::create("org.openpnp.machine.reference.feeder.ReferenceDragFeeder", "R1");
    assert(!JPFeederPipelines::of(drag) && !JPFeederPipelines::reset(drag));
    return 0;
}
