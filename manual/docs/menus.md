# Menus

Entries shown greyed out are features that are not built yet. They are there so you can see where each
feature will live.

The keys shown are the ones jplacer starts with. Any entry can be given a key of your choosing, or have its
key taken off, in [Preferences, Keys](preferences.md#keys); the menu then shows that key.

<!-- src: src/app/JPlacerMenuBuilder.cpp (build) -->

## File

| Entry | |
|---|---|
| **New Job** (Ctrl+N) | Starts an empty job (see [Jobs](jobs.md#new-open-and-save)). |
| **Open Job…** (Ctrl+O) | Opens a `.job.xml` file. |
| **Open Recent Job...** | The ten jobs opened or saved last, newest first. |
| **Save Job** (Ctrl+S) | Saves the open job. |
| **Save Job As…** (Ctrl+Shift+S) | Saves the open job to a file you choose. |
| **Save Configuration** | Saves the parts, packages and the lists of boards and panels, and asks about each board with changes (see [Boards](boards.md#saving-boards)). |
| **Import Placements** | OpenPnP's importers, reading placements into the board chosen on the Boards tab (see [Boards](boards.md#importing-placements)). |
| **Quit** | Closes jplacer, after asking about a job with changes and about each board with changes. If an update has been downloaded, it is installed now. |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the File menu); src/app/JPlacerJob.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/app/JPlacerApp.cpp (run, installStaged) -->

## Edit

| Entry | |
|---|---|
| **Undo** (Ctrl+Z) | Takes back the last change in Machine Setup; it says which ([Undo and Redo](machine-setup.md#undo-and-redo)). |
| **Redo** (Ctrl+Y) | Makes the change undone again. |
| **Add Board/Panel** | **New Board…**, **Existing Board…**, **New Panel…**, **Existing Panel…**: as the Job tab's Add Board/Panel button (see [The Job tab](jobs.md#the-job-tab)). |
| **Remove Board(s)/Panel(s)** | Takes the boards and panels chosen on the Job tab out of the job. Available while one is chosen. |
| **Capture Tool Location** | As the Job tab's button: the chosen board or panel is placed where the nozzle is. Available while one is chosen. |
| **Preferences…** | Opens [Preferences](preferences.md). |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Edit menu); src/ui/JPJobPanel.cpp (addNew, addExisting, removeSelected, captureTool, setEditItems) -->

## View

| Entry | |
|---|---|
| **System Units** | **Millimeters** (to start with) or **Inches**, as in OpenPnP: the units every length is shown and typed in (coordinates, offsets, sizes, axis speeds and limits, the position at the foot of the window, tables, the Jog distances), to one more place in inches; rotations stay in degrees. Lengths are kept in millimetres whichever is chosen. The choice takes effect the next time jplacer starts (it says so). A table's length in other units than these is shown in its own, with their name. The Jog distances are kept apart for each (Edit > Preferences, Jog): to start with 0.01 to 100 mm, or 0.001 to 10 in. |
| **Selections in Tables** | **Unlinked** (to start with) or **Linked**. Linked, what you choose in one tab's table chooses what goes with it on the other tabs, as in OpenPnP (see below). |
| **Language** | **English (United States)** (to start with), **Russian**, **Spanish**, **French**, **Italian**, **German** or **Chinese (China)**, as in OpenPnP: OpenPnP's own translations, so whatever jplacer names as OpenPnP does (menus, tabs, panels' titles, labels, buttons, table headings) is shown in that language, and the rest in English. The choice takes effect the next time jplacer starts (it says so). OpenPnP's German, Spanish, French and Italian translations cover little; its Russian and Chinese, most of it. |

Under them, a tick for each panel: each camera, **Jog**, **Actuators**, **[Parts](parts.md)**, **[Packages](packages.md)**,
**Machine Setup**, **Machine** and **Console**. Untick one to close it; tick it to show it again where it lives (see
[The machine's panels](machine.md#the-machines-panels)).

<!-- src: src/app/JPlacerMenuBuilder.cpp (View); src/app/JPlacerLayout.cpp (rebuildMenu); src/model/JPSystemUnits.cpp; src/setup/JPFormBuilder.h (length); src/ui/JPLengthCell.cpp; src/ui/JPJogPanel.cpp (defaultDistances, jog); src/app/JPlacerSettings.cpp (jogDistancesKey), src/common/JPTranslations.cpp -->

### Linked tables

With **Selections in Tables** set to **Linked**, a choice made in a table on the tab in front chooses, on the
other tabs:

| Chosen | Also chosen |
|---|---|
| A board or panel on the [Job tab](jobs.md#the-job-tab) | A board: the board on the Boards tab, and in a panel, that panel and the board in it on the Panels tab. A panel: that panel on the Panels tab. |
| A placement on the Job tab | The same placement on the Boards tab (or the fiducial on the Panels tab), and its part. |
| A placement on the [Boards](boards.md) tab | The same placement on the Job tab when it shows that board, and its part. |
| A board or panel in a panel on the [Panels](panels.md) tab | It on the Job tab, and a board on the Boards tab. |
| A fiducial on the Panels tab | The same fiducial on the Job tab when it shows that panel. |
| A part on the [Parts](parts.md) tab | Its package on the Packages tab, a feeder that holds it on the Feeders tab (an enabled one first), and the vision settings it uses on the Vision tab (of the type that tab shows). |
| A feeder on the Feeders tab | Its part, and so what goes with the part. |

What is chosen by a link chooses nothing further. A part with no feeder leaves the Feeders tab as it was.

<!-- src: src/app/JPlacerTableLinks.cpp; src/ui/JPFeedersPanel.cpp (selectFeederForPart); src/ui/JPVisionSettingsPanel.cpp (selectFor) -->

## Machine

| Entry | |
|---|---|
| **Import OpenPnP Machine…** | Makes a cell from an OpenPnP `machine.xml` (see [Machine](machine.md#bringing-in-a-machine-set-up-in-openpnp)). |
| **Open Cell…** | Opens a cell file. |
| **Connect** | Connects to the open cell's controllers. Available while a cell is open and not connected. |
| **Disconnect** | Closes the connections. Available while connected. |
| **Home All Axes** (Ctrl+H) | Homes the machine (see [Homing](machine.md#homing)). Available while connected. |
| **Stop** (Escape) | Holds the move under way and drops what is queued; the position is kept (see [Stopping a move](machine.md#stopping-a-move)). |
| **Emergency Stop** | Resets every controller at once; home again before moving. |
| **Park Head** | Takes the head out of the way (see [Parking](machine.md#parking)). Available once homed. |
| **Jog** | The [Jog panel](machine.md#jog)'s moves, with OpenPnP's keys: **X+** / **X-** (Ctrl+Right / Ctrl+Left), **Y+** / **Y-** (Ctrl+Up / Ctrl+Down), **Z+** / **Z-** (Ctrl+' / Ctrl+/), **Turn Anticlockwise** / **Turn Clockwise** (Ctrl+, / Ctrl+.), **Turn to 0**, **Larger** / **Smaller Distance** (Ctrl+= / Ctrl+-), **Faster** / **Slower**, **Park Head** (Ctrl+Shift+P), **Up to Safe Z** (Ctrl+Shift+L), **Head Safe Z** (Ctrl+Shift+Z), **Discard** (Ctrl+Shift+D), **Pick**, **Place**, **Nozzle to the Camera**, **Camera to the Nozzle**. A key is not taken from a text field that uses it. |
| **Machine Setup…** | Shows [Machine Setup](machine-setup.md), to look at and change what the machine is made of. |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Machine menu); src/app/JPlacerMachine.cpp (updateMenu) -->

## Job

| Entry | |
|---|---|
| **Start** (**Pause** while the job runs, **Resume** while it is paused), **Step**, **Stop** | As the Job tab's buttons (see [Running the job](jobs.md#running-the-job)). |
| **Reset All Placed** | Marks every placement of the job not placed, so the job places them all again. |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Job menu); src/ui/JPJobPanel.cpp (setMenuItems, resetAllPlaced, updateJobActions) -->

## Scripts

As OpenPnP's: the scripts in jplacer's scripts folder (`~/.config/jplacer/scripts`), by name, each folder a
submenu of its own (but **Events**, and a folder holding a file named `.ignore`). Choose one to run it; the status
line says when it is done, or why it failed, and what it prints goes to the log. Then **Refresh Scripts** (the
folder read again), **Open Scripts Directory**, and **Clear Scripting Engine Pool**, greyed out: each script runs as
a program of its own, so there is no pool.

A script is a Python (`.py`, run by `python3`), JavaScript (`.js`, run by `node`) or shell (`.sh`) file. It is told
what it runs for, as JSON in the environment variable `JPLACER_GLOBALS` (and the event's name in `JPLACER_EVENT`),
and, as OpenPnP's scripts have `machine`, it can ask the machine through the module `jplacer` that jplacer keeps
beside the scripts (`import jplacer` in Python, `require("jplacer")` in JavaScript):

| | |
|---|---|
| `positions()` | Where each axis is, by name. |
| `location(tool)` | Where a nozzle, camera or actuator (by name) is: `x`, `y`, `z`, `rotation`. |
| `move_to(tool, x, y, z, rotation, speed, straight)` | Moves it there (any left out stay), by way of Safe Z unless `straight`; `speed` a share of the machine's. |
| `safe_z(head, speed)` | The head (the first, when none is named) up to Safe Z. |
| `home()` | Homes the machine, waiting for it. |
| `actuate(actuator, value)` | Switches an actuator (`True`/`False`), or sets it to a number or text. |
| `read(actuator, parameter)` | Reads an actuator. |
| `gcode(line, controller)` | Sends a line to a controller (the first, when none is named). |
| `message(text)` | Shows it in the status bar. |

A request that fails raises `jplacer.Error` (an `Error` in JavaScript) with why. Each is a line of JSON written to
file descriptor 3 (`{"call": "moveTo", "tool": "N1", "x": 10}`) and its answer a line read from 4 (`{"result": …}`
or `{"error": "…"}`), so a shell script can ask too. A script the machine itself runs (an actuator's, or one at
Machine.AfterDriverHoming) cannot move it, switch or read actuators, or home it: waiting for the machine there would
never end, so it is told so.

The **Events** folder's scripts run at OpenPnP's events, those named the event, or the event, a dot and more
(`Job.Starting.2.py`), in name order: **Startup**, **Machine.AfterHoming**, **Job.Starting**, **Job.Finished**,
**Job.Error** (with the `exception`), **Job.Placement.Starting**, **Job.Placement.Complete**,
**Feeder.BeforeFeed**, **Feeder.AfterFeed**, **Nozzle.BeforePick**, **Nozzle.AfterPick**, **Nozzle.BeforePlace**
and **Nozzle.AfterPlace** (each with the `job`, `board`, `placement` and `part`, and the `feeder` or `nozzle`);
**Feeder.Fault** (a deferred placement's feeder failing: the `feeder` and the `exception`); **Job.BeforeDiscard**
and **Job.AfterDiscard** (the `nozzle`); **Vision.PartAlignment.Before** and **Vision.PartAlignment.After** (the
`part`, the `nozzle`, after it the `offsets` found); **Machine.AfterDriverHoming** (the controllers homed, before
visual homing and Machine.AfterHoming); **NozzleCalibration.Starting** and **NozzleCalibration.Finished** (runout
calibration: the `nozzle` and `camera`); **Camera.BeforeSettle**, **Camera.AfterSettle**, **Camera.BeforeCapture**
and **Camera.AfterCapture** (each picture vision takes: the `camera`); and **Camera.AfterPosition** (a camera
moved to look somewhere by a button, once it is there). One that fails (exits other than 0, or runs past a
minute) stops what it runs for, saying why.

<!-- src: src/app/JPlacerScriptsMenu.cpp; src/machine/JPScripting.cpp; src/tasks/JPJobProcessor.cpp (script, placementGlobals); src/app/JPlacerMachine.cpp (runEvent, moveToolTo); src/app/JPlacerApp.cpp; src/tasks/JPCameraLook.cpp (settled); src/app/JPlacerCameraTasks.cpp (calibrateRunout); src/tasks/JPJobProcessor.cpp (discard, align), src/app/JPlacerMachine.cpp (scriptRequest) -->

## Window

As OpenPnP's:

| Entry | |
|---|---|
| **Multiple Window Style** | Ticked, the cameras open in a window of their own, and the machine controls (Jog, Actuators) in another, the main window keeping the rest; unticked, all in the one window. Taken the next time jplacer starts (it says so). |
| **Change Appearance…** | **Appearance Settings**: the **Theme**, the **Font Size** (how big the whole interface is, as Edit > Preferences' interface scale) and **Alternating Rows Style** (every other row of a table shaded; on to begin with). **Apply** shows the choice, **Save** shows and keeps it, **Cancel** puts back what was kept. |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Window menu); src/app/JPlacerLayout.cpp (place); src/app/JPlacerAppearanceDialog.cpp; src/ui/JPTable.cpp -->

## Help

| Entry | |
|---|---|
| **About jplacer** | The version, copyright and licence. |
| **Quick Start** | Opens [Getting started](getting-started.md) in your web browser. |
| **Setup and Calibration** | Opens [Machine Setup](machine-setup.md) in your web browser. |
| **User Manual** | Opens this manual in your web browser. |
| **What's New** | Opens [What's new](whats-new.md) in your web browser. |
| **Check for Updates** | Looks for a newer jplacer now, and tells you the answer (see [Updates](updates.md)). |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Help menu); src/app/JPlacerHelpPages.cpp (opening the manual) -->
