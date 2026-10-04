// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerMenuBuilder.h"

#include "JPKeyMap.h"
#include "JPlacerApp.h"
#include "JPlacerHelpPages.h"

#include <filesystem>

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

// An entry a key can be given (Preferences > Keys): `id` names it there,
// under `group`, starting with the key `byDefault` (none: Unknown). The menu
// shows the key it has, which does nothing while the entry is disabled.
JMenuItem* entry(JPKeyMap& keys, JMenu* menu, JSceneGraph& graph, const std::string& id, const std::string& group,
                 const std::string& label, JMenuShortcut byDefault, std::function<void()> run) {
    JMenuItem* item = menu->add(graph, label);
    item->onTriggered.connect(run);
    keys.add(id, group, label, byDefault, [item] { item->onTriggered.emit(); }, item);
    return item;
}

} // namespace

void JPlacerMenuBuilder::build(JAppWindow& window, JSceneGraph& graph, JPlacerApp& app) {
    JPKeyMap& keys = app.keys();
    using K = JKeyEvent::JKey;
    const JMenuShortcut none{};
    auto ctrl = [](uint32_t k, bool shift = false) { return JMenuShortcut{ static_cast<K>(k), true, false, shift }; };

    JMenu* file = newMenu(window, "File");
    entry(keys, file, graph, "file.newJob", "File", "New Job", ctrl('N'), [&app] { app.job().newJob(); });
    entry(keys, file, graph, "file.openJob", "File", "Open Job\xE2\x80\xA6", ctrl('O'), [&app] { app.job().open(); });
    // Open Recent Job: the jobs opened or saved last, by file name, made again as they change.
    menuStore().push_back(std::make_unique<JMenu>("Open Recent Job..."));
    JMenu* recent = menuStore().back().get();
    auto fillRecent = [&app, &graph, recent] {
        recent->clear();
        for (const std::string& path : app.job().recentJobs())
            recent->add(graph, std::filesystem::path(path).filename().string())->onTriggered.connect([&app, path] {
                app.job().openRecent(path);
            });
    };
    fillRecent();
    app.job().onRecentChanged = fillRecent;
    file->add(graph, "Open Recent Job...", {}, recent);
    file->addSeparator(graph);
    entry(keys, file, graph, "file.saveJob", "File", "Save Job", ctrl('S'), [&app] { app.job().save(); });
    entry(keys, file, graph, "file.saveJobAs", "File", "Save Job As\xE2\x80\xA6", ctrl('S', true), [&app] { app.job().saveAs(); });
    file->addSeparator(graph);
    entry(keys, file, graph, "file.saveConfiguration", "File", "Save Configuration", none,
          [&app] { app.tabs().saveConfiguration(); });
    file->addSeparator(graph);
    // Import Placements: OpenPnP's importers, into the Boards tab's chosen board.
    menuStore().push_back(std::make_unique<JMenu>("Import Placements"));
    JMenu* import = menuStore().back().get();
    for (const auto& importer : app.tabs().importers()) {
        const JPBoardImporter* i = importer.get();
        import->add(graph, i->name())->onTriggered.connect([&app, i] {
            app.tabs().showDock("Boards");
            app.tabs().importBoard(*i);
        });
    }
    file->add(graph, "Import Placements", {}, import);
    file->addSeparator(graph);
    entry(keys, file, graph, "file.quit", "File", "Quit", none, [&window] { window.requestClose(); });

    JMenu* edit = newMenu(window, "Edit");
    // Ctrl+Y redoes: Ctrl+Shift+Z is OpenPnP's Head Safe Z (Machine > Jog).
    JMenuItem* undo = entry(keys, edit, graph, "edit.undo", "Edit", "Undo", ctrl('Z'), [&app] { app.machine().undo(); });
    JMenuItem* redo = entry(keys, edit, graph, "edit.redo", "Edit", "Redo", ctrl('Y'), [&app] { app.machine().redo(); });
    app.machine().setEditItems(undo, redo);
    edit->addSeparator(graph);
    entry(keys, edit, graph, "edit.preferences", "Edit", "Preferences\xE2\x80\xA6", none, [&app] { app.openPreferences(); });

    // A tick for each panel: untick to close it, tick to bring it back where it lives.
    app.machine().layout().setViewMenu(newMenu(window, "View"), graph);

    JMenu* machine = newMenu(window, "Machine");
    entry(keys, machine, graph, "machine.import", "Machine", "Import OpenPnP Machine\xE2\x80\xA6", none,
          [&app] { app.machine().importOpenPnp(); });
    entry(keys, machine, graph, "machine.openCell", "Machine", "Open Cell\xE2\x80\xA6", none, [&app] { app.machine().chooseCell(); });
    machine->addSeparator(graph);
    JMenuItem* connect    = entry(keys, machine, graph, "machine.connect", "Machine", "Connect", none, [&app] { app.machine().connect(); });
    JMenuItem* disconnect = entry(keys, machine, graph, "machine.disconnect", "Machine", "Disconnect", none,
                                  [&app] { app.machine().disconnect(); });
    machine->addSeparator(graph);
    JMenuItem* home = entry(keys, machine, graph, "machine.home", "Machine", "Home All Axes", ctrl('H'), [&app] { app.machine().home(); });
    JMenuItem* park = entry(keys, machine, graph, "machine.park", "Machine", "Park Head", none, [&app] { app.machine().park(); });
    app.machine().setMenuItems(connect, disconnect, home, park);
    // Stopping: the move held and dropped (the position kept), or every
    // controller reset at once (home again after), which has no key unless
    // one is given it: a slip of the finger must not reset the controllers.
    entry(keys, machine, graph, "machine.stop", "Machine", "Stop", JMenuShortcut{ K::Escape, false, false, false },
          [&app] { app.machine().jogAction("stop"); });
    entry(keys, machine, graph, "machine.emergencyStop", "Machine", "Emergency Stop", none,
          [&app] { app.machine().jogAction("emergencyStop"); });
    machine->addSeparator(graph);
    // Jogging from the keyboard, with OpenPnP's keys (the Jog panel's buttons).
    menuStore().push_back(std::make_unique<JMenu>("Jog"));
    JMenu* jog = menuStore().back().get();
    machine->add(graph, "Jog", {}, jog);
    struct J { const char* label; JMenuShortcut key; const char* action; };
    const J jogs[] = {
        { "X+", ctrl(uint32_t(K::Right)), "x+" }, { "X-", ctrl(uint32_t(K::Left)), "x-" },
        { "Y+", ctrl(uint32_t(K::Up)), "y+" },    { "Y-", ctrl(uint32_t(K::Down)), "y-" },
        { "Z+", ctrl('\''), "z+" },              { "Z-", ctrl('/'), "z-" },
        { "Turn Anticlockwise", ctrl(','), "c+" }, { "Turn Clockwise", ctrl('.'), "c-" },
        { "Turn to 0", none, "parkC" },
        { "Larger Distance", ctrl('='), "distance+" }, { "Smaller Distance", ctrl('-'), "distance-" },
        { "Faster", none, "speed+" },              { "Slower", none, "speed-" },
        { "Park Head", ctrl('P', true), "parkXY" }, { "Up to Safe Z", ctrl('L', true), "parkZ" },
        { "Head Safe Z", ctrl('Z', true), "safeZ" }, { "Discard", ctrl('D', true), "discard" },
        { "Pick", none, "pick" },                  { "Place", none, "place" },
        { "Nozzle to the Camera", none, "positionNozzle" }, { "Camera to the Nozzle", none, "positionCamera" },
    };
    for (const J& j : jogs)
        entry(keys, jog, graph, std::string("jog.") + j.action, "Jog", j.label, j.key,
              [&app, action = std::string(j.action)] { app.machine().jogAction(action); });
    machine->addSeparator(graph);
    entry(keys, machine, graph, "machine.setup", "Machine", "Machine Setup\xE2\x80\xA6", none,
          [&app] { app.machine().showDock("Machine Setup"); });

    // Job: OpenPnP's, the Job tab's run buttons and Reset All Placed.
    JMenu* job = newMenu(window, "Job");
    JPJobPanel& jobPanel = app.tabs().jobPanel();
    JMenuItem* start = entry(keys, job, graph, "job.start", "Job", "Start", none, [&jobPanel] {
        if (jobPanel.onStartPauseResume) jobPanel.onStartPauseResume();
    });
    JMenuItem* step = entry(keys, job, graph, "job.step", "Job", "Step", none, [&jobPanel] {
        if (jobPanel.onStep) jobPanel.onStep();
    });
    JMenuItem* stop = entry(keys, job, graph, "job.stop", "Job", "Stop", none, [&jobPanel] {
        if (jobPanel.onStop) jobPanel.onStop();
    });
    job->addSeparator(graph);
    entry(keys, job, graph, "job.resetAllPlaced", "Job", "Reset All Placed", none, [&jobPanel] { jobPanel.resetAllPlaced(); });
    jobPanel.setMenuItems(start, step, stop);

    JMenu* help = newMenu(window, "Help");
    // The manual opens in the browser; whatever went wrong is said in the status bar.
    auto open = [&window](bool (*page)(std::string&), const char* opened) {
        std::string why;
        if (page(why)) window.showStatus(opened, kStatusMs);
        else           window.showStatus(why, kErrorMs);
    };
    entry(keys, help, graph, "help.manual", "Help", "User Manual", none, [open] {
        open(&JPlacerHelpPages::openManual, "User manual opened in your browser");
    });
    entry(keys, help, graph, "help.whatsNew", "Help", "What's New", none, [open] {
        open(&JPlacerHelpPages::openWhatsNew, "What's New opened in your browser");
    });
    help->addSeparator(graph);
    entry(keys, help, graph, "help.checkUpdates", "Help", "Check for Updates", none, [&app] { app.updater().check(true); });
    help->addSeparator(graph);
    entry(keys, help, graph, "help.about", "Help", "About jplacer", none, [&app] { app.showAbout(); });
}

} // inline namespace jf
