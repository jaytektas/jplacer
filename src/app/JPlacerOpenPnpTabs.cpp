// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerOpenPnpTabs.h"

#include "JPlacerChoiceDialog.h"
#include "JPlacerImportDialog.h"
#include "JPlacerSettings.h"

#include "ui/JPFootprintOverlay.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/MenuSystem.h>

#include <filesystem>

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

    auto currentJob = [this]() -> const JPJob* { return &m_job.job(); };
    m_boards = std::make_unique<JPBoardsPanel>(graph, job.configuration(), currentJob,
                                               JSettings::instance().get<double>(JPlacerSettings::kBoardsSplit, kSplit));
    m_boards->openMenu = openMenu;
    m_boards->onChanged = [this] { changed(); };
    m_boards->confirmSave = [this](JPPlacementsHolder& h, std::function<void()> then) { confirmSave(h, std::move(then)); };
    JPBoardPlacementsPanel& placements = m_boards->placements();
    placements.openImporter = [this](const JPBoardImporter& importer, std::function<void(JPBoard&)> imported) {
        m_window.openModal<JPlacerImportDialog>(importer, m_job.configuration(), std::move(imported));
    };
    placements.askChoice = [this](const std::string& title, const std::string& question, std::vector<std::string> options,
                                  int cancelIndex, std::function<void(int)> chosen) {
        m_window.openModal<JPlacerChoiceDialog>(title, question, std::move(options), cancelIndex, std::move(chosen));
    };
    m_boardsDock = std::make_unique<JDockWidget>("Boards", 0.f, 0.f, 0.f, 0.f);
    m_boardsDock->setContent(m_boards.get());
    m_layout.add(m_boardsDock.get(), JPlacerLayout::Home::Work);

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
        m_boards->refresh();
        m_parts->refresh();
        m_packages->refresh();
    });
}

JPlacerOpenPnpTabs::~JPlacerOpenPnpTabs() {
    JSettings::instance().set(JPlacerSettings::kPartsSplit, m_parts->split());
    JSettings::instance().set(JPlacerSettings::kPackagesSplit, m_packages->split());
    JSettings::instance().set(JPlacerSettings::kBoardsSplit, m_boards->split());
    JPlacerSettings::save();
    m_job.unwatch(m_watch);
    m_machine.setCameraOverlay(kFootprintOverlay, nullptr);
    m_layout.remove(m_partsDock.get());
    m_partsDock->setContent(nullptr);
    m_layout.remove(m_packagesDock.get());
    m_packagesDock->setContent(nullptr);
    m_layout.remove(m_boardsDock.get());
    m_boardsDock->setContent(nullptr);
}

void JPlacerOpenPnpTabs::changed() {
    m_job.configurationChanged();
}

const std::vector<std::unique_ptr<JPBoardImporter>>& JPlacerOpenPnpTabs::importers() const {
    return m_boards->placements().importers();
}

void JPlacerOpenPnpTabs::importBoard(const JPBoardImporter& importer) {
    m_boards->placements().importBoard(importer);
}

void JPlacerOpenPnpTabs::confirmSave(JPPlacementsHolder& holder, std::function<void()> then) {
    const std::string name = std::filesystem::path(holder.file).filename().string();
    const std::string file = holder.file;
    m_window.openModal<JPlacerChoiceDialog>(
        "Save " + name + "?", "Do you want to save your changes to " + name + "?\nIf you don't save, your changes will be lost.",
        std::vector<std::string> { "Yes", "No", "Cancel" }, 2, [this, file, then](int choice) {
            if (choice == 0) {
                std::string error;
                for (const auto& b : m_job.configuration().boards())
                    if (b->file == file && !m_job.configuration().saveBoard(*b, error))
                        JDialog::message("Save Error", error);
                for (const auto& p : m_job.configuration().panels())
                    if (p->file == file && !m_job.configuration().savePanel(*p, error))
                        JDialog::message("Save Error", error);
            }
            if (then) then();
        });
}

void JPlacerOpenPnpTabs::confirmSaveAll(std::vector<std::string> files, std::function<void()> then) {
    JPConfiguration& config = m_job.configuration();
    while (!files.empty()) {
        const std::string file = files.front();
        files.erase(files.begin());
        JPPlacementsHolder* holder = nullptr;
        for (const auto& b : config.boards())
            if (b->file == file && b->dirty) holder = b.get();
        for (const auto& p : config.panels())
            if (p->file == file && p->dirty) holder = p.get();
        if (holder) {
            confirmSave(*holder, [this, files, then] { confirmSaveAll(files, then); });
            return;
        }
    }
    if (then) then();
}

void JPlacerOpenPnpTabs::saveConfiguration(std::function<void()> then) {
    m_job.configurationChanged();
    std::vector<std::string> files;
    for (const auto& b : m_job.configuration().boards()) files.push_back(b->file);
    for (const auto& p : m_job.configuration().panels()) files.push_back(p->file);
    confirmSaveAll(files, std::move(then));
}

bool JPlacerOpenPnpTabs::mayClose() {
    if (m_closing) return true;
    bool dirty = false;
    for (const auto& b : m_job.configuration().boards()) dirty = dirty || b->dirty;
    for (const auto& p : m_job.configuration().panels()) dirty = dirty || p->dirty;
    if (!dirty) return true;
    saveConfiguration([this] {
        m_closing = true;
        m_window.requestClose();
    });
    return false;
}

bool JPlacerOpenPnpTabs::showDock(const std::string& title) {
    for (JDockWidget* d : { m_boardsDock.get(), m_partsDock.get(), m_packagesDock.get() })
        if (title == d->title()) {
            m_layout.show(d);
            return true;
        }
    return false;
}

} // inline namespace jf
