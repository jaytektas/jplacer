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
replaces that file, except for what was set or measured here, which is kept: the port each controller
was set to, each camera's calibration and the squareness correction.

What is brought across:

- **Controllers** that OpenPnP talks G-code to, with their serial port, speed and flow control.
- **Axes**: those driven by a controller, those with no hardware behind them (such as a camera's Z),
  and those that follow another axis (such as two nozzles sharing one Z, one of them reversed).
- **The head**, its **nozzles** (with the actuator for each nozzle's vacuum, the nozzle tips that fit
  it and the one on it), **cameras** (each by
  the name its device gives itself, with its light) and **actuators**, and the cameras and actuators
  fixed to the machine.
- **Actuator commands**: how each one is switched on and off, and how a value is read from it.
- **The home command**, every line of it, in order: a machine's homing sequence (release Z, home Y and X
  onto their switches, set the coordinates, home Z…) is the controller's own, and Home runs it.
- **The connect wait**: how long to listen after opening the port before asking anything.
- **Each camera's settings** (exposure, white balance, gain, focus, brightness, contrast and so on, set or
  automatic), which jplacer sets again every time it opens the camera: a camera that dropped off its
  connection comes back as it was, not on its own defaults.
- **Non-squareness**: a machine squared in OpenPnP (its X axis a linear transform adding a share of Y)
  keeps that correction, as jplacer's [squareness](board.md#squaring-the-machine).
