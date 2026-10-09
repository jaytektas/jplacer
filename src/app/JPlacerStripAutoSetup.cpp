// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerStripAutoSetup.h"

#include "camera/JPCameraFeed.h"
#include "common/JPlacerLog.h"
#include "pipeline/JPStageUtil.h"
#include "pipeline/JPStraightPicture.h"
#include "tasks/JPFeederPipelines.h"
#include "tasks/JPStripHoleWalk.h"
#include "tasks/JPStripHoles.h"
#include "ui/JPCameraView.h"

#include <j/core/Dialog.h>
#include <j/core/FrameTimer.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

using OnMain = std::function<void(const std::function<void()>&)>;
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;

// How often the holes are shown while a click is awaited, and for how long each picture.
constexpr int kPreviewEveryMs = 400, kPreviewShownMs = 600;
// How long "Setup complete!" is said.
constexpr int kCompleteMs = 1500;
// Parts in tape are a multiple of 2 mm apart.
constexpr double kPartPitchStepMm = 2.0;
// EIA-481's sprocket hole pitch, and half it: where a part between two holes sits from them.
constexpr double kHolePitchMm = 4.0, kHalfHoleMm = 2.0;
// OpenPnP's showHoles colours (BGR): the lines orange, the best yellow, the holes on it blue, the two nearest green.
const cv::Scalar kLineColour(0, 200, 255), kBestColour(0, 255, 255), kHoleColour(255, 0, 0), kNearColour(0, 255, 0);
const cv::Scalar kHoleCentreColour(0, 255, 255), kNearCentreColour(255, 0, 255);

} // namespace

JPlacerStripAutoSetup::JPlacerStripAutoSetup(JPlacerJob& job, JPlacerMachine& machine, JPlacerJobRun& run, JPlacerPipelines& pipelines)
    : m_job(job), m_machine(machine), m_run(run), m_pipelines(pipelines) {}

JPlacerStripAutoSetup::~JPlacerStripAutoSetup() {
    *m_alive = false;
    stopPreview();
}

void JPlacerStripAutoSetup::setStep(Step s) {
    m_step = s;
    if (onStateChanged) onStateChanged();
}

void JPlacerStripAutoSetup::start(const std::string& feederId) {
    const JPFeeder* f = m_job.configuration().feeder(feederId);
    JPCameraFeed* feed = m_machine.headCameraFeed();
    const JPCell* cell = m_machine.cell();
    if (!f || !feed || !cell) {
        JDialog::message("Auto Setup Failure", "There is no camera on the head to set the feeder up with.");
        return;
    }
    // Scaled by height, the reference hole's Z must be known first (OpenPnP's 3D units per pixel).
    JPFrame frame;
    feed->latest(frame, 0);
    const JPCameraCalibration cal = cell->cameraCalibration(feed->config().id, frame.width, frame.height);
    if (!cal.valid) {
        JDialog::message("Auto Setup Failure", feed->config().name + " is not calibrated: calibrate it first.");
        return;
    }
    if (cal.twoHeights() && f->locationOf("reference-hole-location").z() == 0) {
        JDialog::message("Auto Setup Failure", "Please set the Reference Hole Location Z coordinate first.");
        return;
    }
    m_feederId = feederId;
    m_firstHoles.clear();
    m_view = m_machine.headCameraView();
    // The pipeline the holes are looked for with, given the camera (its live picture, unsettled, for the preview).
    if (auto held = JPFeederPipelines::of(*f)) {
        auto p = std::make_shared<JPPipeline>(std::move(*held));
        m_pipelines.useHeadCamera(*p, m_job.configuration().directory());
        JPFeederPipelines::configureForEditing(m_job.configuration(), *f, *p);
        // The live picture (straightened, as useHeadCamera gives it), unsettled.
        p->context().capture = [take = p->context().capture](const std::string&, const std::string& light, cv::Mat& bgr,
                                                             std::string& w) { return take("Skip", light, bgr, w); };
        m_previewPipeline = p;
    }
    setStep(Step::FirstPart);
    waitForClick("Click on the center of the first part in the tape.");
    startPreview();
}

void JPlacerStripAutoSetup::cancel() {
    stopPreview();
    if (JPCameraView* view = m_view) {
        view->setPrompt("");
        view->onPicked = nullptr;
    }
    m_view = nullptr;
    setStep(Step::Idle);
}

void JPlacerStripAutoSetup::fail(const std::string& why) {
    cancel();
    JDialog::message("Auto Setup Failure", why);
}

