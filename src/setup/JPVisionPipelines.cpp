// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionPipelines.h"

#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"
#include "pipeline/JPDefaultPipelines.h"

inline namespace jf {

namespace {

JPPipeline parse(const std::string& xml) {
    JPXmlElement root;
    std::string error;
    return JPXmlReader::parse(xml, root, error) ? JPPipeline::fromXml(root) : JPPipeline {};
}

const std::string& stock(const JPVisionSettings& s) {
    return s.kind == JPVisionSettings::Kind::Bottom ? JPDefaultPipelines::bottomVision() : JPDefaultPipelines::fiducialLocator();
}

} // namespace

JPPipeline JPVisionPipelines::of(const JPVisionSettings& settings) {
    if (const JPXmlNode* p = settings.pipeline()) return parse(JPXmlWriter::text(*p));
    return parse(stock(settings));
}

void JPVisionPipelines::set(JPVisionSettings& settings, const JPPipeline& pipeline) { settings.setPipeline(pipeline.toXml()); }

void JPVisionPipelines::reset(JPVisionSettings& settings, const JPVisionSettings* machineDefault) {
    if (machineDefault && machineDefault != &settings) set(settings, of(*machineDefault));
    else set(settings, parse(stock(settings)));
}

std::string JPVisionPipelines::copy(const JPVisionSettings& settings) { return JPXmlWriter::text(of(settings).toXml()); }

bool JPVisionPipelines::paste(JPVisionSettings& settings, const std::string& text, std::string& why) {
    JPXmlElement root;
    if (!JPXmlReader::parse(text, root, why)) return false;
    if (root.name != "cv-pipeline") {
        why = "The clipboard holds no pipeline.";
        return false;
    }
    set(settings, JPPipeline::fromXml(root));
    return true;
}

JPPipelineAssignments::Map JPVisionPipelines::assignments(const JPVisionSettings& settings) {
    return JPPipelineAssignments::fromXml(settings.parameterAssignments());
}

void JPVisionPipelines::assign(JPVisionSettings& settings, const std::string& parameter, const JPPipelineValue& value) {
    JPPipelineAssignments::Map m = assignments(settings);
    m[parameter] = value;
    settings.setParameterAssignments(JPPipelineAssignments::toXml(m));
}

} // inline namespace jf
