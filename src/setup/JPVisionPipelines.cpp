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

bool JPVisionPipelines::ensureStock(JPConfiguration& config) {
    using Kind = JPVisionSettings::Kind;
    bool changed = false;
    // One there, with its name, holding the stock pipeline.
    auto stockOf = [&](Kind kind, const std::string& id, const std::string& name, const std::string& xml) {
        if (!config.visionSettings(id)) {
            JPVisionSettings v = JPVisionSettings::create(kind, id);
            v.name = name;
            v.enabled = true;
            config.addVisionSettings(std::move(v));
            changed = true;
        }
        JPVisionSettings& v = *config.visionSettings(id);
        const JPPipeline want = parse(xml);
        // Compared as pipelines (each written the same way), not as the text it was read from.
        if (!v.pipeline() || JPXmlWriter::text(of(v).toXml()) != JPXmlWriter::text(want.toXml())) {
            set(v, want);
            changed = true;
        }
    };
    // The machine's default, on a fresh configuration: the stock pipeline.
    auto defaultOf = [&](Kind kind, const std::string& id, const std::string& name, const std::string& stockId) {
        if (config.visionSettings(id)) return;
        JPVisionSettings v = JPVisionSettings::create(kind, id);
        v.name = name;
        v.enabled = true;
        set(v, of(*config.visionSettings(stockId)));
        config.addVisionSettings(std::move(v));
        changed = true;
    };
    const bool freshBottom = !config.visionSettings(JPVisionSettings::kStockBottomId);
    const bool freshFiducial = !config.visionSettings(JPVisionSettings::kStockFiducialId);
    stockOf(Kind::Bottom, JPVisionSettings::kStockBottomId, "- Stock Bottom Vision Settings -", JPDefaultPipelines::bottomVision());
    stockOf(Kind::Bottom, JPVisionSettings::kStockBottomRectlinearId, "- Rectlinear Symmetry Bottom Vision Settings -",
            JPDefaultPipelines::bottomVisionRectlinear());
    // In OpenPnP's order (a fresh configuration's default before the whole part body's, as its migration adds them).
    if (freshBottom)
        defaultOf(Kind::Bottom, JPVisionSettings::kDefaultBottomId, "- Default Machine Bottom Vision -", JPVisionSettings::kStockBottomId);
    stockOf(Kind::Bottom, JPVisionSettings::kStockBottomBodyId, "- Whole Part Body Bottom Vision Settings -",
            JPDefaultPipelines::bottomVisionBody());
    stockOf(Kind::Fiducial, JPVisionSettings::kStockFiducialId, "- Stock Fiducial Vision Settings -", JPDefaultPipelines::fiducialLocator());
    stockOf(Kind::Fiducial, JPVisionSettings::kStockFiducialTemplateId, "- Footprint Fiducial Vision Settings -",
            JPDefaultPipelines::fiducialLocatorTemplate());
    if (freshFiducial)
        defaultOf(Kind::Fiducial, JPVisionSettings::kDefaultFiducialId, "- Default Machine Fiducial Locator -",
                  JPVisionSettings::kStockFiducialId);
    return changed;
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