void JPlacerStripAutoSetup::waitForClick(const std::string& prompt) {
    JPCameraView* view = m_view;
    if (!view) return;
    view->setPrompt(prompt);
    std::weak_ptr<bool> alive = m_alive;
    view->onPicked = [this, alive](double px, double py) {
        if (const auto a = alive.lock(); a && *a) picked(px, py);
    };
}

void JPlacerStripAutoSetup::picked(double px, double py) {
    // The pixel clicked, on the machine.
    JPCameraFeed* feed = m_machine.headCameraFeed();
    const JPCell* cell = m_machine.cell();
    const JPlacerMachine::Where at = m_machine.whereIs(JPSetupForm::Tool::Camera);
    if (!feed || !cell || !at[0] || !at[1]) return;
    JPFrame frame;
    feed->latest(frame, 0);
    const JPCameraCalibration cal = cell->cameraCalibration(feed->config().id, frame.width, frame.height);
    double x = 0, y = 0;
    if (!cal.machinePoint(px, py, *at[0], *at[1], x, y)) return;
    if (JPCameraView* view = m_view) view->onPicked = nullptr;
    const bool first = m_step == Step::FirstPart;
    (first ? m_firstPart : m_secondPart) = JPLocation(kMm, x, y, 0, 0);
    setStep(first ? Step::CheckingFirst : Step::CheckingSecond);
    if (JPCameraView* view = m_view) view->setPrompt(first ? "Checking first part..." : "Checking second part...");
    check(JPLocation(kMm, x, y, 0, 0));
}

