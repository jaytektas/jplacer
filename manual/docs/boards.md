# Boards

The **Boards** tab (in the work area, before Parts, as in OpenPnP) lists the boards jplacer knows and,
under them, the chosen board's placements, as OpenPnP's Boards tab does. Each board is its own
`.jpboard` file, which holds everything the board needs, its own parts list included (see [The board's
parts](#the-boards-parts)); the list of them is kept in `boards.xml` in jplacer's configuration folder.
OpenPnP's `.board.xml` files are read as they are; saved, one becomes a `.jpboard` beside it (see
[Saving boards](#saving-boards)). A board added to a job or a panel is known here too.

<!-- src: src/ui/JPBoardsPanel.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/model/JPConfiguration.h (kBoardsFile); src/model/JPBoard.h (kExtension) -->

## The board's parts

Each board keeps its own list of parts, and each placement names one of them. A board part keeps what
the files it came from said about it (the part's name, value and footprint) and is one of:

- **From the library**: one of the parts on the Parts tab, which every job draws from.
- **The board's own**: a part (and, where the library has no such package, a package) kept in the board
  only. Its id starts with the board's name (`sim/C_0603-47n`), so two boards' own parts never mix.
  It is not on the Parts tab.
- **Not chosen yet**: only what the file said; its placements cannot be placed until a part is chosen.

Importing never adds to the library. A part no placement names any more is dropped when the board is
saved.

<!-- src: src/model/JPBoardPart.h; src/model/JPBoard.cpp (useLibraryPart, takeParts, dropUnusedParts, scopeName); src/model/JPConfiguration.cpp (part, package, libraryPart) -->

### Choosing a part

A click on a placement's **Part** opens the part picker for its board part, titled with the placement:

- which placements share the part, what the files said about it (value, footprint, MPN, manufacturer,
  supplier, the columns kept as extras), and what it is now;
- the library's parts it may be, best first, each with **Why**: an identifier it has (its MPN, its
  supplier's part number), a value and footprint it learned (and from which board), named by its
  footprint and value (OpenPnP's naming), a value it learned in its package, named by its value, or the
  same value written another way in its package or (100n is 100nF and 0.1µF; 4k7 is 4.7k; 4R7 is 4.7) of the same size (0603). A part of
  another value is never offered for its footprint alone. The one it is now, else the best, is chosen;
  when nothing is suggested, nothing is, so Return cannot take a part by chance;
- a filter, into which typing goes from the start: every word must be in a part's name, its package or
  the package's description, the suggestions kept first; **[x]** clears it.

**Use This Part** (Return, or a double-click on one) makes it that library part; with **Remember** ticked
(as it starts, where the files gave any), the library part keeps the names the files gave (its value and
footprint, MPN and supplier's part number; its package's footprint the footprint's name), so the next board calling it so is
matched without asking. **Add to Library** makes a library part from all the files said (named by its MPN,
else *footprint*-*value*, else its value, numbered when the name is taken; its package the library's of
that footprint, by its name or a footprint's CAD name, else a new one) and uses it. **Make It the Board's
Own** makes a part (and, where the library has no package of its footprint's name, a package) of the
board's from what the files said, its height too; **Leave to Be Chosen** clears it. Each is for every
placement of the part, or, with **Only *designator*** ticked, for that placement alone (it gets a part of
its own, what the files said kept, and the others keep theirs). **Cancel** and Escape change nothing.

**Board's Parts** on the placements' toolbar lists the board's parts one a row: their **Placements**,
**Value**, **Footprint**, **MPN**, what each **Is** now, and the library's **Best match** with **Why**.
**Choose…** (or a double-click, or Return) opens the part picker for the part; **Use Best Matches (*n*)**
gives each part still to be chosen its best match where the evidence is strong (anything but the value
alone); **Only those to choose or review** narrows the list. An import from the CPL and BOM that leaves
parts to be chosen opens it when the placements are in.

A board keeps a copy of each library part it uses (its package, and the footprint its CAD footprint is: the
one of that name, else the package's first), taken when the part is chosen, so the
board is complete on its own: on a machine whose library lacks the part, the board is placed with its
copy. When the library's part has changed since (a height, a package, its footprint's pads or zero
rotation; not a name it learned), or the
library lacks it, **Is** says so (*differs from the library*, *not in this library*) and it is counted
to review: **Take the Library's** makes the board's copy the library's part as it is now; **Give the
Library the Board's** makes the library's part the board's copy (adding it, when the library lacks it).

As a board comes in (any import), a part is taken from the library unasked when the evidence is strong:
an identifier, a name it learned, OpenPnP's *footprint*-*value* name, a value it learned in its package.

<!-- src: src/app/JPlacerPartPickerDialog.cpp; src/app/JPlacerBoardPartsDialog.cpp; src/model/JPPartMatcher.cpp (candidates, valueOf, chipSize, matches, kStrong, kAutomatic, automatic); src/model/JPLibraryLearning.cpp (learn, addFrom); src/model/JPConfiguration.cpp (packageNamed, takeCopy, differs, giveCopy, libraryPartFor, part); src/model/JPLibraryJson.cpp (fingerprint); src/model/JPBoard.cpp (splitPlacement, makeOwn, placementsOf); src/ui/JPPlacementsTableModel.cpp (pick, applyPart); src/ui/JPBoardPlacementsPanel.cpp (importCplBom, merge); src/app/JPlacerOpenPnpTabs.cpp (openBoardParts) -->

## Boards

| Button | |
|---|---|
| **Add Board...** (plus, with a menu) | **Create New Board...** asks where to save it (`.jpboard` is added to a name without it) and makes it, empty. **Existing Board** adds a `.jpboard` file, or OpenPnP's `.board.xml`. |
| **Remove Board** (cross) | Takes the chosen boards off the list; their files stay. A board used by the job or by a known panel is not taken off: it says so. |
| **Copy Board...** | Asks where to save a copy of the chosen board, saves it there and adds it. |
| **Clean Up** | Takes off the list every board neither the job nor a known panel uses. Their files stay. |

Remove Board works on one board or several; Copy Board on one. A board with changes not yet saved asks
whether to save them first (Yes, No, Cancel) before it is taken off.

The table shows each board's **Board Name**, **Width** and **Length**; all three are changed in the
table. A length may be typed with units (`1.5in`); without, millimetres are taken. Pointing at a name
shows its file. Sorting, choosing rows and widening columns work as on the [Parts](parts.md#the-table)
tab.

<!-- src: src/ui/JPPlacementsHoldersGroup.cpp; src/ui/JPPlacementsHolderTableModel.cpp -->

## Placements

The chosen board's placements, with OpenPnP's columns: **Enabled**, **ID**, **Part**, **Side**, **X**,
**Y**, **Rot.**, **Type**, **Error Handling**, **Rank** (pointing at it explains it) and **Comments**.
All but the ID are changed in the table; Part opens the part picker on a click (see [Choosing a
part](#choosing-a-part)), and Side, Type and Error Handling their list.
X and Y keep their own units when typed without. A fiducial's Type stands out. The ID column sorts as
reference designators do: R2 before R10.

| Button | |
|---|---|
| **New Placement** (plus) | Asks for the new placement's ID and adds it: the first part, at 0, 0 on the top. There must be a part first; an ID already on the board is refused. |
| **Remove Placement(s)** (cross) | Takes the chosen placements off the board. |
| **Import Placements** (with a menu) | Reads placements from a CAD tool's file into the board (see below). |
| **Board's Parts** | The board's parts, one a row, to choose them (see [Choosing a part](#choosing-a-part)). |
| **View Board** | Opens the board viewer (see [Panels](panels.md#the-viewer)). |

**Revision**, after the buttons, shows which of the board's revisions is shown and switches to another
(see [Revisions](#revisions)); it says **None yet** until the board has one.

**Search**, at the right, works as on the Parts tab. Right-click for **Set Type**, **Set Side**, **Set
Enabled** and **Set Error Handling**, each for all the chosen placements. **Space** turns the chosen
placement on or off.

A change to a board is made to every use of it, in the job and on panels, as OpenPnP does: what a job
set on its own copy (a placement turned off there) is kept unless the same thing is changed here.

<!-- src: src/ui/JPBoardPlacementsPanel.cpp; src/ui/JPPlacementsTableModel.cpp; src/model/JPDefinitionChanges.h -->

## Importing placements

Import Placements (and **File > Import Placements**) offers **CPL and BOM…** first, then OpenPnP's
importers.

### From the CPL and the BOM

**CPL and BOM…** reads a board from its CAD files as an assembly house takes them: the placement file
(CPL: each part's designator, position, rotation and side) and, where there is one, the BOM (each part's
value, footprint, manufacturer, MPN, supplier and so on), and any other table that names designators.

- **Files**: **Choose Placement File…** (or **Change Placement File…**) and **Add BOM or Table…** add
  them; **Remove** takes out the one chosen. The list shows what each is, how its columns were read
  (*their names (guessed)*, or the profile used) and its rows. Files may be separated by commas,
  semicolons, tabs or spaces (KiCad's `.pos`), quoted or not, in UTF-8, UTF-16 or Latin-1; the header
  row is found among the first lines, after any comments.
- **Columns of** the file chosen: each column's header, what it holds and its first values. What it holds
  is guessed from the header by the names tools give it ("Mid X", "Ref-X(mm)" and "PosX" are X; "LCSC Part
  #" is the supplier's part number) and can be changed: **Designator**, **X**, **Y**, **Rotation**,
  **Side**, **Do Not Place**, **Value**, **Footprint**, **Package**, **Description**, **Manufacturer**,
  **MPN**, **Supplier**, **Supplier PN**, **Height**, **Datasheet**, **Quantity**, **Keep as extra** (kept
  with the part under the column's own name) or **Ignore**. A file's own name for a field is fine: a column
  called "Provider" can be the **Manufacturer**. For a BOM or table, what it is (**BOM** or **Other
  table**); the units of its lengths where a cell does not say ("12.5mm" says; a header such as
  "Ref-X(mil)" sets them, else a comment line such as KiCad's `## Unit = inches`); and how its columns are read.
- **Save Profile…** keeps the file's columns' meanings under a name; the next file of that kind whose
  header has the same columns is read by it, and it can be chosen for a file.
- **What it makes**, kept up to date as the choices change: the placements (those not to be placed, the
  fiducials), the parts (from the library, the board's own, to be chosen), placements no file names a part
  for, designators in a BOM but not the placement file (not placed) and in the placement file but no BOM,
  and each field the files disagree on, with examples and **Take *field* from**, the file whose value is
  used (the placement file for a placement's position; else the first other file that has the field).

A BOM line's designators ("R1, R2, R5-R8") are each joined to the placement of that designator. Parts are
grouped by manufacturer and MPN where the files give them, else by value and footprint, and each keeps
every field the files gave it (a supplier part number column that names its supplier, "LCSC Part #", gives
the supplier). A part is the library's when one is named by its MPN, by OpenPnP's *footprint*-*value*,
or by its value; else, with **Create Missing Parts** ticked, the board's own; else it is to be chosen
(its placements show the files' name, "(to be chosen)"). A placement is not to be placed (not enabled)
when a do-not-place column says so ("DNP", "x" or "yes" in one, "no" in a Populate or Fitted column) or
its value is "DNP"; FID1, REF1 and the like are fiducials. **Import** brings it into the board chosen as
the other importers do (Merge or Replace), and the files themselves, every row and how their columns
were read, are kept in the board.

<!-- src: src/app/JPlacerCplBomImportDialog.cpp; src/import/JPCplBomImport.cpp (build, join, winner, doNotPlace, bottom); src/import/JPImportField.cpp; src/import/JPTableFile.cpp; src/import/JPImportSource.cpp (guess, unitsOfComments, length, provenance); src/import/JPTableFile.cpp (comments); src/import/JPDesignators.cpp; src/import/JPMappingProfiles.cpp; src/ui/JPBoardPlacementsPanel.cpp (importCplBom, take); src/ui/JPPlacementsTableModel.cpp (data) -->

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
**Create Missing Parts** ticked, the board's own part (with the library's package of that name, or a
package of the board's own: an EAGLE board's with the pads its library draws); else one not chosen yet.
A placement is kept either way. The library is changed only where an option says so: **Update Existing
Part Heights** (Reference CSV, Altium) and EAGLE's **Update Existing Parts** change the library's parts.

When the board already has placements, it asks: **Merge** updates those with the same IDs (part, side,
location, comments), keeps the others and adds the new; **Replace** takes them all off first; **New
Revision…** makes what was read the board's next revision (see [Revisions](#revisions)); **Cancel**
leaves the board as it was. A part the board already has keeps the choice made for it (importing again
does not undo it); what the file says about it now is kept with it.

<!-- src: src/model/JPBoardImporter.cpp (all, boardPart); src/model/JPCsvImporter.cpp; src/model/JPKicadPosImporter.cpp (parseFile: Unit); src/model/JPEagleBoardImporter.cpp; src/app/JPlacerImportDialog.cpp; src/ui/JPBoardPlacementsPanel.cpp (importBoard, take, merge); src/model/JPBoard.cpp (takeParts) -->

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

<!-- src: src/model/JPBoardUpgrade.cpp (pairing, findOriginMove, sort, revision); src/model/JPBoard.cpp (addRevision, switchRevision, undoCarry, toJson); src/model/JPBoardRevision.cpp (next); src/model/JPBoardPart.cpp (samePart); src/app/JPlacerBoardUpgradeDialog.cpp; src/ui/JPBoardPlacementsPanel.cpp (take, upgrade, fillRevisions, switchRevision); src/model/JPPlacementsHolderLocation.cpp (toXml); src/model/JPConfiguration.cpp (resolveBoard) -->

## Saving boards

**File > Save Configuration**, and quitting, ask about each board with changes ("Save *name*?": Yes
saves it; No and Cancel leave the file as it was). The list of boards, parts and packages is saved at
once.

A board is saved as jplacer's `.jpboard` file. One read from OpenPnP's `.board.xml` is saved beside it
under its own name (`sim.board.xml` becomes `sim.jpboard`, a number added if that name is taken), and
from then on is that file: the job, the panels that use it and the list of boards follow it, and a board
named after its file takes the new name. The `.board.xml` is left exactly as it was, so OpenPnP can still
read it; in jplacer, whatever still names it (a job not saved since, a job of OpenPnP's) opens the
`.jpboard` made from it instead, so no change is lost.

<!-- src: src/app/JPlacerOpenPnpTabs.cpp (saveConfiguration, mayClose, confirmSave, boardMoved); src/model/JPConfiguration.cpp (saveBoard, board); src/model/JPBoard.h (convertedFrom) -->
