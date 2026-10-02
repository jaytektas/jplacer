# Machine

jplacer drives a machine through a **cell**: one machine's description, kept in a cell file. It lists
the machine's controllers and how jplacer reaches them, its axes, its head with the nozzles, cameras and
actuators on it, and anything fixed to the machine itself. The cell you opened last is opened again when
jplacer starts.

Cell files live in `cells/` in jplacer's configuration folder (`~/.config/jplacer/cells` on Linux).

<!-- src: src/app/JPlacerMachine.cpp (cellsDir, openCell); src/app/JPlacerSettings.h (kMachineCell); src/machine/JPCellConfig.h (what a cell holds) -->

## Bringing in a machine set up in OpenPnP

If your machine already runs under OpenPnP, **Machine ▸ Import OpenPnP Machine…** reads OpenPnP's
`machine.xml` and makes a cell from it, so you do not set the machine up a second time. When OpenPnP's
file is where OpenPnP keeps it (`.openpnp2/machine.xml` in your home folder), jplacer asks whether to
import that one: **Import**, or **Choose Another File…** to pick a different `machine.xml`. In the file
dialog, tick **Show hidden** (or press Ctrl+H) to see folders whose names start with a dot, such as
`.openpnp2`. The new cell is saved as `cells/openpnp.json` and opened straight away. Importing again
replaces that file.

What is brought across:

- **Controllers** that OpenPnP talks G-code to, with their serial port, speed and flow control.
- **Axes**: those driven by a controller, those with no hardware behind them (such as a camera's Z),
  and those that follow another axis (such as two nozzles sharing one Z, one of them reversed).
- **The head**, its **nozzles** (with the actuator for each nozzle's vacuum), **cameras** and
  **actuators**, and the cameras and actuators fixed to the machine.
- **Actuator commands**: how each one is switched on and off, and how a value is read from it.

When the import finishes, a message says what was brought in and lists anything to check: a part of
the OpenPnP set-up jplacer has no equivalent for yet (some axis types, a controller that is not G-code
or is reached over the network), or a command that uses something jplacer cannot fill in yet. Those
parts are left out or kept exactly as OpenPnP wrote them.

Feeders, nozzle tips, parts and packages are not imported.

<!-- src: src/openpnp/JPOpenPnpMachineImporter.cpp (what is read, translate, the notes); src/app/JPlacerMachine.cpp (importOpenPnp, importFrom, kOpenPnpDir, kImportedCellFile); JFramework include/j/platforms/FileDialogWindow.h (Show hidden, Ctrl+H) -->

## Opening a cell

**Machine ▸ Open Cell…** opens a cell file you choose.

<!-- src: src/app/JPlacerMachine.cpp (chooseCell) -->

## Connecting

**Machine ▸ Connect**, or **Connect** on the Machine panel, connects to every controller in the cell.
For each one jplacer:

1. opens its port;
2. asks it what firmware it runs, and picks the matching firmware profile (grblHAL, Grbl, or a
   generic G-code profile when it does not recognise the answer), noting any plugins the firmware
   reports;
3. sends the profile's start-up command;
4. reads the settings the controller stores itself, where the profile says how.

If any controller fails, the others are disconnected again and the panel says why. **Disconnect**
closes every connection.

<!-- src: src/machine/JPCell.cpp (connect); src/machine/JPGcodeDriver.cpp (connect, identify, readSettings); profiles/grblhal.json; profiles/grbl.json; profiles/generic.json -->

### Firmware profiles

A firmware profile is a small file describing one kind of controller firmware: how to recognise it,
the commands it takes, how it replies, how it reports its position, and how its stored settings are
read. jplacer comes with profiles for grblHAL (including the JayTEK plugin's vacuum and analog
readings), Grbl 1.1, and generic G-code. A profile of your own, placed in `profiles/` in jplacer's
configuration folder, is used as well, and replaces a bundled one with the same `id`.

<!-- src: src/machine/JPFirmwareProfile.h; src/machine/JPFirmwareProfile.cpp (profileDirs, loadAll) -->

## The Machine panel

The Machine panel sits on the right of the window while a cell is open.

- **The top line** names the cell, says whether it is connected and, when it is, the firmware each
  controller runs. **Connect** / **Disconnect** is beside it.
- **Position** lists every axis and where it is, updated continuously from the controllers' position
  reports while connected. An axis that follows another shows the position worked out from it.
- **Actuators** lists each actuator with **On** and **Off** if it can be switched, and **Read** if a
  value can be read from it. The result of the last action, or the reason it failed, is shown beside the
  buttons.
- **Console** shows everything sent to and received from the controllers, newest at the top (position
  reports are left out). Type a line in the box below it and press **Send** or Return to send it as it
  is. With more than one controller, choose which one from the list beside the box.

<!-- src: src/ui/JPMachinePanel.cpp; src/machine/JPGcodeDriver.cpp (status lines are not passed on as traffic) -->