void JPlacerStripAutoSetup::check(const JPLocation& at) {
    const bool first = m_step == Step::CheckingFirst;
    std::shared_ptr<JPPipeline> pipeline;
    double tapeWidthMm = 8;
    if (const JPFeeder* f = m_job.configuration().feeder(m_feederId)) {
        tapeWidthMm = f->lengthOf("tape-width", JPLength(8, kMm)).convertToUnits(kMm).value();
        if (m_previewPipeline) pipeline = std::make_shared<JPPipeline>(*m_previewPipeline);
    }
    if (!pipeline) {
        fail("Feeder " + m_feederId + " has no pipeline.");
        return;
    }
    // Settled for the checks, as the pipeline says.
    pipeline->context().capture = nullptr;
    // Half the camera's view (its shorter side): how far down the strip each look goes past the last hole found.
    double stepMm = 0;
    if (JPCameraFeed* feed = m_machine.headCameraFeed(); feed && m_machine.cell()) {
        JPFrame frame;
        feed->latest(frame, 0);
        const JPCameraCalibration cal = m_machine.cell()->cameraCalibration(feed->config().id, frame.width, frame.height);
        if (cal.valid) stepMm = std::min(cal.width / std::abs(cal.scaleX()), cal.height / std::abs(cal.scaleY())) / 2;
    }
    std::weak_ptr<bool> alive = m_alive;
    const bool started = m_run.machineTask([this, alive, at, first, pipeline, tapeWidthMm, stepMm](JPJobMachine& machine,
                                                                                                 const OnMain& onMain, std::string&) {
        auto failed = [&](const std::string& w) {
            onMain([&] {
                if (const auto a = alive.lock(); a && *a) fail(w);
            });
            return true;
        };
        std::string why;
        JPJobMachine::SeenCircles seen;
        if (!machine.positionCamera(at, why) || !machine.seeCircles(at, *pipeline, seen, why)) return failed(why);
        const std::vector<JPLocation> holes = JPStripHoleWalk::holesOf(seen, tapeWidthMm);
        if (holes.empty()) return failed("No hole found at selected location");
        if (first) {
            onMain([&] {
                if (const auto a = alive.lock(); !a || !*a) return;
                m_firstHoles = holes;
                setStep(Step::SecondPart);
                waitForClick("Now click on the center of the second part in the tape.");
            });
            return true;
        }
        JPLocation ref1(kMm), ref2(kMm);
        std::vector<JPLocation> holes1;
        onMain([&] { holes1 = m_firstHoles; });
        if (!JPStripHoles::referenceHoles(m_firstPart, m_secondPart, holes1, holes, ref1, ref2, why)) return failed(why);
        // Parts are 2 or 4 mm apart in tape: the pitch clicked, rounded to it.
        const long steps =
            std::lround(std::hypot(m_secondPart.x() - m_firstPart.x(), m_secondPart.y() - m_firstPart.y()) / kPartPitchStepMm);
        if (steps == 0) return failed("The same part was selected both times");
        // The tape's angle, and how far apart its holes are (OpenPnP stretches the part pitch to it), come from
        // the line through the reference and next holes: from holes 4 mm apart, a few hundredths off in one look
        // go into every part. So the camera follows the holes down the strip to the one by its last part (its
        // Max Feed Count), and that is the last hole: the look's error shared among all the holes between.
        int parts = 0;
        onMain([&] {
            if (const JPFeeder* f = m_job.configuration().feeder(m_feederId)) parts = f->number("max-feed-count");
        });
        const double partPitchMm = kPartPitchStepMm * double(steps);
        const int holesOn = parts > 1 ? int(std::floor((parts - 1) * partPitchMm / kHolePitchMm)) : 0;
        const JPStripHoleWalk::Walked walked = JPStripHoleWalk::walk(
            machine, *pipeline, tapeWidthMm, m_firstPart, ref1, ref2, holesOn, stepMm, [&](int hole, int of) {
                onMain([&] {
                    if (const auto a = alive.lock(); !a || !*a) return;
                    if (JPCameraView* view = m_view)
                        view->setPrompt("Following the holes down the strip: hole " + std::to_string(hole) + " of " + std::to_string(of) + "...");
                });
            });
        ref2 = walked.last;
        {
            const double apart = std::hypot(ref2.x() - ref1.x(), ref2.y() - ref1.y());
            char said[200];
            std::snprintf(said, sizeof said, "%d hole%s apart, %.3f mm: %.4f mm a hole; the tape at %.3f°", walked.holes,
                          walked.holes == 1 ? "" : "s", apart, apart / std::max(1, walked.holes),
                          std::atan2(ref2.y() - ref1.y(), ref2.x() - ref1.x()) * 180 / M_PI);
            if (parts <= 1)
                JLOGC(JPlacerLog::kCamera, JLogLevel::Warn)
                    << "Auto Setup: no Max Feed Count, so the reference and last holes are " << said
                    << ". From holes this close, a few hundredths off in one look go into every part: set Max Feed "
                       "Count and run Auto Setup again to measure them down the strip";
            else if (!walked.reached)
                JLOGC(JPlacerLog::kCamera, JLogLevel::Warn)
                    << "Auto Setup: the holes were followed to hole " << walked.holes << " of the " << holesOn
                    << " the strip's last part sits by (" << parts << " parts), none found past it; the reference and last holes are "
                    << said;
            else
                JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "Auto Setup: the reference and last holes are " << said;
        }
        std::optional<JPLocation> pick;
        onMain([&] {
            JPFeeder* f = m_job.configuration().feeder(m_feederId);
            if (!f) return;
            // The holes found, their Z as they had them.
            const JPLocation was1 = f->locationOf("reference-hole-location").convertToUnits(kMm);
            const JPLocation was2 = f->locationOf("last-hole-location").convertToUnits(kMm);
            f->setLocationOf("reference-hole-location", JPLocation(kMm, ref1.x(), ref1.y(), was1.z(), 0));
            f->setLocationOf("last-hole-location", JPLocation(kMm, ref2.x(), ref2.y(), was2.z(), 0));
            f->setLengthOf("part-pitch", JPLength(kPartPitchStepMm * double(steps), kMm));
            // The click says where the first part is only to the hole: nearer a hole's middle, the part is on
            // it (0 along); nearer halfway between holes, it is a half hole pitch (EIA-481's 2 mm) from it.
            {
                const double len = std::hypot(m_secondPart.x() - m_firstPart.x(), m_secondPart.y() - m_firstPart.y());
                const double along = len > 0 ? ((ref1.x() - m_firstPart.x()) * (m_secondPart.x() - m_firstPart.x())
                                                + (ref1.y() - m_firstPart.y()) * (m_secondPart.y() - m_firstPart.y())) / len
                                             : 0;
                const bool onHole = std::abs(along) < kHalfHoleMm / 2;
                f->setLengthOf("reference-hole-to-part-linear", JPLength(onHole ? 0.0 : kHalfHoleMm, kMm));
            }
            // Where the first part is picked.
            f->setNumber("feed-count", 1);
            pick = f->pickLocation();
            f->setNumber("feed-count", 0);
            if (onFeederChanged) onFeederChanged();
        });
        if (pick && !machine.positionCamera(*pick, why)) return failed(why);
        onMain([&] {
            if (const auto a = alive.lock(); !a || !*a) return;
            if (JPCameraView* view = m_view) view->setPrompt("Setup complete!");
            JFrameTimer::singleShot(float(kCompleteMs), [this, alive] {
                if (const auto a = alive.lock(); a && *a) cancel();
            });
        });
        return true;
    });
    if (!started) cancel();
}

