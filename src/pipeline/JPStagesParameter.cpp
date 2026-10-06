// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's parameter stages (CvAbstractParameterStage): a setting of
// another stage exposed as a parameter of the pipeline, by the stage's own
// name, that its caller assigns (a vision settings page's slider); while
// not assigned, its default. Each run sets the other stage's setting.

#include "JPPipeline.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"
#include "JPVisionUtils.h"

#include <cmath>
#include <sstream>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

// A Java bean property's XML name ("minArea": "min-area").
std::string attributeOf(const std::string& property) {
    std::string out;
    for (const char c : property) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            out += '-';
            out += char(std::tolower(static_cast<unsigned char>(c)));
        } else {
            out += c;
        }
    }
    return out;
}

std::string shown(double v) {
    std::ostringstream s;
    s << v;
    return s.str();
}

// The parameter's name: its stage's, when it does not start like a number
// (the editor's sequential names are not stable).
std::string parameterName(const JPPipelineStage& s) {
    const std::string n = s.name();
    if (n.empty() || std::string("-0123456789").find(n[0]) != std::string::npos) return {};
    return n;
}

// The value the parameter gives its stage's setting (OpenPnP's appliedValue), as written.
std::optional<std::string> applied(const JPPipeline& p, const JPPipelineStage& s, const JPPipelineValue* assigned) {
    if (s.typeName() == "ParameterBool") {
        bool v = s.flag("default-value");
        if (assigned)
            if (const bool* b = std::get_if<bool>(&assigned->value)) v = *b;
        return std::string(v ? "true" : "false");
    }
    const std::string type = s.text("numeric-type");
    double v = s.number("default-value");
    if (assigned) {
        if (const double* d = std::get_if<double>(&assigned->value)) v = *d;
        else if (const long* l = std::get_if<long>(&assigned->value)) v = double(*l);
        else if (const auto* len = std::get_if<JPPipelineValue::LengthMm>(&assigned->value)) v = len->mm;
        else if (const auto* area = std::get_if<JPPipelineValue::AreaMm2>(&assigned->value)) v = area->mm2;
    }
    if (type == "Integer") v = double(JPStageUtil::javaRound(v));
    const auto& ctx = p.context();
    if (type == "MillimetersToPixels") {
        if (ctx.pixelsPerMmX <= 0 || ctx.pixelsPerMmY <= 0) return std::nullopt;
        v = JPVisionUtils::toPixels(JPLength(v, JPLengthUnit::Millimeters), JPVisionUtils::Camera::ofScale(ctx.pixelsPerMmX, ctx.pixelsPerMmY));
    } else if (type == "SquareMillimetersToPixels") {
        if (ctx.pixelsPerMmX <= 0 || ctx.pixelsPerMmY <= 0) return std::nullopt;
        v = JPVisionUtils::toPixels(JPArea(v, JPAreaUnit::SquareMillimeters), JPVisionUtils::Camera::ofScale(ctx.pixelsPerMmX, ctx.pixelsPerMmY));
    }
    return shown(v);
}

// The other stage's setting set (an Integer setting rounded, as OpenPnP's invokeSetter).
void setTarget(JPPipeline& p, JPPipelineStage& target, const std::string& property, std::string value) {
    const std::string attribute = attributeOf(property);
    if (const JPStageType* t = JPStageRegistry::instance().find(target.className()))
        if (const JPStageType::Property* prop = t->property(attribute); prop && prop->kind == Kind::Integer)
            value = std::to_string(JPStageUtil::javaRound(std::strtod(value.c_str(), nullptr)));
    target.set(attribute, value);
    p.noteOverride(target.name(), attribute, value);
}

Output process(JPPipeline& p, JPPipelineStage& s) {
    const std::string name = parameterName(s);
    if (name.empty())
        throw std::runtime_error("Please assign a stable name to the stage. This name will be used as parameter name and should "
                                 "not be changed later, or assigned data will be lost. Names starting with digits are not allowed.");
    const std::string label = s.text("parameter-label"), stageName = s.text("stage-name"), property = s.text("property-name");
    if (label.empty() || stageName.empty() || property.empty()) return Output {};
    JPPipelineStage* target = p.stage(stageName);
    if (!target) throw std::runtime_error("Stage \"" + stageName + "\" not found");
    if (const auto value = applied(p, s, p.property(name))) setTarget(p, *target, property, *value);
    return Output {};
}

std::vector<P> common() {
    return { P { "parameter-label", Kind::Text, "", "Label of the parameter." },
             P { "parameter-description", Kind::Text, "", "Description of the parameter (for tooltip)." },
             P { "stage-name", Kind::StageName, "", "Name of the stage to be controlled by the parameter." },
             P { "property-name", Kind::Text, "", "Name of the property to be controlled by the parameter." },
             P { "effect-stage-name", Kind::StageName, "", "Name of the stage to preview the effect of a parameter change." },
             P { "preview-result", Kind::Flag, "true", "Preview the pipeline result image." } };
}

} // namespace

void JPPipeline::resetToDefaults() {
    for (JPPipelineStage& s : m_stages) {
        if (!s.enabled() || (s.typeName() != "ParameterNumeric" && s.typeName() != "ParameterBool") || parameterName(s).empty())
            continue;
        JPPipelineStage* target = stage(s.text("stage-name"));
        const std::string property = s.text("property-name");
        if (!target || property.empty()) continue;
        if (const auto value = applied(*this, s, nullptr)) setTarget(*this, *target, property, *value);
    }
    m_overrides.clear();
}

void JPStageRegistry::addParameterStages(std::vector<JPStageType>& types) {
    std::vector<P> numeric = common();
    numeric.push_back({ "minimum-value", Kind::Number, "0.0", "Minimum value of the parameter." });
    numeric.push_back({ "maximum-value", Kind::Number, "1.0", "Maximum value of the parameter." });
    numeric.push_back({ "default-value", Kind::Number, "0.5", "Default value of the parameter." });
    numeric.push_back({ "numeric-type", Kind::Choice, "Double", "Type and unit of numeric.",
                        { "Integer", "Double", "Squared", "Exponential", "Millimeters", "MillimetersToPixels", "SquareMillimeters",
                          "SquareMillimetersToPixels" } });
    types.push_back({ std::string(kStages) + "ParameterNumeric", "",
                      "Exposes a numeric stage property as an external parameter to this pipeline.", numeric, process });
    std::vector<P> flag = common();
    flag.push_back({ "invert", Kind::Flag, "false", "Invert the sense of the Boolean, as presented to the user." });
    flag.push_back({ "default-value", Kind::Flag, "false", "Default value of the parameter." });
    types.push_back({ std::string(kStages) + "ParameterBool", "",
                      "Exposes a Boolean stage property as an external parameter to this pipeline.", flag, process });
}

} // inline namespace jf
