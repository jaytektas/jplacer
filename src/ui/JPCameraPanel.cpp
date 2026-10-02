// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraPanel.h"

#include "JPUiParts.h"

#include "camera/JPImageFile.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

inline namespace jf {

JPCameraPanel::JPCameraPanel(JSceneGraph& graph, JGpuHal& hal, const JPCellConfig& cell, std::string capturesDir,
                             const ViewFor& viewFor, CalibrationFor calibrationFor)
    : JContainer(graph), m_capturesDir(std::move(capturesDir)), m_calibrationFor(std::move(calibrationFor)) {
    JPUiParts::asPanel(*this);
    std::vector<std::string> names;
    for (const JPCameraConfig& c : cell.cameras) {
        m_feeds.push_back(std::make_unique<JPCameraFeed>(c));
        m_feeds.back()->setView(viewFor(c));
        names.push_back(c.name);
    }
    if (m_feeds.empty()) {
        add(std::make_unique<JLabel>(graph, "This cell has no cameras."));
        return;
    }
    auto top = JPUiParts::row(graph);
    m_choice = top->add(std::make_unique<JPChoiceRow>(graph, names, 0));
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

    std::weak_ptr<bool> alive = m_alive;
    for (size_t i = 0; i < m_feeds.size(); ++i) {
        JPCameraFeed* feed = m_feeds[i].get();
        m_unwatch.push_back(feed->onRunning.connect([this, alive, i, feed](bool running) {
            JMainThreadDispatcher::instance().post([this, alive, i, feed, running] {
                if (const auto a = alive.lock(); !a || !*a || i != m_shown) return;
                const auto mode = feed->mode();
                m_state->setText(running && mode ? mode->describe() : std::string());
            });
        }));
        m_unwatch.push_back(feed->onError.connect([this, alive, i](std::string why) {
            JMainThreadDispatcher::instance().post([this, alive, i, why] {
                if (const auto a = alive.lock(); !a || !*a || i != m_shown) return;
                m_view->setMessage(why);
                m_state->setText("");
            });
        }));
    }
    m_choice->onChosen.connect([this](int i) { show(size_t(i)); });
    show(0);
}

JPCameraPanel::~JPCameraPanel() {
    *m_alive = false;
    for (const auto& u : m_unwatch) u();
    if (m_view) m_view->setFeed(nullptr);
    for (auto& f : m_feeds) f->stop();
}

std::string JPCameraPanel::shownId() const {
    return m_shown < m_feeds.size() ? m_feeds[m_shown]->config().id : std::string();
}

JPCameraFeed* JPCameraPanel::shownFeed() const {
    return m_shown < m_feeds.size() ? m_feeds[m_shown].get() : nullptr;
}

void JPCameraPanel::setBusy(bool busy) {
    for (JButton* b : m_taskButtons) b->setEnabled(!busy);
    if (m_choice) m_choice->setChoicesEnabled(!busy);
}

void JPCameraPanel::setMarks(std::function<std::vector<JPViewMark>()> marks) {
    if (m_view) m_view->setMarks(std::move(marks));
}

void JPCameraPanel::setView(bool straight, double showAll) {
    m_straight = straight;
    m_showAll = showAll;
    if (m_viewChoice) m_viewChoice->choose(straight ? 1 : 0);
    if (m_edges) m_edges->setValue(float(showAll));
    refreshStraightening();
}

void JPCameraPanel::refreshStraightening() {
    if (!m_view || m_shown >= m_feeds.size()) return;
    const JPCameraConfig& cam = m_feeds[m_shown]->config();
    std::shared_ptr<const JPStraightener> s;
    if (m_straight && m_calibrationFor)
        if (auto made = JPStraightener::make(m_calibrationFor(cam.id), cam.looksUp, m_showAll))
            s = std::make_shared<const JPStraightener>(std::move(*made));
    m_view->setStraightener(s);
    if (m_edges) m_edges->setEnabled(m_straight);
    if (m_straight && !s) setNote(cam.name + " is not calibrated: shown as taken");
}

void JPCameraPanel::setNote(const std::string& text) {
    if (m_note) m_note->setText(text);
}

std::string JPCameraPanel::savePicture() {
    if (m_shown >= m_feeds.size()) return {};
    JPCameraFeed& feed = *m_feeds[m_shown];
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

bool JPCameraPanel::showCamera(const std::string& id) {
    for (size_t i = 0; i < m_feeds.size(); ++i)
        if (m_feeds[i]->config().id == id) {
            if (m_choice) m_choice->choose(int(i));   // shows it, through the choice's signal
            return true;
        }
    return false;
}

void JPCameraPanel::show(size_t index) {
    if (index >= m_feeds.size() || index == m_shown) return;
    const std::string before = m_shown < m_feeds.size() ? m_feeds[m_shown]->config().id : std::string();
    if (m_shown < m_feeds.size()) m_feeds[m_shown]->stop();
    m_shown = index;
    JPCameraFeed& feed = *m_feeds[index];
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Camera: " << feed.config().name;
    m_view->setFeed(&feed);
    m_view->setMessage("Starting " + feed.config().name + "\xE2\x80\xA6");
    m_state->setText("");
    feed.start();
    refreshStraightening();
    if (onShown) onShown(feed.config().id, before);
}

} // inline namespace jf
