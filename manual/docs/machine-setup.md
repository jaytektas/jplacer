# Machine Setup

**Machine Setup** (Machine > Machine Setup…, or its tab beside the Machine panel) is where the machine
is described: what it is made of, and how each part is set up. A machine brought in from OpenPnP arrives
set up; here it is looked at, changed and added to.

<!-- src: src/ui/JPMachineSetupPanel.cpp; src/app/JPlacerMenuBuilder.cpp (Machine Setup…); src/app/JPlacerMachine.cpp (buildPanels) -->

## The tree

The machine is shown as a tree of its parts:

- **Controllers**: the boards the machine is wired to.
- **Axes**: every axis, whichever controller drives it.
- **Heads**: each head, and on it its **Nozzles**, **Cameras** and **Actuators**.
- **Nozzle Tips**: the tips the nozzles take, each with its **Load** and **Unload** steps under it.
- **Cameras**: the cameras fixed to the machine (looking up at the nozzles).
- **Actuators**: the actuators on the machine rather than a head.

Choose a part to see its settings beside the tree, each the full height of the panel; drag the divider between them to give either more
room (where it is is kept for next time). Over the tree, **+** opens every branch, **−** closes
them down to the machine's groups, and **Search** keeps to the rows whose name contains what is typed
(and the groups they are in); its **×** clears it. Right-click a row for the same and more: **Open This
Branch** and **Close This Branch** (the row and everything under it), **Open All**, **Close All**, and
Add and Remove as the buttons above. A camera's gear icon (in its tab) opens Machine Setup on that camera.

