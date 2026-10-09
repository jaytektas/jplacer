// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerCameraTasks.h"

#include <opencv2/imgproc.hpp>

#include "tasks/JPAutoFocus.h"

#include "common/JPlacerLog.h"
#include "tasks/JPCameraCalibrator.h"
#include "tasks/JPCameraLook.h"
#include "tasks/JPPipelineMarkFinder.h"
#include "pipeline/JPDefaultPipelines.h"
#include "tasks/JPVisualHoming.h"
#include "tasks/JPVisualTest.h"
#include "tasks/JPVisionFeature.h"
#include "ui/JPFootprintOverlay.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <cstdio>
#include <atomic>
#include <future>
#include <thread>
#include <optional>

inline namespace jf {

namespace {
// OpenPnP's CalibrateCameraProcess: the mask a quarter of the picture's smaller side, the green centering circle
// half of it, the search a fifth wider than the Detection Diameter, which starts at 25 px when the tip's size
// is not known.
constexpr double kInitialMaskShare = 1 / 4.0, kCenteringShare = 0.5, kMaskOverDiameter = 1.2;
constexpr int    kDefaultDetectionPx = 25, kLeastDetectionPx = 4;
// Tuned for a calibration within this of where the camera was last tuned: not again (tuneForCalibration).
constexpr double kSameSpotMm = 1.0;
// How often a step waiting on the person looks whether it was answered (with nothing to show meanwhile).
constexpr int    kAskPollMs = 50;
} // namespace

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
            // A fixed camera (one looking up): the nozzle moved, as OpenPnP's.
            if (panel->camera().mount.axisX.empty() || panel->camera().mount.axisY.empty()) {
                lookAtFixed(*panel, px, py);
                return;
            }
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
    letGo();
    if (m_worker.joinable()) m_worker.join();
}

