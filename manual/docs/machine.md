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
picture: a machine.xml naming one of OpenPnP's own pictures (`classpath://samples/…`) is given that copy.

<!-- src: src/app/JPlacerMachine.cpp (startWithDefault); src/model/JPConfiguration.cpp (load, defaults); src/openpnp/JPOpenPnpMachineImporter.cpp (classpath pictures); openpnp-defaults/README.md -->

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
  OpenPnP's **NullDriver** (its simulated controller) becomes jplacer's simulated one, its axes given letters
  (X and Y, then Z, A, B, C, U, V, W). An old `machine.xml` whose one controller is a `<driver>` NullDriver
  (OpenPnP's own default machine still is) is first brought up to date as OpenPnP does on loading it: an X
  and a Y axis for all, a Z and a rotation axis of its own for each nozzle (the rotation limited as the
  nozzle was, its old Safe Z the axis's safe zone), virtual ones for each camera, at the old feed rate
  (rotation ten times it) reached in half a second, and the head's homing fiducial at 5.736, 6.112 (the
  lower left fiducial of OpenPnP's test picture).
- **Axes**: those driven by a controller, those with no hardware behind them (such as a camera's Z),
  and those that follow another axis (such as two nozzles sharing one Z, one of them reversed).
- **The head**, its **nozzles** (with the actuator for each nozzle's vacuum, the nozzle tips that fit
  it and the one on it), **cameras** (each by
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
  keeps that correction, as jplacer's squareness.
- **Nozzle tips**: each one's name, the diameter OpenPnP's nozzle tip calibration finds it by, as the
  diameter the camera looking up sees, and its tool changer: its places (start, middle, second middle,
  end, those set) and speeds, and the actuators switched between them, become the tip's
  [load steps](machine-setup.md#a-nozzle-tips-changer), unloading being loading backwards as in OpenPnP.
  A tip a nozzle's list still names after it was deleted in OpenPnP is left out.
- **Vision**: the bottom vision's settings (on or off, the vision settings parts use by default, passes)
  and the fiducial locator's (its vision settings, its tolerances), and the job processor's settings
  (see [Job Processors](machine-setup.md#job-processors)).
- **The head's places**: its homing fiducial and whether it homes visually, its park location, the
  calibration rig's two fiducials (their places, heights and diameters) and test object, and its pump
  (which actuator, when it runs, how long it takes to come up).

When the import finishes, a message says what was brought in and lists anything to check: a part of
the OpenPnP set-up jplacer has no equivalent for yet (some axis types, a controller that is not G-code
or is reached over the network), or a command that uses something jplacer cannot fill in yet. Those
parts are left out or kept exactly as OpenPnP wrote them.

The machine's **feeders** are brought across too, onto the [Feeders](feeders.md) tab, each exactly as
OpenPnP wrote it; they take the place of the feeders jplacer had. Parts and packages are not imported
(copy OpenPnP's `parts.xml` and `packages.xml` into jplacer's configuration folder), and neither is
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
be missed.

<!-- src: src/machine/JPGcodeDriver.cpp (identify: nothing answered); src/machine/JPCell.cpp (onLost); src/app/JPlacerMachine.cpp (showState); JFramework src/io/SerialPort.cpp (one owner) -->

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
readings), Grbl 1.1, and generic G-code. A profile of your own, placed in `profiles/` in jplacer's
configuration folder, is used as well, and replaces a bundled one with the same `id`.

<!-- src: src/machine/JPFirmwareProfile.h; src/machine/JPFirmwareProfile.cpp (profileDirs, loadAll) -->

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
exactly where the head's settings say it is. It looks again to check, and corrects again if that left
more than 0.02 mm (about how closely a machine returns to a place). The line under the camera buttons and
the status bar say by how much it corrected. Until a camera on the head is calibrated, Home says it homed
by the switches only.

<!-- src: src/machine/JPGcodeDriver.cpp (connect, unlockForHoming); src/machine/JPCell.cpp (doHome) -->

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

<!-- src: src/machine/JPAxisConfig.h (Backlash); src/machine/JPCell.cpp (doMove: overshoot, approach, applied; updatePositions; reconfigure); src/openpnp/JPOpenPnpMachineImporter.cpp (backlash) -->

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

Beside the tool, for a nozzle, the **nozzle tip** button opens its tip menu:

- what is on the nozzle now;
- **Load** each tip that fits it (one on another nozzle, or with no load steps, is shown but cannot be
  chosen). The tip on the nozzle is unloaded first, by its own unload steps, then the new one loaded by
  its load steps;
- **Unload** the tip on it;
- **Step Through**: each changer step is shown, with its place and speed, and runs only once you say
  so; stop at any step. On by default, and kept for next time;
- **Manual Change**: say which tip is on the nozzle, or none, when it was changed by hand (nothing moves)
  or a change was stopped;
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

<!-- src: src/machine/JPCell.cpp (homeNozzle, doHomeNozzle, nozzlesHomedWith); src/machine/JPNozzleConfig.h (homeCommand); src/ui/JPJogPanel.cpp (showTipMenu); src/app/JPlacerMachine.cpp (homeNozzle); src/setup/JPSetupProperties.cpp (nozzleForm) -->

**Special** tab: **Head Safe Z** (every Z on the head up to safe Z), **Discard** (the nozzle's part to the
discard location: up, across, down, let go, up again), **Recycle**, and **Pick** and **Place** where the
nozzle is. **Recycle**, as OpenPnP's, puts the nozzle's part back into an enabled feeder that holds it and
can take it back, the nearest to the head's camera: a tape or tray feeder that has fed (its count taken
back), an auto feeder set to Recycle supported, a push-pull, Bamboo or Photon feeder (its next feed then
skipped), a loose part feeder where its part was found, or a heap (dropped back into the heap along its
three moves). It is greyed out when no feeder can; the Feeder.BeforeTakeBack and Feeder.AfterTakeBack
scripting events run round it.

<!-- src: src/ui/JPJogPanel.cpp (specialPage, refreshRecycle); src/model/JPFeeder.cpp (canTakeBackPart, partTakenBack); src/tasks/JPFeederTakeBack.cpp; src/tasks/JPHeapFeeder.cpp (takeBack); src/app/JPlacerOpenPnpTabs.cpp (recycle) -->

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
stays where you put it, until you scroll to the end again. Type a line in the box and press **Send** or
Return to send it as it is. With more than one controller, choose which one from the list beside the box.

Over the lines, what is shown:

- **G-code**: the controllers' traffic, on or off.
- **Log**: how much the log says, every category: Off, Errors, Warnings, Info (what was done and what came
  of it, to begin with), Debug (the steps in between) or Trace (every event, many a second). A warning or
  error line starts with ⚠; a Debug or Trace one with its level.
- **Categories**: a menu of the log's categories (`machine.cell`, `camera`, and so on), each **As Log** or
  at a level of its own: turn one part up (the controllers, `machine.driver`) without the rest, or one
  that is too busy down. **All as Log** puts them all back.
- **Clear** empties the console.

The levels are the log's own, so they set what goes into the log file as well. They are kept for next
time; `--verbose`, `--quiet` or `--trace <category>` on the command line go over them for that run.

<!-- src: src/ui/JPConsolePanel.cpp; src/ui/JPMenuButton.cpp; src/common/JPLogLevels.cpp; src/common/JPlacerLog.h (all); src/app/JPlacerMachine.cpp (the console's settings); src/app/JPlacerApp.cpp (applied at start); src/main.cpp (parseArgs); src/machine/JPGcodeDriver.cpp (status lines are not passed on as traffic) -->

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
| eye | **As taken**: lit, the picture is shown as the camera gives it; off, it is straightened (below). |
| disk | **Save the picture** (below). |
| target | **Calibrate** the camera (below). |
| tick in a ring | **Visual test** of the calibration (below). |
| gear | **The camera's settings**: Machine Setup, with the camera chosen in its tree. |

<!-- src: src/ui/JPCameraPanel.cpp (tabTools); src/ui/JPIcons.cpp; src/ui/JPIconButton.cpp (setLeads); src/app/JPlacerMachine.cpp (buildCameras); JFramework include/j/core/DockWidget.h (addTitleWidget) -->

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

With a camera on the head calibrated and the machine homed, **double-click** anywhere in its picture, or
**Shift+click** it, and the camera moves to look there: the quickest way to put it over a fiducial or a
part. Or **drag** in the picture: a line from the cross to the pointer shows the move, which is made when
the button is let go; let go outside the picture and nothing moves.

<!-- src: src/ui/JPCameraView.cpp (handleMousePress, handleMouseRelease, lookAt); src/app/JPlacerCameraTasks.cpp (onLookAtPixel, lookAt) -->

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

**Right-click** a camera's picture to choose its **reticle**, what is drawn over the picture to measure by:

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

<!-- src: src/ui/JPReticle.cpp (spacings, sizes, draw, thinned); src/ui/JPCameraView.cpp (buildMenu, prepareContextMenu, kLeastGap); src/app/JPlacerSettings.cpp (cameraReticleKey) -->

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
pictures a little late). The head moves at the machine's speed (the Jog panel's **Speed**), as jogs and parks do. While a task runs, its camera's buttons
are off (and another camera task will not start), and the line over the picture says what it is doing; when it ends, that line
and the status bar give the result. A calibration is saved in the cell file and used from then on:
whatever is measured in a picture is straightened through the lens first.

A calibration belongs to the picture size it was measured at: at another size a pixel is another size
on the machine, and the lens's bending lands elsewhere in the picture. A camera keeps one for each size,
and calibrating again at a size replaces only that one. A camera taking pictures at a size it has not
been calibrated at is shown as taken, and a task that measures with it says it is not calibrated for
that size.

<!-- src: src/tasks/JPCameraCalibrator.cpp; src/vision/JPCalibrationFit.cpp (fitWithLens); src/common/JPLens.h; src/tasks/JPCameraLook.cpp (calibration); src/machine/JPCameraConfig.h (calibrationFor, keepCalibration); src/app/JPlacerCameraTasks.cpp (calibrate, notReady, kTaskSpeed); src/ui/JPCameraPanel.cpp (setBusy, the note, refreshStraightening) -->

When the head has a **secondary calibration mark** (Machine Setup, the head's Calibration Rig, brought
across from OpenPnP's calibration rig) at least 1 mm higher or lower than the homing mark, the camera is
measured again over it. A camera's scale goes as one over its distance from what it looks at, so the two
scales give where the camera's centre of projection is, its focal length, its field of view in degrees, and
the scale at any height; the camera's Advanced Calibration tab shows them. Two scales less than 0.1% apart
tell nothing, and are not used. Should the second measuring fail, the first is kept and the result says why.
How the camera is measured, and whether at two heights, is set on that tab.

<!-- src: src/app/JPlacerCameraTasks.cpp (calibrate, secondHeight, kLeastHeightGapMm); src/machine/JPCameraCalibration.cpp (twoHeights, cameraZ, focalPx, scaleAt); src/machine/JPCameraCalibration.h (kLeastScaleChange) -->

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
the mark is looked for again there. The light is left on. The homing mark (Visual Test, visual homing) is
looked for this way.

<!-- src: src/vision/JPRoundMarkFinder.cpp (find, measureAt, edgeCircle, polarityMatches, findAnySize); src/tasks/JPCameraLook.cpp (lightOnly, findTryingHarder) -->
