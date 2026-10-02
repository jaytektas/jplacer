// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraPanel.h"

#include "JPUiParts.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

inline namespace jf {

JPCameraPanel::JPCameraPanel(JSceneGraph& graph, JGpuHal& hal, const JPCellConfig& cell)
    : JContainer(graph) {
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
    m_state->setHSizePolicy(JSizePolicyMode::Expanding, 1);
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
