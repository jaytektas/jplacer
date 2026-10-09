# Machine

jplacer drives a machine through a **cell**: one machine's description, kept in a cell file. It lists
the machine's controllers and how jplacer reaches them, its axes, its head with the nozzles, cameras and
actuators on it, and anything fixed to the machine itself. The cell you opened last is opened again when
jplacer starts.

Cell files live in `cells/` in jplacer's configuration folder (`~/.config/jplacer/cells` on Linux).

<!-- src: src/app/JPlacerMachine.cpp (cellsDir, openCell); src/app/JPlacerSettings.h (kMachineCell); src/machine/JPCellConfig.h (what a cell holds) -->

## The first start

As OpenPnP does, jplacer started with no machine yet brings in OpenPnP's own default machine: a simulated
controller, one nozzle, a camera looking down at OpenPnP's test picture of the table (`pnp-test.png`) and
one looking up, and the strip feeders laid out on that picture. Configuration files not there yet
(packages, parts, vision settings) start as OpenPnP's defaults too. Both travel with jplacer, as does the
picture: a machine.xml naming one of OpenPnP's own pictures (`classpath://samples/…`) is given that copy. OpenPnP's
sample job, `pnp-test.job.xml` with its board and panel, is put in the `samples/pnp-test` folder beside
jplacer's settings (`~/.config/jplacer`), to open with **File ▸ Open Job…**. The machine's cameras work
as OpenPnP's do with no calibration here: an ImageCamera, or a SimulatedUpCamera (which shows the nozzle
tip and the part on it, its body dark and its pads white, as the part is turned), at its Units Per Pixel.
A machine with no discard location has it at the origin, as OpenPnP's default.

The default machine is only brought in when there is no machine at all. With the settings not naming one
but machines already kept beside them (`cells`), jplacer opens the one changed last instead, and never
writes OpenPnP's default over one. Saving the settings never takes anything out of the file: every
setting it has is kept, and only what this run changed is written (a setting put back to its default is
the one thing taken out). A settings file that cannot be read is left exactly as it is, and nothing is
written to it that run. The settings are written whole or not at all, and kept before an update starts the
new version, so the new version always finds them.

<!-- src: src/app/JPlacerSettings.cpp (load); src/app/JPlacerApp.cpp (run); src/app/JPlacerMachine.cpp (startWithDefault); src/model/JPConfiguration.cpp (load, defaults); src/openpnp/JPOpenPnpMachineImporter.cpp (classpath pictures); openpnp-defaults/README.md; src/machine/JPCameraConfig.h (openPnpCalibration); src/app/JPlacerMachine.cpp (setExtras) -->

## Bringing in a machine set up in OpenPnP

