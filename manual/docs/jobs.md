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

## The Parts panel

The **Parts** panel (**Job ▸ Parts**, a dock beside Board) lists every placement in the job with its
part: **Designator**, **State**, **Part** (its MPN, or its value when it has none), **Value**,
**Package**, **Footprint**, **Side**, **Rotation**, **Rotation From**, **Supplier No.** and
**Manufacturer**.

- **List** shows them as a table. Click a column's heading to sort by it, and again to reverse it.
  Drag a heading's edge to widen a column.
- **Tree** groups them by the column chosen in **Group by** (Package to begin with): each group shows
  how many placements are in it, and opens to list them.
- **Filter**: type, and only placements with that text in one of their columns stay. In the tree, the
  groups open to show what matched.
- Choose several at once with Shift-click and Ctrl-click.

Whether you last used the list or the tree, and the tree's grouping, are kept for next time.

**State** says whether the placement can be placed, as far as its parts go:

| State | |
|---|---|
| **Ready** | It has a part, in a package, with a footprint. |
| **No part** | The files said nothing that identifies it. |
| **No package** | Its part is in no package. |
| **No footprint** | Its package has no footprint yet. |
| **Conflict** | Its part's package is not the one its footprint name belongs to, or its footprint's pads do not match the pins the file counts. |
| **Guess** | Matched by its value and package only: confirm it. |
| **Fiducial** | A mark to find the board by; nothing is placed. |
| **Do not place** | The files say it is not fitted. |

<!-- src: src/ui/JPPartsPanel.cpp; src/app/JPlacerParts.cpp (show, columns); src/library/JPPlacementState.cpp (of, name) -->

### Editing

Below the list are four pages for what you have chosen. A field's change is kept when you press
Return or Tab or leave the field; Escape puts back what it was. Each change marks the job as changed.

- **Placement**: what the placement's state is and what the file said about it. **Rotation** sets the
  angle of every chosen placement at once, so a whole footprint's worth is put right in one go: sort
  or filter the list to bring them together, choose them, and type the angle. **Use Part's Rotation**
  takes that back. **Part** gives the chosen placements another of the job's parts. **Confirm Part**
  accepts a guess. **New Part from File** makes a part from what the file said about the first
  placement chosen, and gives it to them all.
- **Part**, **Package**, **Footprint**: the fields of the first chosen placement's part, its package
  and its footprint. A part's **Package** and a package's **Footprint** are chosen from the list: the
  job's own, or one from the library (*Library: …*), which is then brought into the job. A package's
  footprint can also be read from a KiCad footprint file (**Import KiCad Footprint…**) or made from its
  numbers (**Make Dual Footprint…**: pins, pitch, pad centres across, pad length, pad width; **Make Quad
  Footprint…**: pins on each side, pitch, pad centres across, pad length, pad width, exposed pad).
  **Remove…** takes one out of the job, after asking; what used it then shows what it lacks.

A package's **Names** are the CAD footprint names and supplier package names that find it when a board
is read in; a name belongs to one package only. Its **Turn** is added to the imported rotation of every
placement using it, for a package whose zero is not the CAD's.

<!-- src: src/app/JPlacerParts.cpp (placementPage, entryPage, onPlacementField, onPlacementAction, onEntryChoice, onEntryAction); src/app/JPlacerEntryForm.cpp (make); src/library/JPEntryFields.cpp -->

### Rotation

A placement's rotation is the one the file gave, plus its package's **Turn**, unless you set one on the
placement. **Rotation From** says which:

| Rotation From | |
|---|---|
| **as imported** | What the file said. |
| **as its part** | What the file said, plus its package's turn. |
| **unique** | Set on this placement alone, agreeing with neither. |

<!-- src: src/library/JPPlacementRotation.cpp (of, name) -->

### The job's parts and the library

Each part, package and footprint page says whether it came from the library, was made in this job, or
has been changed in this job.

- **Copy to Library** puts it into the library. One the library does not have is copied at once. One
  the library already has (it came from there, or the library has one of that MPN or name) is copied
  only after you have seen what would change in the library's version, field by field, and pressed
  **Replace**. A part brings its package, and a package its footprint, where the library lacks them.
- When the library's version has changed since it was copied into the job, the page says so: **Update
  from Library** takes the library's version; **Keep This Version** keeps the job's, and stops saying so
  until the library changes again.

Nothing in the job changes the library unless you copy it there.

<!-- src: src/library/JPLibrarySync.cpp (copyToLibrary, wouldChange, update, keep, libraryNewer); src/app/JPlacerParts.cpp (copyToLibrary) -->

## The parts library

The library holds the parts, packages and footprints you keep from job to job. Jobs take copies from it;
nothing in a job changes it unless you copy it there.

A new library starts with the common packages and their footprints, made from their standards
(IPC-7351 land patterns, at IPC-7351's zero: pin 1 upper left, a two-terminal part lying along X with
pin 1 on the left): chip sizes 01005 to 2512, SOT-23, SOT-23-5, SOT-23-6, SOT-223, SOD-123, SOD-323,
SMA, SMB, SMC, SOIC (narrow, and wide as SOIC-16W to SOIC-28W), TSSOP-8 to TSSOP-28, QFN (3×3 to 7×7,
0.5 mm pitch, with the exposed pad) and LQFP-32 to LQFP-144. Each is known by the names KiCad, EasyEDA
and suppliers usually give it (`R_0603_1608Metric`, `R0603`, `C0603` and `0603` are all the 0603
package), so most boards find their packages without drawing any. It has no parts: those come from
jobs.

<!-- src: src/library/JPStarterLibrary.cpp (fill); src/library/JPFootprintMaker.cpp -->

It is kept in `library.json`, in jplacer's data folder (`~/.local/share/jplacer/library`) unless you
choose another in [Preferences](preferences.md#parts-library). A library file that cannot be read,
because it is damaged or was written by a newer jplacer, is never overwritten: the library is used
empty and read-only, and the status bar says why. Every save writes the file whole or not at all.

<!-- src: src/library/JPLibrary.cpp (open, save, defaultFolder); src/app/JPlacerJob.cpp (the constructor, setLibraryFolder) -->

### The Library panel

The **Library** panel (**Job ▸ Library**, beside Parts) lists the library's parts, packages and
footprints with the same list, tree and filter, and the same pages to edit them. Each change is saved
to the library as you make it; jobs that copied the entry are told the library's has changed. Choosing
an entry brings its page forward.

- **Bring into Job** copies the chosen entry into the open job; choose it there from a part's
  **Package** or a package's **Footprint** list.
- **New Part** and **New Package** start one in the library.
- A package's footprint is chosen, imported from KiCad, or made from its numbers, as on the Parts panel.
- **Remove…** takes one out of the library, after asking. Jobs keep their own copies.

While the library is read-only, its pages cannot be changed and say why.

<!-- src: src/app/JPlacerLibraryDock.cpp (show, pages, onAction) -->
