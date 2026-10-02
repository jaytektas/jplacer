// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerCameraTasks.h"

#include "common/JPlacerLog.h"
#include "tasks/JPCameraCalibrator.h"
#include "tasks/JPVisualHoming.h"
#include "tasks/JPVisualTest.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <cstdio>
#include <optional>

inline namespace jf {

namespace {

// How fast the tasks move the head, as a share of the axes' rates: slow
// enough to watch, and to stop, while these are new.
constexpr double kTaskSpeed = 0.1;
// How long a result stays in the status bar.
constexpr int kResultMs = 8000;

} // namespace

JPlacerCameraTasks::JPlacerCameraTasks(JAppWindow& window, JPCell& cell, JPCameraPanel& cameras, std::string cellPath)
    : m_window(window), m_cell(cell), m_cameras(cameras), m_cellPath(std::move(cellPath)) {
    m_cameras.onCalibrate  = [this] { calibrate(); };
    m_cameras.onVisualTest = [this] { visualTest(); };
    // Double-click the picture: the camera looks there.
    m_cameras.onLookAtPixel = [this](double px, double py) {
        JPCameraCalibration cal;
        double vx, vy, x, y;
        if (!shownCameraLook(cal, vx, vy)) {
            m_window.showStatus("To look where the picture is clicked, home the machine and calibrate this camera", kResultMs);
            return;
        }
        if (cal.machinePoint(px, py, vx, vy, x, y)) lookAt(x, y);
    };
}

JPlacerCameraTasks::~JPlacerCameraTasks() {
    *m_alive = false;
    m_cameras.onCalibrate   = nullptr;
    m_cameras.onVisualTest  = nullptr;
    m_cameras.onLookAtPixel = nullptr;
    if (m_worker.joinable()) m_worker.join();
}

const JPHeadConfig* JPlacerCameraTasks::head(const JPCameraConfig& camera) const {
    for (const JPHeadConfig& h : m_cell.config().heads)
        if (h.id == camera.mount.headId) return &h;
    return nullptr;
}

std::string JPlacerCameraTasks::notReady(bool needsCalibration, bool needsHomingMark) const {
    if (m_busy) return "a camera task is already under way";
    const JPCameraFeed* feed = m_cameras.shownFeed();
    if (!feed) return "there is no camera to use";
    if (!m_cell.isConnected()) return "connect the machine first";
    if (!m_cell.isHomed()) return "home the machine first";
    const JPHeadConfig* h = head(feed->config());
    if (!h) return feed->config().name + " is not on a head";
    if (needsHomingMark && (!h->homingFiducial || h->homingFiducialDiameter <= 0))
        return "the head's homing mark (its place and size) is not set";
    if (needsCalibration && !m_cell.cameraCalibration(feed->config().id).valid)
        return feed->config().name + " is not calibrated: Calibrate first";
    return {};
}

void JPlacerCameraTasks::run(const std::string& name, Task task, std::function<void(bool)> done) {
    if (m_worker.joinable()) m_worker.join();   // the last one has finished: m_busy says so
    m_busy = true;
    m_cameras.setBusy(true);
    m_cameras.setNote(name + "\xE2\x80\xA6");
    std::weak_ptr<bool> alive = m_alive;
    auto onMain = [alive](std::function<void()> fn) {
        JMainThreadDispatcher::instance().post([alive, fn] {
            if (const auto a = alive.lock(); a && *a) fn();
        });
    };
    m_worker = std::thread([this, name, task, done, onMain] {
        std::string words;
        const bool ok = task(words, [this, name, onMain](const std::string& step) {
            onMain([this, name, step] { m_cameras.setNote(name + ": " + step); });
        });
        JLOGC(JPlacerLog::kCamera, ok ? JLogLevel::Info : JLogLevel::Warn) << name << ": " << words;
        onMain([this, name, ok, words, done] {
            m_busy = false;
            m_cameras.setBusy(false);
            const std::string text = ok ? words : name + " failed: " + words;
            m_cameras.setNote(text);
            m_window.showStatus(text, kResultMs);
            if (done) done(ok);
        });
    });
}

void JPlacerCameraTasks::calibrate() {
    if (const JPCameraFeed* feed = m_cameras.shownFeed();
        feed && (feed->config().mount.axisX.empty() || feed->config().mount.axisY.empty())) {
        calibrateFixed();
        return;
    }
    if (const std::string why = notReady(false, true); !why.empty()) {
        m_window.showStatus("Calibrate: " + why, kResultMs);
        return;
    }
    JPCameraFeed* feed = m_cameras.shownFeed();
    const JPHeadConfig h = *head(feed->config());
    const std::string cameraId = feed->config().id;
    auto result = std::make_shared<JPCameraCalibration>();
    run("Calibrating " + feed->config().name, [this, feed, h, result](std::string& words, const auto& progress) {
        // Over the mark first, as near as the camera's offset on the head says.
        const JPMountConfig& m = feed->config().mount;
        progress("moving over the homing mark");
        if (!m_cell.moveAxesAndWait({ { m.axisX, h.homingFiducial->x - m.offsetX },
                                      { m.axisY, h.homingFiducial->y - m.offsetY } }, kTaskSpeed, words))
            return false;
        JPCameraCalibrator::Options o;
        o.markDiameterMm = h.homingFiducialDiameter;
        o.markZ = h.homingFiducial->z;
        o.speed = kTaskSpeed;
        const auto c = JPCameraCalibrator::run(m_cell, *feed, o, words, progress);
        if (!c) return false;
        *result = *c;
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s: %.3f x %.3f px/mm, turned %.2f deg%s, fit to %.2f px",
                      feed->config().name.c_str(), c->scaleX(), c->scaleY(), c->rotationDeg(feed->config().looksUp),
                      c->mirrored(feed->config().looksUp) ? ", mirrored" : "", c->rmsPx);
        words = buf;
        return true;
    }, [this, cameraId, result](bool ok) {
        if (ok) keepCalibration(cameraId, *result);
    });
}

