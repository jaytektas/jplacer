# Feeders

The **Feeders** tab (in the work area, after Packages, as in OpenPnP) lists the machine's feeders: where
each part is taken from. Each feeder is kept in `feeders.xml` in jplacer's configuration folder, exactly
as OpenPnP keeps it in its `machine.xml`, whatever its kind; **Machine ▸ Import OpenPnP Machine…** brings
an OpenPnP machine's feeders in (see [Machine](machine.md#bringing-in-a-machine-set-up-in-openpnp)).

<!-- src: src/ui/JPFeedersPanel.cpp; src/app/JPlacerOpenPnpTabs.cpp (the Feeders dock, onImported); src/model/JPConfiguration.h (kFeedersFile); src/model/JPFeeder.h -->

## The toolbar

| Button | |
|---|---|
| **New Feeder...** (plus) | Asks which kind of feeder to make (below), then makes it, holding the first part, turned off and named after its kind. There must be a part first. |
| **Delete Feeder...** (cross) | Deletes the chosen feeders, after asking. |
| **Pick...** | Feeds the chosen feeder, then the nozzle chosen on the Jog panel picks its part: up to safe Z, across and turned to the pick location, down, the vacuum on, and up again. |
| **Feed...** | Feeds the chosen feeder: its count moves on to the next part (a strip with vision on has its hole looked at). |
| **Move Camera...** | Moves the camera over the chosen feeder's pick location, at safe Z. |
| **Move Tool...** | Moves the chosen nozzle to the chosen feeder's pick location: up to safe Z, across, and down to the pick height. |

Delete Feeder and the right-click menu work on one feeder or several; the others on one. The machine
must be connected and homed to move. A feed that cannot be made says why: a tray or strip that is
empty ("Tried to feed part: … Feeder … empty."), or a feeder with no part.

**New Feeder...** offers OpenPnP's kinds, in its order: ReferenceStripFeeder, ReferenceTrayFeeder,
ReferenceRotatedTrayFeeder, ReferenceDragFeeder, ReferenceLeverFeeder, ReferencePushPullFeeder,
ReferenceTubeFeeder, ReferenceAutoFeeder, ReferenceSlotAutoFeeder, ReferenceLoosePartFeeder,
AdvancedLoosePartFeeder, ReferenceHeapFeeder, BlindsFeeder, SchultzFeeder, SlotSchultzFeeder, RapidFeeder,
Neoden4Feeder, PhotonFeeder and BambooFeederAutoVision. Click one and **Accept**, or double-click it.
jplacer works out where strip, tray, rotated tray, auto, tube, drag, lever, Schultz, slot, Neoden 4, Photon and Rapid feeders pick, and feeds them. The other kinds are kept and
set up, but feeding them is not yet available (**Pick...** and **Feed...** say so), and **Move Camera...**
and **Move Tool...** say jplacer does not work out where they pick yet.

**Search**, at the right, shows only the feeders with the text typed anywhere in a row, whatever its case
(a regular expression, as on the other tabs).

<!-- src: src/ui/JPFeedersPanel.cpp (newFeeder, deleteFeeders, feed, pickFrom, moveToPick, selectionChanged); src/app/JPlacerClassSelectionDialog.cpp; src/model/JPFeeder.cpp (create, classNames, pickLocation, feed); src/app/JPlacerMachine.cpp (pickAt, moveToolTo); src/machine/JPCell.cpp (pickAt) -->

## The table

| Column | |
|---|---|
| **Name** | The feeder's name, changed in place. |
| **Part** | The part it holds. |
| **Type** | Its kind. |
| **Priority** | **High**, **Normal** or **Low**: of the feeders holding a part, those of the highest priority are used. Chosen from a list. |
| **Faults** | A job's last feeds and picks from it, newest first: `X` a fault, `-` none; empty when there were none. jplacer does not run jobs yet, so it stays empty. |
| **Enabled** | Whether it is used. Greyed while no enabled placement on an enabled board of the job uses its part. |
| **Feed** | **Normal feed**; **Skip next feed** (the next feed picks again where the last one did, then goes back to normal: for tuning a feeder, or after putting a lost part back in its pocket); or **Disable feed** (it always picks where it is). Chosen from a list, for the kinds that offer it (strip, tray, push-pull, auto, Photon and Bamboo feeders). |

