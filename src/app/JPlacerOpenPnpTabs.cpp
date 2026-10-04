// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerOpenPnpTabs.h"

#include "JPlacerSettings.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/MenuSystem.h>

inline namespace jf {

namespace {
constexpr double kPartsSplit = 0.5;   // the table's share before the divider is moved
}

JPlacerOpenPnpTabs::JPlacerOpenPnpTabs(JAppWindow& window, JSceneGraph& graph, JPlacerJob& job, JPlacerLayout& layout)
    : m_window(window), m_job(job), m_layout(layout) {
    auto openMenu = [this](JMenu* menu, float x, float y) {
        if (JMenuManager::instance().onOpenMenu)
            JMenuManager::instance().onOpenMenu(menu, m_window.windowX() + int(x), m_window.windowY() + int(y), false, false);
    };

    m_parts = std::make_unique<JPPartsPanel>(graph, job.configuration(),
                                             JSettings::instance().get<double>(JPlacerSettings::kPartsSplit, kPartsSplit));
    m_parts->openMenu = openMenu;
    m_parts->onChanged = [this] { m_job.configurationChanged(); };
    m_parts->onPickPart = [](const JPPart& part) {
        // As OpenPnP: the first feeder that holds the part; there are none yet.
        JDialog::message("Error", "No valid feeder found for " + part.id);
    };
    m_partsDock = std::make_unique<JDockWidget>("Parts", 0.f, 0.f, 0.f, 0.f);
    m_partsDock->setContent(m_parts.get());
    m_layout.add(m_partsDock.get(), JPlacerLayout::Home::Work);

    m_watch = m_job.watch([this](JPlacerJob::Change) { m_parts->refresh(); });
}

JPlacerOpenPnpTabs::~JPlacerOpenPnpTabs() {
    JSettings::instance().set(JPlacerSettings::kPartsSplit, m_parts->split());
    JPlacerSettings::save();
    m_job.unwatch(m_watch);
    m_layout.remove(m_partsDock.get());
    m_partsDock->setContent(nullptr);
}

bool JPlacerOpenPnpTabs::showDock(const std::string& title) {
    if (title == m_partsDock->title()) {
        m_layout.show(m_partsDock.get());
        return true;
    }
    return false;
}

} // inline namespace jf
