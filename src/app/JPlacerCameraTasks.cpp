// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerCameraTasks.h"

#include <opencv2/imgproc.hpp>

#include "tasks/JPAutoFocus.h"

#include "common/JPlacerLog.h"
#include "tasks/JPCameraCalibrator.h"
#include "tasks/JPCameraLook.h"
#include "tasks/JPVisualHoming.h"
#include "tasks/JPVisualTest.h"
#include "tasks/JPVisionFeature.h"
#include "vision/JPRoundMarkFinder.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <cstdio>
#include <future>
#include <optional>

inline namespace jf {

namespace {

// How fast the tasks move the head, as a share of the axes' rates: full,
// so they go at the machine's speed (the Jog panel's Speed, which scales
// every move), as jogs and parks do.
constexpr double kTaskSpeed = 1.0;
// How long a feature found is shown on the camera's view (OpenPnP's diagnosticsMilliseconds).
constexpr int kFeatureShownMs = 4000;
// OpenPnP's precise nozzle offsets calibration (its angles: the cell's nozzleOffsetAngles): the extra wait after each
// pick and place, the test object's height (a pseudo part's), and centring on it (passes, near enough).
constexpr int    kExtraVacuumDwellMs = 300;
constexpr double kTestObjectHeightMm = 0.01;
constexpr int    kCentrePasses = 3;
constexpr double kCentredMm = 0.01;
// How long each of auto focus's pictures is shown (OpenPnP's 1 s).
constexpr int kAutoFocusShownMs = 1000;
// How long Auto-Tune when homing may take, all told (the camera's automatic moment, 6 s at most, then the search).
constexpr int kTuneWaitMs = 12000;
// How long a result stays in the status bar.
constexpr int kResultMs = 8000;
// Looking for a mark under the camera: how far from the middle, and how small and big, as shares of the
// picture's smaller side (as the calibration's first look).
constexpr double kMarkSearchShare = 0.3;
constexpr double kLeastMarkShare = 0.02;
constexpr double kMostMarkShare = 0.4;
// Two heights closer than this measure the camera's distance too poorly.
constexpr double kLeastHeightGapMm = 1.0;

} // namespace

JPlacerCameraTasks::JPlacerCameraTasks(JAppWindow& window, JPCell& cell, std::vector<JPCameraPanel*> cameras,
                                       std::function<void(JPCameraPanel&)> bringForward, std::string cellPath)
    : m_window(window), m_cell(cell), m_cameras(std::move(cameras)), m_bringForward(std::move(bringForward)),
      m_cellPath(std::move(cellPath)) {
    for (JPCameraPanel* panel : m_cameras) {
        panel->onCalibrate  = [this, panel] { calibrate(*panel); };
        panel->onVisualTest = [this, panel] { visualTest(*panel); };
        // Double-click the picture: the camera looks there.
        panel->onLookAtPixel = [this, panel](double px, double py) {
            JPCameraCalibration cal;
            double vx, vy, x, y;
            if (!cameraLook(panel->camera().id, cal, vx, vy)) {
                m_window.showStatus("To look where the picture is clicked, home the machine and calibrate a camera on the head",
                                    kResultMs);
                return;
            }
            if (cal.machinePoint(px, py, vx, vy, x, y)) lookAt(*panel, x, y);
        };
    }
}

JPlacerCameraTasks::~JPlacerCameraTasks() {
    *m_alive = false;
    for (JPCameraPanel* panel : m_cameras) {
        panel->onCalibrate   = nullptr;
        panel->onVisualTest  = nullptr;
        panel->onLookAtPixel = nullptr;
    }
    if (m_worker.joinable()) m_worker.join();
}

const JPHeadConfig* JPlacerCameraTasks::head(const JPCameraConfig& camera) const {
    for (const JPHeadConfig& h : m_cell.config().heads)
        if (h.id == camera.mount.headId) return &h;
    return nullptr;
}

std::string JPlacerCameraTasks::notReady(const JPCameraPanel* camera, bool needsCalibration, bool needsHomingMark) const {
    if (m_busy) return "a camera task is already under way";
    if (!camera) return "no camera rides on a head";
    const JPCameraConfig& cam = camera->camera();
    if (!m_cell.isConnected()) return "connect the machine first";
    if (!m_cell.isHomed()) return "home the machine first";
    const JPHeadConfig* h = head(cam);
    if (!h) return cam.name + " is not on a head";
    if (needsHomingMark && (!h->homingFiducial || h->homingFiducialDiameter <= 0))
        return "the head's homing mark (its place and size) is not set";
    // At which size is known once it takes pictures; the task checks.
    if (needsCalibration && m_cell.cameraCalibrations(cam.id).empty())
        return cam.name + " is not calibrated: Calibrate first";
    return {};
}

void JPlacerCameraTasks::run(JPCameraPanel& camera, const std::string& name, Task task, std::function<void(bool)> done) {
    if (m_worker.joinable()) m_worker.join();   // the last one has finished: m_busy says so
    m_busy = true;
    if (m_bringForward) m_bringForward(camera);
    JPCameraPanel* panel = &camera;
    panel->setBusy(true);
    panel->setNote(name + "\xE2\x80\xA6");
    if (onTaskOutcome) onTaskOutcome("");
    std::weak_ptr<bool> alive = m_alive;
    auto onMain = [alive](std::function<void()> fn) {
        JMainThreadDispatcher::instance().post([alive, fn] {
            if (const auto a = alive.lock(); a && *a) fn();
        });
    };
    m_worker = std::thread([this, panel, name, task, done, onMain] {
        // The lights, as the cameras' Light settings say: this camera's on
        // before its pictures, other cameras' off against glare.
        const JPCameraConfig& cam = panel->camera();
        const std::string light = cam.lightActuator();
        std::string lightWhy;
        for (JPCameraPanel* other : m_cameras) {
            const std::string theirs = other->camera().lightActuator();
            if (other != panel && other->camera().light.antiGlare && !theirs.empty() && theirs != light)
                m_cell.switchActuatorAndWait(theirs, false, lightWhy);
        }
        if (!light.empty() && cam.light.beforeCapture) m_cell.switchActuatorAndWait(light, true, lightWhy);
        std::string words;
        const bool ok = task(words, [panel, name, onMain](const std::string& step) {
            onMain([panel, name, step] { panel->setNote(name + ": " + step); });
        });
        if (!light.empty() && cam.light.afterCapture) m_cell.switchActuatorAndWait(light, false, lightWhy);
        JLOGC(JPlacerLog::kCamera, ok ? JLogLevel::Info : JLogLevel::Warn) << name << ": " << words;
        onMain([this, panel, name, ok, words, done] {
            m_busy = false;
            panel->setBusy(false);
            const std::string text = ok ? words : name + " failed: " + words;
            panel->setNote(text);
            m_window.showStatus(text, kResultMs);
            if (onTaskOutcome) onTaskOutcome(ok ? std::string() : text);
            if (done) done(ok);
        });
    });
}

void JPlacerCameraTasks::calibrate(JPCameraPanel& camera, std::function<void(bool ok)> finished, bool here) {
    if (camera.camera().mount.axisX.empty() || camera.camera().mount.axisY.empty()) {
        calibrateFixed(camera, std::move(finished));
        return;
    }
    if (const std::string why = notReady(&camera, false, false); !why.empty()) {
        m_window.showStatus("Calibrate: " + why, kResultMs);
        if (finished) finished(false);
        return;
    }
    JPCameraFeed* feed = &camera.feed();
    const JPHeadConfig h = *head(feed->config());
    // Asked to, or no homing mark yet (a new machine): calibrated over the mark it is over now, of a size it finds.
    const bool hasMark = !here && h.homingFiducial && h.homingFiducialDiameter > 0;
    const std::string cameraId = feed->config().id;
    auto result = std::make_shared<JPCameraCalibration>();
    auto finds = showFinds(camera);
    run(camera, "Calibrating " + feed->config().name, [this, feed, h, hasMark, result, finds](std::string& words, const auto& progress) {
        // Over the mark first, as near as the camera's offset on the head says.
        const JPMountConfig& m = feed->config().mount;
        if (hasMark) {
            progress("moving over the homing mark");
            if (!m_cell.moveAxesAndWait({ { m.axisX, h.homingFiducial->x - m.offsetX },
                                          { m.axisY, h.homingFiducial->y - m.offsetY } }, kTaskSpeed, words))
                return false;
        }
        JPCameraCalibrator::Options o;
        o.markDiameterMm = hasMark ? h.homingFiducialDiameter : 0;
        o.markZ = h.homingFiducial ? h.homingFiducial->z : 0;
        o.speed = kTaskSpeed;
        o.calibrating = feed->config().calibrating;
        o.found = finds;
        const auto c = JPCameraCalibrator::run(m_cell, *feed, o, words, progress);
        if (!c) return false;
        *result = *c;
        // Again over the secondary mark, at another height.
        std::string second;
        if (hasMark && o.calibrating.twoHeights && h.rigSecondary
            && std::abs(h.rigSecondary->z - h.homingFiducial->z) >= kLeastHeightGapMm) {
            progress("moving over the secondary mark");
            JPCameraCalibrator::Options o2 = o;
            o2.markDiameterMm = h.rigSecondaryDiameter;
            o2.markZ = h.rigSecondary->z;
            std::string why;
            std::optional<JPCameraCalibration> c2;
            if (m_cell.moveAxesAndWait({ { m.axisX, h.rigSecondary->x - m.offsetX },
                                         { m.axisY, h.rigSecondary->y - m.offsetY } }, kTaskSpeed, why))
                c2 = JPCameraCalibrator::run(m_cell, *feed, o2, why, progress);
            if (c2) secondHeight(*result, *c2);
            else second = "; at the secondary mark: " + why;
        }
        words = calibrated(feed->config(), *result) + second;
        return true;
    }, [this, cameraId, result, finished](bool ok) {
        if (ok) keepCalibration(cameraId, *result);
        if (finished) finished(ok);
    });
}

void JPlacerCameraTasks::secondHeight(JPCameraCalibration& first, const JPCameraCalibration& second) {
    first.secondZ = second.z;
    first.secondScale = second.scale();
    first.secondRmsPx = second.rmsPx;
    first.secondLooked = second.looked;
    first.secondLookedX = second.lookedX;
    first.secondLookedY = second.lookedY;
}

std::string JPlacerCameraTasks::calibrated(const JPCameraConfig& cam, const JPCameraCalibration& c) {
    char buf[240];
    const int n = std::snprintf(buf, sizeof buf, "%s: %.3f x %.3f px/mm, turned %.2f deg%s, fit to %.2f px", cam.name.c_str(),
                                c.scaleX(), c.scaleY(), c.rotationDeg(cam.looksUp), c.mirrored(cam.looksUp) ? ", mirrored" : "",
                                c.rmsPx);
    if (c.twoHeights() && n > 0 && size_t(n) < sizeof buf)
        std::snprintf(buf + n, sizeof buf - size_t(n), "; at two heights, the camera at Z %.1f", c.cameraZ());
    return buf;
}

void JPlacerCameraTasks::keepCalibration(const std::string& cameraId, const JPCameraCalibration& calibration) {
    m_cell.setCameraCalibration(cameraId, calibration);
    std::string error;
    if (!m_cell.config().save(m_cellPath, error)) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Error) << error;
        m_window.showStatus("The calibration is in use but was not saved: " + error, kResultMs);
    }
    if (onCalibrated) onCalibrated(cameraId, calibration);
}

