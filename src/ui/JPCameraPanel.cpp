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

JPCameraPanel::JPCameraPanel(JSceneGraph& graph, JGpuHal& hal, const JPCellConfig& cell, std::string capturesDir)
    : JContainer(graph), m_capturesDir(std::move(capturesDir)) {
    JPUiParts::asPanel(*this);
    std::vector<std::string> names;
    for (const JPCameraConfig& c : cell.cameras) {
        m_feeds.push_back(std::make_unique<JPCameraFeed>(c));
        names.push_back(c.name);
    }
    if (m_feeds.empty()) {
        add(std::make_unique<JLabel>(graph, "This cell has no cameras."));
        return;
    }
    auto top = JPUiParts::row(graph);
    JPChoiceRow* choice = top->add(std::make_unique<JPChoiceRow>(graph, names, 0));
    m_state = top->add(std::make_unique<JLabel>(graph, ""));
    m_note  = top->add(std::make_unique<JLabel>(graph, ""));
    m_note->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    top->add(JPUiParts::button(graph, "Save Picture"))->onClicked.connect([this] { savePicture(); });
    add(std::move(top));
    m_view = add(std::make_unique<JPCameraView>(graph, hal));
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
    choice->onChosen.connect([this](int i) { show(size_t(i)); });
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
    if (onShown) onShown(feed.config().id, before);
}

} // inline namespace jf