void JPlacerCameraTasks::letGo() {
    *m_alive = false;
    for (JPCameraPanel* panel : m_cameras) {
        panel->onCalibrate   = nullptr;
        panel->onVisualTest  = nullptr;
        panel->onLookAtPixel = nullptr;
        panel->onCancelTask  = nullptr;
    }
    m_cameras.clear();
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

void JPlacerCameraTasks::showLookFootprint(JPCameraPanel& camera, const std::optional<JPVisualTest::Look>& look) {
    if (!look || !look->footprint) {
        camera.view().setTaskOverlay(nullptr);
        return;
    }
    // Turned as the camera is (a camera without a rotation axis: as it is mounted).
    const std::string axis = camera.camera().mount.axisRotation;
    camera.view().setTaskOverlay(JPFootprintOverlay::of(*look->footprint, [this, axis] {
        if (axis.empty()) return 0.0;
        const auto p = m_cell.positions();
        const auto at = p.find(axis);
        return at == p.end() ? 0.0 : at->second;
    }));
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
    // The red X beside Calibrate (OpenPnP's Cancel of its CalibrateCameraProcess): it stops before its next
    // move, the one under way left to end where it was going.
    m_cell.setCancelled(false);
    panel->onCancelTask = [this, panel, name] {
        m_cell.setCancelled(true);
        panel->setNote(name + ": cancelling, stopping before the next move\xE2\x80\xA6");
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
        const bool ok = task(words, [this, panel, name, onMain](const std::string& step) {
            onMain([this, panel, name, step] {
                if (!m_cell.isCancelled()) panel->setNote(name + ": " + step);
            });
        });
        // Cancelled: said as such, not as a failure.
        const bool cancelled = !ok && m_cell.isCancelled();
        m_cell.setCancelled(false);
        if (!light.empty() && cam.light.afterCapture) m_cell.switchActuatorAndWait(light, false, lightWhy);
        JLOGC(JPlacerLog::kCamera, ok || cancelled ? JLogLevel::Info : JLogLevel::Warn) << name << ": "
                                                                                      << (cancelled ? "cancelled" : words);
        onMain([this, panel, name, ok, cancelled, words, done] {
            m_busy = false;
            panel->setBusy(false);
            panel->view().setTaskOverlay(nullptr);
            panel->onCancelTask = nullptr;
            panel->endStep();   // a step the person was asked to do, left over
            panel->view().setMarks({});
            const std::string text = ok ? words : cancelled ? name + " cancelled" : name + " failed: " + words;
            m_window.showStatus(text, kResultMs);   // logged above
            if (onTaskOutcome) onTaskOutcome(ok || cancelled ? std::string() : text);
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
    // Over the head's calibration rig's primary mark, as OpenPnP calibrates (its height is the mark's: the homing
    // mark's Z is not one), else the homing mark. Asked to, or neither yet (a new machine): over the mark it is
    // over now, of a size it finds.
    const bool rig = h.rigPrimary && h.rigPrimaryDiameter > 0;
    const std::optional<JPMachineLocation> mark = rig ? h.rigPrimary : h.homingFiducial;
    const double markDiameter = rig ? h.rigPrimaryDiameter : h.homingFiducialDiameter;
    const bool hasMark = !here && mark && markDiameter > 0;
    const std::string cameraId = feed->config().id;
    auto result = std::make_shared<JPCameraCalibration>();
    auto finds = showFinds(camera);
    run(camera, "Calibrating " + feed->config().name, [this, feed, h, rig, mark, markDiameter, hasMark, result, finds](std::string& words,
                                                                                                             const auto& progress) {
        // Over the mark first, as near as the camera's offset on the head says.
        const JPMountConfig& m = feed->config().mount;
        if (hasMark) {
            progress(rig ? "moving over the calibration rig's primary mark" : "moving over the homing mark");
            if (!m_cell.moveAxesAndWait({ { m.axisX, mark->x - m.offsetX }, { m.axisY, mark->y - m.offsetY } }, kTaskSpeed, words))
                return false;
        }
        JPCameraCalibrator::Options o;
        o.markDiameterMm = hasMark ? markDiameter : 0;
        o.markZ = mark ? mark->z : 0;
        o.speed = kTaskSpeed;
        o.calibrating = feed->config().calibrating;
        o.found = finds;
        // Again over the secondary mark, at another height: a second pass.
        const bool twoPasses = hasMark && o.calibrating.twoHeights && h.rigSecondary
                            && std::abs(h.rigSecondary->z - mark->z) >= kLeastHeightGapMm;
        if (twoPasses) o.pass = "pass 1 of 2";
        if (feed->config().autoTuneCalibrating) {
            progress("Auto-Tune over the mark");
            if (!autoTuneHere(*feed, words)) return false;
            tunedHere(*feed);
        }
        const auto c = JPCameraCalibrator::run(m_cell, *feed, o, words, progress);
        if (!c) return false;
        *result = *c;
        std::string second;
        if (twoPasses) {
            progress("pass 2 of 2, moving over the secondary mark");
            JPCameraCalibrator::Options o2 = o;
            o2.pass = "pass 2 of 2";
            o2.markDiameterMm = h.rigSecondaryDiameter;
            o2.markZ = h.rigSecondary->z;
            o2.markPxPerMm = c->scale();   // at the first pass's scale, as a start
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
        // Tuned over the mark it measures, first (Auto-Tune when calibrating?).
        if (feed->config().autoTuneCalibrating) {
            if (!autoTuneAt(*feed, *h.homingFiducial, words)) return false;
            tunedHere(*feed);
        }
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
        // Tuned on the tip first, over the camera at its focus (the camera's Auto-Tune when calibrating?).
        if (camera->camera().autoTuneCalibrating) {
            const JPMountConfig& cm = camera->camera().mount;
            progress("Auto-Tune on the tip");
            if (!m_cell.moveToolAndWait(n.mount, { cm.offsetX, cm.offsetY, cm.offsetZ + t.runoutCalibration.zOffset, std::nullopt },
                                        kTaskSpeed, w)
                || !autoTuneHere(camera->feed(), w))
                return false;
        }
        const auto r = JPRunoutCalibrator::measure(m_cell, camera->feed(), n, t, m_scripting.get(), w, progress, *background,
                                                   showFinds(*camera));
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
            o.found = showFinds(*camera);
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
    start();
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
                JPFrame img;
                if (!JPCameraLook::settled(*feed, img, words)) return false;
                // By the camera's calibration pipeline, as OpenPnP's centerInOnSubjectLocation finds it.
                double diameter = rig.rigTestObjectDiameter, fx = 0, fy = 0;
                const JPRoundMark found = JPPipelineMarkFinder::onMachine(feed->config(), cal, img, x, y, x, y,
                                                                          rig.rigTestObjectDiameter, diameter, 0, 0, fx, fy);
                if (!found.found) {
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
        // Over the test object, tuned there first (Auto-Tune when calibrating?).
        if (feed->config().autoTuneCalibrating) {
            if (!m_cell.moveToolAndWait(cm, { x, y, std::nullopt, std::nullopt }, kTaskSpeed, words)
                || !tuneForCalibration(*feed, progress, words))
                return false;
        }
        progress("finding the test object");
        if (!centreOn(x, y)) return false;
        double sumX = 0, sumY = 0;
        int accumulated = 0;
        const double da = 360.0 / std::max(1, m_cell.config().nozzleOffsetAngles);
        bool ok = true;
        bool holding = false;   // picked and not yet placed
        const int angles = std::max(1, m_cell.config().nozzleOffsetAngles);
        int at = 0;
        std::vector<std::pair<double, double>> each;   // each angle's own estimate (half how far it moved)
        for (double angle = -180 + da / 2; angle < 180 && ok; angle += da) {
            char step[80];
            std::snprintf(step, sizeof step, "pick and place at %.0f deg, %d of %d", angle, ++at, angles);
            progress(step);
            const double beforeX = x, beforeY = y;
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
            if (ok) {
                // This angle's estimate, in the log, so how well they agree can be seen.
                each.push_back({ (x - beforeX) / 2, (y - beforeY) / 2 });
                JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "  " << n.name << " at " << angle << " deg: "
                                                            << each.back().first << ", " << each.back().second << " mm off";
            }
        }
        // The test object let go where it is, if it is still held (as OpenPnP's); then up, unturned, whatever happened.
        if (holding) m_cell.place(n.id);
        std::string up;
        m_cell.moveToolAndWait(n.mount, { std::nullopt, std::nullopt, std::nullopt, 0.0 }, kTaskSpeed, up);
        if (!ok) return false;
        offsets->first = sumX / accumulated;
        offsets->second = sumY / accumulated;
        // How far the angles' estimates are from their mean, at most: how far the result can be trusted.
        double spread = 0;
        for (const auto& [ex, ey] : each) spread = std::max(spread, std::hypot(ex - offsets->first, ey - offsets->second));
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s's offsets %+.4f, %+.4f mm off (%zu angles, each within %.4f mm of that)", n.name.c_str(),
                      offsets->first, offsets->second, each.size(), spread);
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
JPFrame foundFrame(const cv::Mat& picture, double x, double y, double diameterPx) {
    cv::Mat bgr = picture.clone();
    const cv::Scalar green(0, 255, 0);
    const cv::Point c(int(std::lround(x)), int(std::lround(y)));
    const int r = std::max(1, int(std::lround(diameterPx / 2)));
    cv::circle(bgr, c, r, green, 2);
    cv::drawMarker(bgr, c, green, cv::MARKER_CROSS, std::max(9, r / 2), 2);
    return frameOf(bgr);
}

} // namespace

std::function<void(const cv::Mat&, double, double, double, const std::string&)> JPlacerCameraTasks::showFinds(JPCameraPanel& camera) {
    JPCameraPanel* panel = &camera;
    return [panel, alive = std::weak_ptr<bool>(m_alive)](const cv::Mat& picture, double x, double y, double diameterPx,
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
    run(camera, "Feature diameter", [this, panel, px](std::string& words, const auto& progress) {
        if (!tuneForCalibration(panel->feed(), progress, words)) return false;
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
        if (!tuneForCalibration(panel->feed(), progress, words)) return false;
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
    run(camera, "Measuring the feature", [this, panel, px, mm, at](std::string& words, const auto& progress) {
        // Measured where it was sized (the camera taken back there, if it was moved since).
        if (at && !m_cell.moveToolAndWait(panel->camera().mount, { at->first, at->second, std::nullopt, std::nullopt }, kTaskSpeed, words))
            return false;
        if (!tuneForCalibration(panel->feed(), progress, words)) return false;
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
    run(camera, "Auto Focus", [this, panel, t, mount, distance, alive](std::string& words, const auto& progress) {
        JPCameraFeed& feed = panel->feed();
        JPCameraCalibration cal;
        if (!JPCameraLook::calibration(m_cell, feed, cal, words)) return false;
        const JPCameraConfig& cam = feed.config();
        // Tuned on the tip over the camera at its Z first (Auto-Tune when calibrating?).
        if (cam.autoTuneCalibrating
            && (!m_cell.moveToolAndWait(mount, { cam.mount.offsetX, cam.mount.offsetY, cam.mount.offsetZ, std::nullopt }, kTaskSpeed, words)
                || !tuneForCalibration(feed, progress, words)))
            return false;
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
    showLookFootprint(camera, look);
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
        if (!tuneForCalibration(*feed, progress, words)) return false;
        progress("looking for the mark under the camera");
        JPFrame img;
        if (!JPCameraLook::settled(*feed, img, words)) return false;
        const JPCameraCalibration cal = m_cell.cameraCalibration(feed->config().id, img.width, img.height);
        if (!cal.valid) {
            words = feed->config().name + " is not calibrated for its pictures";
            return false;
        }
        // Any size, by the camera's calibration pipeline: the picture's side's shares, in mm at its scale.
        const double sideMm = std::min(img.width, img.height) / cal.scale();
        double diameter = 0;
        const JPRoundMark m = JPPipelineMarkFinder::onMachine(feed->config(), cal, img, vx, vy, vx, vy, kMarkSearchShare * sideMm, diameter,
                                                              kLeastMarkShare * sideMm, kMostMarkShare * sideMm, mark->x, mark->y);
        if (!m.found) {
            words = "no round mark near the middle of the picture: put the camera over the mark (" + m.why + ")";
            return false;
        }
        mark->diameter = diameter;
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
    showLookFootprint(*camera, look);
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

bool JPlacerCameraTasks::tuneForCalibration(JPCameraFeed& feed, const std::function<void(const std::string&)>& progress,
                                            std::string& why) {
    if (!feed.config().autoTuneCalibrating) return true;
    // Once a spot: a camera on the head where it looks now; tuned there already (Auto-Detect Next clicked again,
    // the feature then measured where it was sized), not again.
    const auto at = cameraAt(feed);
    if (at && m_tunedId == feed.config().id && m_tunedAt
        && std::hypot(at->first - m_tunedAt->first, at->second - m_tunedAt->second) < kSameSpotMm)
        return true;
    if (progress) progress("Auto-Tune (Auto-Tune when calibrating?)");
    if (!autoTuneHere(feed, why)) return false;
    m_tunedId = feed.config().id;
    m_tunedAt = at;
    return true;
}

void JPlacerCameraTasks::tunedHere(const JPCameraFeed& feed) {
    m_tunedId = feed.config().id;
    m_tunedAt = cameraAt(feed);
}

std::optional<std::pair<double, double>> JPlacerCameraTasks::cameraAt(const JPCameraFeed& feed) const {
    for (JPCameraPanel* p : m_cameras)
        if (&p->feed() == &feed) return cameraAt(*p);
    return std::nullopt;
}

bool JPlacerCameraTasks::autoTuneAt(JPCameraFeed& feed, const JPMachineLocation& at, std::string& why) {
    const JPMountConfig& mount = feed.config().mount;
    if (!m_cell.moveAxesAndWait({ { mount.axisX, at.x - mount.offsetX }, { mount.axisY, at.y - mount.offsetY } }, kTaskSpeed, why))
        return false;
    return autoTuneHere(feed, why);
}

std::optional<JJson> JPlacerCameraTasks::tuneFor(JPCameraFeed& feed, std::string& why) {
    // Told on the capture thread; shared, so a late answer has somewhere to go.
    auto told = std::make_shared<std::promise<std::optional<JJson>>>();
    std::future<std::optional<JJson>> tuned = told->get_future();
    feed.autoTune(JPCameraFeed::kAutoTuneMs, [told](std::optional<JJson> t) { told->set_value(std::move(t)); });
    if (tuned.wait_for(std::chrono::milliseconds(kTuneWaitMs)) != std::future_status::ready) {
        why = feed.config().name + " was not tuned within " + std::to_string(kTuneWaitMs / 1000) + " s";
        return std::nullopt;
    }
    std::optional<JJson> controls = tuned.get();
    if (!controls)
        why = feed.config().name + " was not tuned (it has no properties of its own, it stopped, or the values found did not give the picture it gave by itself: see the log)";
    return controls;
}

bool JPlacerCameraTasks::autoTuneHere(JPCameraFeed& feed, std::string& why) {
    const std::optional<JJson> controls = tuneFor(feed, why);
    if (!controls) return false;
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
    if (onUserAction) onUserAction(camera.camera().id);
    return true;
}

void JPlacerCameraTasks::lookAtFixed(JPCameraPanel& camera, double px, double py) {
    // OpenPnP's CameraView.moveToClick for a camera not on the head: the nozzle moved so the point clicked (or
    // dropped on) comes to the middle of the picture; straight there within the camera's roaming radius, else by
    // way of safe Z, back at its height. While a camera task waits on the person (jogging the tip into the
    // circle), allowed, and the nozzle is the task's.
    if (m_busy && !m_operatorTurn) {
        m_window.showStatus("A camera task is under way", kResultMs);
        return;
    }
    if (!m_cell.isConnected() || !m_cell.isHomed()) {
        m_window.showStatus("To move the nozzle where the picture is clicked, home the machine first", kResultMs);
        return;
    }
    const JPCameraConfig& cam = camera.camera();
    const auto mode = camera.feed().mode();
    if (!mode) return;
    // The tool: the task's, else the nozzle chosen (Jog), else the first on a head.
    std::optional<JPMountConfig> tool = m_busy ? m_operatorTool : std::nullopt;
    if (!tool) {
        const std::string chosen = chosenNozzle ? chosenNozzle() : std::string();
        for (const JPNozzleConfig& n : m_cell.config().nozzles)
            if (!n.mount.axisX.empty() && !n.mount.axisY.empty() && (n.id == chosen || (!tool && chosen.empty()))) tool = n.mount;
    }
    if (!tool) {
        m_window.showStatus("Choose a nozzle (Jog) to move over " + cam.name, kResultMs);
        return;
    }
    // How far the point is from the middle, on the machine: by the camera's calibration, else its rough scale
    // with the picture taken as seen from above, Y up (OpenPnP's getCameraViewCenterOffsetsFromXy).
    const double camX = cam.mount.offsetX, camY = cam.mount.offsetY;
    const JPCameraCalibration cal = m_cell.cameraCalibration(cam.id, mode->width, mode->height);
    double offX = 0, offY = 0;
    if (cal.valid) {
        double x, y;
        if (!cal.machinePoint(px, py, camX, camY, x, y)) return;
        offX = x - camX;
        offY = y - camY;
    } else if (cam.unitsPerPixelX > 0 && cam.unitsPerPixelY > 0) {
        offX = (px - mode->width / 2.0) * cam.unitsPerPixelX;
        offY = -(py - mode->height / 2.0) * cam.unitsPerPixelY;
    } else {
        m_window.showStatus(cam.name + " has no scale to move by: calibrate it, or set its Units Per Pixel", kResultMs);
        return;
    }
    const auto p = m_cell.positions();
    const auto ax = p.find(tool->axisX), ay = p.find(tool->axisY);
    if (ax == p.end() || ay == p.end()) return;
    const double nowX = ax->second + tool->offsetX, nowY = ay->second + tool->offsetY;
    const double toX = nowX - offX, toY = nowY - offY;
    const double r = cam.roamingRadiusMm;
    if (onUserAction) onUserAction(cam.id);
    if (r > 0 && std::hypot(nowX - camX, nowY - camY) < r && std::hypot(toX - camX, toY - camY) < r) {
        m_cell.moveAxes({ { tool->axisX, ax->second - offX }, { tool->axisY, ay->second - offY } }, kTaskSpeed, true);
    } else {
        std::optional<double> z;
        if (const auto az = p.find(tool->axisZ); az != p.end()) z = az->second + tool->offsetZ;
        m_cell.moveTool(*tool, { toX, toY, z, std::nullopt }, kTaskSpeed);
    }
}

bool JPlacerCameraTasks::askOperator(JPCameraPanel* panel, const std::string& line, const std::string& detail,
                                     const std::function<void()>& meanwhile, const NumberAsk* number) {
    auto answer = std::make_shared<std::atomic<int>>(0);   // 1 Next
    std::weak_ptr<bool> alive = m_alive;
    std::optional<NumberAsk> asked;
    if (number) asked = *number;
    // On the camera: the line saying what to do, Next (its tooltip OpenPnP's words), the number when asked for;
    // the red X cancels (run's onCancelTask).
    JMainThreadDispatcher::instance().post([alive, panel, line, detail, answer, asked] {
        if (const auto a = alive.lock(); !a || !*a) return;
        panel->askStep(line, detail, [answer] { *answer = 1; });
        if (asked) {
            auto into = asked->into;
            panel->showStepNumber(asked->label, *into, asked->min, asked->max, [into](int v) { *into = v; });
        }
    });
    // The person's turn: the camera's picture may move the nozzle meanwhile (lookAtFixed), until the task's
    // next move.
    m_operatorTurn = true;
    while (*answer == 0 && !m_cell.isCancelled()) {
        if (meanwhile) meanwhile();
        else std::this_thread::sleep_for(std::chrono::milliseconds(kAskPollMs));
    }
    // Any move the person started ends before the task moves on.
    while (m_cell.isMoving()) std::this_thread::sleep_for(std::chrono::milliseconds(kAskPollMs));
    m_operatorTurn = false;
    JMainThreadDispatcher::instance().post([alive, panel] {
        if (const auto a = alive.lock(); a && *a) panel->endStep();
    });
    return *answer == 1 && !m_cell.isCancelled();
}

void JPlacerCameraTasks::calibrateFixed(JPCameraPanel& camera, std::function<void(bool ok)> finished) {
    JPCameraFeed* feed = &camera.feed();
    const JPCameraConfig& cam = feed->config();
    std::string why = m_busy ? "a camera task is already under way"
                    : !m_cell.isConnected() ? "connect the machine first"
                    : !m_cell.isHomed() ? "home the machine first"
                    : std::string();
    if (!why.empty()) {
        m_window.showStatus("Calibrate: " + why, kResultMs);
        if (finished) finished(false);
        return;
    }
    // OpenPnP's CalibrateCameraProcess for a camera looking up, step by step on the camera's instructions: the
    // smallest tip loaded; the nozzle over the camera, jogged into the green circle; down to the calibration
    // height, turned through 360 degrees to see it stays in the circle (jogged if not); the Detection Diameter
    // set until the red circle turns green with a + on the tip; then the moves, by themselves. At the second
    // height the same, from the tip back in the circle. Cancel (or the red X) stops it at any step.
    JPCameraPanel* panel = &camera;
    std::weak_ptr<bool> alive = m_alive;
    // Its red X cancels before the task begins (the task's own then takes over).
    panel->onCancelTask = [panel, finished] {
        panel->onCancelTask = nullptr;
        panel->endStep();
        panel->setNote("Calibrating " + panel->camera().name + " cancelled");
        if (finished) finished(false);
    };
    panel->askStep("Load the smallest nozzle tip on the nozzle chosen in Jog, then Next",
                   "Select a nozzle and load it with the smallest available nozzle tip. Click Next when ready to proceed.",
        [this, alive, panel, finished] {
            if (const auto a = alive.lock(); !a || !*a) return;
            panel->endStep();
            panel->onCancelTask = nullptr;
            // The nozzle chosen (in Jog), as OpenPnP's the selected one; else the first that moves on X, Y and Z.
            const std::string chosen = chosenNozzle ? chosenNozzle() : std::string();
            const JPNozzleConfig* nozzle = nullptr;
            for (const JPNozzleConfig& n : m_cell.config().nozzles)
                if (!n.mount.headId.empty() && !n.mount.axisX.empty() && !n.mount.axisY.empty() && !n.mount.axisZ.empty()
                    && (n.id == chosen || (!nozzle && chosen.empty())))
                    nozzle = &n;
            if (!nozzle || m_busy) {
                m_window.showStatus(std::string("Calibrate: ") + (m_busy ? "a camera task is already under way"
                                                                          : "choose a nozzle on a head (Jog) to hold over the camera"),
                                    kResultMs);
                if (finished) finished(false);
                return;
            }
            calibrateFixedWith(*panel, *nozzle, finished);
        });
}

void JPlacerCameraTasks::calibrateFixedWith(JPCameraPanel& camera, const JPNozzleConfig& nozzle, std::function<void(bool ok)> finished) {
    JPCameraFeed* feed = &camera.feed();
    const JPCameraConfig& cam = feed->config();
    // The camera's place is its offset: where it looks, and the height in focus.
    const JPMountConfig place = cam.mount;
    const JPMountConfig tool = nozzle.mount;
    const std::string cameraId = cam.id;
    // The tip's size, as OpenPnP's (its Calibration Tip Diameter): its runout's Vision Diameter, else its own
    // Diameter; the Detection Diameter starts from it, in pixels through the camera's rough scale (else
    // OpenPnP's default).
    double tipMm = 0;
    std::string tipName;
    for (const JPNozzleTipConfig& t : m_cell.config().nozzleTips)
        if (t.id == nozzle.tipId) {
            tipMm = t.runoutCalibration.visionDiameter > 0 ? t.runoutCalibration.visionDiameter : t.diameter;
            tipName = t.name;
        }
    const double roughUpp = (cam.unitsPerPixelX + cam.unitsPerPixelY) / 2;
    const int startPx = tipMm > 0 && roughUpp > 0 ? int(std::lround(tipMm / roughUpp)) : kDefaultDetectionPx;
    JPCameraPanel* panel = &camera;
    const std::string nozzleId = nozzle.id;
    auto result = std::make_shared<JPCameraCalibration>();
    auto finds = showFinds(*panel);
    m_operatorTool = tool;   // the nozzle a click on the picture moves while the person has the turn
    run(*panel, "Calibrating " + feed->config().name, [this, panel, feed, tool, place, result, finds, tipMm, tipName, startPx](std::string& words, const auto& progress) {
        std::weak_ptr<bool> alive = m_alive;
        auto marks = [this, alive, panel](std::vector<JPCameraView::Mark> m) {
            JMainThreadDispatcher::instance().post([alive, panel, m = std::move(m)] {
                if (const auto a = alive.lock(); a && *a) panel->view().setMarks(m);
            });
        };
        // The picture's size, for its middle and the green circle (OpenPnP's: a quarter of its smaller side,
        // its centering half of that).
        JPFrame img;
        if (!JPCameraLook::takenFrame(*feed, img, words, 1)) return false;
        const double cx = (img.width - 1) / 2.0, cy = (img.height - 1) / 2.0, side = std::min(img.width, img.height);
        const double centering = side * kInitialMaskShare * kCenteringShare;
        const auto greenCircle = [&] { marks({ { cx, cy, centering, true, false } }); };
        // Where the tip is (its X and Y) after the person has jogged it.
        double atX = 0, atY = 0;
        const auto here = [&] {
            const auto p = m_cell.positions();
            atX = p.at(tool.axisX);
            atY = p.at(tool.axisY);
        };
        // The Detection Diameter set by the person: the tip looked for at it, round the middle, all the while,
        // red where it is looked for, green with a + where found (OpenPnP's swing worker).
        JPPipelineMarkFinder finder(feed->config().calibrationPipeline.empty() ? JPDefaultPipelines::cameraCalibration()
                                                                              : feed->config().calibrationPipeline);
        const auto detectionDiameter = [&](int start, int& px) {
            auto into = std::make_shared<std::atomic<int>>(start);
            std::string lastSaid;
            NumberAsk ask { "Detection Diameter", into, kLeastDetectionPx, int(side) };
            const bool next = askOperator(panel, "Set the Detection Diameter until the circle turns green with a + on the tip, then Next",
                "Use the mouse scroll wheel to zoom in on the fiducial/nozzle tip and then adjust the Detection Diameter "
                "until the red circle turns green with a + at its center and is sized to just fit the fiducial/nozzle tip "
                "with the + centered on the fiducial/nozzle tip. When ready, click Next to begin the automated "
                "calibration collection sequence.",
                [&] {
                    JPFrame look;
                    std::string ignored;
                    if (!JPCameraLook::takenFrame(*feed, look, ignored, 1)) return;
                    const double d = *into;
                    const JPRoundMark m = finder.find(look, cx, cy, std::max(side * kInitialMaskShare / 2, d * kMaskOverDiameter), d);
                    marks({ m.found ? JPCameraView::Mark { m.x, m.y, d, true, true } : JPCameraView::Mark { cx, cy, d, false, false } });
                    // What the search says, beside the number: found or not (where, or why not, its tooltip and the log).
                    char said[200];
                    if (m.found) std::snprintf(said, sizeof said, "Detection Diameter %.0f px: found at %.1f, %.1f", d, m.x, m.y);
                    else std::snprintf(said, sizeof said, "Detection Diameter %.0f px: not found (%s)", d, m.why.c_str());
                    JMainThreadDispatcher::instance().post([alive, panel, found = m.found, text = std::string(said)] {
                        if (const auto a = alive.lock(); a && *a)
                            panel->setStepNumberLabel(found ? "Detection Diameter: found" : "Detection Diameter: not found", text);
                    });
                    if (said != lastSaid) {   // in the log once each time it changes
                        lastSaid = said;
                        JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << feed->config().name << ": " << lastSaid;
                    }
                },
                &ask);
            marks({});
            px = *into;
            return next;
        };
        const auto cancelled = [&] { words = "cancelled"; return false; };

        // Over the camera at safe Z, the green circle in the middle: jogged into it.
        progress("the nozzle over the camera");
        if (!m_cell.safeZAndWait(tool.headId, kTaskSpeed, words)
            || !m_cell.moveAxesAndWait({ { tool.axisX, place.offsetX - tool.offsetX }, { tool.axisY, place.offsetY - tool.offsetY } },
                                       kTaskSpeed, words))
            return false;
        greenCircle();
        if (!askOperator(panel, "Jog the tip into the green circle, then Next (down to the calibration height)",
                         "Using the jog controls on the Machine Controls panel, jog the nozzle tip so that it is "
                                       "approximately in the center of the green circle. When ready, click Next to lower/raise "
                                       "the nozzle tip to the calibration height.", nullptr))
            return cancelled();
        here();
        // At each height: down (or up) to it where the tip was jogged, turned through 360 to see it stays in the
        // circle, the Detection Diameter set, then measured.
        JPCameraCalibrator::Options o;
        o.speed = kTaskSpeed;
        o.calibrating = feed->config().calibrating;
        o.moving = &tool;
        o.found = finds;
        if (tipMm > 0) {
            o.markDiameterMm = tipMm;
            o.markWhat = "the nozzle tip " + tipName;
            o.markSizeFrom = ", or its Vision Diameter (else its Diameter) set wrong";
        }
        const double raise = o.calibrating.raiseMm;
        const bool twoPasses = o.calibrating.twoHeights && raise >= kLeastHeightGapMm;
        std::optional<JPCameraCalibration> c, c2;
        std::string secondWhy;
        int px = startPx;
        for (int pass = 0; pass < (twoPasses ? 2 : 1); ++pass) {
            const double z = place.offsetZ + (pass == 0 ? 0 : raise);
            o.pass = twoPasses ? "pass " + std::to_string(pass + 1) + " of 2" : std::string();
            o.markZ = z;
            progress(o.pass.empty() ? std::string("to the calibration height") : o.pass + ", to its height");
            if (!m_cell.moveAxesAndWait({ { tool.axisX, atX }, { tool.axisY, atY }, { tool.axisZ, z - tool.offsetZ } }, kTaskSpeed, words))
                break;
            if (pass == 0 && feed->config().autoTuneCalibrating) {
                progress("Auto-Tune on the nozzle's tip");
                if (!autoTuneHere(*feed, words)) break;
            }
            greenCircle();
            if (!askOperator(panel, "Turn the tip 360\xC2\xB0 (Jog): it should stay in the green circle; jog it if not, then Next",
                             "Using the jog controls on the Machine Controls panel, rotate the nozzle tip through "
                                           "360 degrees and verify it stays within the green circle. If necessary, jog it in X "
                                           "and/or Y so that it remains within the circle when it is rotated. Click Next when "
                                           "ready.", nullptr)) {
                cancelled();
                break;
            }
            marks({});
            here();
            if (!detectionDiameter(px, px)) {
                cancelled();
                break;
            }
            o.markPx = px;
            // The moves by themselves now: the instructions put away until the next step.
            JMainThreadDispatcher::instance().post([alive, panel] {
                if (const auto a = alive.lock(); a && *a) panel->hideInstructions();
            });
            auto& got = pass == 0 ? c : c2;
            std::string why;
            got = JPCameraCalibrator::run(m_cell, *feed, o, pass == 0 ? words : why, progress);
            if (!got) {
                if (pass == 1) secondWhy = "; raised: " + why;
                break;
            }
        }
        if (c && c2) secondHeight(*c, *c2);
        // Up again, whatever happened.
        std::string up;
        if (!m_cell.safeZAndWait(tool.headId, kTaskSpeed, up) && words.empty()) words = up;
        if (!c) return false;
        *result = *c;
        words = calibrated(feed->config(), *c) + secondWhy;
        return true;
    }, [this, cameraId, result, finished, nozzleId](bool ok) {
        m_operatorTool.reset();
        if (ok) keepCalibration(cameraId, *result);
        if (finished) finished(ok);
        if (ok && onFixedCalibrated) onFixedCalibrated(cameraId, nozzleId);
    });
}

} // inline namespace jf