If your machine already runs under OpenPnP, **Machine ▸ Import OpenPnP Machine…** reads OpenPnP's
`machine.xml` and makes a cell from it, so you do not set the machine up a second time. When OpenPnP's
file is where OpenPnP keeps it (`.openpnp2/machine.xml` in your home folder), jplacer asks whether to
import that one: **Import**, or **Choose Another File…** to pick a different `machine.xml`. In the file
dialog, tick **Show hidden** (or press Ctrl+H) to see folders whose names start with a dot, such as
`.openpnp2`. The new cell is saved as `cells/openpnp.json` and opened straight away. Importing again
replaces that file, except for what was set, taught or measured here, which is kept: the port each
controller was set to, each camera's calibrations and how much of a straightened picture it shows, the
squareness correction, each nozzle tip's loading and unloading steps, and which tip is on each nozzle
(OpenPnP's file says what it last believed; a hand may have changed it since).

What is brought across:

- **Controllers** that OpenPnP talks G-code to, with their serial port settings (port, speed, flow
  control, parity, data and stop bits, DTR / RTS, line endings), their maximum feed rate and G-code logging.
  An actuator with no controller of its own, or naming one the machine does not have, is the first
  controller's, as in OpenPnP. A G-code controller OpenPnP simulates (Communications **simulated**, or TCP to
  the address `GcodeServer`) is simulated here as OpenPnP does it: by its GcodeServer (see Simulation Mode in
  [Machine Setup](machine-setup.md#settings)). A linear transform axis brings its inputs and factors (OpenPnP
  writes them `input-axis-X-id`, `factor-x`, ...), so a non-squareness transform becomes the squareness
  correction.
  OpenPnP's **NullDriver** (its simulated controller) becomes jplacer's simulated one, a grblHAL: its
  axes given the letters a grblHAL of that many axes has (the first of X, Y, Z, A, B, C, U, V), X and Y by
  type, the rotations A, B, C, a Z the Z, the rest what is left (two nozzles: Z, A, B and C). An old `machine.xml` whose one controller is a `<driver>` NullDriver
  (OpenPnP's own default machine still is) is first brought up to date as OpenPnP does on loading it: an X
  and a Y axis for all, a Z and a rotation axis of its own for each nozzle (the rotation limited as the
  nozzle was, its old Safe Z the axis's safe zone), virtual ones for each camera, at the old feed rate
  (rotation ten times it) reached in half a second, and the head's homing fiducial at 5.736, 6.112 (the
  lower left fiducial of OpenPnP's test picture).
- **Axes**: those driven by a controller, those with no hardware behind them (such as a camera's Z),
  and those that follow another axis (such as two nozzles sharing one Z, one of them reversed).
- **The head**, its **nozzles** (with the actuator for each nozzle's vacuum, the nozzle tips that fit
  it and the one on it; a nozzle from an older OpenPnP naming only its vacuum actuator, or only its vacuum
  sense actuator, uses that one for both, as OpenPnP does on loading it), **cameras** (each by
  the name its device gives itself, with its light; an OpenCvCamera by its device index, `/dev/video<index>`,
  its OpenCV capture properties as the camera's settings; a Webcam by its name; MJPG, image, switcher, ONVIF
  and GStreamer cameras as they were; a SimulatedUpCamera as a simulated camera that sees the nozzle tips in
  Simulation Mode) and **actuators**, and the cameras and actuators fixed to the machine.
- **Actuator commands**: how each one is switched on and off, and how a value is read from it.
- **The home command**, every line of it, in order: a machine's homing sequence (release Z, home Y and X
  onto their switches, set the coordinates, home Z…) is the controller's own, and Home runs it.
- **The connect wait**: how long to listen after opening the port before asking anything.
- **Each camera's settings** (exposure, white balance, gain, focus, brightness, contrast and so on, set or
  automatic), which jplacer sets again every time it opens the camera: a camera that dropped off its
  connection comes back as it was, not on its own defaults.
- **Non-squareness**: a machine squared in OpenPnP (its X axis a linear transform adding a share of Y)
  keeps that correction, as jplacer's squareness. Any other linear transform axis comes in as a linear axis, its inputs,
  factors and offset as they were.
- **Nozzle tips**: each one's name, the diameter OpenPnP's nozzle tip calibration finds it by, as the
  diameter the camera looking up sees, and its tool changer: its places (start, middle, second middle,
  end, those set) and speeds, and the actuators switched between them, become the tip's
  [load steps](machine-setup.md#a-nozzle-tips-changer), unloading being loading backwards as in OpenPnP.
  A tip a nozzle's list still names after it was deleted in OpenPnP is left out.
- **Vision**: the bottom vision's settings (on or off, the vision settings parts use by default, passes)
  and the fiducial locator's (its vision settings, its tolerances), and the job processor's settings
  (see [Job Processors](machine-setup.md#job-processors)).
- **The head's places**: its homing fiducial and whether it homes visually, its park location (none set
  in OpenPnP: the origin, where OpenPnP parks it), the
  calibration rig's two fiducials (their places, heights and diameters) and test object, and its pump
  (which actuator, when it runs, how long it takes to come up).

When the import finishes, a message says what was brought in and lists anything to check: a part of
the OpenPnP set-up jplacer has no equivalent for yet (some axis types, a controller that is not G-code
or is reached over the network), or a command that uses something jplacer cannot fill in yet. Those
parts are left out or kept exactly as OpenPnP wrote them.

The machine's **feeders** are brought across too, onto the [Feeders](feeders.md) tab, each exactly as
OpenPnP wrote it; they take the place of the feeders jplacer had. Parts and packages are not imported
(copy OpenPnP's `parts.xml` and `packages.xml` into jplacer's configuration folder: the library takes those
it lacks), and neither is
OpenPnP's camera calibration: jplacer measures its cameras itself, and the import notes each camera that
had one.

<!-- src: src/openpnp/JPOpenPnpMachineImporter.cpp (what is read, translate, the notes, ReferenceLinearTransformAxis); src/app/JPlacerMachine.cpp (importOpenPnp, importFrom, kOpenPnpDir, kImportedCellFile, onImported); src/app/JPlacerOpenPnpTabs.cpp (onImported); src/model/JPConfiguration.cpp (importFeeders); src/machine/JPVisionConfig.h; JFramework include/j/platforms/FileDialogWindow.h (Show hidden, Ctrl+H) -->

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
3. sends the profile's start-up command, unless the controller reports it is in alarm (Grbl after a
   reset mid-move, an emergency stop, or a limit switch hit). A controller in alarm refuses G-code until
   it is unlocked, so it is connected all the same, the red **ALARM** strip shows, and the start-up
   command waits for **Home**;
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
be missed. In order, most pressing first: **ALARM** (a controller stopped on an alarm), **CONNECTION
LOST**, **FAILED** (a camera task, such as a calibration or the precise nozzle offsets, that failed: what
and why, until the next task starts) and **WAITING** (a wait on purpose while it lasts, such as the pump
coming up to pressure for its Pump On Wait).

<!-- src: src/machine/JPGcodeDriver.cpp (identify: nothing answered); src/machine/JPCell.cpp (onLost, onWaiting, doVacuumOn); src/app/JPlacerMachine.cpp (showState); src/app/JPlacerCameraTasks.cpp (run, onTaskOutcome); JFramework src/io/SerialPort.cpp (one owner) -->

### Choosing the port

USB serial ports are numbered in the order devices happen to start (`ttyACM0`, `ttyACM1`, …), so the
number a controller had yesterday — or in OpenPnP's configuration — can belong to a different device
today. For each controller on a serial port, the Machine panel has a **port** list showing the serial
devices plugged in now, by the name each device gives itself. Choosing one saves it in the cell, using
the device's permanent name (under `/dev/serial/by-id` on Linux), which stays the same whatever order
devices start in. While connected, the connection stays as it is: the port chosen is used the next
time you connect (see [Changes are used as you make them](machine-setup.md#changes-are-used-as-you-make-them)); Undo takes the choice back. A port the cell names but that is not plugged in
is listed as "(not found)".

<!-- src: src/app/JPlacerMachine.cpp (setPort, applySetup); src/machine/JPCell.cpp (connect); src/machine/JPGcodeDriver.cpp (connect, identify, readSettings); profiles/grblhal.json; profiles/grbl.json; profiles/generic.json; src/machine/JPSerialPorts.cpp (stable names); src/ui/JPMachinePanel.cpp (the port list) -->

### Firmware profiles

A firmware profile is a small file describing one kind of controller firmware: how to recognise it,
the commands it takes, how it replies, how it reports its position, and how its stored settings are
read. jplacer comes with profiles for grblHAL (including the JayTEK plugin's vacuum and analog
readings), Grbl 1.1, and generic G-code; and, as OpenPnP sets them up, for Smoothieware, Marlin,
RepRapFirmware (Duet) and TinyG: each known by its answer to M115 (its FIRMWARE_NAME), its position read
with M114, moves waited for with M400 (TinyG: its own `ok` and `err:` replies, G28.2 to home, G28.3 to set
its place). On a controller whose profile reads no position (generic G-code), the axes are where they were
last sent once a move is waited for, as OpenPnP's drivers keep them. A profile of your own, placed in `profiles/` in jplacer's
configuration folder, is used as well, and replaces a bundled one with the same `id`.

<!-- src: src/machine/JPFirmwareProfile.h; src/machine/JPFirmwareProfile.cpp (profileDirs, loadAll); profiles/smoothieware.json; profiles/marlin.json; profiles/reprapfirmware.json; profiles/tinyg.json ; src/machine/JPCell.cpp (reported) -->

## The machine's panels

While a cell is open, its panels sit in docks:

- the **cameras** top left, tabbed together;
- under them the machine controls, **Jog** and **Actuators**;
- the rest of the window, tabbed: [**Machine Setup**](machine-setup.md) and **Machine**;
- the **Console** across the bottom.

Every split between them can be dragged, as far as leaves the panels in the middle room for themselves.
Each is a dock like any other: drag its tab to another place, tear it out into a window of its own, or
stack it with others.

**View** has a tick for each panel. Untick one to close it (or close it with its tab's **×**); tick it
to bring it back, where it lives above, in front of the others there.

jplacer opens as it was last closed: the window's place and size (or maximized), every panel where you
left it (docked, tabbed, split, or in a window of its own, at its place and size), the tab in front of
each group, the splits' sizes, and the panels closed staying closed. A panel new since then (a camera
added) goes to its place above.

<!-- src: src/app/JPlacerLayout.cpp (save, restore); src/app/JPlacerApp.cpp (onCloseRequest); src/app/JPlacerSettings.h (kDockLayout, kClosedDocks, kWindowGeometry) -->

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
squareness correction, and how it homes. It changes as soon as a
calibration does.

<!-- src: src/ui/JPMachinePanel.cpp (refresh, refreshCalibration) -->

### Homing

Clicking the house (or **Machine ▸ Home All Axes**) sends each controller its home command and waits for it to
finish (a controller in alarm is first sent the profile's unlock, `$X` on Grbl, and the start-up command it
missed: homing is what makes its position known again), then tells the controller that every axis is at its home coordinate. Until the machine is homed
it will not move: before that its position means nothing, so its soft limits cannot protect it. A
machine imported from OpenPnP homes the way it did in OpenPnP, with the same command.

The switches put the head within a fraction of a millimetre. Where the head is set to home visually (an
imported head that did so in OpenPnP is), Home then finishes with the camera: the calibrated camera on
the head is brought to the front, looks at the homing mark, and the coordinates are corrected so the mark measures
exactly where the head's settings say it is (the **FIDUCIAL-HOME** part's footprint drawn on the camera
while it looks). As OpenPnP's, it looks again, up to the **FIDUCIAL-HOME**
part's fiducial vision settings' **Max Vision Passes** (3 to begin with), until a look corrects it by less
than their **Max Linear Offset** (0.2 mm to begin with); the last look's correction stands. With the camera's
**Auto-Tune when homing?** ticked, the camera is first tuned over the head's primary fiducial (see Machine
Setup). The status bar (and the log) says by how much it
corrected. Until a camera on the head is calibrated, Home says it homed
by the switches only. Should visual homing fail (the mark not found, the camera not tuned), the homing has
failed, as OpenPnP's: the machine is not homed (its coordinates are the switches', which the mark did not
confirm), Home turns red, and the status bar says why; the tips' recalibration, Machine.AfterHoming and the
park do not follow.

<!-- src: src/machine/JPGcodeDriver.cpp (connect, unlockForHoming); src/app/JPlacerCameraTasks.cpp (visualHome, showLookFootprint); src/tasks/JPVisualHoming.cpp (homeFootprint); src/machine/JPCell.cpp (doHome) -->

<!-- src: src/tasks/JPVisualHoming.cpp (run); src/app/JPlacerOpenPnpTabs.cpp (homeFiducialLook); src/app/JPlacerCameraTasks.cpp (visualHome); src/app/JPlacerMachine.cpp (onHomed, m_finishingHome); src/machine/JPCell.cpp (correctPosition, unhome) -->

The house is grey while the machine is not homed, an amber arc while it homes (the whole homing: the
switches, visual homing, the tips' recalibration and Machine.AfterHoming), and green once homed; a failed
home turns it red and the status bar says why. A red **ALARM** strip runs across the top of the
window when a controller has stopped on an alarm.

<!-- src: src/machine/JPCell.cpp (doHome); src/app/JPlacerMachine.cpp (showState); src/openpnp/JPOpenPnpMachineImporter.cpp (HOME_COMMAND, visual homing note) -->

#### Parking

**Machine ▸ Park Head** (once the machine is homed) takes the head out of the way: every Z axis on the
head comes up into its safe zone first, then the head goes to its park place (an imported head keeps
OpenPnP's), placed by its camera. A park place past a soft limit (often one is set right at the end of
travel) is gone to as near as the limit allows. It moves at half speed.

Once parked in X and Y (not a Z or rotation park), every camera's light is switched off, on screen or not,
and kept off until you next do something at a camera: a move to it (a jog, Position Tool, Move Selected
Nozzle to Camera, a click in its picture) or its light switched on by hand. A camera taking a picture for
vision still lights itself for it, as its Light settings say.

<!-- src: src/machine/JPCell.cpp (doPark, park, parkAndWait, onParked); src/app/JPlacerMachine.cpp (park, kParkSpeed, onParked, lightCameras, userActionLight); src/app/JPlacerMenuBuilder.cpp -->

#### Backlash

Every drive has a little play (backlash): an axis stops in a slightly different place depending on
which way it was travelling. Each axis has a **compensation method** (its Backlash Compensation tab in
Machine Setup):

| Method | |
|---|---|
| None | Where the play leaves it. |
| OneSidedPositioning | Every move ends the same way: to its target plus the offset first (its sign says which side), then in to the target at the speed factor, so the last stretch is always the same. The offset need only be at least the play. |
| OneSidedOptimizedPositioning | As OneSidedPositioning, but a move already arriving the right way goes straight in: fewer moves, its last stretch as long as the move. |
| DirectionalCompensation | A move travelling the way the offset points goes the offset further, taking up the play; the other way, it goes to the target. The offset must be the play itself. |
| DirectionalSneakUp | As DirectionalCompensation, the last **Sneak-up Distance** of each move made at the speed factor, so it cannot overshoot. |
| DistanceAware | jplacer's own, for a drive whose play keeps growing the further it goes (a gap taken up first, then a belt winding up). Calibrate measures the lag behind where the drive is sent against the distance travelled from fully wound the other way. jplacer keeps each drive's lag as it goes: a move starts from wherever the lag is on that curve (a short move the other way leaves it partly wound) and goes on along it by how far the drive travels, and is sent the lag it will have on arriving. A move that would come in less than the **Least Approach** along the curve (where the gap is taken up) first backs off that far, then comes in at the speed factor. |

With a directional offset taken up, the position shown is the axis's own, without the offset. An imported
machine keeps OpenPnP's method, offset, sneak-up distance and speed factor. Changing an axis's backlash,
speed or limits leaves the machine homed: only a change to where an axis is (its kind, controller, letter,
home coordinate or mapping) needs it homed again.

A nozzle tip changer's steps (loading and unloading) make no extra backlash moves: no going past a place and
back (one-sided), no stopping short to sneak up, no distance-aware back-off; a directional offset still
shifts where each step ends. Among the slots, a one-sided axis would carry the tip its offset past a slot's
point, into its wall. (OpenPnP compensates changer moves as any other; its own SpeedOverPrecision, which does
this, it uses only for a heap feeder.)

<!-- src: src/machine/JPAxisConfig.h (Backlash); src/machine/JPCell.cpp (doMove: overshoot, approach, applied; updatePositions; reconfigure; setSpeedOverPrecision); src/tasks/JPTipChanger.cpp (run); src/openpnp/JPOpenPnpMachineImporter.cpp (backlash) -->

**Calibrate** on an X or Y axis's Backlash Compensation tab measures its play with the camera that rides on
it, over the head's homing fiducial (the machine homed, the camera calibrated), compensation off while it
measures:

1. The mark measured several times standing still: three times how far those measurements wander (and at
   least 2 µm) is the **tolerance**. Each measurement, here and after, is the mean of 8 pictures: one
   picture alone wanders three times as much.
2. The play against how far the axis comes in from the other side, at a quarter speed: a short way in
   takes up only part of it; where it levels off is how far a move must sneak up.
3. The play against speed (25, 33, 50, 75 and 100%), coming in from 10 mm.
4. The method: None when the play is within the tolerance. When the play does not level off within 0.8 mm
   (it keeps growing with how far the axis came in, as a stretching belt does), two are tried on the same
   moves and the one that lands them closer together kept: OneSidedPositioning, its offset as far as the
   play takes to level off, at most 2 mm, and at least twice the play; and DistanceAware, its lag half the
   play measured for each distance (made never to fall as the distance grows), its least approach the
   first distance whose lag is no longer behind. Otherwise DirectionalCompensation when the play is the
   same at every speed, DirectionalSneakUp, sneaking up the distance found, when it is not.
5. Each method tried: moves in to the mark from random places either side, and from 10 mm on each side,
   each measured against the mean of them all: how well moves agree with each other, whatever the machine
   slowly drifts by over the run.

What it found is in use at once, kept through Machine Setup (a step to undo), and shown on the tab with
three graphs: the play against how far it came in, against speed, and the errors once compensated.

<!-- src: src/tasks/JPBacklashCalibrator.cpp (run, kLeastToleranceMm, kToleranceSpreads); src/tasks/JPBacklashCalibrator.h (Options, kSpeeds); src/app/JPlacerCameraTasks.cpp (calibrateBacklash); src/app/JPlacerMachine.cpp (setupAction); src/setup/JPSetupProperties.cpp (backlashResults) -->


### Jog

Moving the machine by hand, laid out as OpenPnP's Machine Controls. Choose the tool at the top: each
nozzle (with the tip on it), the camera on the head, and anything else on the head that moves on axes.

**Jog** tab:

- **X/Y**: the arrows move the tool by the distance; the park sign in the middle parks the head.
- **Z**: up and down by the distance; the park sign between takes the tool's Z to its safe Z (the low end
  of its Z axis's safe zone), and first every Z on the head into its safe zone, unless the machine's
  **Park all at Safe Z?** is unticked in [Machine Setup](machine-setup.md#settings).
- **C**: turns the tool either way by the distance (in degrees); the park sign between turns it to 0.
- Beside Z: put the nozzle where the camera is looking, and put the camera over the nozzle (the nozzle
  chosen, or the one chosen last when the camera is chosen). Both go up to safe Z first.
- **Distance** [mm, or degrees turning]: a slider of steps (0.01 to 100 a press, unless set otherwise in
  [Preferences, Jog](preferences.md#jog)).
- **Speed**: the machine's speed, as in OpenPnP: every move goes at this share of its own speed (a jog,
  a park, a camera task such as visual homing, a nozzle tip changer step). A changer step set to 1% with Speed at 5% goes at
  0.05% of the axes' speed.

The pad's buttons are as big as the dock lets them be, and follow it when it is resized. Homing is on
the toolbar.

#### Stopping a move

A move can be stopped while it is under way, from the Machine menu:

- **Stop** (Escape): each controller is told to hold, and slows to a stop on its own acceleration ramp,
  so no steps are lost. Only once it reports the hold complete (Grbl and grblHAL: `Hold:0`, at rest) is
  what was still queued thrown away, with a reset that, the machine at rest, keeps its position. The
  machine stays homed. The move, or the task it was part of, ends as stopped. Should a controller not come
  to rest within its command timeout, it is reset anyway; its position may then be lost, so the machine is
  no longer homed and the strip across the window says to home it again.
- **Emergency Stop** (Machine ▸ Emergency Stop; it has no key until you give it one, so a slip of the finger cannot reset the controllers): every controller is reset at once,
  mid-move. A motor stopped dead can lose its place, so the machine is no longer homed: home it before
  moving it again. Grbl and grblHAL also raise an alarm on a reset during a move.

Neither replaces the machine's own emergency stop switch: they are commands sent to the controller, and
need it to be listening. How a controller is held and reset comes from its firmware profile (its `stop`
section: the hold command, the state it reports while holding, the pattern of a status report once
the hold is complete, and the reset); Grbl and grblHAL have one.
A controller whose profile has no hold is reset for Stop as well; one with no reset cannot be stopped
from jplacer, and the status bar says so.

<!-- src: src/machine/JPGcodeDriver.cpp (halt, ioLoop, onPlaceLost); src/machine/JPCell.cpp (stop, onPlaceLost, parkZ, doParkZ); src/app/JPlacerMachine.cpp (stop); src/ui/JPJogPanel.cpp (act); src/app/JPlacerMenuBuilder.cpp (Stop, Emergency Stop); src/machine/JPFirmwareProfile.cpp (stop); profiles/grblhal.json; profiles/grbl.json -->

#### Nozzle tips

Beside the tool, for a nozzle, the **nozzle tip** button shows at a glance whether the tip on it is
calibrated there: **green** when it is (or its calibration is not enabled), **red** when its calibration is
enabled and it has not been calibrated on that nozzle; plain with no tip on it. Its tooltip says which, and
when it was calibrated. (OpenPnP shows this only on the tip's Calibration tab.) It opens its tip menu:

- what is on the nozzle now;
- **Load** each tip that fits it (one on another nozzle, or with no load steps, is shown but cannot be
  chosen). The tip on the nozzle is unloaded first, by its own unload steps, then the new one loaded by
  its load steps;
- **Unload** the tip on it;
- **Calibrate** the tip on it: its runout measured over the camera looking up, at once (greyed with no tip
  on the nozzle, or the machine not homed);
- **Step Through**: each changer step is shown, with its place and speed, and runs only once you say
  so; stop at any step. On by default, and kept for next time;
- **Manual Change**: first **Move to Manual Change Location**, the nozzle taken where its tip is changed
  by hand (up to safe Z, across and turned, then down to the location's Z, at the Speed slider's speed;
  greyed when the nozzle has no Manual Change Location, or the machine is not homed); then say which tip
  is on the nozzle, or none, when it was changed by hand (nothing moves) or a change was stopped;
- **Home Z**: the nozzle's Z homed alone (see [Homing a nozzle's Z](#homing-a-nozzles-z)).

The machine must be connected and homed. Each step's place is where the nozzle goes, in the axes' own
coordinates: the first move of a list comes in from safe Z (up, across, then down), the others go
straight, and the head ends at safe Z. Each move goes at its step's speed times the machine's speed (the
Speed slider). Once a tip is off (or on), the nozzle is recorded as having it (a step to undo in Machine
Setup); stopped or failed partway, nothing is assumed: look at the nozzle and say which tip is on it
(Manual Change).

#### Homing a nozzle's Z

A tip forced onto a nozzle can make its Z motor slip a step, leaving the nozzle at the wrong height. **Home
Z**, at the bottom of the nozzle's tip menu (or on the nozzle's Homing tab in Machine Setup), homes that Z
alone, without homing the whole machine:

1. every Z on the head goes up to safe Z, and the head goes to its [park place](#parking). The park place
   must be somewhere nothing is below the nozzles, as near the home switches as suits (within a
   centimetre or so is usual), because a Z that has slipped is not where its coordinates say;
2. the nozzle's **Home Command** is sent, line by line, to the controller of the motor behind its Z. It is
   G-code of your own (Machine Setup, the nozzle's Homing tab): typically the Z motor switched off and on
   again (as a full home starts), that axis homed on its switch, and its balance or offset set. On a
   grblHAL machine, for example: `M18 Z`, `G4 P1`, `M17 Z`, `$HZ`, `G92 Z-25.5`, `G0 Z0`;
3. once it has finished, the motor's axis is at its home coordinate, as after a full home.

Where nozzles share one Z motor (a see-saw head), homing either homes both, and the menu says so (**Home Z
(with RIGHT)**). It is shown greyed while the nozzle has no home command or the machine is not homed. Each
nozzle has its own command, so a machine with a motor per nozzle homes each on its own.

<!-- src: src/machine/JPCell.cpp (homeNozzle, doHomeNozzle, nozzlesHomedWith); src/machine/JPNozzleConfig.h (homeCommand); src/ui/JPJogPanel.cpp (showTipMenu, refreshTipButton); src/ui/JPIconButton.cpp (setTone); src/app/JPlacerMachine.cpp (homeNozzle); src/setup/JPSetupProperties.cpp (nozzleForm) -->

**Special** tab: **Head Safe Z** (every Z on the head up to safe Z), **Discard** (the nozzle's part to the
discard location: up, across, down, let go, up again), **Recycle**, and **Pick** and **Place** where the
nozzle is. **Recycle**, as OpenPnP's, puts the nozzle's part back into an enabled feeder that holds it and
can take it back, the nearest to the head's camera: a tape or tray feeder that has fed (its count taken
back), an auto feeder set to Recycle supported, a push-pull, Bamboo or Photon feeder (its next feed then
skipped), a loose part feeder where its part was found, or a heap (dropped back into the heap along its
three moves). It is greyed out when no feeder can, and offered again as soon as one can (a feeder enabled, say, or its count changed); the Feeder.BeforeTakeBack and Feeder.AfterTakeBack
scripting events run round it.

<!-- src: src/ui/JPJogPanel.cpp (specialPage, refreshRecycle); src/model/JPFeeder.cpp (canTakeBackPart, partTakenBack); src/tasks/JPFeederTakeBack.cpp; src/tasks/JPHeapFeeder.cpp (takeBack); src/app/JPlacerOpenPnpTabs.cpp (recycle) -->

**Safety** tab: **Board Protection**, as OpenPnP's, ticked each time jplacer starts. While it is, a jog is
refused (and the status line says which nozzle and which board) when it would leave a nozzle or an actuator
on the head below its safe Z within 1 mm of an enabled board of the job, seen from above, and not above the
board's surface: for a nozzle, 1 mm and half its tip's outside diameter, or, holding a part and with a Max.
Part Diameter larger than the tip, half that; a part held counts down from the nozzle by its height.

<!-- src: src/ui/JPJogPanel.cpp (safetyPage); src/app/JPlacerMachine.cpp (jogSafe); src/machine/JPCell.cpp (jog, inSafeZone) -->

Every button has a key, the same as OpenPnP's to start with (Machine ▸ Jog lists them, and a button's
tooltip says its key); [Preferences, Keys](preferences.md#keys) changes them, and gives keys to the
distance and speed steps, which [Preferences, Jog](preferences.md#jog) sets. The tool, distance and speed
are kept for next time. A nozzle's Z is its own even where two nozzles share one motor: jplacer works
out which way the motor turns. A move that would take an axis outside its soft limits is not made, and
the panel says why.

A nozzle with a vacuum actuator picks and places where it is (nothing moves):

- **Pick**: the head's vacuum pump on, as its Pump Control says (waiting the pump-on time when it starts),
  then the nozzle's vacuum on, then the pick dwell (the nozzle's and its tip's together).
- **Place**: the vacuum off (unless the blow-off closes the valve itself), the blow-off on for the place
  dwell and off again, and the pump off when its control is PartOn or TaskDuration and no other nozzle on
  the head holds a part.

With the tip's Part Detection set, Pick checks a part is on (the vacuum read after the dwell) and Place
checks it is off (the valve opened for the probing time, closed for the dwell, then read); a check that
fails is shown in the strip across the window, with the reading.

<!-- src: src/ui/JPJogPanel.cpp (showTipMenu); src/ui/JPVerticalSlider.cpp; src/app/JPlacerTipChanges.cpp; src/tasks/JPTipChanger.cpp; src/app/JPlacerMenuBuilder.cpp (Jog); src/app/JPlacerSettings.h (kJogTool); src/app/JPKeyMap.cpp; src/machine/JPCell.cpp (jog, doMove, park, safeZ, discard, doPick, doPlace, sensed) -->

### Actuators

Each actuator with **On** and **Off** if it can be switched, a box and **Set** if it takes a value (a
Double or String actuator with a Set Value command: type the value, then Return, Tab, leaving the box or
Set sends it), and **Read** if a value can be read from it. The result of the last action, or the reason it
failed, is shown beside the buttons. An imported Double or String actuator keeps OpenPnP's command for
setting it and its default on and off values.

A **Profile** actuator (OpenPnP's actuator profiles, set up on its **Profiles** tab in Machine Setup) has a
list of its profiles: choose one and each of the actuators it sets is set to its value in that profile.
**On** and **Off** take its Default ON and Default OFF profiles.

<!-- src: src/ui/JPActuatorPanel.cpp; src/machine/JPCell.cpp (setActuator, doSwitch, doSet, doProfile); src/openpnp/JPOpenPnpMachineImporter.cpp (ACTUATE_DOUBLE_COMMAND) -->

### Console

What is sent to and received from the controllers (position reports are left out), and what jplacer's log
says, newest at the bottom. It follows each new line while it is scrolled to the end; scroll back and it
stays where you put it, until you scroll to the end again. The lines are one text, to copy from: drag over
them, or Ctrl+A for all of it, then Ctrl+C; right-click for **Copy**, **Select All** and **Clear**. It keeps
the last 1000 lines. Type a line in the box and press **Send** or
Return to send it, as OpenPnP's G-code console does: with **Force Upper Case** (ticked to begin with) in
capitals, as most controllers want; Up and Down go back through the last 50 lines sent. With more than one
controller, choose which one from the list beside the box.

Over the lines, what is shown:

- **G-code**: the controllers' traffic, on or off.
- **Log**: how much the log says, every category: Off, Errors, Warnings, Info (what was done and what came
  of it, to begin with), Debug (the steps in between) or Trace (every event, many a second). Each log line
  starts with the time it came (to the millisecond) and its level and category, as in the log file:
  `14:03:56.926 [INFO][machine.cell] ...`; a controller's traffic with `[GCODE]` and the controller's name,
  an arrow saying which way it went: `14:03:56.930 [GCODE][Jaytek] → G1 X10`. The traffic is written to the
  log file too (at Info, without the status reports), so the log holds what the console showed.
- **Categories**: a menu of the log's categories (`machine.cell`, `camera`, and so on), each **As Log** or
  at a level of its own: turn one part up (the controllers, `machine.driver`) without the rest, or one
  that is too busy down. **All as Log** puts them all back.
- **Clear** empties the console.

Each choice applies to the lines already there as well as to those to come: choose **Errors** and only the
errors stay; untick **G-code** and the traffic goes (ticked again, it is back, with what passed
meanwhile). A line quieter than the level in force when it was written (a Debug line while the log said
Info) was never logged, so turning the level up later does not bring it back.

The levels are the log's own, so they set what goes into the log file as well. They are kept for next
time; `--verbose`, `--quiet` or `--trace <category>` on the command line go over them for that run.

<!-- src: src/ui/JPConsolePanel.cpp; src/ui/JPHistoryLineEdit.cpp; src/ui/JPMenuButton.cpp; src/common/JPLogLevels.cpp; src/common/JPlacerLog.h (all); src/app/JPlacerMachine.cpp (the console's settings); src/app/JPlacerApp.cpp (applied at start); src/main.cpp (parseArgs); src/machine/JPGcodeDriver.cpp (status lines are not passed on as traffic) -->

### Cameras

The cell's cameras sit top left in the window, each in a dock of its own, named after the camera and
tabbed together to start with. Click a camera's tab to see it, or arrange them like any other dock: side
by side, or torn out into a window of its own. A camera's live picture is fitted to its dock with its
shape kept, and a cross through the middle marks the point the camera is looking at; along the top are
the picture's format, size and rate.

A camera's tools are icons in its tab, beside the close button, while it is the front tab (hover over
one to see what it does). Throughout jplacer, an icon with a small down-triangle in its corner opens a
menu, and one with "…" there takes you somewhere else (the gear, to Machine Setup); one with neither acts
at once:

| Icon | |
|---|---|
| eye | **As taken**: lit, the picture is shown as the camera gives it; off (to begin with), it is straightened (below). The choice is kept for each camera. |
| disk | **Save the picture** (below). |
| target | **Calibrate** the camera (below). |
| tick in a ring | **Visual test** of the calibration (below). |
| half-filled ring with rays | **Auto-Tune** now, on what the camera sees: as Machine Setup's **Defaults, then Auto-Tune**, with the camera's light on (the machine must be on), its properties kept (one step to undo). Not while a job runs. |
| gear | **The camera's settings**: Machine Setup, with the camera chosen in its tree. |

<!-- src: src/ui/JPCameraPanel.cpp (tabTools); src/ui/JPIcons.cpp; src/ui/JPIconButton.cpp (setLeads); src/app/JPlacerMachine.cpp (buildCameras); JFramework include/j/core/DockWidget.h (addTitleWidget) -->

A camera is opened only while the machine is on (connected), and closed, and let go of for other programs,
once it is off: until then its picture says "the machine is off". While the machine is on, a camera runs
while its picture is on screen and stops half a
second after it is not (another tab in front, the window minimised), so a camera nobody sees costs
nothing. A task using a camera brings its tab to the front, and keeps the camera running until it ends.
A job's vision (fiducials, feeders, bottom vision) keeps its camera running, picture on screen or not, and
leaves the tabs as they are, as OpenPnP's: a camera's tab comes to the front for the result vision shows on
it only when the camera has **Auto Camera View?** (Machine Setup, the camera's General Configuration).

<!-- src: src/app/JPlacerMachine.cpp (buildCameras, bringForward, lookWith, visionResultView); src/app/JPlacerJobHost.cpp (showPicture); src/ui/JPCameraPanel.cpp (populateRenderPrimitives, stopIfHidden, kHiddenMs, setBusy, setPowered); src/app/JPlacerMachine.cpp (updateMenu) -->

A camera is found by the name the device gives itself (for example `top: top`), not by the USB socket
it is plugged into, so moving it to another socket or hub does not lose it. jplacer picks the largest
picture the camera offers in MJPG (or YUYV, if that is all it has), unless the cell names a format and
size.

With the eye lit (**as taken**), the picture is shown as the camera takes it; with it off, it is
**straightened**: the lens's bending is taken out and turns it square to the machine, at one scale both ways and centred on what
the camera looks at, so straight edges on the board look straight and what is drawn over the picture is
plain geometry. A camera must be calibrated to be straightened; until then it is shown as taken, and the
status bar and the log say so. Straightened, a wide lens's picture no longer fills a rectangle: how much
of its bent edge shows is one of the camera's settings in [Machine Setup](machine-setup.md#settings),
from 0 *cropped* (enlarged until every part of it has picture behind it) to 100 *whole* (all the camera
sees, with bare edges where the bending was). Each camera keeps the eye's choice for next time. The
straightened picture is drawn by the graphics card where there is one, and by the processor where there
is not. Whichever is shown, vision pipelines are given the straightened picture (see [Pipeline Editor](pipeline-editor.md#the-picture-it-is-given)).

<!-- src: src/ui/JPCameraPanel.cpp (setView, refreshStraightening); src/ui/JPCameraView.cpp (the mesh); src/camera/JPStraightener.cpp; src/tasks/JPPipelineCamera.cpp; src/app/JPlacerSettings.cpp (cameraStraightKey); src/machine/JPCameraConfig.h (showAll) -->

With a camera on the head calibrated and the machine homed, **double-click** anywhere in its picture, or
**Shift+click** it, and the camera moves to look there: the quickest way to put it over a fiducial or a
part. Or **drag** in the picture: a line from the cross to the pointer shows the move, which is made when
the button is let go; let go outside the picture and nothing moves.

In a fixed camera's picture (one looking up) the same moves the **nozzle** instead, as OpenPnP's: the point
clicked or dropped on comes to the middle of the picture. That's the nozzle chosen in Jog, or, while a camera
calibration waits on you (jogging the tip into the green circle), the nozzle being calibrated. It goes
straight there within the camera's **Roaming Radius**, else by way of safe Z, back at the same height. The
distance comes from the camera's calibration, or, before it has one, from its **Units Per Pixel**, the picture
taken as seen from above with Y up (OpenPnP's): on a camera whose picture is mirrored or turned, the nozzle
then moves the other way, until the camera is calibrated.

<!-- src: src/ui/JPCameraView.cpp (handleMousePress, handleMouseRelease, lookAt); src/app/JPlacerCameraTasks.cpp (onLookAtPixel, lookAt, lookAtFixed, askOperator) -->

OpenPnP's **rotation handle** is the circle on a ring about the middle of the picture, at the angle it
turns now (straight up at 0°, counter-clockwise). Drag it round the ring: the ring, where it would turn to
and a cross turned with it are drawn; let go and it turns there, at safe Z first (hold **Alt** to snap to
the nearest 45°). It turns what the footprint on that camera turns with: a camera with a rotation axis
(one on the head), the camera; one without (looking up at the nozzles), the tool chosen in Jog. Without
either, there is no handle.

<!-- src: src/ui/JPCameraView.cpp (rotationRing, ringRotation, handleMousePress, handleMouseRelease); src/app/JPlacerMachine.cpp (rotationMount, reticleRotation, rotateFor) -->

While a page asks for a **selection** on the head camera's picture (a drag feeder's template image or
area of interest), a rectangle with a handle at each corner is drawn over it, its size in pixels by it.
Drag inside it to move it, drag a corner to resize it, or drag anywhere else to draw a new one; the camera
does not move meanwhile. The page's **Confirm** takes it, **Cancel** puts it away.

<!-- src: src/ui/JPCameraView.cpp (setSelectionEnabled, handleMousePress, dragSelection, captureSelection); src/ui/JPFeedersPanel.cpp (selectOnCamera) -->

Turn the **mouse wheel** over a camera's picture to zoom in or out, up to 64 times, about the middle, so
the cross stays on the point the camera is looking at. How much a notch zooms is the picture menu's **Zoom
Sensitivity**, as OpenPnP's: **High**, each notch doubling it; **Medium** (to begin with), two notches
doubling it; **Low**, four. Each camera keeps its own. The zoom shows in the bottom
corner while it is more than fitted, and turning back down stops at fitted. Moving to a point in a zoomed
picture works as it does fitted.

How the picture is drawn is the picture menu's **Rendering Quality**, as OpenPnP's: **Low Quality** (to begin
with), each of the camera's pixels a sharp-edged block when zoomed in; **High Quality**, smoothed; **Highest
Quality (best scale)**, smoothed and drawn only at a whole number of screen pixels to each of the camera's (or of
the camera's to each screen pixel), the wheel then zooming by two at least each notch. Each camera keeps its own.

<!-- src: src/ui/JPCameraView.cpp (handleScroll, zoomPerNotch, kMostZoom, setRenderingQuality, upload, populateRenderPrimitives); src/app/JPlacerSettings.cpp (cameraZoomKey, cameraRenderingKey) -->

**Right-click** a camera's picture (docked, or in a window of its own) to choose its **reticle**, what is drawn over the picture to measure by:

| Reticle | |
|---|---|
| None | Nothing at all, not even the cross. |
| Cross | The cross through the middle, as to begin with. |
| Grid | The cross, and lines every **Spacing** apart. |
| Ruler | The cross, and along the machine's X and Y a mark every **Spacing**, longer at every fifth and every tenth. |
| Circle | The cross, and a circle **Size** across: a fiducial's size, say. |
| Square | The cross, and a square **Size** along each side. |

The grid, ruler, circle and square are in millimetres on the machine, drawn through the camera's
calibration: along the machine's axes however the camera is turned, and bent as the lens bends the
picture when it is shown as taken. A camera not calibrated for its picture size draws only the cross,
and the menu says to calibrate it. Lines that would be closer together on screen than three spacings of
the interface are left out (every second, fifth, tenth and so on is drawn), so a fine grid shows in
full once zoomed in. Each camera keeps its reticle for next time. **Fit the Picture** in the same menu
undoes the zoom.

Reticles and footprints are drawn in millimetres at the height the camera looks at, as OpenPnP's: a camera
with a Z axis (one on the head) put below its safe Z, as moving it to a placement puts it at the board's
surface, looks at that height; otherwise at its **Default Working Plane Z** (else the height it was
calibrated at). Calibrated at two heights, they are drawn at the scale there, nearer things larger.

<!-- src: src/ui/JPReticle.cpp (spacings, sizes, draw, thinned); src/ui/JPCameraView.cpp (buildMenu, prepareContextMenu, kLeastGap, viewingPlaneZ); src/app/JPlacerSettings.cpp (cameraReticleKey); src/app/JPlacerMachine.cpp (viewingPlaneZ) -->

**Show Image Info**, in the same menu, puts a box at the picture's top left, as OpenPnP's does: the picture's
**Resolution**, the **Zoom**, the pictures a second (**FPS**, over the last 24) and a **Histogram** of its
red, green and blue levels, each smoothed and scaled to the tallest (the two most extreme levels, usually
saturated, left out), light where all three overlap.

A camera with a light (Machine Setup, its Light actuator) has OpenPnP's light toggle, a sun at the picture's
top right: bright while the light is on, dim while it is off (or not known: not connected). Click it to
switch the light the other way.

A camera calibrated at two heights has **Estimate Z Coordinate of Object** first in its menu, as OpenPnP's:
instructions over its picture say what to do. Jog the camera (for a fixed camera, the nozzle holding the
object) so a sharp feature of the object is in view, towards the edge, and click it; jog again so the same
feature is elsewhere in view, the farther the better, and click it again. How far it seemed to move
against how far the camera did is how big it looks, and so how far it is from the camera: its Z is said.
**Again** measures another; **Cancel** ends it.

A camera fixed to the machine (looking up) has **Move Selected Nozzle to Camera** first in its menu: the
nozzle chosen on the Jog panel goes, by way of safe Z, over the camera at its focal plane (its place in
Machine Setup), its rotation kept.

<!-- src: src/ui/JPCameraView.cpp (drawImageInfo, drawLightToggle, setLight, handleMouseRelease, kFpsPictures, onMoveNozzleHere); src/app/JPlacerMachine.cpp (showLight, toggleLight, moveNozzleToCamera); src/app/JPlacerEstimateZ.cpp; src/machine/JPCameraCalibration.cpp (estimateObjectZ); src/ui/JPCameraPanel.cpp (showInstructions) -->

A camera can drop off its USB connection (noise from the stepper motors on its cable) or hang without
saying so. jplacer notices either (no picture for a while counts as hung, and so does the very same
picture over and over, which is how some cameras hang), says so across the top of the last picture, which
would otherwise pass for a live one, and opens the camera again by its name every 2 seconds until it is
back; the picture then carries on by itself. Unplugging the camera and plugging it in again is enough.

Anything that is looking through a camera when it is lost (a calibration, a settle test, finding a
fiducial) waits a little for it rather than failing at once: the log says the camera is lost and why, and
the work carries on from the same step if it comes back in time. If it does not, the work stops and says
the camera was lost (and to plug it in again), not that nothing was found.

How long each of these is, is the camera's own, in Machine Setup on its General Configuration tab under
**When the Camera Is Lost**:

| Setting | Default | What it does |
|---|---|---|
| No Picture For (s) | 3 | No picture this long: the camera has hung or dropped off. |
| Same Picture For (s) | 3 | The very same picture this long: hung. 0 never counts it, for a camera that can show a still scene exactly alike. |
| Work Waits For It (s) | 10 | How long work looking through a lost camera waits for it to be back, long enough for a USB drop-out. 0 stops at once. |

<!-- src: src/camera/JPCameraFeed.cpp (run, runSource, kReconnectMs); src/ui/JPCameraView.cpp (the band over the picture); src/tasks/JPCameraLook.cpp (taken); src/machine/JPCameraConfig.h (Lost); src/setup/JPSetupProperties.cpp (cameraForm) -->

**Save the picture** (the disk) writes the camera's latest picture as a PNG (lossless, so it measures the same as
the live picture did) to `captures/` in jplacer's configuration folder, named after the camera and the
moment it was taken; the status bar names the file. A camera that is not running (its picture
hidden behind another tab, say) is started first, its light as for you to look at, and a fresh picture saved
once it has given ten; not its last one, from when it stopped.

A camera only shows what is in front of it, and in an enclosed machine that is dark until its light is
on. While the machine is connected, a camera's light is on while the camera runs (its picture is on
screen) and off once none of the cameras it lights is running; while not connected, the line over the
picture says the light is off.

<!-- src: src/ui/JPCameraPanel.cpp (savePicture); src/camera/JPImageFile.cpp; src/ui/JPCameraView.cpp; src/camera/JPV4L2Source.cpp (found by name); src/camera/JPCaptureFactory.cpp (choose); src/app/JPlacerMachine.cpp (lightCameras) -->

#### Calibrating the head camera

jplacer measures its cameras itself: nothing is taken from another program. **Calibrate** (the target),
in the tab of the camera on the head, works out how big a pixel is on the machine in X and in Y, which way the
camera is turned (or mirrored), and how its lens bends the picture. The machine must be connected and
homed. With the head's homing mark set (its place and diameter, brought across by an OpenPnP import) it
calibrates over that; on a new machine, with none set, it calibrates over the mark the camera is over now
(jog it there first), as Issues & Solutions' first vision step does (see below).

A camera view with no picture (the camera off, or giving none) shows OpenPnP's capture error picture in its
place: dark grey, the picture's shape, with a thick red X in its top left corner. A live picture from a camera
not calibrated for its size gets the same red X in its top left, in proportion, and says so along the top (the
camera's **Warn if camera calibration is not completed**); not while a task is taking that camera's pictures
(the camera's own calibration needs it uncalibrated, and the X would come and go between the pictures it shows).

While it runs, the status bar says which pass of how many (two when it measures at two heights)
and which move of how many ("pass 1 of 2, measuring, move 14 of 38"), and **Cancel** (OpenPnP's red X,
beside Calibrate, greyed but while a task runs on that camera) stops it before its next move: the move under
way ends where it was going, up to safe Z still goes, and it is said to be cancelled, not failed. Closing jplacer
while it runs cancels it the same way, then closes. The view
shows each find as it comes: the picture with a green circle and cross where
the mark (or, for a camera looking up, the nozzle's tip) was found, the size it was found, and which move of
how many ("measuring, move 14 of 38"), so a wrong find shows at once.

1. The camera moves over the head's calibration rig's primary mark, as OpenPnP calibrates (Machine Setup, the
   head's Calibration Rig), and the calibration is at that mark's height; without one, over the homing mark at its
   height.
2. It finds the mark at whatever size it appears (the scale is not known yet), checking that its edge
   is round nearly all the way round. Neither the mark's diameter nor the camera's rough scale known, the
   head is first moved a little along X, twice as far each time, until the mark moves clearly in the
   picture: how far it moved gives a first scale.
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
pictures a little late). The head moves at the machine's speed (the Jog panel's **Speed**), as jogs and parks do. While a task runs, its camera's buttons
are off (and another camera task will not start), and the status bar says what it is doing, each step logged too; when
it ends, the status bar gives the result. A camera's panel is its picture: what it says goes to the status bar and the
log, its room left to the picture. A calibration is saved in the cell file and used from then on:
whatever is measured in a picture is straightened through the lens first.

A calibration belongs to the picture size it was measured at: at another size a pixel is another size
on the machine, and the lens's bending lands elsewhere in the picture. A camera keeps one for each size,
and calibrating again at a size replaces only that one. A camera taking pictures at a size it has not
been calibrated at is shown as taken, and a task that measures with it says it is not calibrated for
that size.

<!-- src: src/tasks/JPCameraCalibrator.cpp; src/vision/JPCalibrationFit.cpp (fitWithLens); src/common/JPLens.h; src/tasks/JPCameraLook.cpp (calibration); src/machine/JPCameraConfig.h (calibrationFor, keepCalibration); src/app/JPlacerCameraTasks.cpp (calibrate, notReady, kTaskSpeed, run); src/ui/JPCameraPanel.cpp (setBusy, the note, refreshStraightening, m_cancelTask); src/ui/JPCameraView.cpp (errorCross, setTaskUnderway); src/tasks/JPCameraCalibrator.h (pass); src/app/JPlacerMachine.cpp (~JPlacerMachine); src/machine/JPCell.cpp (setCancelled, waitFor, moveAxesAndWait) -->

When the head has a **secondary calibration mark** (Machine Setup, the head's Calibration Rig, brought
across from OpenPnP's calibration rig) at least 1 mm higher or lower than the primary mark, the camera is
measured again over it. A camera's scale goes as one over its distance from what it looks at, so the two
scales give where the camera's centre of projection is, its focal length, its field of view in degrees, and
the scale at any height; the camera's Advanced Calibration tab shows them. Two scales less than 0.1% apart
tell nothing, and are not used. Should the second measuring fail, the first is kept and the result says why.
How the camera is measured, and whether at two heights, is set on that tab.

<!-- src: src/app/JPlacerCameraTasks.cpp (calibrate, secondHeight, kLeastHeightGapMm); src/machine/JPCameraCalibration.cpp (twoHeights, cameraZ, focalPx, scaleAt); src/machine/JPCameraCalibration.h (kLeastScaleChange) -->

#### Calibrating a fixed camera

A camera fixed to the machine (one looking up at the nozzles) cannot be moved over a mark, so the mark is
moved over it: **Calibrate** (the target), in that camera's tab, holds a nozzle's tip over it, step by step
as OpenPnP's camera calibration. Each step waiting on you says what to do on a line above the picture,
folded to the panel's width and there only while it waits; **Next** is the green start beside Calibrate in the camera's title
strip (its tooltip OpenPnP's whole wording), and the red X beside it cancels, at any step. Nothing is laid over
the picture or takes room from it:

1. "Select a nozzle and load it with the smallest available nozzle tip." The nozzle chosen in Jog when you
   press **Next** is the one used (as OpenPnP's selected nozzle); none chosen, the first on the head.
2. The head's Z comes up into its safe zone and the nozzle goes over the camera's place; a **green circle**
   in the middle of the picture (an eighth of its smaller side across). Jog the tip into it, then **Next**.
3. The tip goes down to the camera's height (the camera's offset: where it is focused). Turn it through 360
   degrees (Jog) and see it stays in the circle, jogging it if not, then **Next**.
4. **Detection Diameter**: a field under the step's line; the tip is looked for at that size, all the while,
   about the middle: a **red** circle that size where it is looked for, **green with a +** where it is found;
   beside the field, "found" or "not found" (pointed at, where, or why not; the log says each change). It starts at the tip's size (its runout **Vision Diameter**, else its
   **Diameter**, through the camera's rough scale; else 25 px, as OpenPnP's): set it until the circle is green
   and just fits the tip, then **Next**. Only that size is taken, so the nozzle's base round the tip, the
   bigger round thing, is not.
5. The moves, by themselves: the calibration is made by moving the nozzle instead of the camera.

At the second height (the camera's **Calibrating** settings: two heights, the tip raised), steps 3 to 5 again,
from where the tip was jogged. Then the nozzle comes up, whether it worked or not. Measured more than a fifth
off its size, the tip stops the calibration, which says so (the tip, or its size setting, is wrong).

It goes on, as one calibration and without asking again, when the tip on that nozzle has its calibration
enabled (its **Enable?**): the tip's **runout** is measured over the camera (the camera's scale now known),
then **Calibrate Camera Position and Rotation** sends the tip round a circle and sets the camera's true
position and turn, about the nozzle's axis rather than the tip's end. The first step's position is the
tip's, off the axis by the runout at the angle the nozzle held; the last step takes that out. With the
tip's calibration not enabled it stops after the first, saying the position is the tip's. With the camera's
**Auto-Tune when calibrating?**, it tunes on the tip first. Calibrate the nozzle's offsets (with the top
camera) before this: the camera's position is found in that nozzle's terms.

A camera looking up sees the machine as a mirror image of one looking down; its turn and whether it is
mirrored are given against that, so a straight-mounted camera looking up reads as turned 0 and not
mirrored.

<!-- src: src/app/JPlacerCameraTasks.cpp (calibrateFixed, calibrateFixedWith, askOperator, onFixedCalibrated, calibrateRunoutCamera); src/ui/JPCameraPanel.cpp (askStep, endStep, showStepNumber, setStepNumberLabel); src/ui/JPCameraView.cpp (setMarks); src/app/JPlacerMachine.cpp (onFixedCalibrated, calibrateCameraPosition); src/tasks/JPCameraCalibrator.cpp (Options::moving); src/machine/JPCell.cpp (safeZAndWait); src/machine/JPCameraCalibration.cpp (rotationDeg, mirrored) -->

#### Visual Test

**Visual Test** (the tick in a ring) moves a calibrated head camera to look where the head's settings say the homing mark is,
finds the mark, and says how far it really is from there, in mm in X and Y. As OpenPnP's visual homing, the
mark is the **FIDUCIAL-HOME** part: its size is its package's pad, and it is found by that part's fiducial
vision settings' pipeline (the Fiducial Locator's; with no fiducial vision settings, the stock one), on the
camera's picture in colour and straightened, as OpenPnP's pipelines are given it, up to the Fiducial Locator's
**Max. Distance** (4 mm to begin with) from where it should be, as OpenPnP's visual homing allows; without the part, "Visual homing is missing the FIDUCIAL-HOME part. Please create it."
Visual homing finds it the same way. Nothing is changed: right
after a visual home it reads within a few hundredths of a millimetre, and any time later it shows whether
the machine has lost its place.

<!-- src: src/tasks/JPVisualTest.cpp; src/app/JPlacerCameraTasks.cpp (visualTest) ; src/tasks/JPVisualTest.cpp (run, Look); src/tasks/JPVisualHoming.cpp (homeLook); src/app/JPlacerOpenPnpTabs.cpp (homeFiducialLook) -->

#### Finding round marks

Every round mark is found by an OpenPnP pipeline, as OpenPnP finds it, on the camera's picture in colour,
straightened where the camera is calibrated: a fiducial and the homing mark by their fiducial vision settings'
pipeline, a nozzle's tip by its nozzle tip calibration pipeline, and a camera's calibration mark (camera
calibration, backlash calibration, the homing mark's capture) by the camera's calibration pipeline, OpenPnP's
DetectCircularSymmetry to begin with. jplacer does not measure again what a pipeline found: DetectCircularSymmetry
finds a centre to an eighth of a pixel. A pipeline is told where the mark should be, how far from there to look
and, where it is known, the mark's size in pixels.

Where a calibration knows the mark's size only roughly (its size in millimetres at the camera's rough scale), the
camera's calibration pipeline is run at every size about it and the most symmetrical mark taken, as a size a fifth
out finds the mark's edge off centre.

<!-- src: src/tasks/JPPipelineMarkFinder.cpp (find, findAnySize, onMachine); src/tasks/JPCameraCalibrator.cpp; src/tasks/JPBacklashCalibrator.cpp; src/pipeline/JPDefaultPipelines.cpp (cameraCalibration) -->
