// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A vision setting's pipeline: OpenPnP's stock one for its kind until it
// holds its own; reset to the machine's default setting's pipeline, or to
// the stock one when it is the default; copied as OpenPnP's text and pasted
// back (text that is no pipeline refused); a parameter's value assigned and
// kept with the setting, the others left as they were.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPVisionPipelines.h"

using namespace jf;

int main() {
    JPVisionSettings bottom = JPVisionSettings::create(JPVisionSettings::Kind::Bottom, "BVS_A");
    JPVisionSettings fiducial = JPVisionSettings::create(JPVisionSettings::Kind::Fiducial, "FVS_A");
    // The stock pipelines: bottom vision's ends drawing what it found; the fiducial locator's finds round marks.
    JPPipeline stockBottom = JPVisionPipelines::of(bottom);
    assert(stockBottom.stage("results") && stockBottom.stage("pThreshold"));
    assert(JPVisionPipelines::of(fiducial).stage("maxDistance"));
    assert(!bottom.pipeline());

    // Its own: one stage fewer, kept.
    JPPipeline mine = stockBottom;
    mine.stages().pop_back();
    JPVisionPipelines::set(bottom, mine);
    assert(bottom.pipeline() && bottom.pipeline()->name == "cv-pipeline");
    assert(JPVisionPipelines::of(bottom).stages().size() == stockBottom.stages().size() - 1);

    // Reset: to the machine's default setting's pipeline…
    JPVisionSettings machineDefault = JPVisionSettings::create(JPVisionSettings::Kind::Bottom, "BVS_Default");
    JPPipeline shorter = stockBottom;
    shorter.stages().erase(shorter.stages().begin() + 3, shorter.stages().end());
    JPVisionPipelines::set(machineDefault, shorter);
    JPVisionPipelines::reset(bottom, &machineDefault);
    assert(JPVisionPipelines::of(bottom).stages().size() == 3);
    // …or, the default itself, to the stock one.
    JPVisionPipelines::reset(machineDefault, &machineDefault);
    assert(JPVisionPipelines::of(machineDefault).stages().size() == stockBottom.stages().size());

    // Copied and pasted; text that is no pipeline refused, the pipeline kept.
    const std::string text = JPVisionPipelines::copy(machineDefault);
    std::string why;
    assert(JPVisionPipelines::paste(fiducial, text, why));
    assert(JPVisionPipelines::of(fiducial).stage("pThreshold"));
    assert(!JPVisionPipelines::paste(fiducial, "<feeders/>", why) && why == "The clipboard holds no pipeline.");
    assert(JPVisionPipelines::of(fiducial).stage("pThreshold"));

    // Values assigned, each kept.
    JPVisionPipelines::assign(bottom, "pThreshold", JPPipelineValue { 204L });
    JPVisionPipelines::assign(bottom, "pDetail", JPPipelineValue { JPPipelineValue::AreaMm2 { 0.02 } });
    JPVisionPipelines::assign(bottom, "pThreshold", JPPipelineValue { 150L });
    const auto values = JPVisionPipelines::assignments(bottom);
    assert(values.size() == 2 && std::get<long>(values.at("pThreshold").value) == 150);
    assert(bottom.parameterAssignments() && bottom.toXml().children[0].name == "cv-pipeline"
           && bottom.toXml().children[1].name == "pipeline-parameter-assignments");
    return 0;
}
