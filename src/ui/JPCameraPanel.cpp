// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraPanel.h"
#include <j/core/JStyle.h>
#include <j/core/JSpinBox.h>

#include "JPIcons.h"
#include "JPUiParts.h"

#include "camera/JPImageFile.h"
#include "camera/JPStraightener.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

inline namespace jf {

namespace {
// What Next says while nothing waits on it.
constexpr const char* kNextTip = "Next: a step of what is under way on this camera waits for you (its line says what to do)";
// How wide a step's number is, in control heights.
constexpr float kNumberWidths = 4;
} // namespace

JPCameraPanel::JPCameraPanel(JSceneGraph& graph, JGpuHal& hal, const JPCameraConfig& camera, std::string capturesDir,
                             std::function<bool(double&, double&)> view, CalibrationFor calibrationFor)
    : JContainer(graph), m_feed(camera), m_calibrationFor(std::move(calibrationFor)), m_capturesDir(std::move(capturesDir)) {
    JPUiParts::asPanel(*this);
    m_feed.setView(std::move(view));

    // The tools, for the tab (tabTools). As taken (the eye on), or
    // straightened: the lens's bending out and the machine square to the
    // picture.
    m_asTaken = std::make_unique<JPIconButton>(graph, "As Taken", &JPIcons::eye,
                                               "As taken: the picture as the camera gives it, not straightened");
    m_asTaken->setCheckable(true);
    m_asTaken->setChecked(!m_straight);
    m_asTaken->onToggled.connect([this](bool asTaken) {
        m_straight = !asTaken;
        refreshStraightening();
        if (onViewChanged) onViewChanged(m_straight);
    });
    m_save = std::make_unique<JPIconButton>(graph, "Save Picture", &JPIcons::save, "Save the picture");
    m_save->onClicked.connect([this] { savePicture(); });
    m_calibrate = std::make_unique<JPIconButton>(graph, "Calibrate", &JPIcons::target, "Calibrate the camera");
    m_calibrate->onClicked.connect([this] { if (onCalibrate) onCalibrate(); });
    // OpenPnP's red X (its Delete's): cancels the task under way on this camera, before its next move.
    m_cancelTask = std::make_unique<JPIconButton>(graph, "Cancel", "general-remove",
                                                  "Cancel what is under way on this camera (it stops before its next move)");
    m_cancelTask->setEnabled(false);
    m_cancelTask->onClicked.connect([this] {
        m_cancelTask->setEnabled(false);
        if (onCancelTask) onCancelTask();
    });
    // OpenPnP's green start: Next, when a step of a camera task waits on the person (askStep).
    m_next = std::make_unique<JPIconButton>(graph, "Next", "control-start", kNextTip);
    m_next->setEnabled(false);
    m_next->onClicked.connect([this] {
        if (auto f = m_onNext) f();
    });
    m_visualTest = std::make_unique<JPIconButton>(graph, "Visual Test", &JPIcons::check, "Visual test of the calibration");
    m_visualTest->onClicked.connect([this] { if (onVisualTest) onVisualTest(); });
    // Tuned now on what it sees, as Machine Setup's Defaults, then Auto-Tune: its properties kept.
    m_autoTune = std::make_unique<JPIconButton>(graph, "Auto-Tune", &JPIcons::autoTune,
                                                "Auto-Tune now, on what the camera sees, its light on: its properties "
                                                "set to the camera's defaults, tuned a moment, then kept");
    m_autoTune->onClicked.connect([this] { if (onAutoTune) onAutoTune(); });
    m_settings = std::make_unique<JPIconButton>(graph, "Camera Settings", &JPIcons::gear,
                                                "The camera's settings, in Machine Setup");
    m_settings->setLeads(JPIconButton::Leads::Elsewhere);   // to Machine Setup
    m_settings->onClicked.connect([this] { if (onSettings) onSettings(); });

    // The picture's format.
    m_state = add(std::make_unique<JLabel>(graph, ""));
    m_state->setMinWidthFollowsText(true);   // made empty, filled in once the camera runs
    // What a camera task is doing, or the last thing done: a line of its own,
    // so a result reads in full.
    m_note = add(std::make_unique<JLabel>(graph, ""));
    // Folded to the panel's width, as tall as its text (a step's line, a result in full).
    m_note->setWordWrap(true);
    m_note->setVSizePolicy(JSizePolicyMode::Fixed);
    m_note->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    // A number a step asks for (OpenPnP's Detection Diameter), on a row of its own under the note while asked.
    m_stepRow = JPUiParts::row(graph);
    m_stepNumber = m_stepRow->add(std::make_unique<JSpinBox>(graph, 0, 1, JStyle::current().controlHeight * kNumberWidths));
    m_stepNumber->onValueChanged.connect([this](int v) {
        if (auto f = m_onNumber) f(v);
    });
    m_stepNumberLabel = m_stepRow->add(std::make_unique<JLabel>(graph, ""));
    m_asked = add(std::make_unique<JContainer>(graph, 0.f, 0.f));
    m_asked->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_asked->setGap(JStyle::current().spacing);
    m_asked->setVSizePolicy(JSizePolicyMode::Fixed);
    m_asked->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_instructions = std::make_unique<JPInstructions>(graph);
    hideStepNumber();
    m_view = add(std::make_unique<JPCameraView>(graph, hal));
    m_view->onLookAt = [this](double px, double py) { if (onLookAtPixel) onLookAtPixel(px, py); };
    m_view->onReticleChanged = [this](const JPReticle& r) { if (onReticleChanged) onReticleChanged(r); };
    m_view->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_view->setFeed(&m_feed);

    std::weak_ptr<bool> alive = m_alive;
    m_unwatch.push_back(m_feed.onRunning.connect([this, alive](bool running) {
        JMainThreadDispatcher::instance().post([this, alive, running] {
            if (const auto a = alive.lock(); !a || !*a) return;
            const auto mode = m_feed.mode();
            m_state->setText(running && mode ? mode->describe() : std::string());
            if (running) refreshStraightening();   // by the calibration for its picture size
        });
    }));
    m_unwatch.push_back(m_feed.onError.connect([this, alive](std::string why) {
        JMainThreadDispatcher::instance().post([this, alive, why] {
            if (const auto a = alive.lock(); !a || !*a) return;
            m_view->setMessage(why);
            m_state->setText("");
            stopIfHidden();
        });
    }));
    // Each picture (or failed try at one), a check that the panel is still on
    // screen: a camera behind another tab, or closed, is stopped.
    m_unwatch.push_back(m_feed.onFrame.connect([this, alive](uint64_t) {
        JMainThreadDispatcher::instance().post([this, alive] {
            if (const auto a = alive.lock(); !a || !*a) return;
            stopIfHidden();
            // Save Picture asked of it while it was not running: the fresh picture, once it has come.
            JPFrame frame;
            if (m_saveFrom && m_feed.latest(frame, 0) && frame.sequence >= *m_saveFrom) {
                m_saveFrom.reset();
                savePicture();
            }
        });
    }));
}

JPCameraPanel::~JPCameraPanel() {
    *m_alive = false;
    for (const auto& u : m_unwatch) u();
    if (m_view) m_view->setFeed(nullptr);
    m_feed.stop();
}

void JPCameraPanel::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    // Drawn, so on screen: the camera runs.
    m_drawn = std::chrono::steady_clock::now();
    if (!m_feed.isRunning()) start();
    // The instructions as tall as their text folds to at this width.
    // The note as tall as its text folds to at this width.
    if (const float w = m_note->bounds().width; w > 0 && w != m_noteWidth) {
        m_noteWidth = w;
        m_note->setFixedSize(0.f, std::max(JStyle::current().labelHeight, m_note->heightFor(w)));
        invalidate();
    }
    if (const float w = m_asked->bounds().width; m_instructionsShown && w > 0 && w != m_instructionsWidth) {
        m_instructionsWidth = w;
        fitAsked();
    }
    JContainer::populateRenderPrimitives(buf);
}

