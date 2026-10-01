// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerMenuBuilder.h"

#include "JPlacerApp.h"

#include <j/core/MenuSystem.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

namespace {

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
    addPending(machine, graph, { "Connect", "Disconnect" });
    machine->addSeparator(graph);
    addPending(machine, graph, { "Home All Axes", "Park Head" });
    machine->addSeparator(graph);
    addPending(machine, graph, { "Machine Setup\xE2\x80\xA6" });

    JMenu* job = newMenu(window, "Job");
    addPending(job, graph, { "Start", "Pause", "Stop" });
    job->addSeparator(graph);
    addPending(job, graph, { "Board Setup\xE2\x80\xA6", "Feeders\xE2\x80\xA6", "Parts and Packages\xE2\x80\xA6" });

    JMenu* help = newMenu(window, "Help");
    help->add(graph, "Check for Updates")->onTriggered.connect([&app] { app.updater().check(true); });
    help->addSeparator(graph);
    help->add(graph, "About jplacer")->onTriggered.connect([&app] { app.showAbout(); });
}

} // inline namespace jf
