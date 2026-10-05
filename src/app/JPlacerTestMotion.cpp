// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerTestMotion.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <cmath>

inline namespace jf {

namespace {

// How long the status bar shows a result.
constexpr int kResultMs = 15000;

} // namespace

JPlacerTestMotion::JPlacerTestMotion(JAppWindow& window, JPCell& cell) : m_window(window), m_cell(cell) {}

JPlacerTestMotion::~JPlacerTestMotion() {
    *m_alive = false;
    if (m_worker.joinable()) m_worker.join();
}

void JPlacerTestMotion::run(const JPMountConfig& tool, std::function<void(const JPMotionTestResult&)> done) {
    if (!m_cell.isConnected() || !m_cell.isHomed()) {
        m_window.showStatus("Test Motion: home the machine first", kResultMs);
        return;
    }
    const JPMotionPlannerConfig& planner = m_cell.config().motionPlanner;
    const auto first = planner.initial(false), last = planner.initial(true);
    if (!first || !last) {
        m_window.showStatus("Test Motion undefined. Please go to the Motion Planner tab and define/enable the Test Motion "
                            "locations.", kResultMs);
        return;
    }
    if (m_busy.exchange(true)) {
        m_window.showStatus("Test Motion: a run is under way", kResultMs);
        return;
    }
    // Back from the last when the tool is nearer it than the first (X, Y, Z and rotation).
    const auto at = m_cell.positions();
    auto coordinate = [&at](const std::string& axis, double offset) {
        const auto p = at.find(axis);
        return p == at.end() ? 0.0 : p->second + offset;
    };
    const double here[4] = { coordinate(tool.axisX, tool.offsetX), coordinate(tool.axisY, tool.offsetY),
                             coordinate(tool.axisZ, tool.offsetZ), coordinate(tool.axisRotation, 0) };
    auto distance = [&here](const JPMachineLocation& l) {
        return std::sqrt(std::pow(l.x - here[0], 2) + std::pow(l.y - here[1], 2) + std::pow(l.z - here[2], 2)
                         + std::pow(l.rotation - here[3], 2));
    };
    const bool reverse = distance(planner.stops[size_t(*last)].at) < distance(planner.stops[size_t(*first)].at);
    if (m_worker.joinable()) m_worker.join();
    m_window.showStatus("Test Motion running", kResultMs);
    m_worker = std::thread([this, tool, reverse, done = std::move(done), alive = std::weak_ptr<bool>(m_alive)] {
        auto result = std::make_shared<JPMotionTestResult>();
        std::string why;
        const bool ok = m_cell.testMotionAndWait(tool, reverse, *result, why);
        if (!ok) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "Test Motion: " << why;
        JMainThreadDispatcher::instance().post([this, ok, why, result, done, alive] {
            const auto a = alive.lock();
            if (!a || !*a) return;
            m_busy = false;
            if (!ok) {
                m_window.showStatus("Test Motion: " + why, kResultMs);
                return;
            }
            char words[96];
            std::snprintf(words, sizeof words, "Test Motion: planned %.3f s, took %.3f s", result->plannedS, result->actualS);
            m_window.showStatus(words, kResultMs);
            if (done) done(*result);
        });
    });
}

} // inline namespace jf