void JPlacerCameraTasks::calibrateBacklash(const std::string& axisId,
                                           std::function<void(const JPBacklashCalibrator::Result&)> done,
                                           std::function<void(bool ok)> finished) {
    // The head camera that rides on the axis, over its head's homing mark.
    JPCameraPanel* camera = nullptr;
    for (JPCameraPanel* p : m_cameras) {
        const JPMountConfig& m = p->camera().mount;
        if (!camera && (m.axisX == axisId || m.axisY == axisId)) camera = p;
    }
    if (!camera) {
        m_window.showStatus("Calibrate backlash: no camera on a head rides on this axis", kResultMs);
        if (finished) finished(false);
        return;
    }
    if (const std::string why = notReady(camera, true, true); !why.empty()) {
        m_window.showStatus("Calibrate backlash: " + why, kResultMs);
        if (finished) finished(false);
        return;
    }
    JPCameraFeed* feed = &camera->feed();
    const JPHeadConfig h = *head(feed->config());
    std::string name = axisId;
    if (const JPAxisConfig* a = m_cell.config().axis(axisId)) name = a->name;
    auto result = std::make_shared<JPBacklashCalibrator::Result>();
    run(*camera, "Calibrating " + name + "'s backlash", [this, feed, h, axisId, name, result](std::string& words, const auto& progress) {
        JPBacklashCalibrator::Options o;
        o.markX = h.homingFiducial->x;
        o.markY = h.homingFiducial->y;
        o.markDiameterMm = h.homingFiducialDiameter;
        o.speed = kTaskSpeed;
        *result = JPBacklashCalibrator::run(m_cell, *feed, axisId, o, progress);
        if (!result->ok) {
            words = result->why;
            return false;
        }
        char buf[240];
        std::snprintf(buf, sizeof buf, "%s: %s, offset %.4f mm%s; within %.4f mm after", name.c_str(),
                      JPAxisConfig::backlashWord(result->method), result->offset,
                      result->method == JPAxisConfig::Backlash::DirectionalSneakUp
                          ? (", sneaking up " + std::to_string(result->sneakUpMm).substr(0, 5) + " mm").c_str() : "",
                      result->worstAfterMm);
        words = buf;
        return true;
    }, [result, done, finished](bool ok) {
        if (ok && done) done(*result);
        if (finished) finished(ok);
    });
}