void JPCameraPanel::start() {
    if (!m_powered) return;   // opened only while the machine is on
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Camera: " << m_feed.config().name << " on screen";
    m_view->setMessage("Starting " + m_feed.config().name + "\xE2\x80\xA6");
    m_state->setText("");
    m_feed.start();
    if (onRunning) onRunning(true);
}

void JPCameraPanel::stopIfHidden() {
    if (m_busy || !m_keptFor.empty() || !m_feed.isRunning()) return;
    if (std::chrono::steady_clock::now() < m_keepUntil) return;
    // Hidden: asked to draw a while ago and not drawn since (behind another
    // tab, or closed). Drawn since the last asking: asked again. However long
    // a camera takes to give its first picture, nothing is judged until then.
    const auto now = std::chrono::steady_clock::now();
    if (std::max(m_drawn, m_view->drawnAt()) >= m_askedToDraw) {
        m_askedToDraw = now;
        m_view->invalidate();
        return;
    }
    if (now - m_askedToDraw < std::chrono::milliseconds(kHiddenMs)) return;
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Camera: " << m_feed.config().name << " off screen";
    m_feed.stop();
    if (onRunning) onRunning(false);
}

void JPCameraPanel::setView(bool straight) {
    m_straight = straight;
    m_asTaken->setChecked(!straight);
    refreshStraightening();
}

