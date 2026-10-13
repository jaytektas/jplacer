# Boards

A board is the job's. On the [Job tab](jobs.md#the-job-tab), choosing one of its boards (straight in the
job or on a panel) shows its **Placements**: the board's own, every use of it shares them, with the
board's imports, revisions, parts and viewer in its toolbar. A board comes into the job by the
Job tab's **Add Board/Panel** (see [Making and copying boards](#making-and-copying-boards)) and goes with it:
the boards open are the open job's (and its panels'). Leaving the job (**New Job**, **Open Job…**, **Open
Recent Job...**) closes them all, each with changes asked about first ("Save *name*?": **Yes** saves it,
**No** lets the changes go, **Cancel** stays in the job), and the next job opens its own. A board both jobs
use is read again from its file. Each board is its own `.jpboard` file: its placements, and its parts
list, each part pointing at the library's (see [The board's parts](#the-boards-parts)). OpenPnP's
`.board.xml` files are read as they are; saved, one becomes a `.jpboard` beside it (see [Saving
boards](#saving-boards)).

<!-- src: src/ui/JPJobPanel.cpp (showDefinition); src/app/JPlacerJob.cpp (leave, leaveFor, openPath); src/app/JPlacerOpenPnpTabs.cpp (settleBoards, confirmSave, confirmSaveAll); src/model/JPConfiguration.cpp (load, closeAll); src/model/JPBoard.h (kExtension) -->

## The board's parts

Parts and packages are kept in the library only. Each board keeps a list of its parts, and each placement
names one of them. A board part keeps what the files it came from said about it (the part's name, value and
footprint) and is one of:

- **From the library**: one of the parts on the Parts tab, which every job draws from (the board points at
  it; it keeps nothing of the part itself).
- **Not chosen yet**: only what the file said; its placements cannot be placed until a part is chosen.

An import with **Create Missing Parts** ticked makes the parts the library lacks (and the packages, where
the library has none of that name) and adds them to the library when the import is taken (Merge, Replace,
or a new revision made); before that, only the import's window knows of them. A part no placement names
any more is dropped when the board is saved.

A board saved before parts were kept only in the library had parts (and packages) of its own, and copies
of the library parts it used. Opened, its own parts and packages are put in the library under their names
without the board's (one the library has by that name is taken instead), as are copies of parts this
library lacks; its parts then point at the library's, the log says how many were added, and the board
has changes to save.

<!-- src: src/model/JPBoardPart.h; src/model/JPBoardPart.cpp (toJson, fromJson); src/model/JPBoard.cpp (useLibraryPart, takeParts, dropUnusedParts, forEveryPart); src/model/JPConfiguration.cpp (part, package, libraryPart, takeMade, adoptFormer, board); src/ui/JPBoardTools.cpp (takeMade, merge, upgrade) -->

### Choosing a part

A click on a placement's **Part** opens the part picker for its board part, titled with the placement:

- which placements share the part, what the files said about it (value, footprint, MPN, manufacturer,
  supplier, and your part and package fields), and what it is now;
- the library's parts it may be, best first, each with **Why**: an identifier it has (its MPN, its
  supplier's part number), a value and footprint it learned (and from which board), named by its
  footprint and value (OpenPnP's naming), a value it learned in its package, named by its value, or the
  same value written another way in its package or (100n is 100nF and 0.1µF; 4k7 is 4.7k; 4R7 is 4.7) of the same size (0603). A part of
  another value is never offered for its footprint alone. The one it is now, else the best, is chosen;
  when nothing is suggested, nothing is, so Return cannot take a part by chance;
- a filter, into which typing goes from the start: every word must be in a part's name, its package or
  the package's description, the suggestions kept first; **[x]** clears it.

**Use This Part** (Return, or a double-click on one) makes it that library part. The library part always
learns the board's name for it (its value and footprint, as an **Also Known As**), so a part you have
renamed is still found by it; with **Remember** ticked (as it starts, where the files gave any) it keeps the
rest the files gave too (MPN and supplier's part number; its package's footprint the footprint's name), so
the next board calling it so is matched without asking. **Add to Library** makes a library part from all the
files said: known by a new GUID, its **Name** its MPN, else *footprint*-*value*, else its value (numbered
when the name is taken), and known by that name too; its package the library's of
that footprint, by its name or a footprint's CAD name, else a new one) and uses it; **Leave to Be Chosen** clears it. Each is for every
placement of the part, or, with **Only *designator*** ticked, for that placement alone (it gets a part of
its own, what the files said kept, and the others keep theirs). **Cancel** and Escape change nothing.

Several library parts may answer to the same name (a 1 % and a 5 % 10k that both learned
"R_0603-10k"): all are offered, best first, and a board coming in takes one unasked only when no other is as
strong. A part made by an import is known by a GUID, named as the files name it and known by that name; when
the import is taken, every library part a board part is linked to learns what the board calls it.

<!-- src: src/model/JPPartMatcher.cpp (candidates, automatic); src/model/JPLibraryLearning.cpp (learnName, addFrom); src/model/JPBoardImporter.cpp (boardPart); src/import/JPCplBomImport.cpp; src/model/JPConfiguration.cpp (takeMade, partNamed) -->

**Board's Parts** on the placements' toolbar lists the board's parts one a row: their **Placements**,
**Value**, **Footprint**, **MPN**, what each **Is** now, and the library's **Best match** with **Why**.
**Choose…** (or a double-click, or Return) opens the part picker for the part; **Use Best Matches (*n*)**
gives each part still to be chosen its best match where the evidence is strong (anything but the value
alone); **Only those to choose** narrows the list (to those still to be chosen, and those whose library
part the library has not got any more, *not in the library*). An import from the CPL and BOM that leaves
parts to be chosen opens it when the placements are in.

As a board comes in (any import), a part is taken from the library unasked when the evidence is strong:
an identifier, a name it learned, OpenPnP's *footprint*-*value* name, a value it learned in its package.

<!-- src: src/app/JPlacerPartPickerDialog.cpp; src/app/JPlacerBoardPartsDialog.cpp; src/model/JPPartMatcher.cpp (candidates, valueOf, chipSize, matches, kStrong, kAutomatic, automatic); src/model/JPLibraryLearning.cpp (learn, addFrom); src/model/JPConfiguration.cpp (packageNamed, libraryPartFor, part); src/model/JPBoard.cpp (splitPlacement, placementsOf); src/ui/JPPlacementsTableModel.cpp (pick, applyPart); src/ui/JPBoardTools.cpp (importCplBom, merge); src/app/JPlacerOpenPnpTabs.cpp (openBoardParts) -->

## Making and copying boards

The Job tab's **Add Board/Panel** makes or opens a board into the job: **New Board...** asks where to save
it (`.jpboard` is added to a name without it) and makes it, empty; **Existing Board...** adds a `.jpboard`
file, or OpenPnP's `.board.xml`, from those open or by **Browse**; **Copy of the Chosen Board...** saves a
copy of the chosen board as a new file (asking about its changes first) and adds the copy. A board's
**Name**, **Width** and **Length** are changed in the Job tab's table, wherever it is used (a length may be
typed with units, `1.5in`; without, millimetres are taken); pointing at a name shows its file.

<!-- src: src/ui/JPJobPanel.cpp (showAddMenu, addNew, addExisting, copyChosen); src/ui/JPLocationsTableModel.cpp (editable, setText) -->

## Placements

A board's placements are the Job tab's **Placements**, the board chosen (see [Jobs](jobs.md#the-job-tab)):
**Enabled**, **ID**, **Part**, **Side**, **X**, **Y**, **Rot.**, **Type**, **Placed**, **Status**, **Error
Handling**, **Rank** (pointing at it explains it), **Comments** and **Verified**. The board's own (its ID,
Part, Side, where it lies, Type, Rank, Comments) are changed there, and every use of it shares them; Enabled,
Placed and Error Handling are this use's. Part opens the part picker on a click (see [Choosing a
part](#choosing-a-part)), and Side, Type and Error Handling their list. X and Y keep their own units when
typed without. The ID column sorts as reference designators do: R2 before R10. Besides the job's tools,
its toolbar has the board's:

| Button | |
|---|---|
| **Import Placements** (with a menu) | Reads placements from a CAD tool's file into the board (see below). |
| **Board's Parts** | The board's parts, one a row, to choose them (see [Choosing a part](#choosing-a-part)). |
| **View Board** | Opens the board viewer (see [Panels](panels.md#the-viewer)). |

**Revision**, after them, shows which of the board's revisions is shown and switches to another (see
[Revisions](#revisions)); it says **None yet** until the board has one.

A change to a board is made to every use of it, in the job and on panels, as OpenPnP does: what a job
set on its own copy (a placement turned off there) is kept unless the same thing is changed for it.

<!-- src: src/ui/JPJobPlacementsPanel.cpp (setLocation, updateActions); src/ui/JPBoardTools.cpp; src/ui/JPPlacementsTableModel.cpp; src/model/JPDefinitionChanges.h -->

## Importing placements

Import Placements (and **File > Import Placements**) offers **CPL and BOM…** first, then OpenPnP's
importers.

### From the CPL and the BOM

**CPL and BOM…** reads a board from its CAD files as an assembly house takes them: the placement file
(CPL: each part's designator, position, rotation and side) and, where there is one, the BOM (each part's
value, footprint, manufacturer, MPN, supplier and so on), and any other table that names designators.

The placement file can also be a KiCad board itself (`.kicad_pcb`, KiCad 5 on), which gives each part's
footprint as well. It is read as KiCad's own position file would have it: every footprint KiCad would put
in one (not those marked to be left out of position files), its reference, value, footprint, position in
millimetres from the board's drill/place file origin, rotation and side, with a **DNP** column for parts
marked Do Not Populate, then a column for each property the footprints carry (a symbol's fields: its
**Datasheet**, an **LCSC** number, an **MPN**…), to be read into the fields they are. The board's title block
(its **Title**, **Revision**, **Date**, **Company** and comments) is what the file says about itself, for the
board's and the job's fields. Each footprint is taken once, its SMD pads as its library draws them (the
placement's rotation taken off, and a bottom part's turned back over), whatever the library on this
computer has now. A footprint changed on the board, so that two parts of the same name have different
pads, is kept as a second, "*name* (2)", and the window says which parts take it. A package made for the
library (Create Missing Parts, the library having none of that name) gets that footprint's pads; a package
the library has is left as it is.

- **Files**: **Choose Placement File…** (or **Change Placement File…**) and **Add BOM or Table…** add
  them; **Remove** takes out the one chosen. The list shows what each is, how its columns were read
  (*their names (guessed)*, or the profile used) and its rows. Files may be separated by commas,
  semicolons, tabs or spaces (KiCad's `.pos`), quoted or not, in UTF-8, UTF-16 or Latin-1; the header
  row is found among the first lines, after any comments.
- **Columns of** the file chosen: each column's header, what it holds and its first values. What it holds
  is a field: the placement's (**Placement ▸ Designator**, **X**, **Y**, **Rotation**, **Side**, **Do Not
  Place**), the part's (**Part ▸ Name**, **Value**, **Footprint**, **Package**, **Description**, **Manufacturer**,
  **MPN**, **Supplier**, **Supplier PN**, **Height**, **Datasheet**, **Quantity**), or any field you added to
  placements, parts, packages, boards or jobs (see [Fields](tables.md#fields)); or **Ignore**. It is guessed
  from the header by the headings each field is matched by ("Mid X", "Ref-X(mm)" and "PosX" are X; "LCSC
  Part #" is the supplier's part number; "Part Name" and "LibRef" are the part's Name; a field of yours by its name and its **Import matches**), a column
  no field matches is ignored, and any can be changed. A file's own name for a field is fine: a column
  called "Provider" can be the **Manufacturer**. **New Placement Field…**, **New Part Field…**, **New
  Package Field…**, **New Board Field…** and **New Job Field…** at the end of the list make a field of that
  kind named after the column (and matched by its heading from then on), and the column fills it. For a BOM
  or table, what it is (**BOM** or **Other table**); the units of its lengths where a cell does not say
  ("12.5mm" says; a header such as "Ref-X(mil)" sets them, else a comment line such as KiCad's `## Unit =
  inches`); and how its columns are read.
- **Save Profile…** keeps the file's columns' meanings under a name; the next file of that kind whose
  header has the same columns is read by it, and it can be chosen for a file.
- **As last imported**: on **Import**, each file's columns are kept as they were chosen, profile or not; the
  next file of that kind with exactly the same columns is read the same way (ahead of a profile, a column
  set to **Ignore** left so), and **As last imported** can be chosen for it again after changing a column.
  The name is jplacer's own: a profile cannot be saved under it.
- **The Board's and the Job's Fields** (when boards or jobs have fields of yours): each one's value for the
  whole import, as the files give it (a column's first row, or what a file says about itself, matched by the
  field's name or **Import matches**: a KiCad board's **Revision**, a tool's `Key = value` comment), and where
  it came from. Type over it to give another; emptied, the field gets none. The board's go to the board chosen
  and the job's to the job open, each where it has no value yet.
- **What it makes**, kept up to date as the choices change: the placements (those not to be placed, the
  fiducials), the parts (from the library, new ones to be added to it on **Import**, to be chosen), placements no file names a part
  for, designators in a BOM but not the placement file (not placed) and in the placement file but no BOM,
  and each field the files disagree on, with examples and **Take *field* from**, the file whose value is
  used (the placement file for a placement's position, a KiCad board for the footprint; else the first
  other file that has the field). From a KiCad board, it also says how many footprints the new packages
  take from it.
- **Sides**: **Top and Bottom** (to begin with), **Top Only** or **Bottom Only**: the placement file's rows
  of the other side are left out, and **What it makes** says how many. On a **Merge** the board's
  placements on that side stay as they are; a **Replace** takes them off with the rest.
- **Fetch Footprints from LCSC** (kept for the next import): once you **Import**, each package the board's
  parts are of that has no pads is given its footprint from LCSC, by the LCSC part number of its part (see
  [Footprints from LCSC](packages.md#footprints-from-lcsc)).
- **Fetch Dimensions from LCSC** (kept for the next import): once you **Import**, each package the board's
  parts are of that has no body size or no height is given them from its part's 3D model at LCSC (see
  [Dimensions from LCSC](packages.md#dimensions-from-lcsc)). With either fetch ticked, a window shows how
  far it has got and what went wrong.

A BOM line's designators ("R1, R2, R5-R8") are each joined to the placement of that designator. Rows are one
part by the surest they say of it: its manufacturer and MPN, else its supplier's part number, else its
Name, value and footprint together; never by a Name alone (EasyEDA's pick-and-place file names a part by
its value, so an 0402 and an 0603 100nF are both "100nF"). A part is named as the files name it (a column
read as **Part ▸ Name**); where another part already has that name (another of the import's, or a library
part it is not), its footprint is added (`100nF-C0402`). Else its name is made up of its MPN, footprint
and value, those the files give, joined by "-" (`CL10B104KB8NNNC-C0603-100n`). A library part found only
by its name is taken only when nothing else the files say is another part's (its MPN, its supplier's part
number, its footprint naming another package); otherwise it is offered, for you to choose. Each keeps
every field the files gave it (a supplier part number column that names its supplier, "LCSC Part #", gives
the supplier). A placement's fields of yours are its own row's; a part's and its package's are its first
placement's row's, and a part made for the library (and a package made with it) has them. A library part,
or its package, already in the library keeps its own value where it has one, and gets the files' where it
has none, when the import is taken; so does its **Description**, its datasheet, and its MPN and supplier's
part number (where it has none of that kind): importing again never changes what you have set. **What it makes** says each value its field's type cannot read (not
taken), each part whose placements' rows disagree (the first taken), each library part whose value the files
differ from (the library's kept), and a board's or job's column whose rows disagree (the first taken). On a
**Merge**, a placement keeps the values of yours it has, and takes the files' where it has none. A part is the library's when one is named by its MPN, by the Name the files give it, by OpenPnP's *footprint*-*value*,
or by its value; else, with **Create Missing Parts** ticked, one made for the library on **Import** (its value, height,
description, datasheet, MPN and supplier's part number as the files give them); else it is to be chosen
(its placements show the files' name, "(to be chosen)"). A placement is not to be placed (not enabled)
when a do-not-place column says so ("DNP", "x" or "yes" in one, "no" in a Populate or Fitted column) or
its value is "DNP"; FID1, REF1 and the like are fiducials. **Import** brings it into the board chosen as
the other importers do (Merge or Replace), and the files themselves, every row and how their columns
were read, are kept in the board.

<!-- src: src/model/JPPartMatcher.cpp (candidates); src/model/JPFieldSet.cpp (builtIns); src/model/JPLibraryLearning.cpp (madeName); src/app/JPlacerCplBomImportDialog.cpp (fillColumns, fillSingles, choices, runImport); src/ui/JPBoardTools.cpp (takeMade); src/import/JPKicadBoardFile.cpp (properties, title_block); src/import/JPCplBomImport.cpp (build, join, winner, single, doNotPlace, bottom, leftOut); src/import/JPImportField.cpp (offered, label, guess); src/model/JPBoardPart.cpp (added); src/model/JPConfiguration.cpp (takeMade); src/model/JPLibraryLearning.cpp (describe); src/app/JPlacerOpenPnpTabs.cpp (openCplBom); src/import/JPTableFile.cpp; src/import/JPImportSource.cpp (guess, unitsOfComments, length, provenance); src/import/JPTableFile.cpp (comments); src/import/JPDesignators.cpp; src/import/JPMappingProfiles.cpp; src/ui/JPBoardTools.cpp (importCplBom, take); src/ui/JPPlacementsTableModel.cpp (data) -->

### OpenPnP's importers

Each of OpenPnP's importers opens a window
asking for its files (**Browse** finds them) and options, then **Import** reads them; a file left empty
is passed over. What could not be read is shown, and the window stays.

| Importer | Reads |
|---|---|
| **Altium .csv** | Altium's pick and place export: columns Designator, Comment, Footprint, Ref-X/Center-X and Ref-Y/Center-Y (mm or mil), Rotation, Layer, Height, Description. |
| **Diptrace .csv** | DipTrace's pick and place export: RefDes, Name, X (mm), Y (mm), Side, Rotate, Value. |
| **CadSoft EAGLE Board** | An EAGLE `.brd` file itself: each element, its package's SMD pads as the package's footprint, and solder paste pads. Options choose the top, the bottom or both, and whether parts' names carry the library's. |
| **EAGLE mountsmd.ulp** | The `.mnt` (top) and `.mnb` (bottom) files EAGLE's mountsmd.ulp writes. |
| **KiCAD .pos** | KiCad's `.pos` files, top and bottom, in the units their header names (`## Unit = mm` or `## Unit = inches`; inches made millimetres). A bottom placement's X and rotation are turned over as OpenPnP turns them. |
| **Labcenter Proteus .pkp** | Proteus's pick and place file, in mm or thou, with or without stock codes. |
| **Reference CSV** | A CSV file whose header line (found in its first 50 lines; commas or tabs) names the columns the way many CAD tools do. Columns in mils are converted. Placements named FID1, REF2 and so on are fiducials. |

Each part the file names, by OpenPnP's naming *package*-*value* (KiCad can use the value alone),
becomes one of the board's parts: the library's part of that name where there is one; else, with
**Create Missing Parts** ticked, a part made for the library (with the library's package of that name, or
a package made too: an EAGLE board's with the pads its library draws), added when the import is taken; else
one not chosen yet.
A placement is kept either way. The library is changed only where an option says so: **Update Existing
Part Heights** (Reference CSV, Altium) and EAGLE's **Update Existing Parts** change the library's parts.

When the board already has placements, it asks: **Merge** updates those with the same IDs (part, side,
location, comments), keeps the others and adds the new; **Replace** takes them all off first; **New
Revision…** makes what was read the board's next revision (see [Revisions](#revisions)); **Cancel**
leaves the board as it was. A part the board already has keeps the choice made for it (importing again
does not undo it), unless the library part chosen has since been deleted: then it takes the import's (the
part the import found or made in the library, or none yet, to be chosen), on a **Merge**, a **Replace** or a
**New Revision…** alike. What the file says about it now is kept with it.

<!-- src: src/model/JPBoardImporter.cpp (all, boardPart); src/model/JPConfiguration.cpp (takeMade); src/model/JPCsvImporter.cpp; src/model/JPKicadPosImporter.cpp (parseFile: Unit); src/model/JPEagleBoardImporter.cpp; src/app/JPlacerImportDialog.cpp; src/ui/JPBoardTools.cpp (importBoard, take, merge); src/model/JPBoard.cpp (takeParts); src/model/JPBoardUpgrade.cpp (revision) -->

## Revisions

Boards are revised. When a new revision's files come, you don't start the board again: import them into
the board (**CPL and BOM…** or any importer) and choose **New Revision…**. Everything already decided
carries over wherever it still holds, so only what changed asks for attention, and the revision the
board was is kept, to switch back to.

The new files' placements are paired with the board's: by designator first, then those left over by
footprint, side and position (within 0.05 mm), which finds a renumbered part (R12 is now R15). When most
of the placements paired by designator (at least three) moved by the same offset and turn, the CAD's
origin moved, not the parts: that is one change, **Origin moved 2.00, -1.50 mm**, and the new files'
positions are taken back by it, so the board's place in a job and its fiducials still hold.

**New Revision of *board*** shows one summary to work from: how many placements are unchanged, moved
or turned (to verify), have another part (the same footprint), another footprint (to verify), are new,
renamed or removed, and how many have parts still to choose. **Show** opens each count's list; every
placement is listed as it **Was** and is **Now**, its **Change**, and **What changed** in words ("moved
0.50 mm", "turned 90°", "part 100n → 220n", "was R12"). Name **The new revision** (rev B is offered
after rev A; a label the board has is refused), and, the first time, **The revision shown now**. **Make
the New Revision** keeps the revision shown and shows the new one; **Cancel** changes nothing.

What the new revision keeps of each placement paired:

- its identity, which lasts across revisions, and what a job set for it (enabled, error handling,
  rank) and its comments;
- its rotation correction, as a difference from the CAD's rotation: a part turned 90° on the machine
  and then 90° in the CAD is placed at 180°. A new footprint takes its own zero rotation instead;
- its verified mark, unless it moved, turned or has another footprint;
- its part's choice, where its line of the files is the same (footprint, name, value, MPN,
  manufacturer, supplier's part number); a changed line is matched again, as any import.

Removed placements are not in the new revision, and a part no placement uses is not in it either; the
library is never changed. When parts are left to choose, **Board's Parts** opens.

**Switching revisions.** **Revision** shows any of them, newer or older, here and in the job; each is
kept exactly as it was left. Work done on the one you leave is given to the one you switch to, for each
placement that is the same in both (the same identity, side, CAD position and rotation, and part line):
a rotation verified there, a verified mark (only one verified later than this revision's), and a part
chosen there. **Work Given from *rev*** then names those placements; **Keep** keeps it, **Undo** puts
the revision back as it was.

The board file keeps every revision, complete: its label, when it was made, its summary, its files, its
placements and its parts. A job's file records the revision of each of its boards, and opening the job
shows that revision.

<!-- src: src/model/JPBoardUpgrade.cpp (pairing, findOriginMove, sort, revision); src/model/JPBoard.cpp (addRevision, switchRevision, undoCarry, toJson); src/model/JPBoardRevision.cpp (next); src/model/JPBoardPart.cpp (samePart); src/app/JPlacerBoardUpgradeDialog.cpp; src/ui/JPBoardTools.cpp (take, upgrade, fillRevisions, switchRevision); src/model/JPPlacementsHolderLocation.cpp (toXml); src/model/JPConfiguration.cpp (resolveBoard) -->

## Saving boards

**File > Save Configuration**, quitting, and leaving the job ask about each board and panel with changes ("Save
*name*?": Yes saves it; No leaves the file as it was; Cancel stops there, and jplacer does not quit or
leave the job). The parts and packages are saved at once.

A board is saved as jplacer's `.jpboard` file. One read from OpenPnP's `.board.xml` is saved beside it
under its own name (`sim.board.xml` becomes `sim.jpboard`, a number added if that name is taken), and
from then on is that file: the job and the panels that use it follow it, and a board
named after its file takes the new name. The `.board.xml` is left exactly as it was, so OpenPnP can still
read it; in jplacer, whatever still names it (a job not saved since, a job of OpenPnP's) opens the
`.jpboard` made from it instead, so no change is lost.

<!-- src: src/app/JPlacerOpenPnpTabs.cpp (saveConfiguration, mayClose, confirmSave, holderMoved); src/model/JPConfiguration.cpp (saveBoard, board, convertedOf); src/model/JPBoard.h (convertedFrom) -->