void JPlacerCameraTasks::calibrateRunout(const std::string& nozzleId, RunoutDone done) {
    const JPNozzleConfig* nozzle = nullptr;
    for (const JPNozzleConfig& n : m_cell.config().nozzles)
        if (n.id == nozzleId) nozzle = &n;
    const JPNozzleTipConfig* tip = nullptr;
    if (nozzle)
        for (const JPNozzleTipConfig& t : m_cell.config().nozzleTips)
            if (t.id == nozzle->tipId) tip = &t;
    JPCameraPanel* camera = nullptr;
    for (JPCameraPanel* p : m_cameras)
        if (!camera && p->camera().mount.headId.empty() && p->camera().looksUp) camera = p;
    std::string why = m_busy ? "a camera task is already under way"
                    : !m_cell.isConnected() ? "connect the machine first"
                    : !m_cell.isHomed() ? "home the machine first"
                    : !nozzle ? "no such nozzle"
                    : !tip ? nozzle->name + " has no tip on it"
                    : !camera ? "there is no fixed camera looking up"
                    : std::string();
    if (!why.empty()) {
        m_window.showStatus("Calibrate runout: " + why, kResultMs);
        if (done) done(false, JPRunout {}, std::nullopt, why);
        return;
    }
    const JPNozzleConfig n = *nozzle;
    const JPNozzleTipConfig t = *tip;
    auto result = std::make_shared<JPRunout>();
    auto background = std::make_shared<std::optional<JPBackgroundCalibration::Result>>();
    auto words = std::make_shared<std::string>();
    run(*camera, "Measuring " + t.name + "'s runout", [this, camera, n, t, result, background, words](std::string& w, const auto& progress) {
        const auto r = JPRunoutCalibrator::measure(m_cell, camera->feed(), n, t, m_scripting.get(), w, progress, *background);
        *words = w;
        if (!r) return false;
        *result = *r;
        return true;
    }, [result, background, words, done](bool ok) {
        if (done) done(ok, *result, *background, *words);
    });
}

void JPlacerCameraTasks::calibrateRunoutCamera(const std::string& nozzleId, CameraFixDone done) {
    const JPNozzleConfig* nozzle = nullptr;
    for (const JPNozzleConfig& n : m_cell.config().nozzles)
        if (n.id == nozzleId) nozzle = &n;
    const JPNozzleTipConfig* tip = nullptr;
    if (nozzle)
        for (const JPNozzleTipConfig& t : m_cell.config().nozzleTips)
            if (t.id == nozzle->tipId) tip = &t;
    JPCameraPanel* camera = nullptr;
    for (JPCameraPanel* p : m_cameras)
        if (!camera && p->camera().mount.headId.empty() && p->camera().looksUp) camera = p;
    std::string why = m_busy ? "a camera task is already under way"
                    : !m_cell.isConnected() ? "connect the machine first"
                    : !m_cell.isHomed() ? "home the machine first"
                    : !nozzle ? "no such nozzle"
                    : !tip ? nozzle->name + " has no tip on it"
                    : !camera ? "there is no fixed camera looking up"
                    : !tip->runoutOn(nozzle->id) ? "Calibrate the nozzle tip first."
                    : std::string();
    if (!why.empty()) {
        m_window.showStatus("Calibrate camera position and rotation: " + why, kResultMs);
        return;
    }
    const JPNozzleConfig n = *nozzle;
    const JPNozzleTipConfig t = *tip;
    std::weak_ptr<bool> alive = m_alive;
    auto start = [this, alive, camera, n, t, done] {
        if (const auto a = alive.lock(); !a || !*a) return;
        if (m_busy) return;   // another task began while asking
        auto fix = std::make_shared<JPRunoutCalibrator::CameraFix>();
        run(*camera, "Calibrating " + camera->camera().name + "'s position and rotation", [this, camera, n, t, fix](std::string& w, const auto& progress) {
            JPRunoutCalibrator::Options o;
            o.speed = kTaskSpeed;
            const auto r = JPRunoutCalibrator::calibrateCamera(m_cell, camera->feed(), n, t, o, w, progress);
            if (!r) return false;
            *fix = *r;
            char said[200];
            std::snprintf(said, sizeof said, "%s is at %.3f, %.3f and turned %.3f deg (fit %.4f mm)", camera->camera().name.c_str(),
                          r->x, r->y, r->turnDeg, r->rmsMm);
            w = said;
            return true;
        }, [fix, done, id = camera->camera().id](bool ok) {
            if (ok && done) done(id, *fix);
        });
    };
    const JPCameraConfig& cam = camera->camera();
    char body[640];
    std::snprintf(body, sizeof body,
                  "%s's tip %s is sent round a circle over %s (X %.3f, Y %.3f) at Z %.3f, turning as it goes; the camera's "
                  "position and rotation are set by where it is seen.\n\nThe nozzle must hold no part, and nothing must be in its way.",
                  nozzle->name.c_str(), tip->name.c_str(), cam.name.c_str(), cam.mount.offsetX, cam.mount.offsetY,
                  cam.mount.offsetZ + tip->runoutCalibration.zOffset);
    JDialog::confirm("Calibrate " + cam.name + " Position and Rotation", body, start);
}