<!-- src: src/setup/JPSetupTree.cpp (build); src/ui/JPMachineSetupPanel.cpp (the search, showNode, setBranch, collapseAll, the tree's menu, the divider, kTreeShare); src/app/JPlacerMachine.cpp (showSetup); src/app/JPlacerSettings.h (kSetupTreeShare) -->

## Adding, removing and ordering parts

**Add** adds a part of the kind chosen: with an axis (or **Axes**) chosen it reads **Add Axis**, with a
head's **Nozzles** chosen **Add Nozzle**, which goes on that head. A new part has a name saying what it is,
to change, and an id of its own that stays the same whatever it is renamed to.

**Remove** removes the chosen part, unless something else uses it: an axis a nozzle or camera moves on,
a controller an axis is on, an actuator that is a camera's light, a head with parts on it, a nozzle tip
on a nozzle. Then nothing is removed, and the line at the bottom says what uses it. A nozzle tip that
only fits nozzles is taken off their lists with it.

**Up** and **Down** move the chosen part among the others in its group. The order is the order they
are shown in elsewhere (the cameras' tabs, the Axes panel).

<!-- src: src/setup/JPSetupEdits.cpp (addable, add, remove, move, newId) -->

## Settings

A part's settings are laid out as OpenPnP lays out the same part: in **tabs**, and in each tab in titled
**groups**. Each setting has a control the size of what it holds: a box to tick, a number, a choice, a
line of text. Choices of another part (the controller an axis is on, a nozzle's axes, a camera's light) are
made by name. Coordinates are in columns under **X**, **Y**, **Z** and **Rotation**.

| Part | Tabs and groups |
|---|---|
| Machine | **Configuration**: General (name; **Home after connected?**, homing as soon as a connect you asked for succeeds; **Park after homed?**, parking the head once homing, visual homing included, is done), Locations (**Discard Location**, where a nozzle drops a part that is not wanted). **Fiducials**: Measuring a Fiducial (**Vision Passes**, **Centred To**), Parallax (**Parallax Diameter**, **Parallax Angle**): how Locate Board measures each fiducial (see [Locating the board](board.md)) |
| Controller | **Configuration**: Properties (name, firmware profile, or `auto` to recognise it), Communications (line endings: LF, CR or CRLF), Serial Port (port, baud, parity, data bits, stop bits, flow control, and **Set DTR** / **Set RTS** to raise those lines once the port is open). **Driver Settings**: Max. Feed Rate (the fastest any move is sent; 0 for no cap), **Log G-code?** (every line sent and received goes to the log), the timeouts and the status interval. **Gcode**: each of the firmware's commands; empty, the profile's is used (shown greyed), else what is written here, several lines if need be |
| Axis | **Configuration**: Properties (kind: driven by a **controller**, **mapped** to follow another axis through two points, or **virtual**; type; name), Controller Settings (driver, axis letter, home coordinate; Resolution, what one motor step moves it, and its other side Steps / mm, every move going to the nearest whole step (at a soft limit, the step on this side of it); a rotation axis's **Limit to Range**, keeping it within -180..180, and **Wrap Around**, turning the short way round: with both, it turns the short way and its controller is then told the same angle within the range), Axis Mapping (input axis, map points A and B), Kinematic Settings (soft limits and safe zone, each with Enabled?; feed rate per second and per minute; acceleration and jerk, which reach a controller whose move command takes `{acceleration}` or `{jerk}`, scaled with the move's speed). **Backlash Compensation**: the method (None, OneSidedPositioning, DirectionalCompensation, DirectionalSneakUp), its offset, sneak-up distance and speed factor, **Calibrate** (an X or Y axis), and the last calibration's tolerance and graphs (see [Backlash](machine.md#backlash)) |
| Head | **Configuration**: Properties (name), Locations (homing fiducial, its diameter and homing method, with **Visual Test** and **Visual Home**; park location), **Calibration Rig** (the primary and secondary marks, round marks at two heights, and their diameters: a head camera is calibrated again over the secondary mark to measure it at a second height), Pump (the vacuum pump's actuator; Pump Control: None, PartOn, TaskDuration or KeepRunning; how long it takes to come up) |
| Nozzle | **Configuration**: Properties (name), Coordinate System (head; axes and offsets), Settings (pick and place dwell times). **Nozzle Tips**: every tip, whether it is **Compatible?** and whether it is **Loaded?**. **Vacuum**: the vacuum actuator, the blow-off actuator and whether it closes the vacuum itself, the sensing actuator. **Homing**: the **Home Command** that homes this nozzle's Z alone, and **Home Z** to run it (see [Homing a nozzle's Z](machine.md#homing-a-nozzles-z)). **Offset Wizard**: measures where the nozzle is on the head: lower it into putty (or the like) where it leaves a mark, **Store Nozzle Mark Position**, move the camera over the centre of the mark, **Calculate Nozzle Offset**; the nozzle's X and Y offsets change by how far the camera is from where the nozzle thought it was, one step to undo |
| Nozzle tip | **Configuration**: Properties (name), Pick & Place (its pick and place dwell times, added to the nozzle's), Part Dimensions (diameter seen from below), Nozzles (the nozzles it fits). **Part Detection**: Part On and Part Off Vacuum Sensing, each with a Measurement Method (**None**; **Absolute**, the vacuum read within Low..High; **Difference**, the level just before within Low..High and its change within the difference's range), and for part off the probing and dwell times. **Tool Changer**: how it is unloaded (see below) |
| Camera | **General Configuration**: Properties (name, looking down or up), Light (light actuator; ON **Before Capture?**, before a picture for vision, and **User Camera Action?**, while you look at the camera; OFF **After Capture?**, after the picture for vision, and **Anti-Glare?**, while another camera takes one), Units Per Pixel (a rough scale to start calibrating from). **Camera Settling**: how a picture for vision (calibrating, visual homing and testing, locating a board) waits for the camera to stop after a move: **FixedTime**, a set time after the move; or **Maximum**, **Mean**, **Euclidean** or **Square**, comparing each picture with the one before until the difference stays under the threshold for the debounce frames, within the timeout (on a timeout the last picture is used, and the log says so). **Device Settings**: device, format, size, and the camera's own Properties (brightness, backlight compensation, contrast, exposure, focus, gain, gamma, hue, power line frequency, saturation, sharpness, white balance, zoom): each one ticked under **Set?** is set when the camera opens, to its **Value** or, where the camera can, **Auto**; one not ticked is left as the camera has it. **White Balance**: each colour's balance and gamma, applied to every picture (what you see and what vision works on), as in OpenPnP; **Overall** and **Brightest** work them out from what the camera sees now (have something white or grey in view), **Reset** takes them off. **Position**: the head it is on and its axes and offset, or where a fixed camera is. **Advanced Calibration**: **Start Calibration**; how it measures (the grid's **Places Across** and **Places Down**, 3 to 25 each; **Reach**, how far towards the picture's edge the grid goes, 1 as far as leaves room for the mark; **Outlier Limit**, how many times the fit's spread a measurement may be off before it is left out, one in ten at most; **Worst Fit Taken**, the spread in pixels beyond which the calibration is refused; **Two Heights?**, measuring it again at a second height: a head camera over the head's secondary calibration mark, a fixed camera with the nozzle's tip raised by **Raise For The Second**, 2 mm to begin with); how much of a straightened picture's bent edge shows; and for each picture size it was calibrated at, its **Results**: when and at what Z, units per pixel each way, the estimated locating accuracy, the field of view in mm, how far the camera is turned (and mirrored), the lens, how many measurements were made, left out and missed; measured at two heights, the second height's scale, where the camera's centre of projection is, its focal length, its field of view as angles, and how much the scale grows per mm nearer; and three graphs (below) |
| Actuator | **Configuration**: Properties (driver, name), Coordinate System (head), General (Actuation: what it is switched to once connected, once homed and before a disconnect you ask for, **ActuateOn**, **ActuateOff** or left as it is; index, unit read), Commands (on, off, read, reply pattern) |

A camera's calibration results end in three graphs of its measurements, each measurement's **residual**
being how far from where the fit puts it the mark was seen. **Residual Errors in the Order Measured** shows
them along X and Y, one after another: they should look like noise about zero, and a step or a drift says
something moved while measuring (the mark, the camera in its mount, a missed step, a slipping belt).
**Residual Errors, X against Y** shows them all together, with the outlier limit as a circle: one round
cluster about the middle is right; two clusters, or a stretched one, say the mark was found badly or the
backlash or a drive is out. The **Residual Error Map** colours the picture by how far off the measurements
near each place are, coolest to hottest; it shows where, not how much, and should look patchy at random:
rings or stripes mean the lens fits the picture badly there. A calibration made before the graphs were kept
shows its results without them until it is calibrated again.

<!-- src: src/setup/JPSetupProperties.cpp (calibrationResults, cameraForm, headForm); src/machine/JPCameraCalibration.cpp (cameraZ, focalPx, scaleAt, twoHeights); src/app/JPlacerCameraTasks.cpp (calibrate, calibrateFixed, secondHeight); src/ui/JPPlotView.cpp; src/machine/JPCameraConfig.h (Calibrating); src/tasks/JPCameraCalibrator.cpp; src/machine/JPCameraCalibration.h (points) -->

**Loaded?** on a nozzle's Nozzle Tips tab says which tip is on it now: ticking one moves nothing, and a tip
is on one nozzle at a time.

### Places

A place (a homing fiducial, a park location, a fixed camera's location, where a changer step goes) is one
row of coordinates ending in four buttons:

- **Capture Camera** and **Capture Nozzle** set it from where the camera on the head, or the nozzle chosen on
  the Jog panel, is now: one step to undo.
- **Move Camera** and **Move Nozzle** go there: up to safe Z, across, and, for the nozzle, down to the Z
  given. The machine must be connected and homed; the speed is the Jog panel's.

A soft limit or safe zone end has the same two kinds of button for its axis: set it from where the axis is,
or move the axis there.

Only what jplacer acts on is shown. Whatever else a cell carries (brought from OpenPnP for features not
built yet) is kept as it is. A camera's calibrations and the machine's squareness are measured, not set
here (see [Cameras](machine.md#cameras) and [Squaring the machine](board.md#squaring-the-machine)).

Putting a part on a head gives it the head's X and Y axes, as its other parts have; taking it off one
clears its axes.

<!-- src: src/setup/JPSetupProperties.cpp; src/ui/JPSetupForm.cpp; src/ui/JPTextBox.cpp; src/camera/JPV4L2Source.cpp (applyControls); src/tasks/JPCameraLook.cpp (settled, difference); src/camera/JPWhiteBalance.cpp; src/camera/JPCameraFeed.cpp; src/machine/JPSerialLink.cpp; src/machine/JPLinkFactory.cpp; src/machine/JPGcodeDriver.cpp (lineEnding); src/machine/JPCell.cpp (doMove); src/ui/JPGroupFrame.cpp; src/ui/JPMachineSetupPanel.cpp (capture, goTo); src/app/JPlacerMachine.cpp (toolMount, readyToMove, setupAction, nozzleOffsetWizard, watchCell); src/app/JPlacerCameraTasks.cpp (visualHome, run); src/app/JPlacerMachine.cpp (lightCameras); src/machine/JPCellConfig.cpp; src/machine/JPCell.cpp (moveTool, actuateFor) -->

## A nozzle tip's changer

No two machines change tips the same way: from trays at the side, from the back, through a door that
opens, with a lock to release, or by hand. So a tip's changer is not described, it is taught: a list of
steps under the tip's **Load**, run in order, built from

- **move**: the nozzle doing the change goes to X, Y, Z and a rotation, at a speed (a share of top
  speed). One left empty stays as it is (a move giving only Z goes straight up or down).
- **safe Z**: the nozzle up to safe Z.
- **actuator**: switch one on or off (a door, a lock, a valve).
- **wait**: a number of milliseconds.
- **ask**: a message for you, to carry on or cancel (a tip put on by hand, a door to open).

The first step is a move giving X, Y and Z: the nozzle comes to it from safe Z (up, across, then down to
it), and loading ends at safe Z. A tip with no steps is put on by hand.

A move's place is where **the nozzle** goes, not the camera: the nozzle's offset on the head is taken
care of. It is in the axes' own coordinates, before the machine's
[squareness](board.md#squaring-the-machine) correction, so measuring the squareness again never moves
it: up to a few tenths of a millimetre at the far end of the machine, enough to miss a slot.

Choose **Load** (or a step) and **Add Step** to add one after the step chosen; **Up** and **Down**
reorder them.

**Unloading** is loading run backwards unless the tip is set to **steps of its own**: each move goes back
to where the move before it went, at the speed of the move it undoes, starting from where loading ended;
an actuator is switched the other way; a move that came down from safe Z is undone by going up, then
across. The steps it works out are shown under **Unload (loading backwards)**, to check; to change them,
change loading, or choose steps of its own, which start as those.

<!-- src: src/machine/JPChangerStep.h; src/machine/JPNozzleTipConfig.cpp (reversed, problems); src/setup/JPSetupTree.cpp (build, stepLabel); src/setup/JPSetupEdits.cpp (add, remove, move); src/setup/JPSetupProperties.cpp (stepForm, nozzleTipForm) -->

## Changes are used as you make them

There is no Apply: each change goes to the running machine, and is saved in the cell file, as you make it.
A value in a field is changed when you press Return or Tab or leave the field, or step a number up or
down; until then only the field has changed, and **Escape** puts back the value it had. A box ticked or a
choice made is changed at once.

Nothing is disconnected: the controllers take their new settings as they run, and the machine stays
homed. A controller's **connection** settings (its port, speed or flow control, its firmware profile,
how long it waits while connecting) are used the next time you connect; a controller added is connected
then too. After a change to the **axes** the machine must be homed again, as its coordinates then mean
something else.

What is wrong with the setup (a part naming one that is not there) is listed under the settings, and
nothing is handed to the machine until it is put right: the machine keeps the setup it had.

While the machine is moving, changes wait for it to stop. Calibrations and squareness measured while the
setup was being changed are kept. The camera panels are made again only when a camera, a head or an
axis changed.

## Undo and Redo

**Undo** and **Redo** at the bottom of Machine Setup (and **Edit ▸ Undo**, Ctrl+Z, and **Edit ▸ Redo**,
Ctrl+Y) step back and forward through the changes, the machine following; each says what it would
undo or redo ("Undo Add Camera"), and takes you to where the change was made. Changes one after another to
the same setting (a number stepped up several times) are one step. A port chosen on the Machine panel
is a step too. The steps are kept until another cell is opened.

<!-- src: src/ui/JPMachineSetupPanel.cpp (record, handOver, undo, the buttons); src/ui/JPTextField.cpp; src/ui/JPSetupForm.cpp; src/setup/JPSetupHistory.h; src/app/JPlacerMachine.cpp (applySetup, setPort, undo); src/app/JPlacerMenuBuilder.cpp; src/machine/JPCell.cpp (reconfigure); src/machine/JPGcodeDriver.cpp (setConfig); src/machine/JPCellConfig.cpp (problems) -->
