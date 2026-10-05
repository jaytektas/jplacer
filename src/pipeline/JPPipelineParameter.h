// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPipeline.h"
#include "JPPipelineValue.h"

#include <string>
#include <vector>

inline namespace jf {

// One of a pipeline's parameters (a ParameterNumeric or ParameterBool
// stage, OpenPnP's CvAbstractScalarParameterStage) as a slider shows it: a
// whole number from minimumScalar to maximumScalar, and the value it stands
// for (a number spread over its range, squared or exponential as its type
// says; a flag, inverted when asked).
class JPPipelineParameter {
public:
    // The pipeline's enabled parameter stages, in order.
    static std::vector<JPPipelineParameter> of(const JPPipeline& pipeline);

    const std::string& name() const { return m_name; }             // its stage's name: what is assigned
    const std::string& label() const { return m_label; }
    const std::string& description() const { return m_description; }
    const std::string& effectStage() const { return m_effectStage; }   // shown when changed (none: "")
    bool               previewResult() const { return m_previewResult; }

    int             minimumScalar() const { return 0; }
    int             maximumScalar() const { return m_numeric ? 1000 : 1; }
    int             toScalar(const JPPipelineValue* value) const;   // null: its default
    JPPipelineValue toValue(int scalar) const;
    JPPipelineValue defaultValue() const;
    // As OpenPnP's displayValue: "204", "0.500", "0.0178 mm²", "On".
    std::string     display(const JPPipelineValue& value) const;

private:
    double asNumber(const JPPipelineValue* value) const;
    double toScale(double v) const;
    double fromScale(double v) const;
    JPPipelineValue typed(double v) const;

    std::string m_name, m_label, m_description, m_effectStage, m_type;
    bool        m_numeric = true, m_previewResult = true, m_invert = false, m_defaultFlag = false;
    double      m_minimum = 0, m_maximum = 1, m_default = 0.5;
};

} // inline namespace jf