void JPlacerCameraTasks::calibrateNozzleOffsets(JPCameraPanel& camera, const JPNozzleConfig& nozzle,
                                                std::function<void(bool, double, double)> done) {
    if (const std::string why = notReady(&camera, true, false); !why.empty()) {
        m_window.showStatus("Nozzle offsets: " + why, kResultMs);
        if (done) done(false, 0, 0);
        return;
    }
    const JPHeadConfig* h = head(camera.camera());
    if (!h || !h->rigPrimary || h->rigTestObjectDiameter <= 0) {
        m_window.showStatus("Nozzle offsets: the head's calibration rig needs its primary fiducial and the test object's diameter",
                            kResultMs);
        if (done) done(false, 0, 0);
        return;
    }
    const JPHeadConfig rig = *h;
    const JPNozzleConfig n = nozzle;
    JPCameraFeed* feed = &camera.feed();
    // The test object looked for where it was sized (Feature diameter, Auto-Detect Next), else where the camera
    // is now, as OpenPnP's (the camera targeting it), else on the primary fiducial.
    std::pair<double, double> start { h->rigPrimary->x, h->rigPrimary->y };
    if (m_featureAt) start = *m_featureAt;
    else if (const auto now = cameraAt(camera)) start = *now;
    auto offsets = std::make_shared<std::pair<double, double>>(0, 0);
    run(camera, "Calibrating " + nozzle.name + "'s offsets", [this, feed, rig, n, offsets, start](std::string& words, const auto& progress) {
        const JPMountConfig& cm = feed->config().mount;
        JPCameraCalibration cal;
        if (!JPCameraLook::calibration(m_cell, *feed, cal, words)) return false;
        const double scale = cal.scale();
        // Picked and placed at the test object's top: captured, else the primary fiducial's Z (a paper-thin object).
        const double z = rig.rigTestObjectZ.value_or(rig.rigPrimary->z);
        // OpenPnP's centerInOnSubjectLocation: the camera over the test object, until it is centred.
        auto centreOn = [&](double& x, double& y) {
            for (int pass = 0; pass < kCentrePasses; ++pass) {
                if (!m_cell.moveToolAndWait(cm, { x, y, std::nullopt, std::nullopt }, kTaskSpeed, words)) return false;
                JPGrayImage img;
                if (!JPCameraLook::settled(*feed, img, words)) return false;
                double ex = 0, ey = 0;
                if (!cal.pixelFor(x, y, x, y, ex, ey)) { ex = img.width / 2.0; ey = img.height / 2.0; }
                JPRoundMarkFinder::Request rq;
                rq.expectedX = ex;
                rq.expectedY = ey;
                rq.searchRadius = rig.rigTestObjectDiameter * scale;
                rq.diameter = rig.rigTestObjectDiameter * scale;
                const JPRoundMark found = JPCameraLook::findTryingHarder(m_cell, *feed, img, rq);
                double fx = 0, fy = 0;
                if (!found.found || !cal.machinePoint(found.x, found.y, x, y, fx, fy)) {
                    words = "the test object was not found: " + found.why;
                    return false;
                }
                const double moved = std::hypot(fx - x, fy - y);
                x = fx;
                y = fy;
                if (moved < kCentredMm) return true;
            }
            return true;
        };
        double x = start.first, y = start.second;
        progress("finding the test object");
        if (!centreOn(x, y)) return false;
        double sumX = 0, sumY = 0;
        int accumulated = 0;
        const double da = 360.0 / std::max(1, m_cell.config().nozzleOffsetAngles);
        bool ok = true;
        bool holding = false;   // picked and not yet placed
        const int angles = std::max(1, m_cell.config().nozzleOffsetAngles);
        int at = 0;
        for (double angle = -180 + da / 2; angle < 180 && ok; angle += da) {
            char step[80];
            std::snprintf(step, sizeof step, "pick and place at %.0f deg, %d of %d", angle, ++at, angles);
            progress(step);
            sumX -= x;
            sumY -= y;
            // Picked at the angle, placed turned 180: the true axis is midway between the two places.
            ok = m_cell.pickAtAndWait(n.id, { x, y, z, angle }, kTaskSpeed, words);
            holding = ok;
            if (ok) std::this_thread::sleep_for(std::chrono::milliseconds(kExtraVacuumDwellMs));
            ok = ok && m_cell.placeAtAndWait(n.id, { x, y, z + kTestObjectHeightMm, angle + 180 }, kTaskSpeed, words);
            if (ok) holding = false;
            if (ok) std::this_thread::sleep_for(std::chrono::milliseconds(kExtraVacuumDwellMs));
            ok = ok && centreOn(x, y);
            sumX += x;
            sumY += y;
            accumulated += 2;
        }
        // The test object let go where it is, if it is still held (as OpenPnP's); then up, unturned, whatever happened.
        if (holding) m_cell.place(n.id);
        std::string up;
        m_cell.moveToolAndWait(n.mount, { std::nullopt, std::nullopt, std::nullopt, 0.0 }, kTaskSpeed, up);
        if (!ok) return false;
        offsets->first = sumX / accumulated;
        offsets->second = sumY / accumulated;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s's offsets %+.4f, %+.4f mm off", n.name.c_str(), offsets->first, offsets->second);
        words = buf;
        return true;
    }, [offsets, done](bool ok) {
        if (done) done(ok, offsets->first, offsets->second);
    });
}

namespace {

// A settled picture from `feed`, in colour (BGR); false, and why, when there is none.
bool settledColour(JPCameraFeed& feed, cv::Mat& bgr, std::string& why) {
    JPGrayImage settled;
    if (!JPCameraLook::settled(feed, settled, why)) return false;
    JPFrame frame;
    if (!feed.latest(frame, 0) || frame.width <= 0) {
        why = feed.config().name + " gives no picture";
        return false;
    }
    cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data());
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    return true;
}

JPFrame frameOf(const cv::Mat& bgr) {
    cv::Mat rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
    JPFrame f;
    f.width = rgba.cols;
    f.height = rgba.rows;
    f.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    f.captured = std::chrono::steady_clock::now();
    return f;
}

