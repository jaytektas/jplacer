# Jobs

jplacer keeps jobs, boards and panels in files of its own: a job is a `.jpjob`, a board a `.jpboard` and a
panel a `.jppanel`. OpenPnP's `.job.xml`, `.board.xml` and `.panel.xml` are read as they are, so a job,
board or panel made in OpenPnP opens in jplacer; saved, each is written as jplacer's file beside OpenPnP's,
which is left as it was.

- A **job** (`.jpjob`; OpenPnP's `.job.xml` is read) holds the boards and panels to be placed, each where it lies on the machine and
  which side is up, and what the job sets on them: which placements are placed, which boards and
  placements are enabled, whether each board's fiducials are checked, and how each placement's errors
  are handled.
- A **board** (`.jpboard`; OpenPnP's `.board.xml` is read) holds its placements (designator, side,
  location, rotation, part, type, comments, error handling, enabled), its parts list, each pointing
  at a library part (see [Boards](boards.md#the-boards-parts)), its dimensions and outline, and its solder paste pads. A board
  can be used many times, in one job or several; each use shares the board's file.
- A **panel** (`.jppanel`; OpenPnP's `.panel.xml` is read) holds boards and other panels, each where it lies on the panel, the
  panel's own fiducials, and *pseudo-placements*: a placement of one of its boards (a fiducial, say)
  used to line up the whole panel.
- The library's **parts** and **packages**, the only ones there are, are kept in `library.db` (see
  [Parts](parts.md)), in jplacer's configuration
  folder (`~/.config/jplacer`). The boards and panels open are the job's (see [Boards](boards.md)). A part has an id, a name, a height (and the depth it reaches through the
  board), a package, a speed and how many times a pick is tried again. A package has an id, a
  description, a tape specification, vacuum levels, a footprint (its pads and body), the nozzle tips
  that can pick it, and its vision settings. Parts and packages are found by id whatever its case.

The job is edited on the Job tab (below), panels, boards, parts and packages on the [Panels](panels.md),
[Boards](boards.md), [Parts](parts.md) and [Packages](packages.md) tabs, as in OpenPnP.

<!-- src: src/model/JPJob.h; src/model/JPBoard.h; src/model/JPPanel.h; src/model/JPPart.h; src/model/JPPackage.h; src/model/JPConfiguration.h -->

## New, open and save

| Entry | |
|---|---|
| **File ▸ New Job** (Ctrl+N) | Starts an empty job. |
| **File ▸ Open Job…** (Ctrl+O) | Opens a `.jpjob` file, or OpenPnP's `.job.xml`. |
| **File ▸ Save Job** (Ctrl+S) | Saves the job to its file; a job never saved asks where, as Save Job As does. |
| **File ▸ Save Job As…** | Saves the job to a file you choose ([the file chooser](menus.md#choosing-a-file) starts at the job's own file); `.jpjob` is added if you leave it off. |

A job opened from OpenPnP's `.job.xml` is saved beside it as jplacer's file, under its own name
(`sim.job.xml` becomes `sim.jpjob`, a number added if that name is taken; the status bar says *Saved as
jplacer's …*), and from then on the job is that file. The `.job.xml` is left exactly as it was.

The window's title is *jplacer - * and the job's file name (*Untitled.jpjob* for a job never saved),
with a **\*** before the name while it has changes that are not saved. The job you had open is opened
again, with its boards and panels, the next time jplacer starts, when Machine Setup's **Auto-load most
recent job?** is ticked.

If the job has unsaved changes when you start another job, open one, or close jplacer, you are asked
*Do you want to save your changes?* **Yes** saves them first (asking where, for a job never saved),
**No** lets them go, **Cancel** leaves things as they were. Then each of its boards and panels with
changes is asked about the same way, as they close with the job. **File ▸ Open Recent Job...** offers the
ten jobs opened or saved last. Saving over a file that is there asks first whether to replace it.

A job's boards and panels are found by their file names: as written in the job, else beside the panel
that holds them, else beside the job.

<!-- src: src/app/JPlacerJob.cpp (newJob, open, save, saveAs, writeTo, settle, title, mayClose, recentJobs); src/app/JPlacerJob.h (kUntitled); src/model/JPConfiguration.cpp (loadJob, saveJob, besideAs, resolveBoard, resolvePanel); src/app/JPlacerMenuBuilder.cpp (the File menu) -->

## Older OpenPnP jobs

A job saved by an older OpenPnP (a list of boards, perhaps one panel of rows and columns of one board) is
converted when it is opened, as OpenPnP converts it: a panel of rows and columns becomes a panel file of its
own (`name.jppanel`, beside the job), its boards named `Brd[row,column]`; what was placed is kept. The job is
then marked as changed, so saving it writes it as jplacer's `.jpjob` (the old file is left as it was).

<!-- src: src/model/JPConfiguration.cpp (convertLegacyJob) -->

## The Job tab

The **Job** tab (the first in the work area, as in OpenPnP) shows the job's boards and panels and,
under them the chosen one: a board's **Placements**, with its imports, revisions, parts and viewer (see
[Boards](boards.md#placements)); or a panel's definition, its children and fiducials (see
[Panels](panels.md)). There is no other place a board or panel is kept: each is the job's.

**Boards**: every board and panel in the job, those on a panel under it and indented, with a board or
panel mark: **Board/Panel Id**, **Name** (pointing at it shows the file), **Width**, **Length**, **Side**,
**X**, **Y**, **Z**, **Rot.**, **Enabled?** and **Check Fids?**. Where one lies is shown on green once a
fiducial check has set it on the machine, on blue when set on its panel. Those straight in the job are
changed in the table; one on a panel can only be turned on or off and have its fiducial check changed. A
board's or panel's **Name**, **Width** and **Length** are its own, changed wherever it is.
Right-click for **Set Side**, **Set Enabled** and **Set Check Fids**.

| Button | |
|---|---|
| **Start** (**Pause**, **Resume**), **Step**, **Stop** | Run the job (see [Running the job](#running-the-job)). |
| **Alert Errors** / **Defer Errors** | Whether a placement's error (one whose error handling is Default) stops the job at once, or is reported at its end. Click to change. |
| **Add Board/Panel** (plus, with a menu) | **New Board...**, **Existing Board...**, **New Panel...**, **Existing Panel...**, and **Copy of the Chosen Board...** (or **Panel...**: a copy saved as a new file): put one in the job, at the machine's **Default Board Location** ([Machine Setup](machine-setup.md#settings)). |
| **Remove Board(s)/Panel(s)** (cross) | Takes the chosen ones (straight in the job) out of it. |
| **Move Camera To Board Location**, **Move Camera to the Next Board**, **Move Tool To Board Location** | Take the camera (or the Jog panel's nozzle) to where the board lies, at safe Z; Next chooses the next row first. |
| **Capture Camera Location** | Sets where the chosen board lies to where the camera is (its X, Y and rotation; its Z kept). |
| **Capture Tool Location** | Sets the chosen boards' Z to the nozzle's. |
| **Fiducial Check** | Looks at the chosen board's (or panel's) fiducials with the camera and sets where it lies from them, as the job does; one straight in the job has its X, Y and rotation set too (and its Z, measured, with the Fiducial Locator's **Measure Board Z?**, as a job's check sets it). The camera is then taken to it. Each fiducial looked for has its package's footprint drawn over the cameras (as a job's check does too), the last chosen footprint until another is. |
| **Measure Board Z** (a toggle, lit when on) | Machine Setup's Fiducial Locator **Measure Board Z?**, turned on or off from here: with it on, a fiducial check (this one and a job's) measures the board's height at each of its fiducials and sets its Z from their mean. Pressed, only that setting is changed (and saved with the machine's setup, Machine Setup showing it); press it again to take it back. It follows a change made in Machine Setup. |
| **Multiple Point Board Location** | Sets where the chosen board lies from placements you jog the camera over (below). |
| **Photograph Board** (camera) | Photographs the chosen boards (a panel's: every board on it) with the head camera, one after another; **Stop** stops it (see [Board photos](#board-photos)). |
| **View Job** | Opens the job viewer (see [Panels](panels.md#the-viewer)), following the boards chosen. |

<!-- src: src/ui/JPJobPanel.cpp (addBoard, addPanel, setPhotographing, machineChanged); src/app/JPlacerMachine.cpp (setMeasureBoardZ); src/tasks/JPFiducialLocator.cpp (locate); src/tasks/JPCellJobMachine.h (lookingFor); src/ui/JPLocationsTableModel.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/app/JPlacerMachine.cpp (toolLocation, moveToolTo); src/app/JPlacerJobRun.cpp (fiducialCheck) -->

**Placements**: the chosen board's (or panel's) placements on its side facing up, with **Placed** and
**Status** (**Ready**, **Missing Part**, **Missing Feeder**, **Part Height**: its height is not known, or
**Disabled**). A board's own (its placements' IDs, parts, sides, where they lie, types, comments) are
changed here, for every use of it (and New and Remove Placement(s) work: the board's parts that no placement
uses after a removal go with them, so the next import of those parts starts from the library as it is); Enabled, Placed and Error
Handling for this use alone. **Move Camera To Placement Location**, **Move Tool To Placement Location**,
**Capture Camera Placement Location** and **Capture Tool Placement Location** work as the board ones,
for the chosen placement. **Move Camera To Previous Placement Location** and **To Next** (the arrows either
side of it) choose the placement before or after the chosen one, as the table shows them (none chosen: the
last, or the first), and take the camera there. **Edit Placement Feeder** (one placement of a board) opens the
[Feeders](feeders.md) tab on the feeder holding its part: an enabled one first, else a disabled one, else
a new feeder is made for it. Right-click for **Set Type**,
**Set Side**, **Set Placed**, **Set Enabled** and **Set Error Handling**; **Space** turns the chosen
placement on or off.

A placement whose part is not in the library shows in **Part** what the files called it: its part's name
(else its value and footprint) and *(to be chosen)*, or *(not in the library)* when its library part was
deleted. What the files said is kept with the board, whatever happens in the library; point at the cell for
all of it, and click it to choose the part again.

<!-- src: src/ui/JPPlacementsTableModel.cpp (text, cellTooltip, filesName); src/model/JPBoardPart.h; src/ui/JPJobPlacementsPanel.cpp (removePlacements); src/model/JPBoard.cpp (dropUnusedParts) -->

**Columns** also offers each placement's part's columns and its part's package's, headed **Part ▸** and
**Package ▸** (**Part ▸ Value**, **Part ▸ MPN**, **Package ▸ Name**…, the fields you added to parts and
packages too), hidden until you tick them. They are the Parts and Packages tabs' own and are changed here
as there: the library's part (or package) is changed, so every placement of it shows the change, and the
status line says how many placements here are of it. **Edit ▸ Undo** takes it back as a change to the
library. A placement with no part shows nothing there.

<!-- src: src/ui/JPJobPlacementsPanel.cpp (m_linked, libraryEdited); src/ui/JPLinkedColumnsModel.cpp; src/ui/JPTable.cpp (setCells) -->

### Verifying placements

The chosen placement's footprint (a fiducial's too) is drawn over the cameras' pictures: its part's
package's footprint, its pads and pin 1's mark, not its body, as OpenPnP's package reticle. The cameras
show one footprint, the last chosen, here or on the Packages tab (or chosen there by **View ▸ Selections
in Tables ▸ Linked**); it stays when another tab is shown. On a camera with a rotation axis it is turned
by the camera's rotation, as that is now: take the camera to the placement (**Move Camera To Placement
Location**), which turns the camera to the placement's rotation whatever tool Jog has, and look: the
footprint should lie on the board's pads, turned as the part will be placed.

- **Turn 90°** turns the chosen placement a quarter counter-clockwise: a correction to the CAD file's
  rotation (which the placement keeps apart, as it came). Take the camera to it again to see it turned.
- **Verified, Next** marks the chosen placement verified and takes the camera to the next one shown, facing
  up, not verified yet.

The **Verified** column says whether each was, and when (*Yes*, *Position
only*). Changing a placement's position, rotation or part takes its mark off: what was checked is no longer
what is placed.

Importing from the CPL and BOM turns a placement by its footprint's **Zero Rotation** (see
[Packages](packages.md#the-packages-tabs)), where the library's footprint for its part has one: CAD tools
disagree about which way 0° faces. The CAD file's rotation is kept with it.

<!-- src: src/ui/JPJobPlacementsPanel.cpp (footprintOf, showChosenFootprint, turnChosen, verifyChosen); src/ui/JPFootprintOverlay.cpp; src/app/JPlacerMachine.cpp (reticleRotation, moveToolTo); src/ui/JPPlacementsTableModel.cpp (kVerified, setText, applyPart); src/model/JPPlacement.h (cadRotation, Verified); src/import/JPCplBomImport.cpp (build); src/model/JPBoardImporter.cpp (read); src/app/JPlacerOpenPnpTabs.cpp (showFootprint) -->

The status line shows the placements placed: of the whole job, and of the board chosen.

**Multiple Point Board Location** shows its steps across the top of the Job tab, with **Cancel** and
**Next**: choose two or more placements of the board (four, near its corners, is better) and click Next;
the camera goes near the first; jog its crosshairs over the placement's centre and click Next, and so on
for each (the shortest way round). The board is then fitted to them as a fiducial check fits it, and
refused if it scales or shears more than 5 % or moves more than 5 mm. **Finish** takes the camera to the
board's origin; **Cancel** puts the board back where it was.

<!-- src: src/ui/JPBoardLocationProcess.cpp; src/ui/JPInstructions.cpp; src/ui/JPJobPanel.cpp (showInstructions) -->

<!-- src: src/ui/JPJobPlacementsPanel.cpp (onEditFeeder, updateActions); src/ui/JPFeedersPanel.cpp (showFeederForPart); src/ui/JPPlacementsTableModel.cpp (status, setLocation) -->

## Shortages

**Job ▸ Shortages…** sets the job's parts against the stock, the parts shortest of stock first. Each row
gives the **Part**, how many placements are **Left to place** (enabled, on the side facing up, not placed
yet), the **Attrition** allowed (how many more, at the part's own share, or the share its ledger measured,
or *not known*), how many are **In stock** in its open lots, how many it is **Short**, its **Lots** (each
with where it is kept and what it holds) and, when short, where it is bought (**Buy from**: the part's
offers). A part not chosen yet keeps no stock, and says so.
The line at the top counts the placements, the parts and those short.

Stock never stops a run: the feeders hold what is placed, and a part can be loaded as the run reaches it.

<!-- src: src/model/JPShortages.cpp; src/app/JPlacerShortagesDialog.cpp; src/app/JPlacerMenuBuilder.cpp (job.shortages) -->

## Running the job

**Start** runs the job, a step after another, until every placement is placed; while it runs the button
is **Pause**, which stops it after the step under way, and then **Resume**. **Step** does one step (on to the next that moves the
machine, with Machine Setup's Step Next Motion) and pauses. **Stop** stops it: the nozzles are emptied at the discard location and the head is parked. The
machine must be connected (the buttons are greyed until it is) and homed. If every placement is placed
already, Start asks whether to mark them all not placed first. The status line says what the job is
doing ("Feed …", "Pick … using nozzle N1.", "Placing …"), and at the end how many parts were placed and
how fast. While a job runs, another job or another cell is not opened. While the machine is busy with a
task of its own (a Test Alignment, a feeder's or a fiducial check's moves, a Recycle), Start, Step, Stop and
another such task are refused: the status line says "The machine is busy with a task: wait for it to
finish", and the click does nothing (OpenPnP queues it instead). **Start**, **Step** and **Stop** are also
in the toolbar at the top of the window, after the machine's power and home buttons, so a job can be run
from any tab: they are the Job tab's, greyed and turned to **Pause** or **Resume** as those are.

<!-- src: src/app/JPlacerJobRun.cpp (startPauseResume, step, stop, start, run, machineTask, fiducialCheck, taskUnderWay); src/ui/JPJobPanel.cpp (updateJobActions, addRunButtons); src/app/JPlacerJob.cpp (settle); src/app/JPlacerMachine.cpp (openCell) -->

**Checking the job.** Before a run starts (and with **Job ▸ Check Job…** at any time), the job's data is
checked as one list, each thing once with every placement or part it is about (a placement named by its
board's ID and its own, `Brd1⇒R1`), and where it is put right. Only the placements a run places are looked
at: enabled, not placed, their side up on an enabled board.

- **Stop**: the run would refuse it: a placement ID used twice on a board; **No part chosen** (the board's
  part is still to be chosen); a part the library does not have; a part with **No package**; **No nozzle tip
  on the machine fits its package**.
- **Check**: worth putting right first: placements **Not verified on the machine** (see [Verifying
  placements](#verifying-placements)), parts whose **Height** is not known, parts with **No footprint** to
  draw or check against.
- **Note**: parts **No feeder holds yet** (the run asks for them, see [Load as you go](#load-as-you-go));
  parts **Short of stock**, of those whose stock is kept (a part with no lots at all is not).

**Start** with nothing to stop or check runs at once. Otherwise **Check Job** shows the list: with anything
that stops the run it is not started (**Close**); with only things to check or note, **Start Anyway** runs
it as it is and **Cancel** does not. Choose a row to see under the list all it is about (the first 60). The
counts at the top say how many are to put right, to check and to note.

<!-- src: src/model/JPJobCheck.cpp; src/app/JPlacerJobCheckDialog.cpp; src/app/JPlacerJobRun.cpp (start, checkJob, machineTipIds); src/app/JPlacerMenuBuilder.cpp (job.check) -->

A job goes as OpenPnP's does:

1. **Checks.** The placements to place are those enabled, not placed, with their side facing up on an
   enabled board. Each must have a part, the part a package, and a nozzle tip that fits the package and a
   nozzle; a board with an ID twice is refused. A part no feeder holds is not refused: it is asked for when
   the run gets to it (see [Load as you go](#load-as-you-go)). Then the head goes
   to safe Z and anything left on a nozzle is discarded.
2. **Fiducials.** Each board and panel with **Check Fids?** has its fiducials found by the camera (a
   panel's outermost first), each as a round mark the size of its package's footprint pad, looked at
   again once centred as its [Fiducial Vision Settings](vision.md#the-settings) say (by default up to
   three times, until a look moves it less than 0.2 mm; from either side with a parallax diameter). Where the board lies is fitted to them: with two,
   moved, turned and scaled; with three or more, fully. A fit that scales or shears more than 5 %, or moves
   the board more than 5 mm, is refused.
3. **Planning.** The placements still to do, lowest rank first (a rank ten or more above the lowest
   waits for it), are ordered as Machine Setup's **Job order** says (by nozzle tip unless set otherwise,
   see [Job Processors](machine-setup.md#job-processors)); each nozzle is given one, with the tip on it if one fits, else a tip that
   does. A nozzle after the first takes, of those its tip fits, the one quickest for the head to reach from
   the middle of the picks and the middle of the places already planned (as OpenPnP's planner: the time the
   head's X and Y axes take, from their speed and acceleration, to where the head goes for that nozzle);
   with two nozzles, the other way round where that is quicker.
4. **Each cycle**: the nozzle tips changed where needed (by their changer steps), the nozzles turned for
   the pick, each part fed (retried as the feeder's Feed Retry Count says; an empty feeder is turned off
   and the next one holding the part used) and picked (retried as its Pick Retry Count says, a part that
   was not picked discarded), then placed where the board lies, as high as the part, turned to the
   placement's rotation. Then the next cycle, until all are placed; then the head is parked.

5. **Alignment** (bottom vision), when the machine's bottom vision is on and the part's
   [Bottom Vision Settings](vision.md#the-settings) are enabled: the nozzle takes the part over the
   camera looking up, its bottom at the camera's focus, turned to the placement's angle (pre-rotated, as
   the settings' Pre-rotate and the machine say; else at 0°), as OpenPnP's bottom vision does. The part
   is found by its settings' pipeline, as OpenPnP finds it, turned less than 45° either way of the angle
   wanted (all the way round with Rotation: Full). Pre-rotated, the nozzle is moved and turned by what
   was seen and the part looked at again, until it is off by less than the machine's Max. Linear Offset
   (its corner too, as turned) and Max. Angular Offset, or the machine's passes run out. The settings'
   Vision Center Offsets are taken off what was found; offsets further than the nozzle tip's Max. Pick
   Tolerance fail it ("Part R1 bottom vision offsets length 0.713mm larger than the allowed Max. Pick
   Tolerance 0.500mm set on nozzle tip NT1."), as does the Part size check. Each look is tried again up
   to Max Vision Attempts. The part is then placed with what was found taken off: the nozzle turned by
   what the part is off, and moved by its offset on the nozzle. A part with bottom vision off is placed as
   it was picked.

<!-- src: src/machine/JPJobProcessorConfig.h; src/tasks/JPJobProcessor.cpp (preFlight, plan, ordered, planner, pick, align, place, cleanup); src/tasks/JPCellJobMachine.cpp (alignPart); src/tasks/JPBottomVision.cpp (findOffsets); src/tasks/JPAlignRequests.cpp; src/tasks/JPFiducialLocator.cpp; src/model/JPFiducialFit.cpp; src/tasks/JPCellJobMachine.cpp (locateFiducial, changeTip) -->

When something fails, the job pauses and says why (**Job Error**); the board, placement, part or feeder
it is about is chosen on its tab. **Resume** goes on from there. With **Defer Errors** (or a placement's
own error handling set to Defer), a placement that fails is put off instead: its feeder's fault is
counted (shown in the Feeders tab's **Faults**; by default three in its last six feeds turn the feeder off), and it
is tried again later, up to Machine Setup's Max Placement Attempts, or left in error; the job goes on, and says at its end how many
errors there were.

<!-- src: src/tasks/JPJobProcessor.cpp (plannedStep, finish); src/app/JPlacerJobRun.cpp (run); src/app/JPlacerOpenPnpTabs.cpp (showSource); src/model/JPFeeder.cpp (recordJobFault) -->

## The plan

**Job ▸ Plan…** shows the job's placements left to place in groups of one part each, in the order a run
places them: **#**, the **Part**, how many are **Left**, its **Height**, its **Package**, and where it
**Comes from** (the feeder holding it, or a load the run will ask for). The line at the top counts the
parts, the placements and the loads.

**Order by** chooses the order:

- **Machine's job order** (as a job starts): the run orders the placements as Machine Setup's **Job
  Order** says, as OpenPnP does; the groups are listed by name.
- **Height**: the lowest parts first, so tall ones are not in the nozzle's way; then the smallest package.
- **Package size**: the smallest package body first; then the lowest.
- **Most first**: the groups with the most placements first.
- **Name**.

**Move Up** and **Move Down** move the chosen group: groups moved by hand come first, in their order, and
the rest follow as sorted. **Sort Again** drops the order set by hand, and choosing another order sorts
them all again. With an order chosen or a group moved, the run places group by group: the Job Order
applies within a group, and a nozzle is given a placement of the next group only when the groups before
it have none left for it. The plan is kept in the job's file.

<!-- src: src/model/JPJobPlan.cpp; src/app/JPlacerPlanDialog.cpp; src/tasks/JPJobProcessor.cpp (preFlight, plan); src/model/JPJob.cpp (planSort, planOrder) -->

## Load as you go

A part does not have to be on a feeder for a job to start. The run places every part that is loaded first;
when only parts no feeder holds are left, it pauses and asks for the next one: in the plan's order (see
[The plan](#the-plan)), or with no plan the lowest first (tall parts last, out of the nozzle's way), then
by name. **Load *part*** says what to load, how it comes (the
part's first packaging: its tape, pitch and how the part is turned in it), and into which **Lane**: a
lane is a strip feeder, laid by hand. The lanes offered are those free for it, best first: an empty one (no
part, or turned off), then one whose part this run no longer needs (said, so it is taken off first); of
each, those of the part's tape width first, and one of another width says so. A lane holding a part the
run still needs is never offered. Where the part's stock is kept, **Lot** chooses which of its lots is
being loaded (the only one is chosen already).

- **Continue**: lay the strip as the lane's strips lie, its first part at the lane's first hole. The lane
  is set to the part, turned on, its feed count started again (and the holes its vision found forgotten),
  the chosen lot loaded on it (the lot there before taken off), and the run goes on.
- **Skip This Part**: its placements are left unplaced, in error ("Skipped: not loaded"), and the run goes
  on; the end of the run counts them with the errors and the log names each.
- **Stop** stops the run. Escape and the [x] leave it paused: **Resume** asks again.

With no lane free, the prompt says so: set a feeder up for the part on the Feeders tab, then
**Continue**; or skip it.

<!-- src: src/tasks/JPJobProcessor.cpp (preFlight, openPendingWorkable, skipPart, next); src/model/JPLaneChoice.cpp; src/app/JPlacerLoadDialog.cpp; src/app/JPlacerJobRun.cpp (askToLoad, run); src/model/JPFeeder.h (isLane, tapeWidth) -->

## Board photos

A board's photo is one picture of it made from many taken with the head camera: a record of the bare
board, or of what was placed and where. The Job tab's **Photograph Board** takes one of each board chosen
(the machine connected and homed, the job stopped), the status line saying how far it has got
("Photographing Brd1: picture 10 of 49"); **Stop** stops it, and nothing is kept of the board under way.
With the Job Processor's **Photograph boards as the job starts?** or **…as the job ends?** ticked
([Machine Setup](machine-setup.md#job-processors)), the job takes them too: each enabled board once its
fiducials are checked, before a part is placed, and once every placement is done, before the head parks;
a picture a step, so **Pause**, **Step** and **Stop** work between them. A photo that cannot be taken is
said in the status line and the log, and the job goes on.

<!-- src: src/ui/JPJobPanel.cpp (setPhotographing); src/app/JPlacerJobRun.cpp (photograph, stop); src/tasks/JPJobProcessor.cpp (startPhotos, photograph); src/machine/JPJobProcessorConfig.h -->

The pictures cover the board's outline and 1 mm round it (a board with no size: its placements and 2 mm
round them), a quarter of each overlapping the next, row by row from the top, each row the other way from
the last, at the height of the board's surface; a picture out of the camera's reach is left out. Each is
put where the machine says it was taken, then lined up with its neighbours by what they both show, and
they are blended across their overlaps. The photo is at the camera's own scale, coarser where that would
make it more than 25 million pixels, square to the board whatever its angle on the machine. A board needs
its file saved first: its photos are kept beside it, in a folder of its name and `.photos`
(`grblhal.jpboard.photos`), each a PNG named by when it was taken, the board's id in the job and why
(`2026-10-12 14-42-45 Brd1 jobEnd.png`), with what it is beside it (its scale, where it lies on the
board, the job and run). Every photo is kept until you delete it.

<!-- src: src/tasks/JPBoardPhotographer.cpp (begin, centres, finish); src/tasks/JPPhotoStitch.cpp (stitch, shiftBetween); src/model/JPBoardPhoto.cpp (folderOf, fileIn, toJson) -->

What is said of a photo (in the log, and under it in the Board Viewer): a picture moved more than
0.1 mm to line up with its neighbours (the camera was that far from where the machine said: check the head
camera's calibration and the axes' backlash compensation); neighbours still disagreeing by more than
0.05 mm once lined up (the lens not fully straightened, or the board moved); overlaps too plain to line up
by (the bare spaces between boards: those pictures stay where the machine put them); pictures out of reach,
and the share of the board's area not in the photo (shown clear).

<!-- src: src/tasks/JPBoardPhotographer.cpp (finish); src/tasks/JPPhotoStitch.h -->

## Runs

Each **Start** of a job begins a **run**, recorded as it goes in `runs.db` beside the library: when it
started, the job, its boards at their revisions, each part fed (from which feeder, and the stock lot the
feeder carries) and each placement placed. So a run cut short (jplacer closed, or the computer stopped)
still says what it used.

When the run ends (finished, or stopped with **Stop**), its parts are written to the stock's ledger, one
entry of each for every lot its feeders carried: those placed as **Used**, those fed and not placed
(mis-picked, dropped, discarded) as **Lost**, each with the run as its reference. A run left open is
written when jplacer next opens, ended as **Interrupted**. A run paused by an error stays open until it is
resumed and ends, or is stopped.

While a run goes, after each feed from a feeder carrying a lot, if the lot holds fewer by its count than the
run still has to place of the part, the status line and the log say so once: "Upper Strips - 1: reel A has
about 15 left by its count, and 40 of C0603-100n are still to place".

**Job ▸ Runs…** lists the newest 200 runs: **Started**, **Ended**, **How** it ended (**Finished**,
**Stopped**, **Interrupted**, or **Open** while it runs), the **Job**, the **Boards** (each with its
revision), how many were **Placed**, and whether its parts are in the **Ledger** yet.

<!-- src: src/model/JPRunStore.cpp; src/model/JPRunLedger.cpp (write, writeAll); src/app/JPlacerJobRun.cpp (beginRun, endRun, start); src/tasks/JPJobProcessor.cpp (pick, place: material); src/model/JPConfiguration.cpp (load); src/app/JPlacerRunsDialog.cpp -->
