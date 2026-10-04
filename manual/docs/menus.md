# Menus

Entries shown greyed out are features that are not built yet. They are there so you can see where each
feature will live.

The keys shown are the ones jplacer starts with. Any entry can be given a key of your choosing, or have its
key taken off, in [Preferences, Keys](preferences.md#keys); the menu then shows that key.

<!-- src: src/app/JPlacerMenuBuilder.cpp (build, addPending) -->

## File

| Entry | |
|---|---|
| **New Job** (Ctrl+N) | Starts an empty job (see [Jobs](jobs.md#new-open-and-save)). |
| **Open Job…** (Ctrl+O) | Opens a `.jpjob` file. |
| **Save Job** (Ctrl+S) | Saves the open job. |
| **Save Job As…** (Ctrl+Shift+S) | Saves the open job to a file you choose. |
| **Import Pick-and-Place File…** | Reads a board into the open job and gives its placements their parts ([Reading a board into the job](jobs.md#reading-a-board-into-the-job)). |
| **Quit** | Closes jplacer. If an update has been downloaded, it is installed now. |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the File menu); src/app/JPlacerJob.cpp; src/app/JPlacerApp.cpp (run, installStaged) -->

## Edit

| Entry | |
|---|---|
| **Undo** (Ctrl+Z) | Takes back the last change in Machine Setup; it says which ([Undo and Redo](machine-setup.md#undo-and-redo)). |
| **Redo** (Ctrl+Y) | Makes the change undone again. |
| **Preferences…** | Opens [Preferences](preferences.md). |

## View

A tick for each panel: each camera, **Jog**, **Actuators**, **Board**, **Parts**, **Library**,
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
| **Parts** | Shows the [Parts](jobs.md#the-parts-panel) panel: the job's placements and their parts. |
| **Library** | Shows the [Library](jobs.md#the-library-panel) panel: the parts library's parts, packages and footprints. |

**Start**, **Pause**, **Stop**, **Board Setup…** and **Feeders…** are not yet available.

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Job menu) -->

## Help

| Entry | |
|---|---|
| **User Manual** | Opens this manual in your web browser. |
| **What's New** | Opens [What's new](whats-new.md) in your web browser. |
| **Check for Updates** | Looks for a newer jplacer now, and tells you the answer (see [Updates](updates.md)). |
| **About jplacer** | The version, copyright and licence. |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Help menu); src/app/JPlacerHelpPages.cpp (opening the manual) -->
