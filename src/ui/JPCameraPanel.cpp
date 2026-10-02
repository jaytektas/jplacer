// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraPanel.h"

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

    auto top = JPUiParts::row(graph);
    m_state = top->add(std::make_unique<JLabel>(graph, ""));
    m_state->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    top->add(JPUiParts::button(graph, "Save Picture"))->onClicked.connect([this] { savePicture(); });
    JButton* calibrate = top->add(JPUiParts::button(graph, "Calibrate"));
    calibrate->onClicked.connect([this] { if (onCalibrate) onCalibrate(); });
    JButton* test = top->add(JPUiParts::button(graph, "Visual Test"));
    test->onClicked.connect([this] { if (onVisualTest) onVisualTest(); });
    m_taskButtons = { calibrate, test };
    add(std::move(top));
    // As taken, or straightened: the lens's bending out and the machine square
    // to the picture, as much of the bent edge shown as the slider says.
    auto viewRow = JPUiParts::row(graph);
    m_viewChoice = viewRow->add(std::make_unique<JPChoiceRow>(graph, std::vector<std::string>{ "As Taken", "Straightened" }, 0));
    m_viewChoice->onChosen.connect([this](int i) {
        m_straight = i == 1;
        refreshStraightening();
        if (onViewChanged) onViewChanged(m_straight, m_showAll);
    });
    viewRow->add(std::make_unique<JLabel>(graph, "Edges: cropped"));
    m_edges = viewRow->add(std::make_unique<JSlider>(graph, 0.f));
    m_edges->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_edges->setValue(0);
    m_edges->onValueChanged.connect([this](float v) {
        m_showAll = v;
        refreshStraightening();
        if (onViewChanged) onViewChanged(m_straight, m_showAll);
    });
    viewRow->add(std::make_unique<JLabel>(graph, "whole"));
    add(std::move(viewRow));
    // What a camera task is doing, or the last thing done: a line of its own,
    // so a result reads in full.
    m_note = add(std::make_unique<JLabel>(graph, ""));
    m_view = add(std::make_unique<JPCameraView>(graph, hal));
    m_view->onPictureDoubleClicked = [this](double px, double py) { if (onLookAtPixel) onLookAtPixel(px, py); };
    m_view->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_view->setFeed(&m_feed);

    std::weak_ptr<bool> alive = m_alive;
    m_unwatch.push_back(m_feed.onRunning.connect([this, alive](bool running) {
        JMainThreadDispatcher::instance().post([this, alive, running] {
            if (const auto a = alive.lock(); !a || !*a) return;
            const auto mode = m_feed.mode();
            m_state->setText(running && mode ? mode->describe() : std::string());
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
    JContainer::populateRenderPrimitives(buf);
}

void JPCameraPanel::start() {
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Camera: " << m_feed.config().name << " on screen";
    m_view->setMessage("Starting " + m_feed.config().name + "\xE2\x80\xA6");
    m_state->setText("");
    m_feed.start();
    refreshStraightening();
    if (onRunning) onRunning(true);
}

void JPCameraPanel::stopIfHidden() {
    if (m_busy || !m_feed.isRunning() || std::chrono::steady_clock::now() - m_drawn < std::chrono::milliseconds(kHiddenMs)) return;
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Camera: " << m_feed.config().name << " off screen";
    m_feed.stop();
    if (onRunning) onRunning(false);
}

void JPCameraPanel::setView(bool straight, double showAll) {
    m_straight = straight;
    m_showAll = showAll;
    if (m_viewChoice) m_viewChoice->choose(straight ? 1 : 0);
    if (m_edges) m_edges->setValue(float(showAll));
    refreshStraightening();
}

void JPCameraPanel::refreshStraightening() {
    const JPCameraConfig& cam = m_feed.config();
    std::shared_ptr<const JPStraightener> s;
    if (m_straight && m_calibrationFor)
        if (auto made = JPStraightener::make(m_calibrationFor(cam.id), cam.looksUp, m_showAll))
            s = std::make_shared<const JPStraightener>(std::move(*made));
    m_view->setStraightener(s);
    m_edges->setEnabled(m_straight);
    if (m_straight && !s) setNote(cam.name + " is not calibrated: shown as taken");
}

void JPCameraPanel::setBusy(bool busy) {
    for (JButton* b : m_taskButtons) b->setEnabled(!busy);
    // A task needs pictures whether or not anyone is looking.
    m_busy = busy;
    if (busy && !m_feed.isRunning()) start();
}

void JPCameraPanel::setMarks(std::function<std::vector<JPViewMark>()> marks) {
    m_view->setMarks(std::move(marks));
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

} // inline namespace jf
