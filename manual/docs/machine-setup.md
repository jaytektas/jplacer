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

Choose a part to see its settings below the tree. **Search** keeps to the rows whose name contains what
is typed (and the groups they are in).

<!-- src: src/setup/JPSetupTree.cpp (build); src/ui/JPMachineSetupPanel.cpp (the search) -->

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

Each part's settings are in groups, with a control for each: a box to tick, a number, a choice, a line
of text. Choices of another part (the controller an axis is on, a nozzle's axes, a camera's light) are
made by name.

| Part | Settings |
|---|---|
| Machine | its name |
| Controller | name, firmware profile (or `auto`, to recognise it), its serial port, baud rate and flow control, and how long it waits for things |
| Axis | name, kind (driven by a **controller**, **mapped** to follow another axis through two points, or **virtual**), type, its controller and axis letter, home coordinate, soft limits, safe zone, top speed and backlash |
| Head | name, its homing mark (where, its size, and whether Home finishes with the camera), its park place |
| Nozzle | name, the head it is on, the axes that move it, its offset, and the nozzle tip on it (one of those that fit it) |
| Nozzle tip | name, the diameter of its end as the camera looking up sees it, how it is unloaded (see below), and the nozzles it fits (a box for each) |
| Camera | name, looking down or up, the head it is on (or fixed to the machine, and where), the device's name, the picture's format and size, its light, and a rough scale to start calibrating from |
| Actuator | name, the head it is on (or the machine), its controller, index and commands |

Only what jplacer acts on is shown. Whatever else a cell carries (brought from OpenPnP for features not
built yet) is kept as it is. A camera's calibrations and the machine's squareness are measured, not set
here (see [Cameras](machine.md#cameras) and [Squaring the machine](board.md#squaring-the-machine)).

Putting a part on a head gives it the head's X and Y axes, as its other parts have; taking it off one
clears its axes.

<!-- src: src/setup/JPSetupProperties.cpp; src/ui/JPPropertyForm.cpp -->

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

## Applying

Changes are made to a copy of the machine: nothing happens to the machine until **Apply**. **Reset**
goes back to the machine as it is. Both are available once something has changed.

What is wrong with the copy (a part naming one that is not there) is listed above the buttons, and
Apply waits until it is put right. Apply then saves the cell file and opens it again; while the
machine is connected it asks first, as the machine is disconnected, connected again, and must be homed
again before it moves. Calibrations and squareness measured while the setup was being changed are kept.

<!-- src: src/ui/JPMachineSetupPanel.cpp (update, the buttons); src/app/JPlacerMachine.cpp (applySetup); src/machine/JPCellConfig.cpp (problems) -->
