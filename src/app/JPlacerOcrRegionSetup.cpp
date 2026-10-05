// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerOcrRegionSetup.h"

#include "model/JPPushPullTemplates.h"
#include "tasks/JPVisionTapeFeeder.h"

#include <j/core/Dialog.h>
#include <j/core/JStyle.h>

#include <cmath>
#include <vector>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
constexpr const char* kTitle = "Setup OCR Region";
constexpr const char* kOverlay = "OcrRegion";
// A camera moved less than this (mm) is taken as not moved.
constexpr double kLeastOffsetMm = 0.1;
// A corner's mark is this many lines across.
constexpr float kMarkLines = 3;
// The steps, in OpenPnP's words.
const char* const kInstructions[] = {
    "Camera position can be offset with jog or by selecting position in the camera view. Click Next to continue.",
    "Click on what will become the upper left corner of your region of interest. Click again to reset and retry. Click Next to continue.",
    "Now, click on what will become the upper right corner of your region of interest. Click again to reset and retry. Click Next to "
    "continue.",
    "Next, click on what will become the lower left corner of your region of interest. Click again to reset and retry. Click Next to "
    "continue.",
    "Finally, click to toggle beween a rectangular and parallelogrammatic region. Click Next to continue.",
    "The region of interest has been defined. Click Finish to accept it, or Cancel to quit.",
};
constexpr int kSteps = 7;   // Init … Save, as OpenPnP counts them

} // namespace

JPlacerOcrRegionSetup::JPlacerOcrRegionSetup(JPlacerJob& job, JPlacerMachine& machine, JPlacerJobRun& run)
    : m_job(job), m_machine(machine), m_run(run) {}

JPlacerOcrRegionSetup::~JPlacerOcrRegionSetup() {
    *m_alive = false;
    onStateChanged = nullptr;
    onFeederChanged = nullptr;
    cancel();
}

std::string JPlacerOcrRegionSetup::proceedLabel() const { return m_step == Step::Complete ? "Finish" : "Next"; }

void JPlacerOcrRegionSetup::start(const std::string& feederId) {
    const JPFeeder* f = m_job.configuration().feeder(feederId);
    if (!f) return;
    m_feederId = feederId;
    // The camera over the vision location first.
    const JPLocation at = JPVisionTapeFeeder::nominalVisionLocation(*f);
    std::weak_ptr<bool> alive = m_alive;
    m_run.machineTask([this, at, alive](JPJobMachine& machine, const std::function<void(const std::function<void()>&)>& onMain,
                                        std::string& why) {
        if (!machine.positionCamera(at, why)) return false;
        onMain([this, alive] {
            if (const auto a = alive.lock(); a && *a) begin();
        });
        return true;
    });
}

void JPlacerOcrRegionSetup::begin() {
    m_view = m_machine.headCameraView();
    if (!m_view) return;
    m_startCamera = m_machine.toolLocation(JPSetupForm::Tool::Camera);
    m_corners.clear();
    m_rectify = false;
    // The corners and the region as it stands, drawn about the camera's centre.
    m_view->setOverlay(kOverlay, [this, alive = std::weak_ptr<bool>(m_alive)](JVectorCanvas& vg, const JPReticle::Place& place, float line) {
        const auto a = alive.lock();
        if (!a || !*a) return;
        const uint8_t* mark = Colors::Warning;
        const uint8_t* edge = Colors::Danger;
        const JPaint markPaint = JPaint::solid(rgb(mark[0], mark[1], mark[2]));
        const JPaint edgePaint = JPaint::solid(rgb(edge[0], edge[1], edge[2]));
        auto screen = [&](const JPLocation& l, float& x, float& y) { return place(l.x(), l.y(), x, y); };
        for (const auto& [step, l] : m_corners) {
            float x, y;
            if (screen(l, x, y)) vg.strokeCircle(x, y, kMarkLines * line, line, markPaint);
        }
        const auto ul = m_corners.find(Step::UpperLeft), ur = m_corners.find(Step::UpperRight), ll = m_corners.find(Step::LowerLeft);
        if (ul == m_corners.end() || ur == m_corners.end() || ll == m_corners.end()) return;
        const JPLocation u = ur->second.subtract(ul->second);
        JPLocation lower = ll->second;
        if (m_rectify) {
            // Rectified: square to the top edge, as high as the lower left says.
            const double d = std::hypot(u.x(), u.y());
            const JPLocation down = ll->second.subtract(ul->second);
            const double nx = u.y() / d, ny = -u.x() / d, h = nx * down.x() + ny * down.y();
            lower = ul->second.add(JPLocation(kMm, h * nx, h * ny, 0, 0));
        }
        const std::vector<JPLocation> corners { ul->second, ur->second, lower.add(u), lower };
        std::vector<JVectorCanvas::JVec2> pts;
        for (const JPLocation& c : corners) {
            float x, y;
            if (!screen(c, x, y)) return;
            pts.push_back({ x, y });
        }
        vg.strokePolyline(pts, line, edgePaint, true);
    });
    setStep(Step::CameraOffset);
}