void JPlacerStripAutoSetup::startPreview() {
    stopPreview();
    if (!m_previewPipeline) return;
    JPCameraFeed* feed = m_machine.headCameraFeed();
    const JPCell* cell = m_machine.cell();
    if (!feed || !cell) return;
    JPFrame frame;
    feed->latest(frame, 0);
    // In the pipeline's pictures (straightened, JPPipelineCamera): where the camera looks, and their scale.
    JPCameraCalibration cal = cell->cameraCalibration(feed->config().id, frame.width, frame.height);
    if (const auto straight = JPStraightPicture::of(cal, feed->config().looksUp, feed->config().showAll)) cal = straight->calibration();
    double cx = frame.width / 2.0, cy = frame.height / 2.0;
    const JPlacerMachine::Where at = m_machine.whereIs(JPSetupForm::Tool::Camera);
    if (at[0] && at[1]) cal.pixelFor(*at[0], *at[1], *at[0], *at[1], cx, cy);
    const double px = (cal.scaleX() + cal.scaleY()) / 2;
    double tapeWidthMm = 8;
    if (const JPFeeder* f = m_job.configuration().feeder(m_feederId))
        tapeWidthMm = f->lengthOf("tape-width", JPLength(8, kMm)).convertToUnits(kMm).value();
    m_previewing = true;
    const std::shared_ptr<const JPPipeline> source = m_previewPipeline;
    std::weak_ptr<bool> alive = m_alive;
    m_preview = std::thread([this, alive, source, cx, cy, px, tapeWidthMm] {
        while (m_previewing) {
            JPPipeline p = *source;
            std::string why;
            if (p.process(why)) {
                std::vector<JPStripHoles::Circle> circles;
                if (const JPPipeline::Result* r = p.result("results"))
                    if (const auto* l = std::get_if<std::vector<JPPipelineModel::Circle>>(&r->model.value))
                        for (const auto& c : *l) circles.push_back({ c.x, c.y, c.diameter });
                const JPStripHoles::Result holes = JPStripHoles::find(circles, { cx, cy }, px, tapeWidthMm);
                // As OpenPnP's showHoles: every line, the best, its holes, the two nearest.
                cv::Mat shown;
                std::string ignored;
                cv::Mat rgba;
                if (JPStageUtil::toRgba(p.workingImage(), p.workingColorSpace(), true, rgba, ignored)) {
                    cv::cvtColor(rgba, shown, cv::COLOR_RGBA2BGR);
                    for (const JPRansac::Line& l : holes.lines) cv::line(shown, { int(l.a.x), int(l.a.y) }, { int(l.b.x), int(l.b.y) }, kLineColour, 1);
                    if (holes.hasBest)
                        cv::line(shown, { int(holes.best.a.x), int(holes.best.a.y) }, { int(holes.best.b.x), int(holes.best.b.y) }, kBestColour, 2);
                    for (size_t i = 0; i < holes.inLine.size(); ++i) {
                        const auto& c = holes.inLine[i];
                        const bool nearest = i < 2;
                        cv::circle(shown, cv::Point2d(c.x, c.y), int(c.diameter / 2), nearest ? kNearColour : kHoleColour, 2, cv::LINE_AA);
                        cv::circle(shown, cv::Point2d(c.x, c.y), 1, nearest ? kNearCentreColour : kHoleCentreColour, 2, cv::LINE_AA);
                    }
                    cv::cvtColor(shown, rgba, cv::COLOR_BGR2RGBA);
                    auto picture = std::make_shared<JPFrame>();
                    picture->width = rgba.cols;
                    picture->height = rgba.rows;
                    picture->rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
                    picture->straightened = true;   // the pipeline's picture
                    JMainThreadDispatcher::instance().post([this, alive, picture] {
                        if (const auto a = alive.lock(); !a || !*a || !m_previewing) return;
                        if (JPCameraView* view = m_view) view->showPicture(*picture, "", kPreviewShownMs);
                    });
                }
            } else {
                JLOGC(JPlacerLog::kPipeline, JLogLevel::Debug) << "auto setup preview: " << why;
            }
            for (int waited = 0; waited < kPreviewEveryMs && m_previewing; waited += kPreviewEveryMs / 4)
                std::this_thread::sleep_for(std::chrono::milliseconds(kPreviewEveryMs / 4));
        }
    });
}

void JPlacerStripAutoSetup::stopPreview() {
    m_previewing = false;
    if (m_preview.joinable()) m_preview.join();
}

} // inline namespace jf
