# Parts

The **Parts** tab (in the work area, after Boards, as in OpenPnP) lists the library's parts, as
OpenPnP's Parts tab does: those every job draws from. A board's own parts are kept in the board and are
not listed here (see [Boards](boards.md#the-boards-parts)).

The library is kept in `library.db` in jplacer's configuration folder: its parts and packages, each with
an ID that stays with it whatever it is named, and what boards' files call it. It is made from OpenPnP's
`parts.xml` and `packages.xml` the first time jplacer starts without one (those files are left as they
are). OpenPnP's files copied into the folder later are looked at again: their parts and packages the
library does not have are added to it (the start says how many); those it has are not changed.

<!-- src: src/ui/JPPartsPanel.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/model/JPConfiguration.cpp (load, save, openPnpStamp); src/model/JPLibraryStore.cpp -->

## The toolbar

| Button | |
|---|---|
| **New Part...** (plus) | Asks for the new part's ID and makes it, in the first package. There must be a package first. An ID already used is refused, and asked for again. |
| **Delete Part** (cross) | Deletes the chosen parts, after asking. |
| **Pick Part** | Feeds and picks the chosen part, as the Feeders tab's **Pick...** does, from the feeder OpenPnP would take it from: of the enabled feeders holding it, those of the highest priority, and of those the one closest to the camera. With none, it says no valid feeder was found. |
| **Copy Part to Clipboard** | Puts the chosen part on the clipboard as text, as OpenPnP's `parts.xml` writes a part. |
| **Create Part from Clipboard** | Asks for an ID and makes a part from what the clipboard holds. |

Delete Part works on one part or several; Pick Part and Copy Part on one.

**Search**, at the right, shows only the parts with the text typed anywhere in a row, whatever its case.
The text is a regular expression: `^C0402` finds parts whose ID starts with C0402.

<!-- src: src/ui/JPPartsPanel.cpp (newPart, deleteParts, copyPart, pastePart, updateWizards); src/app/JPlacerOpenPnpTabs.cpp (onPickPart); src/model/JPConfiguration.cpp (findFeeder); src/ui/JPTable.cpp (setFilter) -->

## The table

| Column | |
|---|---|
| **ID** | The part's ID. |
| **Description** | Its name. |
| **Value** | Its electrical value as written (100n, 4k7); matched to a board's however that is written. |
| **MPN** | Its manufacturer's part number: the first of its identifiers (the **Library** tab has them all). |
| **Height** | The distance between the board's surface and where the nozzle picks it, in mm (in its own units, with their name, when those are not mm). |
| **Through-Board Depth** | How far any of it reaches below the board when placed; added to the height for Dynamic Safe Z. Zero for surface-mount parts. |
| **Package** | Its package, chosen from a list. |
| **Speed %** | How fast it is moved, as a share of full speed. |
| **BottomVision**, **FiducialVision** | The vision settings it uses, chosen from a list; empty: its package's. |
| **Placements** | How many placements on the known boards use it. |
| **Feeders** | How many feeders hold it. |

Everything but the ID, the MPN and the two counts is changed in the table: double-click a cell, press F2, or start
typing; **Return** or **Tab** keeps the change, **Escape** puts back what was there. A height may be typed
with units (`0.5mm`, `20mil`); without, the part's own units are taken. Package and the vision settings
open their list on a click. Each change is saved at once.

Click a column's heading to sort by it; click it again to turn it round. Clicking another heading sorts by
that first and by the earlier ones after it (up to three); the later ones' arrows are fainter. Choose
several rows with **Shift**+click and **Ctrl**+click; **Ctrl+A** chooses them all, and **Ctrl+C** copies the
chosen rows, a tab between cells. Drag a heading's edge to widen a column; the columns after it give way.

<!-- src: src/ui/JPPartsTableModel.cpp; src/ui/JPTable.cpp (clickHeader, handleKeyEvent, startEditing, setColumnWidth); src/ui/JPLengthCell.cpp -->

## The part's tabs

Under the table, the chosen part's tabs.

**Library** is what the library keeps of the part for matching it to boards' parts (see [Choosing a
part](boards.md#choosing-a-part)): its **Value** and **Datasheet** (a link or a file), the ID it is known
by for good; its **Identifiers**, each an **MPN** or a **Supplier PN** with its **Manufacturer /
Supplier** and **Code** (a board part with one of them is this part, without asking); and **Also Known
As**, what boards' files call it: by **Value and footprint** (written *value*|*footprint*), **Value** or
**Footprint**, each saying where it was learned and when (or *typed*). Names are learned when this part is
chosen for a board's part with **Remember** ticked; the plus adds one, the cross deletes one.

**Settings** holds its **Pick Conditions**: **Feed & Pick Retry
Count**, how many times the feed and pick is tried again for each placement (the nozzle is cleared, and
the part discarded, after each failed try).

**Bottom Vision Settings** and **Fiducial Vision Settings** show the vision settings the part uses (its
own, else its package's, else the machine's), as on the [Vision](vision.md#the-settings) tab: a change is a
change to those settings, for everything that uses them. **Specialize for** the part makes a copy of them,
named after the part, for this part alone; it says so when the part has its own already.

<!-- src: src/ui/JPPartsPanel.cpp (updateWizards, formFor, libraryPage, libraryAct, act); src/setup/JPVisionForms.cpp (addPage, act) -->
