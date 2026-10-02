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
replaces that file, except for the port each controller was set to here, which is kept.

What is brought across:

- **Controllers** that OpenPnP talks G-code to, with their serial port, speed and flow control.
- **Axes**: those driven by a controller, those with no hardware behind them (such as a camera's Z),
  and those that follow another axis (such as two nozzles sharing one Z, one of them reversed).
- **The head**, its **nozzles** (with the actuator for each nozzle's vacuum), **cameras** and
  **actuators**, and the cameras and actuators fixed to the machine.
- **Actuator commands**: how each one is switched on and off, and how a value is read from it.
- **The home command**, every line of it, in order: a machine's homing sequence (release Z, home Y and X
  onto their switches, set the coordinates, home Z…) is the controller's own, and Home runs it.
- **The connect wait**: how long to listen after opening the port before asking anything.

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

Two icons at the left of the toolbar show the machine's state at a glance, and are clicked to change
it: the **chip** for the connection and the **house** for homing. Each sits in a ring: grey when idle,
an amber arc while it is working, green when done, red with a break when the last attempt failed.
Hover over one to see what it means now.

<!-- src: src/ui/JPStateIcon.cpp; src/ui/JPConnectIcon.h; src/ui/JPHomeIcon.h; src/app/JPlacerMachine.cpp (showState) -->

Clicking the chip (or **Machine ▸ Connect**) connects to every controller in the cell.
For each one jplacer:

1. opens its port, and listens for a moment (one second unless the cell says otherwise; an imported
   machine keeps OpenPnP's connect wait) so a board that greets a new connection has finished before
   it is asked anything;
2. asks it what firmware it runs, and picks the matching firmware profile (grblHAL, Grbl, or a
   generic G-code profile when it does not recognise the answer), noting any plugins the firmware
   reports;
3. sends the profile's start-up command;
4. reads the settings the controller stores itself, where the profile says how.

If any controller fails, the others are disconnected again and the panel says why. A port where
nothing answers is a failure, not a connection: it is the wrong port, or the controller is off. A port
another program has open is refused ("in use by another program"): two programs on one port would each
get part of what the controller says. Close the other program (OpenPnP, another jplacer, a terminal)
first.
**Disconnect** closes every connection.

When a connection fails, the chip turns red and the status bar at the bottom of the window says why.
If a connection is lost while working (a cable pulled, the controller reset), a red **CONNECTION LOST**
strip runs across the top of the window until you connect again: that strip is kept for what must not
be missed.

<!-- src: src/machine/JPGcodeDriver.cpp (identify: nothing answered); src/machine/JPCell.cpp (onLost); src/app/JPlacerMachine.cpp (showState); JFramework src/io/SerialPort.cpp (one owner) -->

### Choosing the port

USB serial ports are numbered in the order devices happen to start (`ttyACM0`, `ttyACM1`, …), so the
number a controller had yesterday — or in OpenPnP's configuration — can belong to a different device
today. For each controller on a serial port, the Machine panel has a **port** list showing the serial
devices plugged in now, by the name each device gives itself. Choosing one saves it in the cell, using
the device's permanent name (under `/dev/serial/by-id` on Linux), which stays the same whatever order
devices start in. A port the cell names but that is not plugged in is listed as "(not found)".

<!-- src: src/machine/JPCell.cpp (connect); src/machine/JPGcodeDriver.cpp (connect, identify, readSettings); profiles/grblhal.json; profiles/grbl.json; profiles/generic.json; src/machine/JPSerialPorts.cpp (stable names); src/ui/JPMachinePanel.cpp (the port list) -->

### Firmware profiles

A firmware profile is a small file describing one kind of controller firmware: how to recognise it,
the commands it takes, how it replies, how it reports its position, and how its stored settings are
read. jplacer comes with profiles for grblHAL (including the JayTEK plugin's vacuum and analog
readings), Grbl 1.1, and generic G-code. A profile of your own, placed in `profiles/` in jplacer's
configuration folder, is used as well, and replaces a bundled one with the same `id`.

<!-- src: src/machine/JPFirmwareProfile.h; src/machine/JPFirmwareProfile.cpp (profileDirs, loadAll) -->

## The machine's panels

While a cell is open, its panels sit in docks: **Machine**, **Jog** and **Actuators** on the right,
**Console** and **Axes** along the bottom. Each is a dock like any other — drag its tab to another
place, tear it out into a window of its own, or stack it with others.

<!-- src: src/app/JPlacerMachine.cpp (buildPanels) -->

### Machine

The top line names the cell and says whether it is connected and, when it is, the firmware each
controller runs and whether the machine is homed. Below are what each controller says it is doing (Idle, Run, Alarm…) and, for each controller on a
serial port, the port list (see [Choosing the port](#choosing-the-port)).

<!-- src: src/ui/JPMachinePanel.cpp -->

### Homing

Clicking the house (or **Machine ▸ Home All Axes**) sends each controller its home command and waits for it to
finish, then tells the controller that every axis is at its home coordinate. Until the machine is homed
it will not move: before that its position means nothing, so its soft limits cannot protect it. A
machine imported from OpenPnP homes the way it did in OpenPnP, with the same command. OpenPnP may also
have corrected the home position with a fiducial seen by the camera; jplacer does not do that yet, and
the import says so — the head must then be at its home position when you home.

The house is grey while the machine is not homed, an amber arc while it homes, and green once homed; a
failed home turns it red and the status bar says why. A red **ALARM** strip runs across the top of the
window when a controller has stopped on an alarm.

<!-- src: src/machine/JPCell.cpp (doHome); src/app/JPlacerMachine.cpp (showState); src/openpnp/JPOpenPnpMachineImporter.cpp (HOME_COMMAND, visual homing note) -->

### Jog

Moving a tool by hand. Choose the tool along the top — each nozzle, the camera on the head, and anything
else on the head that moves on axes. Each of the tool's coordinates (X, Y, Z, Rotation) is a row:

- the box shows where the tool is now; type a coordinate in it and press Return to go there;
- **-** and **+** move it by one step.

**Step** is in mm (degrees for Rotation); **Speed** is a share of the speed of the slowest axis that
moves. A nozzle's Z is its own even where two nozzles share one motor: jplacer works out which way the
motor turns. A move that would take an axis outside its soft limits is not made, and the panel says why.

<!-- src: src/ui/JPJogPanel.cpp; src/machine/JPCell.cpp (jog, doMove) -->

### Actuators

Each actuator with **On** and **Off** if it can be switched, and **Read** if a value can be read from
it. The result of the last action, or the reason it failed, is shown beside the buttons.

<!-- src: src/ui/JPActuatorPanel.cpp -->

### Console

Everything sent to and received from the controllers, newest at the top (position reports are left
out). Type a line in the box and press **Send** or Return to send it as it is. With more than one
controller, choose which one from the list beside the box.

<!-- src: src/ui/JPConsolePanel.cpp; src/machine/JPGcodeDriver.cpp (status lines are not passed on as traffic) -->

### Axes

Every axis in the cell — those a controller drives, those that follow another, and those with no
hardware — and where each one is, live. For setting up and checking a machine; moving it is the Jog
panel's.

<!-- src: src/ui/JPAxesPanel.cpp -->