void JPCameraPanel::setOverlay(const std::string& key, JPCameraView::Overlay overlay) {
    m_view->setOverlay(key, std::move(overlay));
}

void JPCameraPanel::setReticle(const JPReticle& reticle) {
    m_view->setReticle(reticle);
}

void JPCameraPanel::refreshStraightening() {
    const JPCameraConfig& cam = m_feed.config();
    const auto mode = m_feed.mode();
    const JPCameraCalibration cal = m_calibrationFor && mode ? m_calibrationFor(cam.id, mode->width, mode->height)
                                                             : JPCameraCalibration{};
    m_view->setCalibration(cal);
    std::shared_ptr<const JPStraightener> s;
    if (mode)
        if (auto made = JPStraightener::make(cal, cam.looksUp, cam.showAll))
            s = std::make_shared<const JPStraightener>(std::move(*made));
    m_view->setStraightener(s, m_straight);
    if (m_straight && mode && !s)
        setNote(cam.name + " is not calibrated for its " + std::to_string(mode->width) + "\xC3\x97"
                + std::to_string(mode->height) + " pictures: shown as taken");
}

void JPCameraPanel::setPowered(bool on) {
    if (on && m_powered) return;
    m_powered = on;
    if (on) {
        // Opened when next drawn (or for a task).
        m_view->setMessage("");
        invalidate();
        return;
    }
    if (m_feed.isRunning()) {
        JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Camera: " << m_feed.config().name << " closed: the machine is off";
        m_feed.stop();
        if (onRunning) onRunning(false);
    }
    m_view->clearPicture();
    m_view->setMessage(m_feed.config().name + ": the machine is off");
}

void JPCameraPanel::setBusy(bool busy) {
    m_calibrate->setEnabled(!busy);
    m_cancelTask->setEnabled(busy);
    m_visualTest->setEnabled(!busy);
    // A task needs pictures whether or not anyone is looking.
    m_busy = busy;
    // Its pictures are the task's: one calibrating this camera is not to be crossed out as uncalibrated (its
    // live pictures between the ones it shows would flash the cross on and off).
    m_view->setTaskUnderway(busy);
    if (busy && !m_feed.isRunning()) start();
}

void JPCameraPanel::keepRunning(int ms) {
    m_keepUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    if (!m_feed.isRunning()) start();
}

void JPCameraPanel::keepRunningFor(const std::string& who, bool kept) {
    if (kept) m_keptFor.insert(who);
    else m_keptFor.erase(who);
    if (kept && !m_feed.isRunning()) start();
}

std::vector<JWidget*> JPCameraPanel::tabTools() const {
    return { m_asTaken.get(), m_save.get(), m_calibrate.get(), m_next.get(), m_cancelTask.get(), m_visualTest.get(), m_autoTune.get(),
             m_settings.get() };
}

