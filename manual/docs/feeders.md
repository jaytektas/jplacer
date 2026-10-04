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
| **Feed...** | Feeds the chosen feeder: its count moves on to the next part. |
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
jplacer works out where a strip and a tray feeder pick, and feeds them. The other kinds are kept and
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
  **Auto Setup** is not yet available.
- **Vision**: **Use Vision?**, **Extrapolation Distance**, **Parallax Diameter** and **Parallax Angle** are
  kept, and **Reset Vision** forgets the holes vision found; jplacer does not yet check the holes with
  the camera, so the parts are picked where the hole locations put them. **Edit Pipeline** and **Reset
  Pipeline** are not yet available.
- **Locations**: the **Reference Hole Location**, the hole nearest the first part's centre, in the
  direction the parts continue, with the pick height as its Z; and the **Next Hole Location**, any hole
  further along.

The part is picked across the tape from its hole, as EIA-481 tape lays it out: half the tape width less
0.5 mm across, 2 mm along, then one part pitch further for each part taken.

<!-- src: src/setup/JPFeederForms.cpp (stripForm, act); src/model/JPFeeder.cpp (pickLocation, idealLineLocations, feed) -->

### Tray feeder

Parts in rows and columns in a tray. **Pick Location** is the first part's place. **Offsets** are the
distances from one part to the next in X and in Y, and **Tray Count** how many there are each way; an
offset of 0 with more than one part that way is kept, but jplacer says it will fail. **Feed Count** is
the parts taken so far (**Reset** sets it to 0).

<!-- src: src/setup/JPFeederForms.cpp (trayForm); src/model/JPFeeder.cpp (pickLocation, feed) -->

### The other kinds

Their **General Settings** and **Pick Location**; their own settings are kept as OpenPnP wrote them.

<!-- src: src/setup/JPFeederForms.cpp (forFeeder) -->

## From the other tabs

**Parts ▸ Pick Part** picks from a feeder holding the part (see [Parts](parts.md#the-toolbar)), and the
Job tab's **Edit Placement Feeder** comes here, to the placement's feeder (see [Jobs](jobs.md)).

<!-- src: src/app/JPlacerOpenPnpTabs.cpp (onPickPart, onEditFeeder) -->
