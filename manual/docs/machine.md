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
- **The head**, its **nozzles** (with the actuator for each nozzle's vacuum), **cameras** (each by
  the name its device gives itself, with its light) and **actuators**, and the cameras and actuators
  fixed to the machine.
- **Actuator commands**: how each one is switched on and off, and how a value is read from it.
- **The home command**, every line of it, in order: a machine's homing sequence (release Z, home Y and X
  onto their switches, set the coordinates, home Z…) is the controller's own, and Home runs it.
- **The connect wait**: how long to listen after opening the port before asking anything.
- **Non-squareness**: a machine squared in OpenPnP (its X axis a linear transform adding a share of Y)
  keeps that correction, as jplacer's [squareness](board.md#squaring-the-machine).
- **The head's places**: its homing fiducial and whether it homes visually, its park location, the
  calibration rig's two fiducials (their places, heights and diameters) and test object, and its pump
  (which actuator, when it runs, how long it takes to come up).

When the import finishes, a message says what was brought in and lists anything to check: a part of
the OpenPnP set-up jplacer has no equivalent for yet (some axis types, a controller that is not G-code
or is reached over the network), or a command that uses something jplacer cannot fill in yet. Those
parts are left out or kept exactly as OpenPnP wrote them.

Feeders, nozzle tips, parts and packages are not imported, and neither is OpenPnP's camera calibration:
jplacer measures its cameras itself, and the import notes each camera that had one.

<!-- src: src/openpnp/JPOpenPnpMachineImporter.cpp (what is read, translate, the notes, ReferenceLinearTransformAxis); src/app/JPlacerMachine.cpp (importOpenPnp, importFrom, kOpenPnpDir, kImportedCellFile); JFramework include/j/platforms/FileDialogWindow.h (Show hidden, Ctrl+H) -->

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

While a cell is open, its panels sit in docks: **Machine**, **Jog**, **Actuators** and
[**Board**](board.md) on the right, **Console** and **Axes** along the bottom. Each is a dock like any other — drag its tab to another
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
machine imported from OpenPnP homes the way it did in OpenPnP, with the same command.

The switches put the head within a fraction of a millimetre. Where the head is set to home visually (an
imported head that did so in OpenPnP is), Home then finishes with the camera: the calibrated camera on
the head is shown, looks at the homing mark, and the coordinates are corrected so the mark measures
exactly where the head's settings say it is. It looks again to check, and corrects again if that left
more than 0.02 mm (about how closely a machine returns to a place). The line under the camera buttons and
the status bar say by how much it corrected. Until a camera on the head is calibrated, Home says it homed
by the switches only.

<!-- src: src/tasks/JPVisualHoming.cpp (kHomedWithinMm, kCorrections); src/app/JPlacerCameraTasks.cpp (visualHome); src/app/JPlacerMachine.cpp (onHomed); src/machine/JPCell.cpp (correctPosition) -->

The house is grey while the machine is not homed, an amber arc while it homes, and green once homed; a
failed home turns it red and the status bar says why. A red **ALARM** strip runs across the top of the
window when a controller has stopped on an alarm.

<!-- src: src/machine/JPCell.cpp (doHome); src/app/JPlacerMachine.cpp (showState); src/openpnp/JPOpenPnpMachineImporter.cpp (HOME_COMMAND, visual homing note) -->

#### Backlash

Every drive has a little play (backlash): an axis stops in a slightly different place depending on
which way it was travelling. An axis with a backlash offset is positioned one-sided: every move ends
travelling the same way. A move that would arrive the other way first goes past its target by the
offset, then comes back to it at the axis's backlash speed. The offset only needs to be at least the
play; the axis then ends in the same place whichever way it came. An imported machine keeps the offset
and speed OpenPnP measured, whichever way OpenPnP compensated.

<!-- src: src/machine/JPAxisConfig.h (Backlash); src/machine/JPCell.cpp (doMove: overshoot, approach); src/openpnp/JPOpenPnpMachineImporter.cpp (backlash) -->


### Jog

Moving a tool by hand. Choose the tool along the top — each nozzle, the camera on the head, and anything
else on the head that moves on axes. Each of the tool's coordinates (X, Y, Z, Rotation) is a row:

