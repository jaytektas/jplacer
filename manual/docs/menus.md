# Menus

Entries shown greyed out are features that are not built yet. They are there so you can see where each
feature will live.

<!-- src: src/app/JPlacerMenuBuilder.cpp (build, addPending) -->

## File

| Entry | |
|---|---|
| **New Job**, **Open Job…**, **Save Job**, **Save Job As…** | Not yet available. |
| **Quit** | Closes jplacer. If an update has been downloaded, it is installed now. |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the File menu); src/app/JPlacerApp.cpp (run, installStaged) -->

## Edit

| Entry | |
|---|---|
| **Preferences…** | Opens [Preferences](preferences.md). |

## Machine

| Entry | |
|---|---|
| **Import OpenPnP Machine…** | Makes a cell from an OpenPnP `machine.xml` (see [Machine](machine.md#bringing-in-a-machine-set-up-in-openpnp)). |
| **Open Cell…** | Opens a cell file. |
| **Connect** | Connects to the open cell's controllers. Available while a cell is open and not connected. |
| **Disconnect** | Closes the connections. Available while connected. |
| **Home All Axes** | Homes the machine (see [Homing](machine.md#homing)). Available while connected. |

**Park Head** and **Machine Setup…** are not yet available.

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Machine menu); src/app/JPlacerMachine.cpp (updateMenu) -->

## Job

**Start**, **Pause**, **Stop**, **Board Setup…**, **Feeders…** and **Parts and Packages…** are not yet
available.

## Help

| Entry | |
|---|---|
| **User Manual** | Opens this manual in your web browser. |
| **What's New** | Opens [What's new](whats-new.md) in your web browser. |
| **Check for Updates** | Looks for a newer jplacer now, and tells you the answer (see [Updates](updates.md)). |
| **About jplacer** | The version, copyright and licence. |

<!-- src: src/app/JPlacerMenuBuilder.cpp (the Help menu); src/app/JPlacerHelpPages.cpp (opening the manual) -->