void JPCameraPanel::askStep(const std::string& line, const std::string& detail, std::function<void()> onNext) {
    setNote(line);
    m_onNext = std::move(onNext);
    m_next->setTooltip("Next: " + detail);
    m_next->setEnabled(true);
    m_cancelTask->setEnabled(true);
}

void JPCameraPanel::endStep() {
    m_onNext = nullptr;
    m_next->setTooltip(kNextTip);
    m_next->setEnabled(false);
    m_cancelTask->setEnabled(m_busy);
    hideStepNumber();
}

void JPCameraPanel::showStepNumber(const std::string& label, int value, int min, int max, std::function<void(int)> changed) {
    m_onNumber = nullptr;   // setting it up is not a change
    m_stepNumberLabel->setText(label);
    m_stepNumber->setRange(min, max);
    m_stepNumber->setValue(value);
    m_onNumber = std::move(changed);
    m_stepShown = true;
    fitAsked();
}

void JPCameraPanel::setStepNumberLabel(const std::string& label, const std::string& tooltip) {
    m_stepNumberLabel->setText(label);
    m_stepNumberLabel->setTooltip(tooltip);
}

void JPCameraPanel::hideStepNumber() {
    m_onNumber = nullptr;
    m_stepShown = false;
    fitAsked();
}

void JPCameraPanel::setNote(const std::string& text) {
    m_note->setText(text);
    m_noteWidth = -1;   // sized to it on the next frame
    invalidate();
}

std::string JPCameraPanel::savePicture() {
    JPCameraFeed& feed = m_feed;
    JPFrame frame;
    if (!feed.isRunning()) {
        // Its last picture is from when it stopped (dark, as likely as not): a fresh one instead.
        if (!m_powered) {
            setNote(feed.config().name + ": the machine is off");
            return {};
        }
        const uint64_t last = feed.latest(frame, 0) ? frame.sequence : 0;
        m_saveFrom = last + kSaveSkipFrames;
        keepRunning(kSaveRunMs);
        setNote("Saving a picture once " + feed.config().name + " has started\xE2\x80\xA6");
        return {};
    }
    if (!feed.latest(frame, 0)) {
        setNote("No picture yet to save.");
        return {};
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(m_capturesDir, ec);
    // <camera>-<date>-<time>.<ms>: in taking order, and never one over another.
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    char stamp[48];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&t));
    char name[256];
    std::snprintf(name, sizeof name, "%s-%s.%03d.png", feed.config().name.c_str(), stamp, ms);
    const std::string path = (fs::path(m_capturesDir) / name).string();
    std::string error;
    if (!JPImageFile::writePng(path, frame, error)) {
        JLOGC(JPlacerLog::kCamera, JLogLevel::Error) << error;
        setNote("Not saved: " + error);
        return {};
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "saved " << path;
    setNote(std::string("Saved ") + name);
    return path;
}

void JPCameraPanel::showInstructions(const std::string& title, const std::string& text, const std::string& proceedLabel,
                                     std::function<void()> onCancel, std::function<void()> onProceed) {
    m_instructions->set(title, text, proceedLabel, std::move(onCancel), std::move(onProceed));
    m_instructionsShown = true;
    m_instructionsWidth = -1;   // sized to its text on the next frame
    fitAsked();
}

void JPCameraPanel::hideInstructions() {
    m_instructionsShown = false;
    fitAsked();
}

void JPCameraPanel::fitAsked() {
    m_asked->clear();
    float h = 0;
    if (m_stepShown) {
        m_asked->add(m_stepRow.get());
        h += std::max(JStyle::current().buttonHeight, JStyle::current().controlHeight);   // a row's height (JPUiParts::row)
    }
    if (m_instructionsShown) {
        m_asked->add(m_instructions.get());
        if (m_stepShown) h += JStyle::current().spacing;
        h += m_instructionsWidth > 0 ? m_instructions->heightFor(m_instructionsWidth) : JPInstructions::height();
    }
    m_asked->setFixedSize(0.f, h);
    invalidate();
}

} // inline namespace jf
