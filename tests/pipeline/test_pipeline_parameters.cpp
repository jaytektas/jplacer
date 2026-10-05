// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A vision setting's parameter values read as OpenPnP wrote them (an
// Integer, an Area in square millimetres, a Length in inches, a Boolean)
// and written back the same way; a parameter's slider: a threshold spread
// over 0..1000, an area squared along it, a flag inverted, each value turned
// to the slider and back, and shown as OpenPnP shows it.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"
#include "pipeline/JPPipelineAssignments.h"
#include "pipeline/JPPipelineParameter.h"

#include <cmath>

using namespace jf;

int main() {
    JPXmlElement root;
    std::string error;
    assert(JPXmlReader::parse(R"(<pipeline-parameter-assignments class="java.util.HashMap">
         <entry><string>pDetail</string><object class="org.openpnp.model.Area" value="0.01782225" units="SquareMillimeters"/></entry>
         <entry><string>pThreshold</string><object class="java.lang.Integer">204</object></entry>
         <entry><string>pReach</string><object class="org.openpnp.model.Length" value="0.1" units="Inches"/></entry>
         <entry><string>pOn</string><object class="java.lang.Boolean">true</object></entry>
      </pipeline-parameter-assignments>)",
                              root, error));
    const JPXmlNode node = JPXmlNode::from(root);
    const JPPipelineAssignments::Map m = JPPipelineAssignments::fromXml(&node);
    assert(m.size() == 4);
    assert(std::get<long>(m.at("pThreshold").value) == 204);
    assert(std::abs(std::get<JPPipelineValue::AreaMm2>(m.at("pDetail").value).mm2 - 0.01782225) < 1e-12);
    assert(std::abs(std::get<JPPipelineValue::LengthMm>(m.at("pReach").value).mm - 2.54) < 1e-12);
    assert(std::get<bool>(m.at("pOn").value));
    // Written back: read again the same.
    const JPXmlNode back = JPPipelineAssignments::toXml(m);
    assert(back.name == "pipeline-parameter-assignments" && *back.get("class") == "java.util.HashMap" && back.children.size() == 4);
    const JPPipelineAssignments::Map again = JPPipelineAssignments::fromXml(&back);
    assert(std::get<long>(again.at("pThreshold").value) == 204 && std::get<bool>(again.at("pOn").value));
    assert(std::abs(std::get<JPPipelineValue::LengthMm>(again.at("pReach").value).mm - 2.54) < 1e-12);

    // The stock bottom vision pipeline's parameters, and a flag.
    assert(JPXmlReader::parse(R"(<cv-pipeline><stages>
<cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pThreshold" enabled="true" parameter-label="Threshold" parameter-description="Set the brightness threshold." stage-name="threshold" property-name="threshold" effect-stage-name="threshold" preview-result="true" minimum-value="1.0" maximum-value="254.0" default-value="100.0" numeric-type="Integer"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pDetail" enabled="true" parameter-label="Min. Detail Size" parameter-description="" stage-name="filterContours" property-name="minArea" effect-stage-name="contours" preview-result="true" minimum-value="0.0" maximum-value="0.25" default-value="0.01" numeric-type="SquareMillimetersToPixels"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ParameterBool" name="pInvert" enabled="true" parameter-label="Dark parts" stage-name="threshold" property-name="invert" effect-stage-name="" preview-result="false" invert="true" default-value="false"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.ParameterBool" name="pOff" enabled="false" parameter-label="Off" stage-name="threshold" property-name="auto"/>
<cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="threshold" enabled="true" threshold="100" auto="false" invert="false"/>
</stages></cv-pipeline>)",
                              root, error));
    const std::vector<JPPipelineParameter> params = JPPipelineParameter::of(JPPipeline::fromXml(root));
    assert(params.size() == 3);   // the disabled one left out
    const JPPipelineParameter& threshold = params[0];
    assert(threshold.name() == "pThreshold" && threshold.label() == "Threshold" && threshold.effectStage() == "threshold");
    assert(threshold.minimumScalar() == 0 && threshold.maximumScalar() == 1000);
    // 204 of 1..254: 802 of 1000 along; back to 204; its default (100) when nothing assigned.
    assert(threshold.toScalar(&m.at("pThreshold")) == 802);
    assert(std::get<long>(threshold.toValue(802).value) == 204);
    assert(threshold.toScalar(nullptr) == 391 && threshold.display(threshold.defaultValue()) == "100");
    // An area: square roots spread evenly (0.01 mm² of 0.25 mm²: a fifth of the way).
    const JPPipelineParameter& detail = params[1];
    assert(detail.toScalar(nullptr) == 200);
    assert(std::abs(std::get<JPPipelineValue::AreaMm2>(detail.toValue(200).value).mm2 - 0.01) < 1e-12);
    assert(detail.display(detail.toValue(200)) == "0.0100 mm²");
    // A flag, inverted for the user: on is the slider's left.
    const JPPipelineParameter& flag = params[2];
    assert(flag.maximumScalar() == 1 && flag.toScalar(nullptr) == 0);
    const JPPipelineValue on { true };
    assert(flag.toScalar(&on) == 0 && std::get<bool>(flag.toValue(0).value) && flag.display(on) == "On");
    return 0;
}