- the box shows where the tool is now, in its own coordinates (where its axes are, plus its offset on
  the head, so a nozzle's X is where the nozzle is, not where the head is); type a coordinate in it and
  press Return to take the tool there;
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

### Cameras

The cell's cameras fill the middle of the window. Choose a camera from the buttons along the top; its
live picture shows below, fitted to the space with its shape kept, and a cross through the middle
marks the point the camera is looking at. Beside the buttons are the picture's format, size and rate.
Only the camera shown is running: the others are stopped until chosen.

A camera is found by the name the device gives itself (for example `top: top`), not by the USB socket
it is plugged into, so moving it to another socket or hub does not lose it. jplacer picks the largest
picture the camera offers in MJPG (or YUYV, if that is all it has), unless the cell names a format and
size.

A camera can drop off its USB connection (noise from the stepper motors on its cable) or hang without
saying so. jplacer notices either (no picture for 3 seconds counts as hung), says so across the top of
the last picture, which would otherwise pass for a live one, and opens the camera again by its name every
2 seconds until it is back; the picture then carries on by itself.

<!-- src: src/camera/JPCameraFeed.cpp (run, runSource, kStalledMs, kReconnectMs); src/ui/JPCameraView.cpp (the band over the picture) -->

**Save Picture** writes the shown camera's latest picture as a PNG (lossless, so it measures the same as
the live picture did) to `captures/` in jplacer's configuration folder, named after the camera and the
moment it was taken; the line under the buttons names the file.

A camera only shows what is in front of it, and in an enclosed machine that is dark until its light is
on. While the machine is connected, the shown camera's light is switched on (and the previous camera's
off); while not connected, the line under the buttons says the light is off.

<!-- src: src/ui/JPCameraPanel.cpp (savePicture); src/camera/JPImageFile.cpp; src/ui/JPCameraView.cpp; src/camera/JPV4L2Source.cpp (found by name); src/camera/JPCaptureFactory.cpp (choose); src/app/JPlacerMachine.cpp (lightCameras) -->

#### Calibrating the head camera

jplacer measures its cameras itself: nothing is taken from another program. **Calibrate**, with the
camera on the head shown, works out how big a pixel is on the machine in X and in Y, which way the
camera is turned (or mirrored), and how its lens bends the picture. The machine must be connected and
homed, and the head's homing mark (its place and diameter, brought across by an OpenPnP import) must be
set.

1. The camera moves over the homing mark.
2. It finds the mark at whatever size it appears (the scale is not known yet), checking that its edge
   is round nearly all the way round.
3. Three small moves show which way the mark goes in the picture; then the head steps through a 5 by 5
   grid that carries the mark across the middle part of the picture, finding it after every move.
4. A fit of the grid's finds gives the camera's scale and turn, and the lens: a wide lens pulls the
   edges of the picture in (barrel), around a centre that on a small camera is rarely the middle of the
   picture. If the finds disagree with the fit by more than a pixel, or the mark measures far from its
   set diameter, nothing is kept and the panel says why.
5. The head goes back to where it started.

Every move arrives from the same side (see [Backlash](#backlash)), so play in the drives cannot creep
into the scale, and each picture measured is one taken after the move ended (a camera hands over
pictures a little late). The head moves at a tenth of its speed. While a task runs, its buttons and the
choice of camera are off, and the line under the buttons says what it is doing; when it ends, that line
and the status bar give the result. A calibration is saved in the cell file and used from then on:
whatever is measured in a picture is straightened through the lens first.

<!-- src: src/tasks/JPCameraCalibrator.cpp; src/vision/JPCalibrationFit.cpp (fitWithLens); src/common/JPLens.h; src/tasks/JPCameraLook.cpp; src/app/JPlacerCameraTasks.cpp (calibrate, notReady, kTaskSpeed); src/ui/JPCameraPanel.cpp (setBusy, the note) -->

#### Visual Test

**Visual Test** moves a calibrated head camera to look where the head's settings say the homing mark is,
finds the mark, and says how far it really is from there, in mm in X and Y. Nothing is changed: right
after a visual home it reads within a few hundredths of a millimetre, and any time later it shows whether
the machine has lost its place.

<!-- src: src/tasks/JPVisualTest.cpp; src/app/JPlacerCameraTasks.cpp (visualTest) -->

#### Finding round marks

A round mark (a fiducial, the homing mark) is found in two steps. A search looks for the most circular
things of the expected size near where the mark should be, on a reduced copy of the picture so it is
quick, and keeps the best few: a board is full of round things (holes, vias, pads, round letters,
reflections of the light). Each is then measured on the full picture: narrow strips are cast out from
its centre all the way round, each finds where the brightness changes fastest (a change that stands
well above the strip's own grain), and a circle is fitted through those points. A mark is accepted when its size is the size asked for and its edge is round
nearly all the way round; of those, the one that fits best wins, and between equally good ones the one
nearest where the mark should be.

Only the edge is measured, not the inside: shiny copper straight under a camera reflects the camera's
own dark lens in its middle, and off to the side reflects the light, so the same fiducial looks
different from place to place. The edge gives the centre and the diameter to a small fraction of a
pixel, and is not pulled by light falling more on one side. Where it matters which way round the mark
is, a search can ask for a bright mark (copper on solder mask, a white dot) or a dark one (a hole), so
that a hole beside a fiducial is not taken for it.

<!-- src: src/vision/JPRoundMarkFinder.cpp (find, measureAt, edgeCircle, polarityMatches, findAnySize) -->