- **Nozzle tips**: each one's name, the diameter OpenPnP's nozzle tip calibration finds it by, as the
  diameter the camera looking up sees, and its tool changer: its places (start, middle, second middle,
  end, those set) and speeds, and the actuators switched between them, become the tip's
  [load steps](machine-setup.md#a-nozzle-tips-changer), unloading being loading backwards as in OpenPnP.
  A tip a nozzle's list still names after it was deleted in OpenPnP is left out.
- **The head's places**: its homing fiducial and whether it homes visually, its park location, the
  calibration rig's two fiducials (their places, heights and diameters) and test object, and its pump
  (which actuator, when it runs, how long it takes to come up).

When the import finishes, a message says what was brought in and lists anything to check: a part of
the OpenPnP set-up jplacer has no equivalent for yet (some axis types, a controller that is not G-code
or is reached over the network), or a command that uses something jplacer cannot fill in yet. Those
parts are left out or kept exactly as OpenPnP wrote them.

Feeders, parts and packages are not imported, and neither is OpenPnP's camera calibration:
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
devices start in. While connected, the connection stays as it is: the port chosen is used the next
time you connect (see [Applying](machine-setup.md#applying)). A port the cell names but that is not plugged in
is listed as "(not found)".

<!-- src: src/app/JPlacerMachine.cpp (setPort, applySetup); src/machine/JPCell.cpp (connect); src/machine/JPGcodeDriver.cpp (connect, identify, readSettings); profiles/grblhal.json; profiles/grbl.json; profiles/generic.json; src/machine/JPSerialPorts.cpp (stable names); src/ui/JPMachinePanel.cpp (the port list) -->

### Firmware profiles

A firmware profile is a small file describing one kind of controller firmware: how to recognise it,
the commands it takes, how it replies, how it reports its position, and how its stored settings are
read. jplacer comes with profiles for grblHAL (including the JayTEK plugin's vacuum and analog
readings), Grbl 1.1, and generic G-code. A profile of your own, placed in `profiles/` in jplacer's
configuration folder, is used as well, and replaces a bundled one with the same `id`.

<!-- src: src/machine/JPFirmwareProfile.h; src/machine/JPFirmwareProfile.cpp (profileDirs, loadAll) -->

## The machine's panels

While a cell is open, its panels sit in docks:

- the **cameras** top left, tabbed together;
- under them the machine controls, **Jog** and **Actuators**;
- the rest of the window, tabbed: [**Board**](board.md), [**Machine Setup**](machine-setup.md) and
  **Machine**;
- the **Console** across the bottom.

Every split between them can be dragged, as far as leaves the panels in the middle room for themselves.
Each is a dock like any other: drag its tab to another place, tear it out into a window of its own, or
stack it with others.

**View** has a tick for each panel. Untick one to close it (or close it with its tab's **×**); tick it
to bring it back, where it lives above, in front of the others there.

<!-- src: src/app/JPlacerLayout.cpp (place, show, rebuildMenu, kLeftShare, kBottomShare); src/app/JPlacerMachine.cpp (buildPanels, buildCameras); src/app/JPlacerMenuBuilder.cpp (View); JFramework include/j/core/DockSpace.h (sideCap) -->

### Where the tool is

The right of the status bar says where the tool chosen in **Jog** is: X, Y, Z and C (its rotation), as
Jog's boxes have them, in green. Click it to measure: it reads **Relative**, in blue, counting from where
the tool was at that moment, so jogging to a second place reads the distance between the two. Click it
again to go back to where the tool is.

<!-- src: src/ui/JPPositionReadout.cpp; src/ui/JPJogPanel.cpp (where); src/app/JPlacerMachine.cpp (the status bar) -->

### Machine

The top line names the cell and says whether it is connected and, when it is, the firmware each
controller runs and whether the machine is homed. Below are what each controller says it is doing (Idle, Run, Alarm…) and, for each controller on a
serial port, the port list (see [Choosing the port](#choosing-the-port)).

Under **Calibration** is what the machine has been measured for: each camera's calibrations, one for each
picture size it was measured at (the size, its scale in X and Y, how far it is turned, its lens, how
closely the measurements fitted, and when), the
[squareness](board.md#squaring-the-machine) correction, and how it homes. It changes as soon as a
calibration does.

<!-- src: src/ui/JPMachinePanel.cpp (refresh, refreshCalibration) -->

### Homing

Clicking the house (or **Machine ▸ Home All Axes**) sends each controller its home command and waits for it to
finish, then tells the controller that every axis is at its home coordinate. Until the machine is homed
it will not move: before that its position means nothing, so its soft limits cannot protect it. A
machine imported from OpenPnP homes the way it did in OpenPnP, with the same command.

The switches put the head within a fraction of a millimetre. Where the head is set to home visually (an
imported head that did so in OpenPnP is), Home then finishes with the camera: the calibrated camera on
the head is brought to the front, looks at the homing mark, and the coordinates are corrected so the mark measures
exactly where the head's settings say it is. It looks again to check, and corrects again if that left
more than 0.02 mm (about how closely a machine returns to a place). The line under the camera buttons and
the status bar say by how much it corrected. Until a camera on the head is calibrated, Home says it homed
by the switches only.

<!-- src: src/tasks/JPVisualHoming.cpp (kHomedWithinMm, kCorrections); src/app/JPlacerCameraTasks.cpp (visualHome); src/app/JPlacerMachine.cpp (onHomed); src/machine/JPCell.cpp (correctPosition) -->

The house is grey while the machine is not homed, an amber arc while it homes, and green once homed; a
failed home turns it red and the status bar says why. A red **ALARM** strip runs across the top of the
window when a controller has stopped on an alarm.

<!-- src: src/machine/JPCell.cpp (doHome); src/app/JPlacerMachine.cpp (showState); src/openpnp/JPOpenPnpMachineImporter.cpp (HOME_COMMAND, visual homing note) -->

#### Parking

**Machine ▸ Park Head** (once the machine is homed) takes the head out of the way: every Z axis on the
head comes up into its safe zone first, then the head goes to its park place (an imported head keeps
OpenPnP's), placed by its camera. A park place past a soft limit (often one is set right at the end of
travel) is gone to as near as the limit allows. It moves at half speed.

<!-- src: src/machine/JPCell.cpp (doPark); src/app/JPlacerMachine.cpp (park, kParkSpeed); src/app/JPlacerMenuBuilder.cpp -->

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

The cell's cameras sit top left in the window, each in a dock of its own, named after the camera and
tabbed together to start with. Click a camera's tab to see it, or arrange them like any other dock: side
by side, or torn out into a window of its own. A camera's live picture is fitted to its dock with its
shape kept, and a cross through the middle marks the point the camera is looking at; along the top are
the picture's format, size and rate.

A camera's tools are icons in its tab, beside the close button, while it is the front tab (hover over
one to see what it does):

| Icon | |
|---|---|
| eye | **As taken**: lit, the picture is shown as the camera gives it; off, it is straightened (below). |
| disk | **Save the picture** (below). |
| target | **Calibrate** the camera (below). |
| tick in a ring | **Visual test** of the calibration (below). |
| gear | **The camera's settings**: Machine Setup, with the camera chosen in its tree. |

<!-- src: src/ui/JPCameraPanel.cpp (tabTools); src/ui/JPIcons.cpp; src/ui/JPIconButton.cpp; src/app/JPlacerMachine.cpp (buildCameras); JFramework include/j/core/DockWidget.h (addTitleWidget) -->

A camera runs while its picture is on screen and stops half a
second after it is not (another tab in front, the window minimised), so a camera nobody sees costs
nothing. A task using a camera brings its tab to the front, and keeps the camera running until it ends.

<!-- src: src/app/JPlacerMachine.cpp (buildCameras, bringForward); src/ui/JPCameraPanel.cpp (populateRenderPrimitives, stopIfHidden, kHiddenMs, setBusy) -->

A camera is found by the name the device gives itself (for example `top: top`), not by the USB socket
it is plugged into, so moving it to another socket or hub does not lose it. jplacer picks the largest
picture the camera offers in MJPG (or YUYV, if that is all it has), unless the cell names a format and
size.

With the eye lit (**as taken**), the picture is shown as the camera takes it; with it off, it is
**straightened**: the lens's bending is taken out and turns it square to the machine, at one scale both ways and centred on what
the camera looks at, so straight edges on the board look straight and what is drawn over the picture is
plain geometry. A camera must be calibrated to be straightened; until then it is shown as taken, and the
line over the picture says so. Straightened, a wide lens's picture no longer fills a rectangle: how much
of its bent edge shows is one of the camera's settings in [Machine Setup](machine-setup.md#settings),
from 0 *cropped* (enlarged until every part of it has picture behind it) to 100 *whole* (all the camera
sees, with bare edges where the bending was). Each camera keeps the eye's choice for next time. The
straightened picture is drawn by the graphics card where there is one, and by the processor where there
is not. jplacer measures on the picture as taken, through the lens's calibration, whichever is shown.

<!-- src: src/ui/JPCameraPanel.cpp (setView, refreshStraightening); src/ui/JPCameraView.cpp (the mesh); src/camera/JPStraightener.cpp; src/app/JPlacerSettings.cpp (cameraStraightKey); src/machine/JPCameraConfig.h (showAll) -->

With a camera on the head calibrated and the machine homed, **double-click** anywhere in its picture and
the camera moves to look there: the quickest way to put it over a fiducial or a part.

<!-- src: src/ui/JPCameraView.cpp (handleMousePress); src/app/JPlacerCameraTasks.cpp (onLookAtPixel, lookAt) -->

A camera can drop off its USB connection (noise from the stepper motors on its cable) or hang without
saying so. jplacer notices either (no picture for 3 seconds counts as hung), says so across the top of
the last picture, which would otherwise pass for a live one, and opens the camera again by its name every
2 seconds until it is back; the picture then carries on by itself.

<!-- src: src/camera/JPCameraFeed.cpp (run, runSource, kStalledMs, kReconnectMs); src/ui/JPCameraView.cpp (the band over the picture) -->

**Save the picture** (the disk) writes the camera's latest picture as a PNG (lossless, so it measures the same as
the live picture did) to `captures/` in jplacer's configuration folder, named after the camera and the
moment it was taken; the line over the picture names the file.

A camera only shows what is in front of it, and in an enclosed machine that is dark until its light is
on. While the machine is connected, a camera's light is on while the camera runs (its picture is on
screen) and off once none of the cameras it lights is running; while not connected, the line over the
picture says the light is off.

<!-- src: src/ui/JPCameraPanel.cpp (savePicture); src/camera/JPImageFile.cpp; src/ui/JPCameraView.cpp; src/camera/JPV4L2Source.cpp (found by name); src/camera/JPCaptureFactory.cpp (choose); src/app/JPlacerMachine.cpp (lightCameras) -->

#### Calibrating the head camera

jplacer measures its cameras itself: nothing is taken from another program. **Calibrate** (the target),
in the tab of the camera on the head, works out how big a pixel is on the machine in X and in Y, which way the
camera is turned (or mirrored), and how its lens bends the picture. The machine must be connected and
homed, and the head's homing mark (its place and diameter, brought across by an OpenPnP import) must be
set.

1. The camera moves over the homing mark.
2. It finds the mark at whatever size it appears (the scale is not known yet), checking that its edge
   is round nearly all the way round.
3. Three small moves show which way the mark goes in the picture; then the head carries the mark to
   7 by 5 places across the whole picture, out to as near its edges as leaves room for the mark, nearest
   the middle first, finding it at each. Towards the corners the mark can be too dim and bent to measure;
   such a place is skipped, as long as no more than one in five are.
4. A fit of the finds gives the camera's scale and turn, and the lens: a wide lens pulls the edges of
   the picture in (barrel), more so towards the corners, around a centre that on a small camera is rarely
   the middle of the picture. A find far from the fit (a glint taken for the mark, a missed step) is left
   out and the rest fitted again. If the finds still disagree with the fit by more than a pixel, or the
   mark measures far from its set diameter, nothing is kept and the panel says why.
5. The head goes back to where it started.

Every move arrives from the same side (see [Backlash](#backlash)), so play in the drives cannot creep
into the scale, and each picture measured is one taken after the move ended (a camera hands over
pictures a little late). The head moves at a tenth of its speed. While a task runs, its camera's buttons
are off (and another camera task will not start), and the line over the picture says what it is doing; when it ends, that line
and the status bar give the result. A calibration is saved in the cell file and used from then on:
whatever is measured in a picture is straightened through the lens first.

A calibration belongs to the picture size it was measured at: at another size a pixel is another size
on the machine, and the lens's bending lands elsewhere in the picture. A camera keeps one for each size,
and calibrating again at a size replaces only that one. A camera taking pictures at a size it has not
been calibrated at is shown as taken, and a task that measures with it says it is not calibrated for
that size.

<!-- src: src/tasks/JPCameraCalibrator.cpp; src/vision/JPCalibrationFit.cpp (fitWithLens); src/common/JPLens.h; src/tasks/JPCameraLook.cpp (calibration); src/machine/JPCameraConfig.h (calibrationFor, keepCalibration); src/app/JPlacerCameraTasks.cpp (calibrate, notReady, kTaskSpeed); src/ui/JPCameraPanel.cpp (setBusy, the note, refreshStraightening) -->

#### Calibrating a fixed camera

A camera fixed to the machine (one looking up at the nozzles) cannot be moved over a mark, so the mark
is moved over it: **Calibrate** (the target), in that camera's tab, holds a nozzle's tip over it. jplacer first asks,
naming the nozzle and the height it goes down to, as a nozzle going down near a camera must hold no part
and have nothing in its way. Then the head's Z comes up into its safe zone, the nozzle goes over the
camera's place and down to the camera's height (both from the camera's offset, where it is and the height
it is focused at), the calibration is made by moving the nozzle instead of the camera, and the nozzle
comes up again, whether it worked or not. The tip's size need not be known: the camera's rough scale (an
imported camera keeps OpenPnP's) is enough to start from.

A camera looking up sees the machine as a mirror image of one looking down; its turn and whether it is
mirrored are given against that, so a straight-mounted camera looking up reads as turned 0 and not
mirrored.

<!-- src: src/app/JPlacerCameraTasks.cpp (calibrateFixed); src/tasks/JPCameraCalibrator.cpp (Options::moving); src/machine/JPCell.cpp (safeZAndWait); src/machine/JPCameraCalibration.cpp (rotationDeg, mirrored) -->

#### Visual Test

**Visual Test** (the tick in a ring) moves a calibrated head camera to look where the head's settings say the homing mark is,
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

When a mark is not found in a picture and the camera has a light, jplacer tries harder before failing:
it takes one picture with the camera's light off and one with it on, and takes the first from the second.
What is left is only what the camera's light lights, whatever the sun or the room's lights are doing, and
the mark is looked for again there. The light is left on. The homing mark (Visual Test, visual homing)
and fiducials (Locate Board) are looked for this way.

<!-- src: src/vision/JPRoundMarkFinder.cpp (find, measureAt, edgeCircle, polarityMatches, findAnySize); src/tasks/JPCameraLook.cpp (lightOnly, findTryingHarder) -->