// A calibration's find drawn on its picture, as vision shows its results: a circle the size it was found and a
// cross at its centre.
JPFrame foundFrame(const JPGrayImage& picture, double x, double y, double diameterPx) {
    cv::Mat gray(picture.height, picture.width, CV_32F, const_cast<float*>(picture.pixels.data()));
    cv::Mat bytes, bgr;
    gray.convertTo(bytes, CV_8U);
    cv::cvtColor(bytes, bgr, cv::COLOR_GRAY2BGR);
    const cv::Scalar green(0, 255, 0);
    const cv::Point c(int(std::lround(x)), int(std::lround(y)));
    const int r = std::max(1, int(std::lround(diameterPx / 2)));
    cv::circle(bgr, c, r, green, 2);
    cv::drawMarker(bgr, c, green, cv::MARKER_CROSS, std::max(9, r / 2), 2);
    return frameOf(bgr);
}

} // namespace

std::function<void(const JPGrayImage&, double, double, double, const std::string&)> JPlacerCameraTasks::showFinds(JPCameraPanel& camera) {
    JPCameraPanel* panel = &camera;
    return [panel, alive = std::weak_ptr<bool>(m_alive)](const JPGrayImage& picture, double x, double y, double diameterPx,
                                                          const std::string& step) {
        JPFrame frame = foundFrame(picture, x, y, diameterPx);
        char text[160];
        std::snprintf(text, sizeof text, "%s: found at %.1f, %.1f px (%.0f px across)", step.c_str(), x, y, diameterPx);
        JMainThreadDispatcher::instance().post([alive, panel, frame = std::move(frame), text = std::string(text)] {
            if (const auto a = alive.lock(); a && *a) panel->view().showPicture(frame, text, kFeatureShownMs);
        });
    };
}

std::optional<std::pair<double, double>> JPlacerCameraTasks::cameraAt(const JPCameraPanel& camera) const {
    const JPMountConfig& m = camera.camera().mount;
    const auto p = m_cell.positions();
    const auto x = p.find(m.axisX), y = p.find(m.axisY);
    if (m.axisX.empty() || m.axisY.empty() || x == p.end() || y == p.end()) return std::nullopt;
    return std::pair { x->second + m.offsetX, y->second + m.offsetY };
}

void JPlacerCameraTasks::previewFeature(JPCameraPanel& camera, int px) {
    if (m_busy) return;   // another task's pictures are not to be taken from under it
    if (const auto at = cameraAt(camera)) m_featureAt = at;
    JPCameraPanel* panel = &camera;
    run(camera, "Feature diameter", [this, panel, px](std::string& words, const auto&) {
        cv::Mat bgr;
        if (!settledColour(panel->feed(), bgr, words)) return false;
        double score = 0;
        const auto found = JPVisionFeature::detect(bgr, px, JPVisionFeature::kPreviewSearch, true, true, score);
        char text[96];
        std::snprintf(text, sizeof text, "Diameter %d px - Score %.2f", px, score);
        words = text;
        std::weak_ptr<bool> alive = m_alive;
        JMainThreadDispatcher::instance().post([alive, panel, frame = frameOf(bgr), text = std::string(text)] {
            if (const auto a = alive.lock(); a && *a) panel->view().showPicture(frame, text, kFeatureShownMs);
        });
        return found.has_value();
    }, nullptr);
}

void JPlacerCameraTasks::autoDetectFeature(JPCameraPanel& camera, int fromPx, std::function<void(std::optional<int>)> done) {
    if (m_busy) {
        if (done) done(std::nullopt);
        return;
    }
    if (const auto at = cameraAt(camera)) m_featureAt = at;
    JPCameraPanel* panel = &camera;
    auto result = std::make_shared<std::optional<int>>();
    run(camera, "Auto-Detect Next", [this, panel, fromPx, result](std::string& words, const auto& progress) {
        cv::Mat bgr;
        if (!settledColour(panel->feed(), bgr, words)) return false;
        progress("trying every diameter");
        *result = JPVisionFeature::next(bgr, fromPx);
        if (!*result) {
            words = "no feature found";
            return false;
        }
        // The best diameter shown again.
        double score = 0;
        JPVisionFeature::detect(bgr, **result, JPVisionFeature::kPreviewSearch, true, true, score);
        char text[96];
        std::snprintf(text, sizeof text, "Best Diameter %d px", **result);
        words = text;
        std::weak_ptr<bool> alive = m_alive;
        JMainThreadDispatcher::instance().post([alive, panel, frame = frameOf(bgr), text = std::string(text)] {
            if (const auto a = alive.lock(); a && *a) panel->view().showPicture(frame, text, kFeatureShownMs);
        });
        return true;
    }, [result, done](bool) {
        if (done) done(*result);
    });
}

void JPlacerCameraTasks::measureFeature(JPCameraPanel& camera, int px, std::function<void(std::optional<double>)> done) {
    if (const std::string why = m_busy ? std::string("a camera task is already under way") : notReady(&camera, true, false); !why.empty()) {
        m_window.showStatus("Feature: " + why, kResultMs);
        if (done) done(std::nullopt);
        return;
    }
    JPCameraPanel* panel = &camera;
    auto mm = std::make_shared<std::optional<double>>();
    const auto at = m_featureAt;
    run(camera, "Measuring the feature", [this, panel, px, mm, at](std::string& words, const auto&) {
        // Measured where it was sized (the camera taken back there, if it was moved since).
        if (at && !m_cell.moveToolAndWait(panel->camera().mount, { at->first, at->second, std::nullopt, std::nullopt }, kTaskSpeed, words))
            return false;
        JPCameraCalibration cal;
        if (!JPCameraLook::calibration(m_cell, panel->feed(), cal, words)) return false;
        cv::Mat bgr;
        if (!settledColour(panel->feed(), bgr, words)) return false;
        double score = 0;
        const auto found = JPVisionFeature::detect(bgr, px, 0, false, false, score);
        if (!found) {
            words = "Subject not found.";
            return false;
        }
        *mm = found->diameter / cal.scale();
        char text[96];
        std::snprintf(text, sizeof text, "the feature is %.3f mm (%.1f px) across", **mm, found->diameter);
        words = text;
        return true;
    }, [mm, done](bool) {
        if (done) done(*mm);
    });
}

