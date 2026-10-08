// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A pipeline as it is kept (JPPipeline::stored): what its parameter stages wrote into the stages they control as it
// ran (an assigned value) is not kept, so running it is not an edit; its parameters' defaults are kept, as OpenPnP
// writes a pipeline; a real change is still one.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"
#include "pipeline/JPPipeline.h"

using namespace jf;

int main() {
    JPXmlElement root;
    std::string error;
    assert(JPXmlReader::parse(R"(<cv-pipeline><stages>
<cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pThreshold" enabled="true" parameter-label="Threshold" stage-name="threshold" property-name="threshold" minimum-value="1.0" maximum-value="254.0" default-value="100.0" numeric-type="Integer"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="threshold" enabled="true" threshold="100" auto="false" invert="false"/>
</stages></cv-pipeline>)",
                              root, error));
    JPPipeline p = JPPipeline::fromXml(root);
    const std::string before = p.storedText();

    // Run with the parameter assigned 180: the stage it controls holds 180 while it runs...
    p.setProperty("pThreshold", JPPipelineValue { 180L });
    std::string why;
    p.process(why);   // no picture: the threshold fails, after the parameter is applied
    assert(p.stage("threshold")->integer("threshold") == 180);
    // ...but as kept it is unchanged: the parameter's default there, the run's value not taken for an edit.
    assert(p.storedText() == before);
    assert(p.stored().stage("threshold")->integer("threshold") == 100);
    // A real change is one.
    p.stage("threshold")->set("invert", "true");
    assert(p.storedText() != before);
    return 0;
}
