# Jobs

jplacer keeps jobs, boards, panels, parts and packages exactly as OpenPnP does, in OpenPnP's own files,
so a job, board or panel made in OpenPnP opens in jplacer as it is, and one saved by jplacer opens in
OpenPnP.

- A **job** (`.job.xml`) holds the boards and panels to be placed, each where it lies on the machine and
  which side is up, and what the job sets on them: which placements are placed, which boards and
  placements are enabled, whether each board's fiducials are checked, and how each placement's errors
  are handled.
- A **board** (`.jpboard`; OpenPnP's `.board.xml` is read) holds its placements (designator, side,
  location, rotation, part, type, comments, error handling, enabled), its own parts list (see
  [Boards](boards.md#the-boards-parts)), its dimensions and outline, and its solder paste pads. A board
  can be used many times, in one job or several; each use shares the board's file.
- A **panel** (`.panel.xml`) holds boards and other panels, each where it lies on the panel, the
  panel's own fiducials, and *pseudo-placements*: a placement of one of its boards (a fiducial, say)
  used to line up the whole panel.
- The library's **parts** and **packages** are kept in `library.db` (see [Parts](parts.md); a board's
  own parts, and a copy of each library part it uses, are kept in the board), and the boards and panels in
  use are listed in `boards.xml` and `panels.xml`, all in jplacer's configuration folder
  (`~/.config/jplacer`). A part has an id, a name, a height (and the depth it reaches through the
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
| **File ▸ Open Job…** (Ctrl+O) | Opens a `.job.xml` file. |
| **File ▸ Save Job** (Ctrl+S) | Saves the job to its file; a job never saved asks where, as Save Job As does. |
| **File ▸ Save Job As…** | Saves the job to a file you choose ([the file chooser](menus.md#choosing-a-file) starts at the job's own file); `.job.xml` is added if you leave it off. |

The window's title is *jplacer - * and the job's file name (*Untitled.job.xml* for a job never saved),
with a **\*** before the name while it has changes that are not saved. The job you had open is opened
again the next time jplacer starts.

If the job has unsaved changes when you start another job, open one, or close jplacer, you are asked
*Do you want to save your changes?* **Yes** saves them first (asking where, for a job never saved),
**No** lets them go, **Cancel** leaves things as they were. **File ▸ Open Recent Job...** offers the
ten jobs opened or saved last. Saving over a file that is there asks first whether to replace it.

A job's boards and panels are found by their file names: as written in the job, else beside the panel
that holds them, else beside the job.

<!-- src: src/app/JPlacerJob.cpp (newJob, open, save, saveAs, settle, title, mayClose, recentJobs); src/model/JPConfiguration.cpp (resolveBoard, resolvePanel); src/app/JPlacerMenuBuilder.cpp (the File menu) -->

## Older OpenPnP jobs

A job saved by an older OpenPnP (a list of boards, perhaps one panel of rows and columns of one board) is
converted when it is opened, as OpenPnP converts it: a copy of the old file is kept beside it as
`name.legacy.job.xml`; a panel of rows and columns becomes a panel file of its own (`name.panel.xml`),
its boards named `Brd[row,column]`; what was placed is kept. The job is then marked as changed, so
saving it writes the new form.

<!-- src: src/model/JPConfiguration.cpp (convertLegacyJob) -->

## The Job tab

The **Job** tab (the first in the work area, as in OpenPnP) shows the job's boards and panels and,
under them, the chosen one's placements.

**Boards**: every board and panel in the job, those on a panel under it and indented, with a board or
panel mark: **Board/Panel Id**, **Name** (pointing at it shows the file), **Width**, **Length**, **Side**,
**X**, **Y**, **Z**, **Rot.**, **Enabled?** and **Check Fids?**. Where one lies is shown on green once a
fiducial check has set it on the machine, on blue when set on its panel. Those straight in the job are
changed in the table; one on a panel can only be turned on or off and have its fiducial check changed.
Right-click for **Set Side**, **Set Enabled** and **Set Check Fids**.

| Button | |
|---|---|
| **Start** (**Pause**, **Resume**), **Step**, **Stop** | Run the job (see [Running the job](#running-the-job)). |
| **Alert Errors** / **Defer Errors** | Whether a placement's error (one whose error handling is Default) stops the job at once, or is reported at its end. Click to change. |
| **Add Board/Panel** (plus, with a menu) | **New Board...**, **Existing Board...**, **New Panel...**, **Existing Panel...**: put one in the job, at the machine's **Default Board Location** ([Machine Setup](machine-setup.md#settings)). |
| **Remove Board(s)/Panel(s)** (cross) | Takes the chosen ones (straight in the job) out of it. |
| **Move Camera To Board Location**, **Move Camera to the Next Board**, **Move Tool To Board Location** | Take the camera (or the Jog panel's nozzle) to where the board lies, at safe Z; Next chooses the next row first. |
| **Capture Camera Location** | Sets where the chosen board lies to where the camera is (its X, Y and rotation; its Z kept). |
| **Capture Tool Location** | Sets the chosen boards' Z to the nozzle's. |
| **Fiducial Check** | Looks at the chosen board's (or panel's) fiducials with the camera and sets where it lies from them, as the job does; one straight in the job has its X, Y and rotation set too. The camera is then taken to it. Each fiducial looked for has its package's footprint drawn over the cameras (as a job's check does too), the last chosen footprint until another is. |
| **Multiple Point Board Location** | Sets where the chosen board lies from placements you jog the camera over (below). |
| **View Job** | Opens the job viewer (see [Panels](panels.md#the-viewer)), following the boards chosen. |

<!-- src: src/ui/JPJobPanel.cpp (addBoard, addPanel); src/tasks/JPFiducialLocator.cpp (locate); src/tasks/JPCellJobMachine.h (lookingFor); src/ui/JPLocationsTableModel.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/app/JPlacerMachine.cpp (toolLocation, moveToolTo); src/app/JPlacerJobRun.cpp (fiducialCheck) -->

**Placements**: the chosen board's (or panel's) placements on its side facing up, with **Placed** and
**Status** (**Ready**, **Missing Part**, **Missing Feeder**, **Part Height**: its height is not known, or
**Disabled**). A board used once, straight in the job, is changed here as on the Boards tab (and New and
Remove Placement(s) work); otherwise only Enabled, Placed and Error Handling are changed, for this use of
it alone. **Move Camera To Placement Location** (and **To Next**), **Move Tool To Placement Location**,
**Capture Camera Placement Location** and **Capture Tool Placement Location** work as the board ones,
for the chosen placement. **Edit Placement Feeder** (one placement of a board) opens the
[Feeders](feeders.md) tab on the feeder holding its part: an enabled one first, else a disabled one, else
a new feeder is made for it. Right-click for **Set Type**,
**Set Side**, **Set Placed**, **Set Enabled** and **Set Error Handling**; **Space** turns the chosen
placement on or off.

### Verifying placements

The chosen placement's footprint (a fiducial's too) is drawn over the cameras' pictures: its part's
package's footprint, its pads and pin 1's mark, not its body, as OpenPnP's package reticle. The cameras
show one footprint, the last chosen, here or on the Packages tab (or chosen there by **View ▸ Selections
in Tables ▸ Linked**); it stays when another tab is shown. As OpenPnP's, it is turned by the rotation of
the tool chosen in Jog, as that is now: take the camera to the placement (**Move Camera To Placement
Location**), which turns the camera's rotation axis to the placement's rotation, and look: the footprint
should lie on the board's pads, turned as the part will be placed.

- **Turn 90°** turns the chosen placement a quarter counter-clockwise: a correction to the CAD file's
  rotation (which the placement keeps apart, as it came). Take the camera to it again to see it turned.
- **Verified, Next** marks the chosen placement verified and takes the camera to the next one shown, facing
  up, not verified yet.

The **Verified** column (here and on the Boards tab) says whether each was, and when (*Yes*, *Position
only*). Changing a placement's position, rotation or part takes its mark off: what was checked is no longer
what is placed.

Importing from the CPL and BOM turns a placement by its footprint's **Zero Rotation** (see
[Packages](packages.md#the-packages-tabs)), where the library's footprint for its part has one: CAD tools
disagree about which way 0° faces. The CAD file's rotation is kept with it.

<!-- src: src/ui/JPJobPlacementsPanel.cpp (footprintOf, showChosenFootprint, turnChosen, verifyChosen); src/ui/JPFootprintOverlay.cpp; src/app/JPlacerMachine.cpp (selectedToolRotation, moveToolTo); src/ui/JPPlacementsTableModel.cpp (kVerified, setText, applyPart); src/model/JPPlacement.h (cadRotation, Verified); src/import/JPCplBomImport.cpp (build); src/model/JPBoardImporter.cpp (read); src/app/JPlacerOpenPnpTabs.cpp (showFootprint) -->

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
offers). A part that is not in the library (the board's own, or not chosen yet) keeps no stock, and says so.
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
how fast. While a job runs, another job or another cell is not opened.

<!-- src: src/app/JPlacerJobRun.cpp (startPauseResume, step, stop, start, run); src/ui/JPJobPanel.cpp (updateJobActions); src/app/JPlacerJob.cpp (settle); src/app/JPlacerMachine.cpp (openCell) -->

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
   is found by its settings' pipeline (or by its package's footprint pads, else its body, drawn as the
   camera should see them and matched against the picture), turned less than 45° either way of the angle
   wanted (all the way round with Rotation: Full). Pre-rotated, the nozzle is moved and turned by what
   was seen and the part looked at again, until it is off by less than the machine's Max. Linear Offset
   (its corner too, as turned) and Max. Angular Offset, or the machine's passes run out. The settings'
   Vision Center Offsets are taken off what was found; offsets further than the nozzle tip's Max. Pick
   Tolerance fail it ("Part R1 bottom vision offsets length 0.713mm larger than the allowed Max. Pick
   Tolerance 0.500mm set on nozzle tip NT1."), as does the Part size check. Each look is tried again up
   to Max Vision Attempts. The part is then placed with what was found taken off: the nozzle turned by
   what the part is off, and moved by its offset on the nozzle. A part with bottom vision off is placed as
   it was picked.

<!-- src: src/machine/JPJobProcessorConfig.h; src/tasks/JPJobProcessor.cpp (preFlight, plan, ordered, planner, pick, align, place, cleanup); src/vision/JPPartFinder.cpp; src/tasks/JPCellJobMachine.cpp (alignPart); src/tasks/JPBottomVision.cpp (findOffsets); src/tasks/JPAlignRequests.cpp; src/tasks/JPFiducialLocator.cpp; src/model/JPFiducialFit.cpp; src/tasks/JPCellJobMachine.cpp (locateFiducial, changeTip) -->

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
