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

Importing never adds to the library. Choosing a placement's **Part** on the placements list makes its
board part that library part; when other placements share it, the chosen one gets a board part of its
own (the file's value and footprint kept), and the others keep theirs. A part no placement names any
more is dropped when the board is saved.

<!-- src: src/model/JPBoardPart.h; src/model/JPBoard.cpp (useLibraryPart, matchPlacement, takeParts, dropUnusedParts, scopeName); src/model/JPConfiguration.cpp (part, package, libraryPart); src/ui/JPPlacementsTableModel.cpp (setChoice) -->

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
All but the ID are changed in the table; Part, Side, Type and Error Handling open their list on a click.
X and Y keep their own units when typed without. A fiducial's Type stands out. The ID column sorts as
reference designators do: R2 before R10.

| Button | |
|---|---|
| **New Placement** (plus) | Asks for the new placement's ID and adds it: the first part, at 0, 0 on the top. There must be a part first; an ID already on the board is refused. |
| **Remove Placement(s)** (cross) | Takes the chosen placements off the board. |
| **Import Placements** (with a menu) | Reads placements from a CAD tool's file into the board (see below). |
| **View Board** | Opens the board viewer (see [Panels](panels.md#the-viewer)). |

**Search**, at the right, works as on the Parts tab. Right-click for **Set Type**, **Set Side**, **Set
Enabled** and **Set Error Handling**, each for all the chosen placements. **Space** turns the chosen
placement on or off.

A change to a board is made to every use of it, in the job and on panels, as OpenPnP does: what a job
set on its own copy (a placement turned off there) is kept unless the same thing is changed here.

<!-- src: src/ui/JPBoardPlacementsPanel.cpp; src/ui/JPPlacementsTableModel.cpp; src/model/JPDefinitionChanges.h -->

## Importing placements

Import Placements (and **File > Import Placements**) offers OpenPnP's importers. Each opens a window
asking for its files (**Browse** finds them) and options, then **Import** reads them; a file left empty
is passed over. What could not be read is shown, and the window stays.

| Importer | Reads |
|---|---|
| **Altium .csv** | Altium's pick and place export: columns Designator, Comment, Footprint, Ref-X/Center-X and Ref-Y/Center-Y (mm or mil), Rotation, Layer, Height, Description. |
| **Diptrace .csv** | DipTrace's pick and place export: RefDes, Name, X (mm), Y (mm), Side, Rotate, Value. |
| **CadSoft EAGLE Board** | An EAGLE `.brd` file itself: each element, its package's SMD pads as the package's footprint, and solder paste pads. Options choose the top, the bottom or both, and whether parts' names carry the library's. |
| **EAGLE mountsmd.ulp** | The `.mnt` (top) and `.mnb` (bottom) files EAGLE's mountsmd.ulp writes. |
| **KiCAD .pos** | KiCad's `.pos` files, top and bottom. A bottom placement's X and rotation are turned over as OpenPnP turns them. |
| **Labcenter Proteus .pkp** | Proteus's pick and place file, in mm or thou, with or without stock codes. |
| **Reference CSV** | A CSV file whose header line (found in its first 50 lines; commas or tabs) names the columns the way many CAD tools do. Columns in mils are converted. Placements named FID1, REF2 and so on are fiducials. |

Each part the file names, by OpenPnP's naming *package*-*value* (KiCad can use the value alone),
becomes one of the board's parts: the library's part of that name where there is one; else, with
**Create Missing Parts** ticked, the board's own part (with the library's package of that name, or a
package of the board's own: an EAGLE board's with the pads its library draws); else one not chosen yet.
A placement is kept either way. The library is changed only where an option says so: **Update Existing
Part Heights** (Reference CSV, Altium) and EAGLE's **Update Existing Parts** change the library's parts.

When the board already has placements, it asks: **Merge** updates those with the same IDs (part, side,
location, comments), keeps the others and adds the new; **Replace** takes them all off first; **Cancel**
leaves the board as it was. A part the board already has keeps the choice made for it (importing again
does not undo it); what the file says about it now is kept with it.

<!-- src: src/model/JPBoardImporter.cpp (all, boardPart); src/model/JPCsvImporter.cpp; src/model/JPKicadPosImporter.cpp; src/model/JPEagleBoardImporter.cpp; src/app/JPlacerImportDialog.cpp; src/ui/JPBoardPlacementsPanel.cpp (importBoard, merge); src/model/JPBoard.cpp (takeParts) -->

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
