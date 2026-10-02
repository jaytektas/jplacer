// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerMenuBuilder.h"

#include "JPlacerApp.h"
#include "JPlacerHelpPages.h"

#include <j/core/MenuSystem.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

namespace {

// How long the Help menu's status messages stay: a confirmation briefly, a
// failure long enough to read the reason.
constexpr int kStatusMs = 3000;
constexpr int kErrorMs  = 8000;

// JAppWindow's menu bar stores raw JMenu pointers, so the menus must outlive the
// builder call. They live here for the process lifetime, which is exactly as
// long as the window that references them.
std::vector<std::unique_ptr<JMenu>>& menuStore() {
    static std::vector<std::unique_ptr<JMenu>> store;
    return store;
}

JMenu* newMenu(JAppWindow& window, const std::string& title) {
    menuStore().push_back(std::make_unique<JMenu>(title));
    JMenu* m = menuStore().back().get();
    window.menuBar().addMenu(m);
    return m;
}

// Entries for features that are not built yet: shown, so the shape of the
// application is visible, and disabled (see JPlacerMenuBuilder.h).
void addPending(JMenu* menu, JSceneGraph& graph, std::initializer_list<const char*> labels) {
    for (const char* label : labels) menu->add(graph, label)->setEnabled(false);
}

} // namespace

void JPlacerMenuBuilder::build(JAppWindow& window, JSceneGraph& graph, JPlacerApp& app) {
    JMenu* file = newMenu(window, "File");
    addPending(file, graph, { "New Job", "Open Job\xE2\x80\xA6", "Save Job", "Save Job As\xE2\x80\xA6" });
    file->addSeparator(graph);
    file->add(graph, "Quit")->onTriggered.connect([&window] { window.requestClose(); });

    JMenu* edit = newMenu(window, "Edit");
    edit->add(graph, "Preferences\xE2\x80\xA6")->onTriggered.connect([&app] { app.openPreferences(); });

    JMenu* machine = newMenu(window, "Machine");
    machine->add(graph, "Import OpenPnP Machine\xE2\x80\xA6")->onTriggered.connect([&app] { app.machine().importOpenPnp(); });
    machine->add(graph, "Open Cell\xE2\x80\xA6")->onTriggered.connect([&app] { app.machine().chooseCell(); });
    machine->addSeparator(graph);
    JMenuItem* connect    = machine->add(graph, "Connect");
    JMenuItem* disconnect = machine->add(graph, "Disconnect");
    connect->onTriggered.connect([&app] { app.machine().connect(); });
    disconnect->onTriggered.connect([&app] { app.machine().disconnect(); });
    app.machine().setMenuItems(connect, disconnect);
    machine->addSeparator(graph);
    addPending(machine, graph, { "Home All Axes", "Park Head" });
    machine->addSeparator(graph);
    addPending(machine, graph, { "Machine Setup\xE2\x80\xA6" });

    JMenu* job = newMenu(window, "Job");
    addPending(job, graph, { "Start", "Pause", "Stop" });
    job->addSeparator(graph);
    addPending(job, graph, { "Board Setup\xE2\x80\xA6", "Feeders\xE2\x80\xA6", "Parts and Packages\xE2\x80\xA6" });

    JMenu* help = newMenu(window, "Help");
    // The manual opens in the browser; whatever went wrong is said in the status bar.
    auto open = [&window](bool (*page)(std::string&), const char* opened) {
        std::string why;
        if (page(why)) window.showStatus(opened, kStatusMs);
        else           window.showStatus(why, kErrorMs);
    };
    help->add(graph, "User Manual")->onTriggered.connect([open] {
        open(&JPlacerHelpPages::openManual, "User manual opened in your browser");
    });
    help->add(graph, "What's New")->onTriggered.connect([open] {
        open(&JPlacerHelpPages::openWhatsNew, "What's New opened in your browser");
    });
    help->addSeparator(graph);
    help->add(graph, "Check for Updates")->onTriggered.connect([&app] { app.updater().check(true); });
    help->addSeparator(graph);
    help->add(graph, "About jplacer")->onTriggered.connect([&app] { app.showAbout(); });
}

} // inline namespace jf
