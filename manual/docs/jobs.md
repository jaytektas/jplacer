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

Parts and packages are edited on the [Parts](parts.md) and [Packages](packages.md) tabs, as in OpenPnP; the other tabs are being built.

<!-- src: src/model/JPJob.h; src/model/JPBoard.h; src/model/JPPanel.h; src/model/JPPart.h; src/model/JPPackage.h; src/model/JPConfiguration.h -->

## New, open and save

| Entry | |
|---|---|
| **File ▸ New Job** (Ctrl+N) | Starts an empty job. |
| **File ▸ Open Job…** (Ctrl+O) | Opens a `.job.xml` file. |
| **File ▸ Save Job** (Ctrl+S) | Saves the job to its file; a job never saved asks where, as Save Job As does. |
| **File ▸ Save Job As…** (Ctrl+Shift+S) | Saves the job to a file you choose; `.job.xml` is added if you leave it off. |

The window's title is *jplacer - * and the job's file name (*Untitled.job.xml* for a job never saved),
with a **\*** before the name while it has changes that are not saved. The job you had open is opened
again the next time jplacer starts.

If the job has unsaved changes when you start another job, open one, or close jplacer, you are asked
*Do you want to save your changes?* **Yes** saves them first (asking where, for a job never saved),
**No** lets them go.

A job's boards and panels are found by their file names: as written in the job, else beside the panel
that holds them, else beside the job.

<!-- src: src/app/JPlacerJob.cpp (newJob, open, save, saveAs, settle, title, mayClose); src/model/JPConfiguration.cpp (resolveBoard, resolvePanel); src/app/JPlacerMenuBuilder.cpp (the File menu) -->

## Older OpenPnP jobs

A job saved by an older OpenPnP (a list of boards, perhaps one panel of rows and columns of one board) is
converted when it is opened, as OpenPnP converts it: a copy of the old file is kept beside it as
`name.legacy.job.xml`; a panel of rows and columns becomes a panel file of its own (`name.panel.xml`),
its boards named `Brd[row,column]`; what was placed is kept. The job is then marked as changed, so
saving it writes the new form.

<!-- src: src/model/JPConfiguration.cpp (convertLegacyJob) -->

## Reading a board into the job

Not available yet: **File ▸ Import Pick-and-Place File…** is shown, but disabled.

<!-- src: src/app/JPlacerMenuBuilder.cpp (the File menu) -->