Columns sort as on the other tabs; Priority and Feed sort in the order listed above. Right-click a row
for **Set Enabled** (**Enabled**, **Disabled**) and **Set Feed option**, for every chosen feeder at once.

<!-- src: src/ui/JPFeedersTableModel.cpp; src/ui/JPFeedersPanel.cpp (buildMenu); src/model/JPFeeder.cpp (summariseJobFaults, supportsFeedOptions, feedOptionsName) -->

## The feeder's setup

Under the table, the chosen feeder's **Configuration**. Each change is made as soon as it is entered
(Return, Tab or leaving the field), so there is no Apply. Every kind has **General Settings**: its
**Part**, **Feed Retry Count** and **Pick Retry Count**. A place has OpenPnP's four buttons after it:
**Position Camera** and **Position Tool** take the camera, or the nozzle chosen on the Jog panel, to the
place at safe Z; **Get Camera Coordinates** and **Get Tool Coordinates** set the place from where the
camera or the nozzle is now (the camera's X and Y; the nozzle's Z as well).

<!-- src: src/setup/JPFeederForms.cpp (general, pickLocation); src/ui/JPSetupForm.cpp (locationButtons); src/ui/JPFeedersPanel.cpp (capture, goTo) -->

### Strip feeder

A strip of cut tape lying on the machine, its parts picked one after the other along it.

- **General Settings** add **Rotation In Tape**: how the part lies in its pocket, looking at the tape
  with its sprocket holes at the top (0° as the part is drawn in its library; counter-clockwise is
  positive).