void JPlacerCameraTasks::autoFocusTest(JPCameraPanel& camera, const JPNozzleConfig& nozzle, std::function<void(double)> done) {
    const JPNozzleTipConfig* tip = nullptr;
    for (const JPNozzleTipConfig& t : m_cell.config().nozzleTips)
        if (t.id == nozzle.tipId) tip = &t;
    if (const std::string why = m_busy ? std::string("a camera task is already under way")
                              : !tip ? std::string("A nozzle tip must be loaded.")
                              : notReady(&camera, true, false);
        !why.empty()) {
        m_window.showStatus("Auto Focus: " + why, kResultMs);
        return;
    }
    const JPNozzleTipConfig t = *tip;
    const JPMountConfig mount = nozzle.mount;
    auto distance = std::make_shared<double>(0);
    JPCameraPanel* panel = &camera;
    std::weak_ptr<bool> alive = m_alive;
    run(camera, "Auto Focus", [this, panel, t, mount, distance, alive](std::string& words, const auto&) {
        JPCameraFeed& feed = panel->feed();
        JPCameraCalibration cal;
        if (!JPCameraLook::calibration(m_cell, feed, cal, words)) return false;
        const JPCameraConfig& cam = feed.config();
        JPAutoFocus::Request rq;
        rq.tool = mount;
        rq.x = cam.mount.offsetX;
        rq.y = cam.mount.offsetY;
        rq.z1 = cam.mount.offsetZ;
        rq.z0 = rq.z1 + t.maxPartHeightMm;
        rq.subjectMaxSizeMm = t.maxPartDiameterMm + 2 * t.maxPickToleranceMm;
        rq.mmPerPixel = cal.scale() > 0 ? 1 / cal.scale() : 0;
        rq.settings = cam.autoFocus;
        rq.machineSpeed = m_cell.speed();
        auto show = [panel, alive](const JPFrame& frame, const std::string& text) {
            JMainThreadDispatcher::instance().post([panel, alive, frame, text] {
                if (const auto a = alive.lock(); a && *a) panel->view().showPicture(frame, text, kAutoFocusShownMs);
            });
        };
        const auto z = JPAutoFocus::run(m_cell, feed, rq, show, words);
        if (!z) return false;
        *distance = *z - rq.z1;
        char text[96];
        std::snprintf(text, sizeof text, "in focus at Z %.3f, %.3f mm above the camera's Z", *z, *distance);
        words = text;
        return true;
    }, [distance, done](bool ok) {
        if (ok && done) done(*distance);
    });
}

void JPlacerCameraTasks::settleTest(JPCameraPanel& camera, const JPMountConfig* tool, SettleMove move,
                                    std::function<void(const JPSettleTrace&)> done) {
    // What moves: the camera, on a head; else the tool held over it.
    const bool fixed = camera.camera().mount.headId.empty();
    const JPMountConfig* moving = fixed ? tool : &camera.camera().mount;
    const bool planar = move.dx != 0 || move.dy != 0;
    const bool moves = planar && moving && !moving->axisX.empty() && !moving->axisY.empty();
    const bool turns = move.dc != 0 && fixed && moving && !moving->axisRotation.empty();
    std::string why = m_busy ? "a camera task is already under way" : std::string();
    if (why.empty() && (moves || turns || move.up) && !m_cell.isHomed()) why = "home the machine first";
    if (why.empty() && planar && !moves) why = fixed ? "no nozzle to move over it" : "it does not move on X and Y";
    if (why.empty() && move.dc != 0 && !turns) why = "no nozzle to turn over it";
    if (why.empty() && move.up && (!fixed || !moving)) why = "no nozzle to bring over it";
    if (!why.empty()) {
        m_window.showStatus("Settling test: " + why, kResultMs);
        return;
    }
    JPCameraFeed* feed = &camera.feed();
    auto trace = std::make_shared<JPSettleTrace>();
    const JPMountConfig mount = moving ? *moving : JPMountConfig{};
    const JPMountConfig cameraMount = camera.camera().mount;
    run(camera, "Settling test", [this, feed, moves, turns, move, mount, cameraMount, trace](std::string& words, const auto& progress) {
        const auto at = m_cell.jogBase();
        if (moves) {
            progress("moving and back");
            const double x = at.at(mount.axisX), y = at.at(mount.axisY);
            if (!m_cell.moveAxesAndWait({ { mount.axisX, x + move.dx }, { mount.axisY, y + move.dy } }, kTaskSpeed, words)
                || !m_cell.moveAxesAndWait({ { mount.axisX, x }, { mount.axisY, y } }, kTaskSpeed, words))
                return false;
        }
        if (turns) {
            progress("turning and back");
            const double c = at.at(mount.axisRotation);
            if (!m_cell.moveAxesAndWait({ { mount.axisRotation, c + move.dc } }, kTaskSpeed, words)
                || !m_cell.moveAxesAndWait({ { mount.axisRotation, c } }, kTaskSpeed, words))
                return false;
        }
        if (move.up) {
            // Where the nozzle is, against where the camera is.
            auto coordinate = [&at](const std::string& axis, double offset) {
                const auto p = at.find(axis);
                return (p == at.end() ? 0.0 : p->second) + offset;
            };
            const double x = coordinate(mount.axisX, mount.offsetX), y = coordinate(mount.axisY, mount.offsetY),
                         z = coordinate(mount.axisZ, mount.offsetZ);
            const double far = std::sqrt(std::pow(x - cameraMount.offsetX, 2) + std::pow(y - cameraMount.offsetY, 2)
                                         + std::pow(z - cameraMount.offsetZ, 2));
            if (far > kUpNearMm) {
                progress("bringing the nozzle over the camera");
                if (!m_cell.moveToolAndWait(mount, { cameraMount.offsetX, cameraMount.offsetY, cameraMount.offsetZ, std::nullopt },
                                            kTaskSpeed, words))
                    return false;
            } else {
                progress("up to Safe Z and back");
                if (!m_cell.safeZAndWait(mount.headId, kTaskSpeed, words)
                    || !m_cell.moveToolStraightAndWait(mount, { x, y, z, std::nullopt }, kTaskSpeed, words))
                    return false;
            }
        }
        progress("letting it settle");
        JPGrayImage picture;
        if (!JPCameraLook::settled(*feed, picture, words, trace.get())) return false;
        char buf[160];
        if (trace->settledMs >= 0)
            std::snprintf(buf, sizeof buf, "%s settled after %.0f ms (%zu pictures)", feed->config().name.c_str(),
                          trace->settledMs, trace->points.size());
        else
            std::snprintf(buf, sizeof buf, "%s did not settle within its timeout", feed->config().name.c_str());
        words = buf;
        return true;
    }, [trace, done](bool ok) {
        if (ok && done) done(*trace);
    });
}

