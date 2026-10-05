// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerEstimateZ.h"

#include <cstdio>

inline namespace jf {

namespace {

constexpr const char* kTitle = "Instructions to Estimate an Object's Z Coordinate";

// OpenPnP's words, for a camera on the head and for a fixed one; the last has the estimate in it.
const char* const kMovable[] = {
    "Jog the camera so that an easily identifiable feature of the object (such as a sharp corner) is in the camera's "
    "field-of-view (FOV). It is better for the feature to be towards the edge of the FOV rather than centered. When "
    "ready, click the mouse on the feature to capture its apparent position.",
    "Now jog the camera in X and/or Y so that the same feature is visible in another part of the camera's FOV. The "
    "farther away from the original position, the better.  When ready, click the mouse on the feature to capture its "
    "new apparent position and estimate its Z coordinate.",
    "The estimated Z coordinate of the feature is %s. Click Cancel if finished or Again to perform another "
    "measurment.",
};
const char* const kFixed[] = {
    "Jog the nozzle so that an easily identifiable feature of the object (such as a sharp corner) is in the camera's "
    "field-of-view (FOV). It is better for the feature to be towards the edge of the FOV rather than centered. When "
    "ready, click the mouse on the feature to capture its apparent position.",
    "Now jog the nozzle in X and/or Y so that the same feature is visible in another part of the camera's FOV. The "
    "farther away from the original position, the better.  When ready, click the mouse on the feature to capture its "
    "new apparent position and estimate its Z coordinate.",
    "The estimated Z coordinate of the feature is %s. Click Cancel if finished or Again to perform another "
    "measurment.",
};

} // namespace

JPlacerEstimateZ::~JPlacerEstimateZ() {
    *m_alive = false;
    cancel();
}

void JPlacerEstimateZ::start(JPCameraPanel& panel, bool fixed, Where where, Calibration calibration) {
    cancel();
    m_panel = &panel;
    m_fixed = fixed;
    m_where = std::move(where);
    m_calibration = std::move(calibration);
    m_estimate = "unavailable";
    m_panel->view().onPicked = [this, alive = std::weak_ptr<bool>(m_alive)](double px, double py) {
        if (const auto a = alive.lock(); a && *a) clicked(px, py);
    };
    step(0);
}

void JPlacerEstimateZ::step(int s) {
    m_step = s;
    if (!m_panel) return;
    const char* const* words = m_fixed ? kFixed : kMovable;
    char text[512];
    std::snprintf(text, sizeof text, words[s], m_estimate.c_str());
    std::weak_ptr<bool> alive = m_alive;
    m_panel->showInstructions(kTitle, text, "Again",
                              [this, alive] {
                                  if (const auto a = alive.lock(); a && *a) cancel();
                              },
                              [this, alive] {
                                  if (const auto a = alive.lock(); a && *a && m_step == 2) step(0);
                              });
    // Again only once there is an estimate, as OpenPnP's.
    m_panel->setProceedEnabled(s == 2);
}

void JPlacerEstimateZ::clicked(double px, double py) {
    if (m_step != 0 && m_step != 1) return;
    const auto at = m_where ? m_where() : std::nullopt;
    if (!at) {
        m_estimate = "unavailable (due to the machine's position not being known)";
        step(2);
        return;
    }
    if (m_step == 0) {
        m_px1 = px;
        m_py1 = py;
        m_at1 = *at;
        step(1);
        return;
    }
    const JPCameraView& view = m_panel->view();
    const JPCameraCalibration cal = m_calibration(view.pictureWidth(), view.pictureHeight());
    double z = 0;
    std::string why;
    if (cal.estimateObjectZ(m_px1, m_py1, px, py, at->first - m_at1.first, at->second - m_at1.second, z, why)) {
        char mm[32];
        std::snprintf(mm, sizeof mm, "%.3fmm", z);
        m_estimate = mm;
    } else {
        m_estimate = "unavailable (due to " + why + ")";
    }
    step(2);
}

void JPlacerEstimateZ::cancel() {
    if (m_panel) {
        m_panel->view().onPicked = nullptr;
        m_panel->hideInstructions();
    }
    m_panel = nullptr;
    m_step = -1;
}

} // inline namespace jf
