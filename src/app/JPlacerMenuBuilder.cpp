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

// An entry with a key: the menu shows the key, and the key does what the
// entry does, unless the entry is disabled (a menu's shortcut is only its
// label until it is registered with the menu manager).
JMenuItem* withKey(JMenu* menu, JSceneGraph& graph, const std::string& label, JMenuShortcut key, std::function<void()> run) {
    JMenuItem* item = menu->add(graph, label, key);
    item->onTriggered.connect(run);
    JMenuManager::instance().registerShortcut(key, [item] {
        if (item->isEnabled()) item->onTriggered.emit();
    });
    return item;
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
    using K = JKeyEvent::JKey;
    // Ctrl+Y redoes: Ctrl+Shift+Z is OpenPnP's Head Safe Z (Machine > Jog).
    JMenuItem* undo = withKey(edit, graph, "Undo", JMenuShortcut{ K::Z, true, false, false }, [&app] { app.machine().undo(); });
    JMenuItem* redo = withKey(edit, graph, "Redo", JMenuShortcut{ K::Y, true, false, false }, [&app] { app.machine().redo(); });
    app.machine().setEditItems(undo, redo);
    edit->addSeparator(graph);
    edit->add(graph, "Preferences\xE2\x80\xA6")->onTriggered.connect([&app] { app.openPreferences(); });

    // A tick for each panel: untick to close it, tick to bring it back where it lives.
    app.machine().layout().setViewMenu(newMenu(window, "View"), graph);

    JMenu* machine = newMenu(window, "Machine");
    machine->add(graph, "Import OpenPnP Machine\xE2\x80\xA6")->onTriggered.connect([&app] { app.machine().importOpenPnp(); });
    machine->add(graph, "Open Cell\xE2\x80\xA6")->onTriggered.connect([&app] { app.machine().chooseCell(); });
    machine->addSeparator(graph);
    JMenuItem* connect    = machine->add(graph, "Connect");
    JMenuItem* disconnect = machine->add(graph, "Disconnect");
    connect->onTriggered.connect([&app] { app.machine().connect(); });
    disconnect->onTriggered.connect([&app] { app.machine().disconnect(); });
    machine->addSeparator(graph);
    JMenuItem* home = withKey(machine, graph, "Home All Axes", JMenuShortcut{ JKeyEvent::JKey::H, true, false, false },
                              [&app] { app.machine().home(); });
    JMenuItem* park = machine->add(graph, "Park Head");
    park->onTriggered.connect([&app] { app.machine().park(); });
    app.machine().setMenuItems(connect, disconnect, home, park);
    // Stopping: the move held and dropped (the position kept), or every
    // controller reset at once (home again after).
    withKey(machine, graph, "Stop", JMenuShortcut{ K::Escape, false, false, false },
            [&app] { app.machine().jogAction("stop"); });
    withKey(machine, graph, "Emergency Stop", JMenuShortcut{ K::Escape, false, false, true },
            [&app] { app.machine().jogAction("emergencyStop"); });
    machine->addSeparator(graph);
    // Jogging from the keyboard, with OpenPnP's keys (the Jog panel's buttons).
    menuStore().push_back(std::make_unique<JMenu>("Jog"));
    JMenu* jog = menuStore().back().get();
    machine->add(graph, "Jog", {}, jog);
    auto key = [](uint32_t k, bool shift = false) { return JMenuShortcut{ static_cast<K>(k), true, false, shift }; };
    struct J { const char* label; JMenuShortcut key; const char* action; };
    const J jogs[] = {
        { "X+", key(uint32_t(K::Right)), "x+" }, { "X-", key(uint32_t(K::Left)), "x-" },
        { "Y+", key(uint32_t(K::Up)), "y+" },    { "Y-", key(uint32_t(K::Down)), "y-" },
        { "Z+", key('\''), "z+" },              { "Z-", key('/'), "z-" },
        { "Turn Anticlockwise", key(','), "c+" }, { "Turn Clockwise", key('.'), "c-" },
        { "Larger Distance", key('='), "distance+" }, { "Smaller Distance", key('-'), "distance-" },
        { "Park Head", key('P', true), "parkXY" }, { "Up to Safe Z", key('L', true), "parkZ" },
        { "Head Safe Z", key('Z', true), "safeZ" }, { "Discard", key('D', true), "discard" },
    };
    for (const J& j : jogs)
        withKey(jog, graph, j.label, j.key, [&app, action = std::string(j.action)] { app.machine().jogAction(action); });
    machine->addSeparator(graph);
    machine->add(graph, "Machine Setup\xE2\x80\xA6")->onTriggered.connect([&app] { app.machine().showDock("Machine Setup"); });

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
