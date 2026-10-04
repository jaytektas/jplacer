// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardLocationProcess.h"

#include "common/JPlacerLog.h"
#include "model/JPFiducialFit.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>

#include <cmath>
#include <cstdio>
#include <limits>

inline namespace jf {

namespace {

// OpenPnP's MultiPlacementBoardLocationProperties.
constexpr double kScalingTolerance = 0.05, kShearingTolerance = 0.05, kBoardLocationToleranceMm = 5.0;

const char* const kInstructions[] = {
    "Select two or more (four or more is better) easily identifiable placements in the placements table. They "
    "should be near the corners of the board. Click Next to continue and the camera will move near one of the "
    "selected placements.",
    "Now, manually jog the camera's crosshairs over the center of %s. Try to be as precise as possible. Click Next "
    "to continue to the next placement.",
    "The board's location and rotation have been set. Click Finish to position the camera at the board's origin, "
    "or Cancel to reject the changes.",
};

double distance(const JPLocation& a, const JPLocation& b) {
    const JPLocation x = a.convertToUnits(JPLengthUnit::Millimeters), y = b.convertToUnits(JPLengthUnit::Millimeters);
    return std::hypot(x.x() - y.x(), x.y() - y.y());
}

std::string fmt(const char* f, double a, double b = 0, double c = 0) {
    char buf[200];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

} // namespace

JPBoardLocationProcess::JPBoardLocationProcess(JPPlacementsHolderLocation& location, bool topLevel, Hooks hooks)
    : m_location(location), m_topLevel(topLevel), m_hooks(std::move(hooks)), m_side(location.globalSide()),
      m_savedLocation(location.location()) {
    if (location.transformStatus() == JPPlacementsHolderLocation::TransformStatus::LocallySet)
        m_savedTransform = location.localToParentTransform();
    // Measured as it lies by its location, not as a check last set it.
    m_location.setLocalToParentTransform(std::nullopt);
    advance();
}

void JPBoardLocationProcess::showStep() {
    char text[600];
    std::snprintf(text, sizeof text, kInstructions[m_step], m_placementId.c_str());
    char title[64];
    std::snprintf(title, sizeof title, "Set Board Location (%d / 3)", m_step + 1);
    m_hooks.show(title, text, m_step == 2 ? "Finish" : "Next", [this] { cancel(); }, [this] { advance(); });
}

void JPBoardLocationProcess::advance() {
    bool ok = true;
    if (m_step == 0) ok = step1();
    else if (m_step == 1) ok = step2();
    else if (m_step == 2) {
        // The camera to the board's origin.
        m_hooks.moveTool(Tool::Camera, m_location.globalLocation());
    }
    if (!ok) return;
    ++m_step;
    if (m_step == 3) {
        m_hooks.finished();
        return;
    }
    showStep();
}

std::vector<JPPlacement> JPBoardLocationProcess::shortestOrder(std::vector<JPPlacement> placements) const {
    // From where the camera is, nearest first, towards the board's origin at the end.
    std::vector<JPPlacement> out;
    std::optional<JPLocation> at = m_hooks.toolLocation(Tool::Camera);
    while (!placements.empty()) {
        size_t best = 0;
        double bestD = std::numeric_limits<double>::max();
        for (size_t i = 0; i < placements.size(); ++i) {
            const JPLocation l = m_location.placementLocation(placements[i].location);
            double d = at ? distance(*at, l) : double(i);
            if (placements.size() == 2 && at) d -= distance(l, m_location.globalLocation()) * 1e-9;
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        at = m_location.placementLocation(placements[best].location);
        out.push_back(placements[best]);
        placements.erase(placements.begin() + long(best));
    }
    return out;
}

void JPBoardLocationProcess::moveCameraTo(const JPPlacement& p) {
    m_hooks.moveTool(Tool::Camera, m_location.placementLocation(p.location));
}

bool JPBoardLocationProcess::step1() {
    m_placements.clear();
    for (const JPPlacement* p : m_hooks.chosenPlacements()) m_placements.push_back(*p);
    if (m_placements.size() < 2) {
        JDialog::message("Error", "Please select at least two placements.");
        return false;
    }
    m_placements = shortestOrder(m_placements);
    moveCameraTo(m_placements.front());
    m_index = 0;
    m_placementId = m_placements.front().id;
    m_hooks.selectPlacement(m_placementId);
    m_expected.push_back(m_placements.front().location.invert(m_side == JPSide::Bottom, false, false, false));
    return true;
}

bool JPBoardLocationProcess::step2() {
    const auto measured = m_hooks.toolLocation(Tool::Camera);
    if (!measured) {
        JDialog::message("Error", "Please position the camera.");
        return false;
    }
    m_measured.push_back(*measured);
    ++m_index;
    if (m_index < m_placements.size()) {
        const JPPlacement& next = m_placements[m_index];
        m_placementId = next.id;
        m_hooks.selectPlacement(m_placementId);
        m_expected.push_back(next.location.invert(m_side == JPSide::Bottom, false, false, false));
        moveCameraTo(next);
        --m_step;   // the same step again, for the next placement
        return true;
    }
    return setLocation(true);
}

bool JPBoardLocationProcess::setLocation(bool check) {
    const JPLocation saved = m_location.globalLocation();
    JPAffineTransform tx = JPFiducialFit::derive(m_expected, m_measured);
    if (m_side == JPSide::Bottom) tx.scale(-1, 1);
    m_location.setLocalToGlobalTransform(tx);
    JPLocation origin(JPLengthUnit::Millimeters);
    if (m_side == JPSide::Bottom && m_location.holder)
        origin = m_location.holder->dimensions.convertToUnits(JPLengthUnit::Millimeters).derive(std::nullopt, 0.0, 0.0, 0.0);
    JPLocation moved = m_location.placementLocation(origin).convertToUnits(saved.units());
    moved = moved.derive(std::nullopt, std::nullopt, saved.z(), std::nullopt);
    if (m_topLevel) {
        m_location.setLocation(moved);
        m_location.setLocalToGlobalTransform(tx);
    }
    if (!check) return true;
    const double offset = distance(m_location.globalLocation(), saved);
    const JPAffineTransform::Info ai = m_location.localToParentTransform().info();
    JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "board origin offset distance: " << offset << " mm";
    std::string err;
    if (ai.xScale > 0 && std::abs(ai.xScale - 1) > kScalingTolerance)
        err += fmt("x scaling = %.5f which is outside the expected range of [%.5f, %.5f], ", ai.xScale, 1 - kScalingTolerance,
                   1 + kScalingTolerance);
    else if (ai.xScale < 0 && std::abs(ai.xScale + 1) > kScalingTolerance)
        err += fmt("x scaling = %.5f which is outside the expected range of [-%.5f, -%.5f], ", ai.xScale,
                   1 + kScalingTolerance, 1 - kScalingTolerance);
    if (std::abs(ai.yScale - 1) > kScalingTolerance)
        err += fmt("y scaling = %.5f which is outside the expected range of [%.5f, %.5f], ", ai.yScale, 1 - kScalingTolerance,
                   1 + kScalingTolerance);
    if (std::abs(ai.xShear) > kShearingTolerance)
        err += fmt("x shearing = %.5f which is outside the expected range of [%.5f, %.5f], ", ai.xShear, -kShearingTolerance,
                   kShearingTolerance);
    if (offset > kBoardLocationToleranceMm)
        err += fmt("the board origin moved %.4fmm which is greater than the allowed amount of %.4fmm, ", offset,
                   kBoardLocationToleranceMm);
    if (!err.empty()) {
        err.resize(err.size() - 2);
        JDialog::message("Error", "Results invalid because " + err +
                                      "; double check to ensure you are jogging the camera to the correct placements.  "
                                      "Other potential remidies include setting the initial board X, Y, Z, and Rotation "
                                      "in the Boards panel; using a different set of placements; or changing the "
                                      "allowable tolerances.");
        cancel();
        return false;
    }
    return true;
}

void JPBoardLocationProcess::cancel() {
    m_location.setLocation(m_savedLocation);
    m_location.setLocalToParentTransform(m_savedTransform);
    if (m_savedTransform) m_location.setTransformStatus(JPPlacementsHolderLocation::TransformStatus::LocallySet);
    m_hooks.finished();
}

} // inline namespace jf