- **Tape Settings**: **Part Pitch** (from one part to the next) and **Tape Width**, in mm; **Feed
  Count**, the parts taken so far (**Reset** sets it to 0); **Max Feed Count**, the parts on the strip
  (0: no limit), which **Auto Set MaxFeedCount** works out from the hole locations and the part pitch.
  **Auto Setup** sets the strip up from two clicks on the camera's view, as OpenPnP's does. The head
  camera's view asks "Click on the center of the first part in the tape."; the camera moves there and the strip's pipeline finds the round marks around it. Among them it takes the
  sprocket holes: a line of marks 4 mm apart, a quarter of the tape width to half the tape width plus
  1.25 mm from the part. While waiting for a click, the camera's view shows the lines found, the best one
  and its holes. "Now click on the center of the second part in the tape." brings the same look at the
  second part. Then the **Reference Hole Location** and **Next Hole Location** are set from the holes
  beside the two parts (their Z kept), the **Part Pitch** to the distance between the parts rounded to
  2 mm, and the **Feed Count** to 0, and the camera goes to the first part's pick location ("Setup
  complete!"). The button reads **Cancel Auto Setup** while it runs. It stops with an **Auto Setup
  Failure** when the camera is not calibrated, when no hole is found by a part, when the same part is
  clicked twice, or when the holes are on the wrong side for the direction the parts were clicked in ("The
  tape is oriented incorrectly for the feed direction of the components selected"). With a camera
  calibrated at two heights, the reference hole location's Z must be set first.
- **Vision**: with **Use Vision?** ticked, each feed has the camera look at the hole it feeds from (and at
  the first hole too when picking starts mid-strip), and the parts are picked where the holes were found
  rather than where the hole locations put them. A hole is looked for within half a hole pitch of where it
  should be, as a round mark 1.5 mm across, light or dark; not found, or found more than 2 mm off, the
  strip is taken as finished ("Unable to locate reference hole. End of strip?"). **Extrapolation Distance**:
  how far along the strip to go before looking again (0: every hole; near the strip's start it looks more
  often). **Parallax Diameter** and **Parallax Angle**: look at the hole from either side of it, that far
  apart and turned that way, and take the middle (for clear tape that reflects the camera's light).
  **Reset Vision** forgets the holes found. **Edit Pipeline...** opens the strip's OpenPnP pipeline in the
  [Pipeline Editor](pipeline-editor.md), and **Reset Pipeline** puts OpenPnP's default back; jplacer's own
  hole finder, described here, does not use it.
- **Locations**: the **Reference Hole Location**, the hole nearest the first part's centre, in the
  direction the parts continue, with the pick height as its Z; and the **Next Hole Location**, any hole
  further along.

The part is picked across the tape from its hole, as EIA-481 tape lays it out: half the tape width less
0.5 mm across, 2 mm along, then one part pitch further for each part taken.

<!-- src: src/setup/JPFeederForms.cpp (stripForm, act); src/model/JPFeeder.cpp (pickLocation, idealLineLocations, feed, visionExpected, setVisionFound); src/tasks/JPFeederFeed.cpp; src/app/JPlacerJobMachine.cpp (locateHole, seeCircles); src/tasks/JPFeederPipelines.cpp; src/app/JPlacerStripAutoSetup.cpp; src/tasks/JPStripHoles.cpp; src/vision/JPRansac.cpp -->

### Tray feeder

Parts in rows and columns in a tray. **Pick Location** is the first part's place. **Offsets** are the
distances from one part to the next in X and in Y, and **Tray Count** how many there are each way; an
offset of 0 with more than one part that way is kept, but jplacer says it will fail. **Feed Count** is
the parts taken so far (**Reset** sets it to 0).

<!-- src: src/setup/JPFeederForms.cpp (trayForm); src/model/JPFeeder.cpp (pickLocation, feed) -->

### Rotated tray feeder

A tray turned on the machine. **Tray Component Locations**: **Point A** (the first row's first part),
**Point B** (the first row's last) and **Point C** (the last row's last), each with the location
buttons. **Tray Parameters**: the **Number of Tray Rows** and **Columns**, the **Feed Count** (**Reset**
sets it to 0) and the components remaining, the **Component Rotation in Tray** (relative to the row,
A to B), the **Z Height**, and **Calculate Offsets & Tray Rotation**, which works out the **Column
Offset**, **Row Offset** and **Tray Rotation** from the three points: it says what is wrong when the points
and counts do not agree, or the corner at B is not square (within 2.5°). Parts are taken along a row,
then the next.

<!-- src: src/setup/JPFeederForms.cpp (rotatedTrayForm, act); src/model/JPFeeder.cpp (pickLocation, feed) -->

### Auto feeder and tube feeder

An **auto feeder** feeds itself when told to: its **Pick Location**, and **Actuators**: the **Feed**
actuator and the value it is actuated with (a switch on when not 0), the **Post Pick** actuator and its
value (after each pick), **Move before feed** (the nozzle over the pick location first) and **Recycle
supported**. **Test feed** and **Test post pick** actuate them. A repeated feed (Skip next feed) does not
actuate. A **tube feeder** is picked at its pick location with nothing to feed.

<!-- src: src/setup/JPFeederForms.cpp (autoForm); src/tasks/JPFeederFeed.cpp (feed, postPick); src/app/JPlacerJobMachine.cpp (actuate); src/app/JPlacerOpenPnpTabs.cpp (machineAction) -->

### Drag feeder

A tape pulled along by a pin on the head, as OpenPnP's drag feeder. Besides the **General Settings** and
**Pick Location** every feeder has, a second **General Settings**: the **Part Pitch** (said beside it when
the part is an 0402: "0402 Part DETECTED"), the **Feed Speed %** the tape is dragged at (of the machine's
speed), the **Actuator Name** of the pin, and a **Peel Off Actuator Name**. **Locations**: the **Feed
Start Location** where the pin goes into the tape and the **Feed End Location** it drags it to, X, Y and
Z; with an actuator named, their tool buttons are **Position Actuator** (a red circle) and **Get Actuator
Coordinates** (a blue one), which take the pin there at safe Z and take where it is. The **Backoff
Distance** moves the pin back along the drag before it lets go, to take the tension off it.

A feed goes up to safe Z, puts the pin over the start, actuates it, lowers it into the tape, drags to the
end at the feed speed, pulses the peel off actuator, backs off, and lets go. At a 2 mm part pitch one drag
brings two parts: the first is picked 2 mm back along the tape, and the next feed does not drag. Without
an actuator name it says "No actuator name set."

**Vision**: with **Vision Enabled?** ticked, the head camera looks over the pick location for the
**Template Image** within the **Area of Interest** (X, Y, Width and Height in the camera's pixels), and
the pick location, and the next drag's start, move by how far from it the template is found. It looks
before the first drag and after every one. **Select** under the template image, or beside the area of
interest, puts a selection on the head camera's picture (see [Machine](machine.md)); **Confirm** takes it
(the template image is written into OpenPnP's configuration, as OpenPnP keeps it), **Cancel** puts it
away. **Reset vision offsets** forgets where the template was last found, so the next feed looks again
first. Without a template image or an area of interest the feed says it is required.

<!-- src: src/setup/JPFeederForms.cpp (pinForm); src/tasks/JPFeederFeed.cpp (pinFeed); src/model/JPFeeder.cpp (pickLocation, templatePath); src/ui/JPFeedersPanel.cpp (selectOnCamera, confirmTemplate); src/app/JPlacerJobMachine.cpp (moveActuator, matchTemplate); src/vision/JPTemplateFinder.cpp; src/ui/JPSetupForm.cpp (locationButtons) -->

### Lever feeder

A feeder whose lever the head pushes to move the tape on, as OpenPnP's lever feeder. Its page is the drag
feeder's without the Backoff Distance or the 0402 note: the **Feed Start Location** is where the pin meets
the lever, the **Feed End Location** where it pushes it to. A feed puts the pin over the start (at the
height it is), actuates it, pushes to the end at the feed speed, turns the take up (peel off) actuator on,
lets the lever back to the start, and turns both off: once for every 4 mm of the part pitch, so an 8 mm
pitch is two pushes. At a 2 mm pitch one push brings two parts. Its vision looks for the template after
each push only (not before the first), and the 2 mm step to the first of two parts is taken only with
vision on, as OpenPnP does.

<!-- src: src/setup/JPFeederForms.cpp (pinForm); src/tasks/JPFeederFeed.cpp (pinFeed); src/model/JPFeeder.cpp (pickLocation) -->

### Schultz feeder

An electric feeder driven through actuators, as OpenPnP's Schultz feeder. Besides the **General Settings**
and **Pick Location**, its **Actuators**: the **Feeder Number** (the value each actuator is actuated or
read with), then a row for each actuator, chosen from the machine's, with its button: **Get ID**, **Pre
Pick** (*Test pre pick*), **Post Pick** (*Test post pick*, the feed count read after), **Get Feed Count**,
**Clear Feed Count**, **Get Pitch**, **Toggle Pitch** (between 2 mm and 4 mm, the pitch read after) and
**Get Status**. What the Get buttons read is shown beside them; with the machine connected, the ID, feed
count, pitch and status are read when the feeder is chosen. A feed takes the nozzle over the pick location
at safe Z and actuates the pre pick actuator; after the pick, the post pick actuator. Without a pre pick
actuator a feed does nothing, as OpenPnP's.

<!-- src: src/setup/JPFeederForms.cpp (schultzForm, readsOnShow); src/tasks/JPFeederActions.cpp; src/tasks/JPFeederFeed.cpp (feed, postPick); src/machine/JPCell.cpp (readActuatorAndWait); src/ui/JPFeedersPanel.cpp (showReading) -->

### Slot feeders

A **slot auto feeder** and a **slot Schultz feeder** are places on the machine that feeders are put into,
as OpenPnP's ReferenceSlotAutoFeeder and SlotSchultzFeeder. The feeders belong to **banks**: each a name
and its feeders, each feeder a name, the **Part** it holds and its **Offsets** from the slot. Imported
from OpenPnP, the banks come with the machine. A slot's name in the table has what is loaded in it after
it ("Slot 1 (Feeder 7)", or "(None)"); its part is the loaded feeder's, and it is enabled only with a
feeder holding a part loaded. A feeder can be in one slot at a time: loading it into another takes it out
of the first.

Its page: **Slot**, the **Feeder** loaded (chosen from the slot's bank, its name editable beside it), with
**New** (a new feeder in the bank, loaded) and **Delete** (the loaded feeder taken out of the bank), or for
a slot Schultz feeder **Load** (the feeder its **Get ID** last read: the bank's of that name, else a new one
of it, 5 mm left of and 30 mm below the slot) and Delete; its **Location**, with the location buttons (and
for a slot Schultz feeder, **Update feeder location based on fiducial**: its **Fiducial Part** found near the
location by the head's camera, as a board's fiducial is, and the location's X and Y set to where it is);
the **Feed** and **Pick Retry Count**; the **Bank** (choosing another empties the slot), named beside it,
with **New** and **Delete** (not the only bank: "Can't delete the only bank. There must always be one bank
defined."). **Feeder**: the loaded feeder's **Offsets**, whose location buttons go to and take places as
offsets from the slot's location (turned with it), and its **Part**. Then the **Actuators** of an auto feeder
(each value noted "For Boolean: 1 = True, 0 = False") or of a Schultz feeder. A slot with nothing loaded
says "No feeder loaded in slot." when fed.

<!-- src: src/model/JPSlotBanks.cpp; src/model/JPConfiguration.cpp (importFeeders, resolveSlots, loadSlot, setSlotBank); src/model/JPFeeder.cpp (name, enabled, partId, pickLocation, feed); src/setup/JPFeederForms.cpp (slotForm, slotAct); src/tasks/JPFeederActions.cpp (updateLocation); src/ui/JPFeedersPanel.cpp (capture, goTo) -->

### Neoden 4 feeder

A Neoden 4 machine's own feeder, as OpenPnP's Neoden4Feeder. Besides the **General Settings** and **Pick
Location**, **Other**: the **Pitch In Tape [mm]** and **Rotation In Tape [deg]** (added to the pick
location's rotation), the **Actuator Name** with **Actuate** (the actuator actuated with the pitch), and
the **Feed Count** with **Reset**. A feed actuates the actuator with the pitch and counts. Its **Vision**
is a drag feeder's (see above), but its **Area of Interest**'s X and Y are from the middle of the camera's
picture, each kept within 512 pixels, as OpenPnP's; a template not found leaves the pick where it was.

<!-- src: src/setup/JPFeederForms.cpp (neoden4Form, templateVision); src/tasks/JPFeederFeed.cpp (feed); src/tasks/JPFeederActions.cpp (actuate); src/vision/JPTemplateFinder.cpp (placed); src/model/JPFeeder.cpp (pickLocation); src/ui/JPFeedersPanel.cpp (selectOnCamera) -->

### Photon feeder

An Opulo Photon feeder on its bus, as OpenPnP's PhotonFeeder: commands go as packets through the machine's
**PhotonFeederData** actuator (its controller's `M485`). Until it has a hardware id it is "Unconfigured
PhotonFeeder" and shows only **Global Config**; then its name shows its slot ("Reel 1 (Slot: 5)", or
"(Slot: None)") and its **Feeder** page has **Info** (Hardware ID, Slot Address and **Find**), **Part**
(the part, **Part Pitch** with **Feed** and **Feed 1mm**, the retries) and **Location** (the **Slot
Location**, kept for the slot address, and the **Part Offset** from it, whose location buttons work from
the slot; **Move While Feeding?**). **Global Config**'s **Search** asks every address up to **Maximum
Feeder Address To Scan**, a strip showing each as it is asked, found or missing, and adds the feeders it
finds. A feed finds the feeder's address and sets it up when needed, moves it on by its pitch with the
nozzle taken over its pick meanwhile, and waits until it says it is done, trying again as OpenPnP does; a
job finds and sets up the Photon feeders it uses first. When the machine has no PhotonFeederData actuator,
one is made on its first controller (a Machine Setup step, undone like any other).

**Program Feeder Slots ▸ Start Wizard** (the machine connected) programs slots you built yourself: take
every Photon feeder out and press **Next**; then put a feeder into the slot whose number is shown (change
it if you like). The feeder is given that slot's address, set up there, and the number moves on to the
next, for the next slot, until **Finish**. The search's highest address is raised to the last one
programmed.

<!-- src: src/app/JPlacerPhotonSlotsDialog.cpp; src/app/JPlacerMachine.cpp (ensurePhotonActuator); src/tasks/JPPhotonFeeders.cpp; src/tasks/JPPhotonCommands.cpp; src/tasks/JPPhotonPacket.cpp; src/tasks/JPPhotonBus.cpp; src/model/JPPhotonProperties.cpp; src/model/JPFeeder.cpp (name, photonUnconfigured, pickLocation); src/setup/JPFeederForms.cpp (photonForm); src/ui/JPSearchStrip.cpp; src/tasks/JPJobProcessor.cpp (preFlight) -->

### Rapid feeder

A feeder told what to do by its address, as OpenPnP's Rapid feeder: **Rapid Feeder Config** has its
**Address** and **Pitch**, and a feed sends "address pitch" to the machine's actuator named
**RAPIDFEEDER**. **Rapid Feeder Scanning** keeps the **Scan Start** and **End Location** and the **Scan
Increment**; its **Scan** (finding the feeders by their QR codes) needs a QR code reader, not in jplacer
yet.

<!-- src: src/setup/JPFeederForms.cpp (rapidForm); src/tasks/JPFeederFeed.cpp (feed, kRapidActuator) -->

### Loose part feeders

A **ReferenceLoosePartFeeder** and an **AdvancedLoosePartFeeder** hold parts lying loose in a bin. Their
**Location** is over the bin, its Z the bin's floor (the part's height is added for the pick, as the part
lies on it), its rotation added to the part's. To feed, the head camera looks over the location and runs
the feeder's OpenPnP pipeline (see [Pipeline Editor](pipeline-editor.md)); its "results" are the parts it
found, and the one nearest the camera's centre is looked at again from over it, three looks in all, and
picked where the last look found it. None found: "Feeder Bin: No parts found." The pipeline's picture is
shown on the camera's view.

- **Vision** (loose part feeder): **Edit Pipeline...** and **Reset Pipeline** (OpenPnP's default back).
- An advanced loose part feeder says it is experimental, as OpenPnP's does, and keeps two pipelines:
  **Feed Pipeline** (what finds the parts) and **Training Pipeline** (for making a part's template
  image), each with **Edit...** and **Reset**. Its part's angle is taken the other way round, as OpenPnP
  takes it, and a part found outside the camera's first view over the location is not picked.

The editor needs the feeder's part ("Feeder Bin has no part."): its pipelines are titled by it, and its
stages can read and write the part's template image.

<!-- src: src/setup/JPFeederForms.cpp (looseForm, advancedLooseForm); src/tasks/JPFeederFeed.cpp (looseFeed); src/tasks/JPFeederPipelines.cpp; src/model/JPFeeder.cpp (pickLocation, partHeightAbovePickLocation); src/app/JPlacerJobMachine.cpp (seeRects); src/app/JPlacerOpenPnpTabs.cpp (pipelineAction) -->

### The other kinds

Their **General Settings** and **Pick Location**; their own settings are kept as OpenPnP wrote them.

<!-- src: src/setup/JPFeederForms.cpp (forFeeder) -->

## From the other tabs

**Parts ▸ Pick Part** picks from a feeder holding the part (see [Parts](parts.md#the-toolbar)), and the
Job tab's **Edit Placement Feeder** comes here, to the placement's feeder (see [Jobs](jobs.md)).

<!-- src: src/app/JPlacerOpenPnpTabs.cpp (onPickPart, onEditFeeder) -->