void JPlacerCameraTasks::keepCalibration(const std::string& cameraId, const JPCameraCalibration& calibration) {
    m_cell.setCameraCalibration(cameraId, calibration);
    std::string error;
    if (!m_cell.config().save(m_cellPath, error)) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Error) << error;
        m_window.showStatus("The calibration is in use but was not saved: " + error, kResultMs);
    }
}

void JPlacerCameraTasks::visualTest() {
    if (const std::string why = notReady(true, true); !why.empty()) {
        m_window.showStatus("Visual Test: " + why, kResultMs);
        return;
    }
    JPCameraFeed* feed = m_cameras.shownFeed();
    const JPHeadConfig h = *head(feed->config());
    run("Visual Test", [this, feed, h](std::string& words, const auto& progress) {
        progress("looking at the homing mark");
        const JPVisualTest::Result r = JPVisualTest::run(m_cell, *feed, h, kTaskSpeed);
        if (!r.found) {
            words = r.why;
            return false;
        }
        char buf[200];
        std::snprintf(buf, sizeof buf, "The homing mark is %+.3f mm in X and %+.3f mm in Y from its setting",
                      r.offsetX, r.offsetY);
        words = buf;
        return true;
    });
}

void JPlacerCameraTasks::visualHome() {
    const JPHeadConfig* homing = nullptr;
    for (const JPHeadConfig& h : m_cell.config().heads)
        if (h.visualHoming) homing = &h;
    if (!homing) return;
    const JPCameraConfig* camera = nullptr;
    for (const JPCameraConfig& c : m_cell.config().cameras)
        if (!camera && c.mount.headId == homing->id && m_cell.cameraCalibration(c.id).valid) camera = &c;
    if (!camera) {
        m_window.showStatus("Homed by the switches only: calibrate a camera on " + homing->name
                            + " to finish homing with the homing mark", kResultMs);
        return;
    }
    if (m_busy) return;
    m_cameras.showCamera(camera->id);
    if (const std::string why = notReady(true, true); !why.empty()) {
        m_window.showStatus("Visual homing: " + why, kResultMs);
        return;
    }
    JPCameraFeed* feed = m_cameras.shownFeed();
    const JPHeadConfig h = *homing;
    run("Visual homing", [this, feed, h](std::string& words, const auto& progress) {
        progress("looking at the homing mark");
        const JPVisualHoming::Result r = JPVisualHoming::run(m_cell, *feed, h, kTaskSpeed);
        if (!r.ok) {
            words = r.why;
            return false;
        }
        char buf[200];
        std::snprintf(buf, sizeof buf, "Homed: the homing mark corrected the position by %+.3f mm in X and %+.3f mm in Y",
                      r.correctedX, r.correctedY);
        words = buf;
        return true;
    });
}

bool JPlacerCameraTasks::shownCameraView(double& x, double& y, std::string& why) const {
    const JPCameraFeed* feed = m_cameras.shownFeed();
    if (!feed) { why = "there is no camera to use"; return false; }
    const JPMountConfig& m = feed->config().mount;
    if (m.axisX.empty() || m.axisY.empty()) { why = feed->config().name + " does not ride on a head"; return false; }
    if (!m_cell.isHomed()) { why = "home the machine first"; return false; }
    const auto at = m_cell.jogBase();
    x = at.at(m.axisX) + m.offsetX;
    y = at.at(m.axisY) + m.offsetY;
    return true;
}

bool JPlacerCameraTasks::shownCameraLook(JPCameraCalibration& calibration, double& viewX, double& viewY) const {
    const JPCameraFeed* feed = m_cameras.shownFeed();
    std::string why;
    if (!feed || !shownCameraView(viewX, viewY, why)) return false;
    calibration = m_cell.cameraCalibration(feed->config().id);
    return calibration.valid;
}

bool JPlacerCameraTasks::lookAt(double x, double y) {
    if (m_busy) {
        m_window.showStatus("A camera task is under way", kResultMs);
        return false;
    }
    double vx, vy;
    std::string why;
    if (!shownCameraView(vx, vy, why)) {
        m_window.showStatus(why, kResultMs);
        return false;
    }
    const JPMountConfig& m = m_cameras.shownFeed()->config().mount;
    m_cell.moveAxes({ { m.axisX, x - m.offsetX }, { m.axisY, y - m.offsetY } }, kTaskSpeed);
    return true;
}

