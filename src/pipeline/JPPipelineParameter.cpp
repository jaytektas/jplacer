// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelineParameter.h"

#include "JPStageUtil.h"

#include "openpnp/JPXmlWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

std::vector<JPPipelineParameter> JPPipelineParameter::of(const JPPipeline& pipeline) {
    std::vector<JPPipelineParameter> out;
    for (const JPPipelineStage& s : pipeline.stages()) {
        const std::string type = s.typeName();
        if (!s.enabled() || (type != "ParameterNumeric" && type != "ParameterBool")) continue;
        JPPipelineParameter p;
        p.m_name = s.name();
        p.m_label = s.text("parameter-label");
        p.m_description = s.text("parameter-description");
        p.m_effectStage = s.text("effect-stage-name");
        p.m_previewResult = s.flag("preview-result");
        p.m_numeric = type == "ParameterNumeric";
        if (p.m_numeric) {
            p.m_type = s.text("numeric-type");
            p.m_minimum = s.number("minimum-value");
            p.m_maximum = s.number("maximum-value");
            p.m_default = s.number("default-value");
        } else {
            p.m_invert = s.flag("invert");
            p.m_defaultFlag = s.flag("default-value");
        }
        out.push_back(std::move(p));
    }
    return out;
}

double JPPipelineParameter::toScale(double v) const {
    if (m_type == "Squared" || m_type == "SquareMillimeters" || m_type == "SquareMillimetersToPixels") return std::sqrt(std::max(0.0, v));
    if (m_type == "Exponential") return std::log(std::max(0.0, v));
    return v;
}

double JPPipelineParameter::fromScale(double v) const {
    if (m_type == "Squared" || m_type == "SquareMillimeters" || m_type == "SquareMillimetersToPixels") return v * v;
    if (m_type == "Exponential") return std::exp(v);
    return v;
}

JPPipelineValue JPPipelineParameter::typed(double v) const {
    if (m_type == "Integer") return JPPipelineValue { long(JPStageUtil::javaRound(v)) };
    if (m_type == "Millimeters" || m_type == "MillimetersToPixels") return JPPipelineValue { JPPipelineValue::LengthMm { v } };
    if (m_type == "SquareMillimeters" || m_type == "SquareMillimetersToPixels") return JPPipelineValue { JPPipelineValue::AreaMm2 { v } };
    return JPPipelineValue { v };
}

double JPPipelineParameter::asNumber(const JPPipelineValue* value) const {
    if (value) {
        if (const double* d = std::get_if<double>(&value->value)) return *d;
        if (const long* l = std::get_if<long>(&value->value)) return double(*l);
        if (const auto* len = std::get_if<JPPipelineValue::LengthMm>(&value->value)) return len->mm;
        if (const auto* area = std::get_if<JPPipelineValue::AreaMm2>(&value->value)) return area->mm2;
    }
    return m_default;
}

JPPipelineValue JPPipelineParameter::defaultValue() const {
    return m_numeric ? typed(m_default) : JPPipelineValue { m_defaultFlag };
}

int JPPipelineParameter::toScalar(const JPPipelineValue* value) const {
    int scalar;
    if (m_numeric) {
        // Mapped to 0..1 over its range.
        const double lo = toScale(m_minimum), hi = toScale(m_maximum);
        const double ratio = (toScale(asNumber(value)) - lo) / (hi - lo);
        scalar = minimumScalar() + int(JPStageUtil::javaRound(ratio * (maximumScalar() - minimumScalar())));
    } else {
        bool on = m_defaultFlag;
        if (value)
            if (const bool* b = std::get_if<bool>(&value->value)) on = m_invert != *b;
        scalar = on ? maximumScalar() : minimumScalar();
    }
    return std::clamp(scalar, minimumScalar(), maximumScalar());
}

JPPipelineValue JPPipelineParameter::toValue(int scalar) const {
    if (!m_numeric) return JPPipelineValue { m_invert != (scalar != 0) };
    const double ratio = double(scalar - minimumScalar()) / (maximumScalar() - minimumScalar());
    const double lo = toScale(m_minimum), hi = toScale(m_maximum);
    return typed(fromScale(lo + ratio * (hi - lo)));
}

std::string JPPipelineParameter::display(const JPPipelineValue& value) const {
    char buf[48];
    if (const bool* b = std::get_if<bool>(&value.value)) return *b ? "On" : "Off";
    if (const long* l = std::get_if<long>(&value.value)) return std::to_string(*l);
    if (const auto* len = std::get_if<JPPipelineValue::LengthMm>(&value.value)) {
        std::snprintf(buf, sizeof buf, "%.3f", len->mm);
        return buf;
    }
    if (const auto* area = std::get_if<JPPipelineValue::AreaMm2>(&value.value)) {
        std::snprintf(buf, sizeof buf, "%.4f mm²", area->mm2);
        return buf;
    }
    // As Java's String.valueOf(double): "0.5", "100.0".
    if (const double* d = std::get_if<double>(&value.value)) return JPXmlWriter::number(*d);
    return {};
}

} // inline namespace jf
