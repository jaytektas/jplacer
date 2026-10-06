// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMotionControlType.h"

inline namespace jf {

const char* JPMotionControlType::name() const {
    switch (m_value) {
        case ToolpathFeedRate: return "ToolpathFeedRate";
        case EuclideanAxisLimits: return "EuclideanAxisLimits";
        case ConstantAcceleration: return "ConstantAcceleration";
        case ModeratedConstantAcceleration: return "ModeratedConstantAcceleration";
        case SimpleSCurve: return "SimpleSCurve";
        case Simulated3rdOrderControl: return "Simulated3rdOrderControl";
        case Full3rdOrderControl: return "Full3rdOrderControl";
    }
    return "ToolpathFeedRate";
}

std::optional<JPMotionControlType> JPMotionControlType::fromName(const std::string& name) {
    for (Value v : kAll)
        if (name == JPMotionControlType(v).name()) return JPMotionControlType(v);
    return std::nullopt;
}

} // inline namespace jf