void JPlacerOcrRegionSetup::setStep(Step s) {
    m_step = s;
    m_clicks = 0;
    if (JPCameraView* view = m_view) {
        if (s == Step::Idle) {
            view->setPrompt("");
            view->onPicked = nullptr;
        } else {
            const int index = int(s);
            char title[64];
            std::snprintf(title, sizeof title, "%s (%d / %d)", kTitle, index, kSteps - 1);
            view->setPrompt(std::string(title) + ": " + kInstructions[index - 1]);
            // Moving the camera, its view looks where clicked; else a click marks.
            if (s == Step::CameraOffset) view->onPicked = nullptr;
            else
                view->onPicked = [this, alive = std::weak_ptr<bool>(m_alive)](double px, double py) {
                    if (const auto a = alive.lock(); a && *a) picked(px, py);
                };
        }
    }
    if (onStateChanged) onStateChanged();
}

void JPlacerOcrRegionSetup::picked(double px, double py) {
    if (m_step == Step::SelectMode) {
        m_rectify = !m_rectify;
        ++m_clicks;
        return;
    }
    if (m_step != Step::UpperLeft && m_step != Step::UpperRight && m_step != Step::LowerLeft) return;
    // A first click marks, the next takes it back.
    if (m_clicks++ % 2 == 1) {
        m_corners.erase(m_step);
        return;
    }
    JPCameraFeed* feed = m_machine.headCameraFeed();
    const JPCell* cell = m_machine.cell();
    const std::optional<JPLocation> at = m_machine.toolLocation(JPSetupForm::Tool::Camera);
    if (!feed || !cell || !at) return;
    JPFrame frame;
    feed->latest(frame, 0);
    const JPCameraCalibration cal = cell->cameraCalibration(feed->config().id, frame.width, frame.height);
    double x = 0, y = 0;
    if (!cal.valid || !cal.machinePoint(px, py, at->x(), at->y(), x, y)) return;
    m_corners[m_step] = JPLocation(kMm, x - at->x(), y - at->y(), 0, 0);
}

void JPlacerOcrRegionSetup::next() {
    if (m_step == Step::Complete) {
        save();
        cancel();
        return;
    }
    // A corner to mark is needed before going on.
    if ((m_step == Step::UpperLeft || m_step == Step::UpperRight || m_step == Step::LowerLeft) && !m_corners.count(m_step)) return;
    setStep(Step(int(m_step) + 1));
}

void JPlacerOcrRegionSetup::save() {
    JPFeeder* f = m_job.configuration().feeder(m_feederId);
    if (!f || m_corners.size() < 3) return;
    JPPushPullTemplates::OcrRegion r;
    r.upperLeft = m_corners[Step::UpperLeft];
    r.upperRight = m_corners[Step::UpperRight];
    r.lowerLeft = m_corners[Step::LowerLeft];
    r.rectify = m_rectify;
    // Where the camera was moved to, when it was.
    const std::optional<JPLocation> end = m_machine.toolLocation(JPSetupForm::Tool::Camera);
    if (end && m_startCamera) {
        const JPLocation moved = end->subtract(*m_startCamera).derive(std::nullopt, std::nullopt, 0.0, 0.0);
        if (moved.linearDistanceTo(JPLocation::origin()) >= kLeastOffsetMm) r.offsets = moved;
    }
    JPPushPullTemplates::setOcrRegion(*f, r);
    if (onFeederChanged) onFeederChanged();
}

void JPlacerOcrRegionSetup::cancel() {
    if (JPCameraView* view = m_view) {
        view->setPrompt("");
        view->onPicked = nullptr;
        view->setOverlay(kOverlay, nullptr);
    }
    m_view = nullptr;
    if (m_step != Step::Idle) setStep(Step::Idle);
}

} // inline namespace jf
