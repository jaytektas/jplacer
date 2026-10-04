# Jobs

A **job** is one board as your PCB tool describes it, together with the parts it needs. You keep it as a
file (`.jpjob`) wherever you like, and open it again to build the same board another day.

A job keeps its **own copy** of every part, package and footprint it uses. Changing the parts library
later never changes a job you have already set up, so a job runs the way it ran before.

<!-- src: src/job/JPJob.h; src/library/JPPartsStore.h (copyPart) -->

## New, open and save

| Entry | |
|---|---|
| **File ▸ New Job** (Ctrl+N) | Starts an empty job. |
| **File ▸ Open Job…** (Ctrl+O) | Opens a `.jpjob` file. |
| **File ▸ Save Job** (Ctrl+S) | Saves the job to its file; a job never saved asks where, as Save Job As does. |
| **File ▸ Save Job As…** (Ctrl+Shift+S) | Saves the job to a file you choose; `.jpjob` is added if you leave it off. |

The window's title names the open job, with a **\*** after the name while it has changes that are not
saved. The job you had open is opened again the next time jplacer starts.

If the job has unsaved changes when you start another job, open one, or close jplacer, you are asked
whether to save them: **Save** saves them first (asking where, for a job never saved), **Don't Save**
lets them go. Only those buttons answer: Escape does not close the question.

A job file is written whole or not at all, so a crash while saving leaves the file as it was. A job
written by another version of jplacer is refused with a message, not half read.

<!-- src: src/app/JPlacerJob.cpp (newJob, open, save, saveAs, settle, title, mayClose); src/app/JPlacerMenuBuilder.cpp (the File menu); src/common/JPWholeFile.cpp -->

## Reading a board into the job

**File ▸ Import Pick-and-Place File…** (also **Import Pick-and-Place…** on the
[Board](board.md#reading-the-board) panel) reads your PCB tool's pick-and-place file into the open job.
It replaces the job's board; the parts the job already has stay, so placements that are the same as before
find them again.

Only the designator, its position, rotation and side are sure to be in such a file. Everything else a
column says about a part is kept with its placement, whatever the tool calls the column:

- **what orders it**: supplier part numbers (several suppliers' columns, or one with a Supplier column
  beside it), the manufacturer and the manufacturer's part number (MPN);
- **what it is**: the value, and ratings (tolerance, voltage, power, dielectric, temperature), whether it
  is surface mount or through-hole, and whether it is not to be placed;
- **what it fits**: the CAD footprint name, the supplier's name for the package, and the number of pins;
- **pad 1's position**, where the file gives it;
- every other column, as text.

<!-- src: src/app/JPlacerJob.cpp (importCpl); src/import/JPCplImporter.cpp (classify, mountingOf); src/job/JPPlacement.h -->

## How each placement gets its part

Each placement is then given its part, using the strongest thing the file says about it:

1. **A supplier part number** the job or the library already knows: that part, for certain.
2. **Else the MPN**, compared without regard to case, spaces, dashes or dots: also certain. The
   manufacturer is not compared, since one maker's name is written many ways.
3. **Else the value, ratings and package**: a library part with the same value (100R is 100Ω, 0.1uF is
   100nF), no rating that disagrees, in the package the footprint name belongs to. This is only a
   **guess**, marked for you to confirm.
4. **Else a new part** in the job, made from what the file says, in the package its footprint name
   belongs to, or a new package of that name with no footprint yet.

Placements that are the same thing share one part. A part found in the library is copied into the job;
the library itself is not changed. Fiducials have no part, and a placement the file says nothing about
(no number, MPN, value or footprint) is left without one. The status bar says how many were matched,
guessed and made new.

<!-- src: src/library/JPPartMatcher.cpp (match, certainPart, guessedPart, packageFor); src/library/JPValue.cpp; src/library/JPPartsStore.cpp (mpnKey, partByNumber) -->

## Part, package, footprint

A placement can be placed only when it has a **part**, the part has a **package**, and the package has a
**footprint**. Each link is to exactly one:

- a **part** is one thing you can buy, in one package (the same chip in another package is another
  part);
- a **package** is the body it comes in (0603, SOIC-8), with its size and height, which nozzle tips can
  pick it, and its footprint;
- a **footprint** is the package's pads, as drawn over the camera's picture.

Many parts can be in one package, and many packages can use one footprint. A CAD footprint name, or a
supplier's package name, belongs to one package only.

A placement whose part's package is not the one its footprint name belongs to, or whose footprint has a
different number of pads from the pins the file counts, is in **conflict**: it is not placed until that
is put right.

<!-- src: src/library/JPPlacementState.cpp (of); src/library/JPPart.h; src/library/JPPackage.h; src/library/JPFootprint.h -->

## The parts library

The library holds the parts, packages and footprints you keep from job to job. Jobs take copies from it;
nothing in a job changes it. It is kept in `library.json` in jplacer's data folder
(`~/.local/share/jplacer/library`). A library file that cannot be read, because it is damaged or was
written by a newer jplacer, is never overwritten: the library is used empty and read-only, and the status
bar says why.

<!-- src: src/library/JPLibrary.cpp (open, save, defaultFolder); src/app/JPlacerJob.cpp (the constructor) -->
