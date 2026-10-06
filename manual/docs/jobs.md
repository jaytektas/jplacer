# Jobs

jplacer keeps jobs, boards, panels, parts and packages exactly as OpenPnP does, in OpenPnP's own files,
so a job, board or panel made in OpenPnP opens in jplacer as it is, and one saved by jplacer opens in
OpenPnP.

- A **job** (`.job.xml`) holds the boards and panels to be placed, each where it lies on the machine and
  which side is up, and what the job sets on them: which placements are placed, which boards and
  placements are enabled, whether each board's fiducials are checked, and how each placement's errors
  are handled.
- A **board** (`.board.xml`) holds its placements (designator, side, location, rotation, part, type,
  comments, error handling, enabled), its dimensions and outline, and its solder paste pads. A board
  can be used many times, in one job or several; each use shares the board's file.
- A **panel** (`.panel.xml`) holds boards and other panels, each where it lies on the panel, the
  panel's own fiducials, and *pseudo-placements*: a placement of one of its boards (a fiducial, say)
  used to line up the whole panel.
- **Parts** and **packages** are kept in `parts.xml` and `packages.xml`, and the boards and panels in
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
| **File ▸ Save Job As…** | Saves the job to a file you choose; `.job.xml` is added if you leave it off. |

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
| **Fiducial Check** | Looks at the chosen board's (or panel's) fiducials with the camera and sets where it lies from them, as the job does; one straight in the job has its X, Y and rotation set too. The camera is then taken to it. |
| **Multiple Point Board Location** | Sets where the chosen board lies from placements you jog the camera over (below). |
| **View Job** | Opens the job viewer (see [Panels](panels.md#the-viewer)), following the boards chosen. |

<!-- src: src/ui/JPJobPanel.cpp (addBoard, addPanel); src/ui/JPLocationsTableModel.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/app/JPlacerMachine.cpp (toolLocation, moveToolTo); src/app/JPlacerJobRun.cpp (fiducialCheck) -->

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

The status line shows the placements placed: of the whole job, and of the board chosen.

**Multiple Point Board Location** shows its steps across the top of the Job tab, with **Cancel** and
**Next**: choose two or more placements of the board (four, near its corners, is better) and click Next;
the camera goes near the first; jog its crosshairs over the placement's centre and click Next, and so on
for each (the shortest way round). The board is then fitted to them as a fiducial check fits it, and
refused if it scales or shears more than 5 % or moves more than 5 mm. **Finish** takes the camera to the
board's origin; **Cancel** puts the board back where it was.

<!-- src: src/ui/JPBoardLocationProcess.cpp; src/ui/JPInstructions.cpp; src/ui/JPJobPanel.cpp (showInstructions) -->

<!-- src: src/ui/JPJobPlacementsPanel.cpp (onEditFeeder, updateActions); src/ui/JPFeedersPanel.cpp (showFeederForPart); src/ui/JPPlacementsTableModel.cpp (status, setLocation) -->

## Running the job

**Start** runs the job, a step after another, until every placement is placed; while it runs the button
is **Pause**, which stops it after the step under way, and then **Resume**. **Step** does one step (on to the next that moves the
machine, with Machine Setup's Step Next Motion) and pauses. **Stop** stops it: the nozzles are emptied at the discard location and the head is parked. The
machine must be connected (the buttons are greyed until it is) and homed. If every placement is placed
already, Start asks whether to mark them all not placed first. The status line says what the job is
doing ("Feed …", "Pick … using nozzle N1.", "Placing …"), and at the end how many parts were placed and
how fast. While a job runs, another job or another cell is not opened.

<!-- src: src/app/JPlacerJobRun.cpp (startPauseResume, step, stop, start, run); src/ui/JPJobPanel.cpp (updateJobActions); src/app/JPlacerJob.cpp (settle); src/app/JPlacerMachine.cpp (openCell) -->

A job goes as OpenPnP's does:

1. **Checks.** The placements to place are those enabled, not placed, with their side facing up on an
   enabled board. Each must have a part, the part a package, a nozzle tip that fits the package and a
   nozzle, and an enabled feeder holding the part; a board with an ID twice is refused. Then the head goes
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

<!-- src: src/machine/JPJobProcessorConfig.h; src/tasks/JPJobProcessor.cpp (preFlight, plan, ordered, planner, pick, align, place, cleanup); src/vision/JPPartFinder.cpp; src/app/JPlacerJobMachine.cpp (alignPart); src/tasks/JPBottomVision.cpp (findOffsets); src/tasks/JPAlignRequests.cpp; src/tasks/JPFiducialLocator.cpp; src/model/JPFiducialFit.cpp; src/app/JPlacerJobMachine.cpp (locateFiducial, changeTip) -->

When something fails, the job pauses and says why (**Job Error**); the board, placement, part or feeder
it is about is chosen on its tab. **Resume** goes on from there. With **Defer Errors** (or a placement's
own error handling set to Defer), a placement that fails is put off instead: its feeder's fault is
counted (shown in the Feeders tab's **Faults**; by default three in its last six feeds turn the feeder off), and it
is tried again later, up to Machine Setup's Max Placement Attempts, or left in error; the job goes on, and says at its end how many
errors there were.

<!-- src: src/tasks/JPJobProcessor.cpp (plannedStep, finish); src/app/JPlacerJobRun.cpp (run); src/app/JPlacerOpenPnpTabs.cpp (showSource); src/model/JPFeeder.cpp (recordJobFault) -->
