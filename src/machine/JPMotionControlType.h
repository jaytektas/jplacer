// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <optional>
#include <string>

inline namespace jf {

// OpenPnP's Driver.MotionControlType: how the motion planner plans a move and how it talks to the controller,
// in OpenPnP's order (the order matters: the "controlling" questions below are ranges of it).
//   ToolpathFeedRate: the driver's feed rate (times the speed factor) along the tool path, no acceleration control.
//   EuclideanAxisLimits: the axes' feed rate, acceleration and jerk limits (times the speed factors), over the
//     Euclidean metric so a diagonal may go faster; the profile is the controller's.
//   ConstantAcceleration: planned for a controller with constant acceleration.
//   ModeratedConstantAcceleration: as ConstantAcceleration, the acceleration and velocity moderated to resemble
//     3rd order control, the move taking the same time.
//   SimpleSCurve: planned for a controller with simplified S-curves (no constant acceleration phase: TinyG, Marlin).
//   Simulated3rdOrderControl: constant acceleration on the controller, 3rd order control simulated by time step
//     interpolation.
//   Full3rdOrderControl: planned for a controller with full 3rd order motion control.
class JPMotionControlType {
public:
    enum Value {
        ToolpathFeedRate,
        EuclideanAxisLimits,
        ConstantAcceleration,
        ModeratedConstantAcceleration,
        SimpleSCurve,
        Simulated3rdOrderControl,
        Full3rdOrderControl,
    };
    static constexpr Value kAll[] = { ToolpathFeedRate,    EuclideanAxisLimits,      ConstantAcceleration, ModeratedConstantAcceleration,
                                      SimpleSCurve,        Simulated3rdOrderControl, Full3rdOrderControl };

    constexpr JPMotionControlType(Value v = ToolpathFeedRate) : m_value(v) {}
    constexpr Value value() const { return m_value; }
    constexpr bool operator==(JPMotionControlType o) const { return m_value == o.m_value; }

    // OpenPnP's names (as kept in its machine.xml and shown in its choice).
    const char* name() const;
    static std::optional<JPMotionControlType> fromName(const std::string& name);

    constexpr bool isConstantAcceleration() const { return m_value == ToolpathFeedRate || m_value == ConstantAcceleration; }
    constexpr bool isInterpolated() const { return m_value == Simulated3rdOrderControl; }
    constexpr bool isSupportingUncoordinated() const { return m_value == Simulated3rdOrderControl || m_value == Full3rdOrderControl; }
    constexpr bool isUnpredictable() const { return m_value <= EuclideanAxisLimits; }
    constexpr bool isControllingFeedRate() const { return m_value >= EuclideanAxisLimits; }
    constexpr bool isControllingAcceleration() const { return m_value >= ConstantAcceleration; }
    // Only the SimpleSCurve really needs the jerk; the others fall back to constant acceleration without it.
    constexpr bool isControllingJerk() const { return m_value == SimpleSCurve; }

private:
    Value m_value;
};

} // inline namespace jf