void JPlacerCameraTasks::locateBoard(const JPBoard& board, const JPBoardSide& guess,
                                     std::function<void(const JPBoardLocator::Result&)> done) {
    if (const std::string why = notReady(true, false); !why.empty()) {
        m_window.showStatus("Locate Board: " + why, kResultMs);
        return;
    }
    JPCameraFeed* feed = m_cameras.shownFeed();
    auto result = std::make_shared<JPBoardLocator::Result>();
    run("Locating the board", [this, feed, board, guess, result](std::string& words, const auto& progress) {
        JPBoardLocator::Options o;
        o.speed = kTaskSpeed;
        *result = JPBoardLocator::run(m_cell, *feed, board, guess, o, progress);
        if (!result->ok) {
            words = result->why;
            return false;
        }
        int found = 0;
        for (const auto& f : result->fiducials) found += f.found;
        char buf[200];
        std::snprintf(buf, sizeof buf, "Board found by %d of %zu fiducials, turned %.3f deg, fit to %.3f mm", found,
                      result->fiducials.size(), result->board.toMachine.rotationDeg(), result->rmsMm);
        words = buf;
        return true;
    }, [result, done](bool) { done(*result); });
}

void JPlacerCameraTasks::calibrateFixed() {
    JPCameraFeed* feed = m_cameras.shownFeed();
    const JPCameraConfig& cam = feed->config();
    // The mark is a nozzle's tip: the first nozzle on a head that moves on X, Y and Z.
    const JPNozzleConfig* nozzle = nullptr;
    for (const JPNozzleConfig& n : m_cell.config().nozzles)
        if (!nozzle && !n.mount.headId.empty() && !n.mount.axisX.empty() && !n.mount.axisY.empty() && !n.mount.axisZ.empty())
            nozzle = &n;
    std::string why = m_busy ? "a camera task is already under way"
                    : !m_cell.isConnected() ? "connect the machine first"
                    : !m_cell.isHomed() ? "home the machine first"
                    : !nozzle ? "there is no nozzle on a head to hold over " + cam.name
                    : std::string();
    if (!why.empty()) {
        m_window.showStatus("Calibrate: " + why, kResultMs);
        return;
    }
    // The camera's place is its offset: where it looks, and the height in focus.
    const JPMountConfig& place = cam.mount;
    char body[640];
    std::snprintf(body, sizeof body,
                  "%s is calibrated with a nozzle's tip held over it: %s goes over the camera (X %.3f, Y %.3f), "
                  "down to Z %.3f, and moves about in a grid a few millimetres across.\n\nThe nozzle must hold no part, "
                  "and nothing must be in its way.",
                  cam.name.c_str(), nozzle->name.c_str(), place.offsetX, place.offsetY, place.offsetZ);
    const JPMountConfig tool = nozzle->mount;
    const std::string cameraId = cam.id;
    std::weak_ptr<bool> alive = m_alive;
    JDialog::confirm("Calibrate " + cam.name, body, [this, alive, feed, tool, place, cameraId] {
        if (const auto a = alive.lock(); !a || !*a) return;
        auto result = std::make_shared<JPCameraCalibration>();
        run("Calibrating " + feed->config().name, [this, feed, tool, place, result](std::string& words, const auto& progress) {
            progress("the nozzle over the camera");
            const bool over = m_cell.safeZAndWait(tool.headId, kTaskSpeed, words)
                && m_cell.moveAxesAndWait({ { tool.axisX, place.offsetX - tool.offsetX },
                                            { tool.axisY, place.offsetY - tool.offsetY } }, kTaskSpeed, words)
                && m_cell.moveAxesAndWait({ { tool.axisZ, place.offsetZ - tool.offsetZ } }, kTaskSpeed, words);
            std::optional<JPCameraCalibration> c;
            if (over) {
                JPCameraCalibrator::Options o;
                o.markZ = place.offsetZ;
                o.speed = kTaskSpeed;
                o.moving = &tool;
                c = JPCameraCalibrator::run(m_cell, *feed, o, words, progress);
            }
            // Up again, whatever happened.
            std::string up;
            if (!m_cell.safeZAndWait(tool.headId, kTaskSpeed, up) && words.empty()) words = up;
            if (!c) return false;
            *result = *c;
            const bool lookingUp = feed->config().looksUp;
            char buf[200];
            std::snprintf(buf, sizeof buf, "%s: %.3f x %.3f px/mm, turned %.2f deg%s, fit to %.2f px",
                          feed->config().name.c_str(), c->scaleX(), c->scaleY(), c->rotationDeg(lookingUp),
                          c->mirrored(lookingUp) ? ", mirrored" : "", c->rmsPx);
            words = buf;
            return true;
        }, [this, cameraId, result](bool ok) {
            if (ok) keepCalibration(cameraId, *result);
        });
    });
}

} // inline namespace jf
