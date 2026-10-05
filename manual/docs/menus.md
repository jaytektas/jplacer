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

A tick for each panel: each camera, **Jog**, **Actuators**, **[Parts](parts.md)**, **[Packages](packages.md)**,
**Machine Setup**, **Machine** and **Console**. Untick one to close it; tick it to show it again where it lives (see
[The machine's panels](machine.md#the-machines-panels)).

<!-- src: src/app/JPlacerMenuBuilder.cpp (View); src/app/JPlacerLayout.cpp (rebuildMenu) -->

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

## Scripts and Window

OpenPnP's **Scripts** menu (**Refresh Scripts**, **Open Scripts Directory**, **Clear Scripting Engine Pool**) and
**Window** menu (**Multiple Window Style**, **Change Appearance…**) are there, greyed out: they are not built yet.

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Scripts and Window menus) -->

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