void JPlacerCameraTasks::visualTest(JPCameraPanel& camera) {
    if (const std::string why = notReady(&camera, true, true); !why.empty()) {
        m_window.showStatus("Visual Test: " + why, kResultMs);
        return;
    }
    JPCameraFeed* feed = &camera.feed();
    const JPHeadConfig h = *head(feed->config());
    const auto look = homeFiducialLook ? homeFiducialLook() : std::nullopt;
    run(camera, "Visual Test", [this, feed, h, look](std::string& words, const auto& progress) {
        progress("looking at the homing mark");
        const JPVisualTest::Result r = JPVisualTest::run(m_cell, *feed, h, kTaskSpeed, look ? &*look : nullptr);
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

void JPlacerCameraTasks::captureMark(const std::string& headId, std::function<void(std::optional<Mark>)> done) {
    JPCameraPanel* camera = nullptr;
    for (JPCameraPanel* p : m_cameras)
        if (!camera && p->camera().mount.headId == headId && !p->camera().mount.axisX.empty()) camera = p;
    const std::string why = !camera ? "the head has no camera" : notReady(camera, true, false);
    if (!why.empty()) {
        m_window.showStatus("Visual Homing: " + why, kResultMs);
        if (done) done(std::nullopt);
        return;
    }
    JPCameraFeed* feed = &camera->feed();
    double vx = 0, vy = 0;
    std::string where;
    if (!cameraView(camera->camera(), vx, vy, where)) {
        m_window.showStatus("Visual Homing: " + where, kResultMs);
        if (done) done(std::nullopt);
        return;
    }
    auto mark = std::make_shared<Mark>();
    run(*camera, "Finding the homing mark", [this, feed, vx, vy, mark](std::string& words, const auto& progress) {
        progress("looking for the mark under the camera");
        JPGrayImage img;
        if (!JPCameraLook::settled(*feed, img, words)) return false;
        const double side = std::min(img.width, img.height);
        const JPRoundMark m = JPRoundMarkFinder::findAnySize(img, img.width / 2.0, img.height / 2.0, kMarkSearchShare * side,
                                                            kLeastMarkShare * side, kMostMarkShare * side);
        if (!m.found) {
            words = "no round mark near the middle of the picture: put the camera over the mark (" + m.why + ")";
            return false;
        }
        const JPCameraCalibration cal = m_cell.cameraCalibration(feed->config().id, img.width, img.height);
        if (!cal.valid || !cal.machinePoint(m.x, m.y, vx, vy, mark->x, mark->y)) {
            words = feed->config().name + " is not calibrated for its pictures";
            return false;
        }
        mark->diameter = m.diameter / cal.scale();
        char buf[160];
        std::snprintf(buf, sizeof buf, "The homing mark: %.3f mm across at X %.3f, Y %.3f", mark->diameter, mark->x, mark->y);
        words = buf;
        return true;
    }, [mark, done](bool ok) {
        if (done) done(ok ? std::optional(*mark) : std::nullopt);
    });
}

void JPlacerCameraTasks::visualHome(std::function<void(bool)> done) {
    const JPHeadConfig* homing = nullptr;
    for (const JPHeadConfig& h : m_cell.config().heads)
        if (h.visualHoming) homing = &h;
    if (!homing) {
        if (done) done(true);
        return;
    }
    JPCameraPanel* camera = nullptr;
    for (JPCameraPanel* p : m_cameras)
        if (!camera && p->camera().mount.headId == homing->id && !m_cell.cameraCalibrations(p->camera().id).empty()) camera = p;
    if (!camera) {
        m_window.showStatus("Homed by the switches only: calibrate a camera on " + homing->name
                            + " to finish homing with the homing mark", kResultMs);
        if (done) done(true);
        return;
    }
    if (m_busy) {
        if (done) done(false);
        return;
    }
    if (const std::string why = notReady(camera, true, true); !why.empty()) {
        m_window.showStatus("Visual homing: " + why, kResultMs);
        if (done) done(false);
        return;
    }
    JPCameraFeed* feed = &camera->feed();
    const JPHeadConfig h = *homing;
    const auto look = homeFiducialLook ? homeFiducialLook() : std::nullopt;
    // Auto-Tune when homing: over the head's primary fiducial (else its homing fiducial), the light on as run()
    // switches it for vision, before the homing fiducial is looked for.
    const std::optional<JPMachineLocation> tuneAt =
        camera->camera().autoTuneOnHoming ? (h.rigPrimary ? h.rigPrimary : h.homingFiducial) : std::nullopt;
    run(*camera, "Visual homing", [this, feed, h, look, tuneAt](std::string& words, const auto& progress) {
        if (tuneAt) {
            progress("auto-tuning on the primary fiducial");
            if (!autoTuneAt(*feed, *tuneAt, words)) {
                words = "Auto-Tune when homing: " + words;
                return false;
            }
        }
        progress("looking at the homing mark");
        const JPVisualHoming::Result r = JPVisualHoming::run(m_cell, *feed, h, kTaskSpeed, look ? &*look : nullptr);
        if (!r.ok) {
            words = r.why;
            return false;
        }
        char buf[200];
        std::snprintf(buf, sizeof buf, "Homed: the homing mark corrected the position by %+.3f mm in X and %+.3f mm in Y",
                      r.correctedX, r.correctedY);
        words = buf;
        return true;
    }, std::move(done));
}

bool JPlacerCameraTasks::autoTuneAt(JPCameraFeed& feed, const JPMachineLocation& at, std::string& why) {
    const JPMountConfig& mount = feed.config().mount;
    if (!m_cell.moveAxesAndWait({ { mount.axisX, at.x - mount.offsetX }, { mount.axisY, at.y - mount.offsetY } }, kTaskSpeed, why))
        return false;
    // Told on the capture thread; shared, so a late answer has somewhere to go.
    auto told = std::make_shared<std::promise<std::optional<JJson>>>();
    std::future<std::optional<JJson>> tuned = told->get_future();
    feed.autoTune(JPCameraFeed::kAutoTuneMs, [told](std::optional<JJson> t) { told->set_value(std::move(t)); });
    if (tuned.wait_for(std::chrono::milliseconds(kTuneWaitMs)) != std::future_status::ready) {
        why = feed.config().name + " was not tuned within " + std::to_string(kTuneWaitMs / 1000) + " s";
        return false;
    }
    const std::optional<JJson> controls = tuned.get();
    if (!controls) {
        why = feed.config().name + " was not tuned (it has no properties of its own, it stopped, or the values found did not give the picture it gave by itself: see the log)";
        return false;
    }
    // Kept in the cell and saved, as a calibration is, on the main thread.
    std::weak_ptr<bool> alive = m_alive;
    JMainThreadDispatcher::instance().post([this, alive, id = feed.config().id, c = *controls] {
        if (const auto a = alive.lock(); !a || !*a) return;
        m_cell.setCameraControls(id, c);
        std::string error;
        if (!m_cell.config().save(m_cellPath, error)) {
            JLOGC(JPlacerLog::kApp, JLogLevel::Error) << error;
            m_window.showStatus("The camera's tuned properties are in use but were not saved: " + error, kResultMs);
        }
        if (onTuned) onTuned(id, c);
    });
    return true;
}

JPCameraPanel* JPlacerCameraTasks::headCamera() const {
    JPCameraPanel* first = nullptr;
    for (JPCameraPanel* p : m_cameras) {
        const JPMountConfig& m = p->camera().mount;
        if (m.axisX.empty() || m.axisY.empty()) continue;
        if (!m_cell.cameraCalibrations(p->camera().id).empty()) return p;
        if (!first) first = p;
    }
    return first;
}

bool JPlacerCameraTasks::cameraView(const JPCameraConfig& camera, double& x, double& y, std::string& why) const {
    const JPMountConfig& m = camera.mount;
    if (m.axisX.empty() || m.axisY.empty()) { why = camera.name + " does not ride on a head"; return false; }
    if (!m_cell.isHomed()) { why = "home the machine first"; return false; }
    const auto at = m_cell.jogBase();
    x = at.at(m.axisX) + m.offsetX;
    y = at.at(m.axisY) + m.offsetY;
    return true;
}

bool JPlacerCameraTasks::cameraLook(const std::string& cameraId, JPCameraCalibration& calibration, double& viewX,
                                    double& viewY) const {
    std::string why;
    for (JPCameraPanel* p : m_cameras)
        if (p->camera().id == cameraId) {
            const auto mode = p->feed().mode();
            if (!mode || !cameraView(p->camera(), viewX, viewY, why)) return false;
            calibration = m_cell.cameraCalibration(cameraId, mode->width, mode->height);
            return calibration.valid;
        }
    return false;
}

bool JPlacerCameraTasks::lookAt(JPCameraPanel& camera, double x, double y) {
    if (m_busy) {
        m_window.showStatus("A camera task is under way", kResultMs);
        return false;
    }
    double vx, vy;
    std::string why;
    if (!cameraView(camera.camera(), vx, vy, why)) {
        m_window.showStatus(why, kResultMs);
        return false;
    }
    const JPMountConfig& m = camera.camera().mount;
    m_cell.moveAxes({ { m.axisX, x - m.offsetX }, { m.axisY, y - m.offsetY } }, kTaskSpeed);
    return true;
}

void JPlacerCameraTasks::calibrateFixed(JPCameraPanel& camera, std::function<void(bool ok)> finished) {
    JPCameraFeed* feed = &camera.feed();
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
        if (finished) finished(false);
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
    JPCameraPanel* panel = &camera;
    JDialog::confirm("Calibrate " + cam.name, body, [this, alive, panel, feed, tool, place, cameraId, finished] {
        if (const auto a = alive.lock(); !a || !*a) return;
        if (m_busy) {   // another task began while asking
            if (finished) finished(false);
            return;
        }
        auto result = std::make_shared<JPCameraCalibration>();
        auto finds = showFinds(*panel);
        run(*panel, "Calibrating " + feed->config().name, [this, feed, tool, place, result, finds](std::string& words, const auto& progress) {
            progress("the nozzle over the camera");
            const bool over = m_cell.safeZAndWait(tool.headId, kTaskSpeed, words)
                && m_cell.moveAxesAndWait({ { tool.axisX, place.offsetX - tool.offsetX },
                                            { tool.axisY, place.offsetY - tool.offsetY } }, kTaskSpeed, words)
                && m_cell.moveAxesAndWait({ { tool.axisZ, place.offsetZ - tool.offsetZ } }, kTaskSpeed, words);
            std::optional<JPCameraCalibration> c;
            std::string secondWhy;
            if (over) {
                JPCameraCalibrator::Options o;
                o.markZ = place.offsetZ;
                o.speed = kTaskSpeed;
                o.calibrating = feed->config().calibrating;
                o.moving = &tool;
                o.found = finds;
                c = JPCameraCalibrator::run(m_cell, *feed, o, words, progress);
                // Again with the tip raised (away from a camera looking up).
                const double raise = o.calibrating.raiseMm;
                if (c && o.calibrating.twoHeights && raise >= kLeastHeightGapMm) {
                    char said[64];
                    std::snprintf(said, sizeof said, "the nozzle raised %.1f mm", raise);
                    progress(said);
                    JPCameraCalibrator::Options o2 = o;
                    o2.markZ = place.offsetZ + raise;
                    std::string why;
                    std::optional<JPCameraCalibration> c2;
                    if (m_cell.moveAxesAndWait({ { tool.axisZ, place.offsetZ + raise - tool.offsetZ } }, kTaskSpeed, why))
                        c2 = JPCameraCalibrator::run(m_cell, *feed, o2, why, progress);
                    if (c2) secondHeight(*c, *c2);
                    else secondWhy = "; raised: " + why;
                }
            }
            // Up again, whatever happened.
            std::string up;
            if (!m_cell.safeZAndWait(tool.headId, kTaskSpeed, up) && words.empty()) words = up;
            if (!c) return false;
            *result = *c;
            words = calibrated(feed->config(), *c) + secondWhy;
            return true;
        }, [this, cameraId, result, finished](bool ok) {
            if (ok) keepCalibration(cameraId, *result);
            if (finished) finished(ok);
        });
    }, [finished] {
        if (finished) finished(false);   // not confirmed
    });
}

} // inline namespace jf
