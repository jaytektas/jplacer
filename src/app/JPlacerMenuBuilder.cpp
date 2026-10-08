// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerMenuBuilder.h"

#include "JPKeyMap.h"
#include "JPlacerApp.h"
#include "JPlacerHelpPages.h"
#include "JPlacerScriptsMenu.h"
#include "JPlacerSettings.h"
#include "common/JPTranslations.h"

#include <filesystem>

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
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
    // Open Recent Job: the jobs opened or saved last, by file name, made again as they change; greyed while
    // there are none (not an empty submenu).
    menuStore().push_back(std::make_unique<JMenu>("Open Recent Job..."));
    JMenu* recent = menuStore().back().get();
    JMenuItem* recentEntry = file->add(graph, "Open Recent Job...", {}, recent);
    auto fillRecent = [&app, &graph, recent, recentEntry] {
        recent->clear();
        const std::vector<std::string> jobs = app.job().recentJobs();
        for (const std::string& path : jobs)
            recent->add(graph, std::filesystem::path(path).filename().string())->onTriggered.connect([&app, path] {
                app.job().openRecent(path);
            });
        recentEntry->setEnabled(!jobs.empty());
    };
    fillRecent();
    app.job().onRecentChanged = fillRecent;
    file->addSeparator(graph);
    entry(keys, file, graph, "file.saveJob", "File", "Save Job", ctrl('S'), [&app] { app.job().save(); });
    entry(keys, file, graph, "file.saveJobAs", "File", "Save Job As\xE2\x80\xA6", none, [&app] { app.job().saveAs(); });
    file->addSeparator(graph);
    entry(keys, file, graph, "file.saveConfiguration", "File", "Save Configuration", none,
          [&app] { app.tabs().saveConfiguration(); });
    file->addSeparator(graph);
    // Import Placements: OpenPnP's importers, into the Boards tab's chosen board.
    menuStore().push_back(std::make_unique<JMenu>("Import Placements"));
    JMenu* import = menuStore().back().get();
    import->add(graph, "CPL and BOM…")->onTriggered.connect([&app] {
        app.tabs().showDock("Boards");
        app.tabs().importCplBom();
    });
    import->addSeparator(graph);
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
    // OpenPnP's: the Job tab's Add Board/Panel, Remove and Capture Tool Location.
    JPJobPanel& jobs = app.tabs().jobPanel();
    menuStore().push_back(std::make_unique<JMenu>("Add Board/Panel"));
    JMenu* addBoard = menuStore().back().get();
    entry(keys, addBoard, graph, "edit.newBoard", "Edit", "New Board\xE2\x80\xA6", none, [&jobs] { jobs.addNew(false); })
        ->setTooltip("Create a new board and add it to the job.");
    entry(keys, addBoard, graph, "edit.existingBoard", "Edit", "Existing Board\xE2\x80\xA6", none, [&jobs] { jobs.addExisting(false); })
        ->setTooltip("Add an existing board to the job.");
    addBoard->addSeparator(graph);
    entry(keys, addBoard, graph, "edit.newPanel", "Edit", "New Panel\xE2\x80\xA6", none, [&jobs] { jobs.addNew(true); })
        ->setTooltip("Create a new panel and add it to the job.");
    entry(keys, addBoard, graph, "edit.existingPanel", "Edit", "Existing Panel\xE2\x80\xA6", none, [&jobs] { jobs.addExisting(true); })
        ->setTooltip("Add an existing panel to the job.");
    edit->add(graph, "Add Board/Panel", {}, addBoard);
    JMenuItem* removeBoard =
        entry(keys, edit, graph, "edit.removeBoard", "Edit", "Remove Board(s)/Panel(s)", none, [&jobs] { jobs.removeSelected(); });
    edit->addSeparator(graph);
    JMenuItem* captureTool = entry(keys, edit, graph, "edit.captureTool", "Edit", "Capture Tool Location", none, [&jobs] { jobs.captureTool(); });
    removeBoard->setTooltip("Remove the selected board(s) and/or panel(s) from the job.");
    captureTool->setTooltip("Set the board's Z to the tool's current Z.");
    jobs.setEditItems(removeBoard, captureTool);
    edit->addSeparator(graph);
    entry(keys, edit, graph, "edit.preferences", "Edit", "Preferences\xE2\x80\xA6", none, [&app] { app.openPreferences(); });

    // OpenPnP's System Units, Selections in Tables and Language; under them a
    // tick for each panel: untick to close it, tick to bring it back where it lives.
    auto subMenu = [](const std::string& title) {
        menuStore().push_back(std::make_unique<JMenu>(title));
        return menuStore().back().get();
    };
    // A tick to show which is chosen (one not built yet greyed out).
    auto tick = [&graph](JMenu* m, const std::string& label, bool chosen, bool enabled) {
        JMenuItem* i = m->add(graph, label);
        i->setCheckable(true);
        i->setChecked(chosen);
        i->setEnabled(enabled);
        return i;
    };
    // As OpenPnP's: kept, and taken at the next start (said so).
    JMenu* units = subMenu("System Units");
    const bool inches = JSettings::instance().get<std::string>(JPlacerSettings::kSystemUnits, "Millimeters") == "Inches";
    JMenuItem* inchesItem = tick(units, "Inches", inches, true);
    JMenuItem* mmItem = tick(units, "Millimeters", !inches, true);
    auto setUnits = [inchesItem, mmItem](bool toInches) {
        inchesItem->setChecked(toInches);
        mmItem->setChecked(!toInches);
        JSettings::instance().set(JPlacerSettings::kSystemUnits, std::string(toInches ? "Inches" : "Millimeters"));
        JPlacerSettings::save();
        JDialog::message("Notice", "Please restart jplacer for the changes to take effect.");
    };
    inchesItem->onTriggered.connect([setUnits] { setUnits(true); });
    mmItem->onTriggered.connect([setUnits] { setUnits(false); });
    JMenu* tables = subMenu("Selections in Tables");
    const bool linked = JSettings::instance().get<bool>(JPlacerSettings::kTablesLinked, false);
    JMenuItem* unlinkedItem = tick(tables, "Unlinked", !linked, true);
    JMenuItem* linkedItem   = tick(tables, "Linked", linked, true);
    auto setLinked = [unlinkedItem, linkedItem](bool on) {
        JSettings::instance().set(JPlacerSettings::kTablesLinked, on);
        unlinkedItem->setChecked(!on);
        linkedItem->setChecked(on);
    };
    unlinkedItem->onTriggered.connect([setLinked] { setLinked(false); });
    linkedItem->onTriggered.connect([setLinked] { setLinked(true); });
    // Set elsewhere too (Issues & Solutions): the ticks follow.
    JSettings::instance().onChange.connect([unlinkedItem, linkedItem](std::string key, JVariant value) {
        if (key != JPlacerSettings::kTablesLinked) return;
        unlinkedItem->setChecked(!value.toBool());
        linkedItem->setChecked(value.toBool());
    });
    // As OpenPnP's: kept, and taken at the next start (said so).
    JMenu* language = subMenu("Language");
    {
        const std::string chosen = JSettings::instance().get<std::string>(JPlacerSettings::kLanguage, "en");
        auto items = std::make_shared<std::vector<std::pair<JMenuItem*, std::string>>>();
        for (const JPTranslations::Language& l : JPTranslations::languages())
            items->emplace_back(tick(language, l.name, chosen == l.code, true), l.code);
        for (const auto& [item, code] : *items)
            item->onTriggered.connect([items, code = code] {
                for (const auto& [other, otherCode] : *items) other->setChecked(otherCode == code);
                JSettings::instance().set(JPlacerSettings::kLanguage, code);
                JPlacerSettings::save();
                JDialog::message("Notice", "Please restart jplacer for the changes to take effect.");
            });
    }
    app.machine().layout().setViewMenu(newMenu(window, "View"), graph, [&graph, units, tables, language](JMenu& view) {
        view.add(graph, "System Units", {}, units);
        view.add(graph, "Selections in Tables", {}, tables);
        view.add(graph, "Language", {}, language);
    });

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
        { "Park C", none, "parkC" },
        { "Raise Jog Increment", ctrl('='), "distance+" }, { "Lower Jog Increment", ctrl('-'), "distance-" },
        { "First Jog Increment", ctrl(uint32_t(K::F1), true), "increment:1" },
        { "Second Jog Increment", ctrl(uint32_t(K::F2), true), "increment:2" },
        { "Third Jog Increment", ctrl(uint32_t(K::F3), true), "increment:3" },
        { "Fourth Jog Increment", ctrl(uint32_t(K::F4), true), "increment:4" },
        { "Fifth Jog Increment", ctrl(uint32_t(K::F5), true), "increment:5" },
        { "Faster", none, "speed+" },              { "Slower", none, "speed-" },
        { "Park XY", ctrl('P', true), "parkXY" }, { "Park Z", ctrl('L', true), "parkZ" },
        { "Head Safe Z", ctrl('Z', true), "safeZ" }, { "Discard", ctrl('D', true), "discard" },
        { "Pick", none, "pick" },                  { "Place", none, "place" },
        { "Move last selected tool to camera position", none, "positionNozzle" },
        { "Move camera to position of selected tool", none, "positionCamera" },
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
    JMenuItem* start = entry(keys, job, graph, "job.start", "Job", "Start", ctrl('R', true), [&jobPanel] {
        if (jobPanel.onStartPauseResume) jobPanel.onStartPauseResume();
    });
    JMenuItem* step = entry(keys, job, graph, "job.step", "Job", "Step", ctrl('S', true), [&jobPanel] {
        if (jobPanel.onStep) jobPanel.onStep();
    });
    JMenuItem* stop = entry(keys, job, graph, "job.stop", "Job", "Stop", ctrl('A', true), [&jobPanel] {
        if (jobPanel.onStop) jobPanel.onStop();
    });
    job->addSeparator(graph);
    entry(keys, job, graph, "job.resetAllPlaced", "Job", "Reset All Placed", none, [&jobPanel] { jobPanel.resetAllPlaced(); })
        ->setTooltip("Reset the Placed status for every placement in the job.");
    entry(keys, job, graph, "job.shortages", "Job", "Shortages\xE2\x80\xA6", none, [&app] { app.tabs().openShortages(); })
        ->setTooltip("The job's parts against the stock: left to place, attrition, in stock, short, where kept.");
    // OpenPnP's descriptions, as its menu entries' tooltips.
    step->setTooltip("Process one step of the job and pause.");
    stop->setTooltip("Stop processing the job.");
    jobPanel.setMenuItems(start, step, stop);

    // OpenPnP's Scripts menu: the scripts folder's scripts (JPlacerScriptsMenu), for as long as the window.
    static std::unique_ptr<JPlacerScriptsMenu> scriptsMenu;
    scriptsMenu = std::make_unique<JPlacerScriptsMenu>(window, graph, app.machine().sharedScripting(), newMenu(window, "Scripts"));
    // OpenPnP's Window menu: the cameras and the machine controls each in a window of their own (taken at
    // the next start, said so), and the appearance.
    JMenu* windows = newMenu(window, "Window");
    JMenuItem* multiple = windows->add(graph, "Multiple Window Style");
    multiple->setCheckable(true);
    multiple->setChecked(JSettings::instance().get<bool>(JPlacerSettings::kMultipleWindows, false));
    // The tick has already flipped when this runs.
    multiple->onTriggered.connect([multiple] {
        JSettings::instance().set(JPlacerSettings::kMultipleWindows, multiple->isChecked());
        JPlacerSettings::save();
        JDialog::message("Windows Style Changed", "Windows style changed. Please restart jplacer for the changes to take effect.");
    });
    windows->add(graph, "Change Appearance\xE2\x80\xA6")->onTriggered.connect([&app] { app.openAppearance(); });

    JMenu* help = newMenu(window, "Help");
    // The manual opens in the browser; whatever went wrong is said in the status bar.
    auto open = [&window](bool (*page)(std::string&), const char* opened) {
        std::string why;
        if (page(why)) window.showStatus(opened, kStatusMs);
        else           window.showStatus(why, kErrorMs);
    };
    // In OpenPnP's order: About, the guides, the manual; the change log; updates.
    entry(keys, help, graph, "help.about", "Help", "About jplacer", none, [&app] { app.showAbout(); });
    entry(keys, help, graph, "help.quickStart", "Help", "Quick Start", none, [open] {
        open(&JPlacerHelpPages::openQuickStart, "Quick Start opened in your browser");
    });
    entry(keys, help, graph, "help.setupAndCalibration", "Help", "Setup and Calibration", none, [open] {
        open(&JPlacerHelpPages::openSetupAndCalibration, "Setup and Calibration opened in your browser");
    });
    entry(keys, help, graph, "help.manual", "Help", "User Manual", none, [open] {
        open(&JPlacerHelpPages::openManual, "User manual opened in your browser");
    });
    help->addSeparator(graph);
    entry(keys, help, graph, "help.whatsNew", "Help", "Change Log", none, [open] {
        open(&JPlacerHelpPages::openWhatsNew, "Change Log opened in your browser");
    });
    help->addSeparator(graph);
    entry(keys, help, graph, "help.submitDiagnostics", "Help", "Submit Diagnostics\xE2\x80\xA6", none, [&app] { app.openDiagnostics(); });
    entry(keys, help, graph, "help.checkUpdates", "Help", "Check For Updates\xE2\x80\xA6", none, [&app] { app.updater().check(true); });
}

} // inline namespace jf
