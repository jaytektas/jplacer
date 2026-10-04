// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerOpenPnpTabs.h"

#include "JPlacerSettings.h"

#include "ui/JPFootprintOverlay.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/MenuSystem.h>

inline namespace jf {

namespace {
constexpr double kSplit = 0.5;   // a table's share before its divider is moved
// The cameras' overlay for a package's footprint (OpenPnP's reticle key).
constexpr const char* kFootprintOverlay = "PackageVisionWizard";
}

JPlacerOpenPnpTabs::JPlacerOpenPnpTabs(JAppWindow& window, JSceneGraph& graph, JPlacerJob& job, JPlacerMachine& machine)
    : m_window(window), m_job(job), m_machine(machine), m_layout(machine.layout()) {
    auto openMenu = [this](JMenu* menu, float x, float y) {
        if (JMenuManager::instance().onOpenMenu)
            JMenuManager::instance().onOpenMenu(menu, m_window.windowX() + int(x), m_window.windowY() + int(y), false, false);
    };

    m_parts = std::make_unique<JPPartsPanel>(graph, job.configuration(),
                                             JSettings::instance().get<double>(JPlacerSettings::kPartsSplit, kSplit));
    m_parts->openMenu = openMenu;
    m_parts->onChanged = [this] { m_job.configurationChanged(); };
    m_parts->onPickPart = [](const JPPart& part) {
        // As OpenPnP: the first feeder that holds the part; there are none yet.
        JDialog::message("Error", "No valid feeder found for " + part.id);
    };
    m_partsDock = std::make_unique<JDockWidget>("Parts", 0.f, 0.f, 0.f, 0.f);
    m_partsDock->setContent(m_parts.get());
    m_layout.add(m_partsDock.get(), JPlacerLayout::Home::Work);

    m_packages = std::make_unique<JPPackagesPanel>(graph, job.configuration(),
                                                   JSettings::instance().get<double>(JPlacerSettings::kPackagesSplit, kSplit));
    m_packages->openMenu = openMenu;
    m_packages->nozzleTips = [this] { return m_machine.nozzleTips(); };
    m_packages->onShowFootprint = [this](const JPFootprint* f) {
        m_machine.setCameraOverlay(kFootprintOverlay, f ? JPFootprintOverlay::of(*f) : nullptr);
    };
    m_packages->onChanged = [this] { m_job.configurationChanged(); };
    m_packagesDock = std::make_unique<JDockWidget>("Packages", 0.f, 0.f, 0.f, 0.f);
    m_packagesDock->setContent(m_packages.get());
    m_layout.add(m_packagesDock.get(), JPlacerLayout::Home::Work);

    m_watch = m_job.watch([this](JPlacerJob::Change) {
        m_parts->refresh();
        m_packages->refresh();
    });
}

JPlacerOpenPnpTabs::~JPlacerOpenPnpTabs() {
    JSettings::instance().set(JPlacerSettings::kPartsSplit, m_parts->split());
    JSettings::instance().set(JPlacerSettings::kPackagesSplit, m_packages->split());
    JPlacerSettings::save();
    m_job.unwatch(m_watch);
    m_machine.setCameraOverlay(kFootprintOverlay, nullptr);
    m_layout.remove(m_partsDock.get());
    m_partsDock->setContent(nullptr);
    m_layout.remove(m_packagesDock.get());
    m_packagesDock->setContent(nullptr);
}

bool JPlacerOpenPnpTabs::showDock(const std::string& title) {
    for (JDockWidget* d : { m_partsDock.get(), m_packagesDock.get() })
        if (title == d->title()) {
            m_layout.show(d);
            return true;
        }
    return false;
}

} // inline namespace jf
