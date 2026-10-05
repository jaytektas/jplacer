// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraPanel.h"

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
    m_asTaken->setChecked(true);
    m_asTaken->onToggled.connect([this](bool asTaken) {
        m_straight = !asTaken;
        refreshStraightening();
        if (onViewChanged) onViewChanged(m_straight);
    });
    m_save = std::make_unique<JPIconButton>(graph, "Save Picture", &JPIcons::save, "Save the picture");
    m_save->onClicked.connect([this] { savePicture(); });
    m_calibrate = std::make_unique<JPIconButton>(graph, "Calibrate", &JPIcons::target, "Calibrate the camera");
    m_calibrate->onClicked.connect([this] { if (onCalibrate) onCalibrate(); });
    m_visualTest = std::make_unique<JPIconButton>(graph, "Visual Test", &JPIcons::check, "Visual test of the calibration");
    m_visualTest->onClicked.connect([this] { if (onVisualTest) onVisualTest(); });
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
    m_instructionsHolder = add(std::make_unique<JContainer>(graph, 0.f, 0.f));
    m_instructionsHolder->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_instructionsHolder->setVSizePolicy(JSizePolicyMode::Fixed);
    m_instructionsHolder->setFixedSize(0.f, 0.f);
    m_instructionsHolder->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_instructions = std::make_unique<JPInstructions>(graph);
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
            if (const auto a = alive.lock(); a && *a) stopIfHidden();
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
    if (const float w = m_instructionsHolder->bounds().width; m_instructionsShown && w > 0 && w != m_instructionsWidth) {
        m_instructionsWidth = w;
        m_instructionsHolder->setFixedSize(0.f, m_instructions->heightFor(m_instructionsWidth));
        invalidate();
    }
    JContainer::populateRenderPrimitives(buf);
}

void JPCameraPanel::start() {
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Camera: " << m_feed.config().name << " on screen";
    m_view->setMessage("Starting " + m_feed.config().name + "\xE2\x80\xA6");
    m_state->setText("");
    m_feed.start();
    if (onRunning) onRunning(true);
}

void JPCameraPanel::stopIfHidden() {
    if (m_busy || !m_feeding.empty() || !m_feed.isRunning()) return;
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
    if (m_straight && mode)
        if (auto made = JPStraightener::make(cal, cam.looksUp, cam.showAll))
            s = std::make_shared<const JPStraightener>(std::move(*made));
    m_view->setStraightener(s);
    if (m_straight && mode && !s)
        setNote(cam.name + " is not calibrated for its " + std::to_string(mode->width) + "\xC3\x97"
                + std::to_string(mode->height) + " pictures: shown as taken");
}

void JPCameraPanel::setBusy(bool busy) {
    m_calibrate->setEnabled(!busy);
    m_visualTest->setEnabled(!busy);
    // A task needs pictures whether or not anyone is looking.
    m_busy = busy;
    if (busy && !m_feed.isRunning()) start();
}

void JPCameraPanel::keepRunning(int ms) {
    m_keepUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    if (!m_feed.isRunning()) start();
}

void JPCameraPanel::setFeeding(const std::string& cameraId, bool feeding) {
    if (feeding) m_feeding.insert(cameraId);
    else m_feeding.erase(cameraId);
    if (feeding && !m_feed.isRunning()) start();
}

std::vector<JWidget*> JPCameraPanel::tabTools() const {
    return { m_asTaken.get(), m_save.get(), m_calibrate.get(), m_visualTest.get(), m_settings.get() };
}

void JPCameraPanel::setNote(const std::string& text) {
    m_note->setText(text);
}

std::string JPCameraPanel::savePicture() {
    JPCameraFeed& feed = m_feed;
    JPFrame frame;
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
    if (!m_instructionsShown) m_instructionsHolder->add(m_instructions.get());
    m_instructionsShown = true;
    m_instructionsWidth = -1;   // sized to its text on the next frame
    m_instructionsHolder->setFixedSize(0.f, JPInstructions::height());
    invalidate();
}

void JPCameraPanel::hideInstructions() {
    m_instructionsHolder->clear();
    m_instructionsShown = false;
    m_instructionsHolder->setFixedSize(0.f, 0.f);
    invalidate();
}

} // inline namespace jf
