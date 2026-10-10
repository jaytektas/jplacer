# Parts

The **Parts** tab (in the work area, after Boards, as in OpenPnP) lists the library's parts, as
OpenPnP's Parts tab does: those every job draws from. Under them are the parts of each open board (see
[Boards](boards.md#the-boards-parts)), so what a board actually carries is in view beside the library's.

## Where a part lives

A board's rows have the board icon before their ID, and their **Source** (the board's name) is tinted;
the library's say *Library* and are plain. **Status** says how a board's part stands to the library:

| Status | |
|---|---|
| **Own** | The board's own part: the library has none. It is changed here like a library part (the board then has changes to save, asked about as boards are), and right-click **Add to Library** copies it (and its package, where the library has none of that name) into the library under its name without the board's, for the next boards; the board keeps its own. |
| **Matched** (green) | The board's copy of a library part, as the library has it. It is not changed here: change the library's. |
| **Library changed** (amber) | The board's copy, the library's part changed since. Right-click **Update from Library** (or the button on its page) takes the copy again. |
| **To be chosen** (red) | A part the board's files named and that has not been chosen yet: only what the files said (its value, its footprint). Choose it on the Boards tab. |

A board's ID is shown without the board's name before it. Only the library's parts are deleted here. A
board's own part has its **Settings** and vision settings pages, not **Library** or **Stock** (the
library's); a board's copy, or a part to be chosen, has a page saying what it is.

**Show**, beside Search, lists **All**, the **Library**'s only, the **Boards**' only, or one open board's.

<!-- src: src/ui/JPPartsTableModel.cpp (cellIcon, cellTint, rowShown, showChoices, editable); src/ui/JPPartsPanel.cpp (formFor, addToLibrary, updateFromLibrary, updateWizards, refresh); src/model/JPCatalog.cpp (parts, addToLibrary, updateFromLibrary, statusName); src/app/JPlacerOpenPnpTabs.cpp (boardChanged) -->

The library is kept in `library.db` in jplacer's configuration folder: its parts and packages, each with
an ID that stays with it whatever it is named, and what boards' files call it. It is made from OpenPnP's
`parts.xml` and `packages.xml` the first time jplacer starts without one (those files are left as they
are). OpenPnP's files copied into the folder later are looked at again: their parts and packages the
library does not have are added to it (the start says how many); those it has are not changed, but for
vision settings: a part or package the library has with no bottom or fiducial vision settings of its own takes
OpenPnP's (a board's import may have made it first, without them, and its fiducials were then found by the
machine's default settings); one it has is never replaced. A library made before this takes them once.

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
The text is a regular expression: `^C0402` finds parts whose ID starts with C0402. The **✕** at its right
end, there while it holds text, empties it and shows every part again.

<!-- src: src/ui/JPPartsPanel.cpp (newPart, deleteParts, copyPart, pastePart, updateWizards); src/app/JPlacerOpenPnpTabs.cpp (onPickPart); src/model/JPConfiguration.cpp (findFeeder); src/ui/JPTable.cpp (setFilter) -->

## The table

| Column | |
|---|---|
| **ID** | The part's ID (a board's without the board's name before it). |
| **Source** | Where it lives: *Library*, or the open board's name. |
| **Status** | How a board's part stands to the library (see above); empty for the library's. |
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

Everything but the ID, Source, Status, the MPN and the two counts is changed in the table (a board's copy or a part to be chosen not at all): double-click a cell, press F2, or start
typing; **Return** keeps the change, **Escape** puts back what was there. **Tab** keeps it and goes on to the
next cell that can be changed (**Shift+Tab**: the one before), along the row and on to the next: a text or number
cell opens with what is in it chosen, ready to type over; a choice or tick box is chosen, for F2 or Space. Every
table edits this way. A height may be typed
with units (`0.5mm`, `20mil`); without, the part's own units are taken. Package and the vision settings
open their list on a click: a drop-down under the cell, as wide as its longest name, twelve at a time and
scrolled for the rest, the one chosen now marked. Each change is saved at once.

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
**Manufacturers' Names…** opens the manufacturers the library knows, a row each with its **Name** and its
other names (commas between: Texas Instruments: TI, Texas Instruments Inc.), **Add a Manufacturer** and the
cross to delete one; a BOM's manufacturer by any of them is that manufacturer. An MPN the library has under
another manufacturer is offered but never taken unasked, and says so.

**Packagings**: how the part comes, a row each: **Kind** (**Cut tape**, **Reel**, **Tray**, **Tube**,
**Loose**); for tape, its width (**Tape [mm]**: 8 to 56), the **Pitch [mm]** between pockets and whether
it is **Paper** or **Embossed**; the part's **Rotation [°]** as it sits in the packaging (pin 1 against the
tape's sprocket holes at 0), set once here; **Quantity**, how many a reel, tray or tube holds; a **Note**.
Choosing another kind shows or hides the tape's columns.

**Offers**: where it is bought, a row each: **Supplier**, **SKU** (their part number), **Packaging**,
**MOQ** (the least they sell), **Price breaks** (`1: 0.0100, 100: 0.0050`), **Last price** and **Link**.

A board's copy of the part (see [Choosing a part](boards.md#choosing-a-part)) counts its packagings: a
change in how it comes is a change to review; a new offer, or a name learned, is not.

**Stock** is what you have of the part: a lot is one reel, strip of cut tape, tray, tube or bag of it.
Stock is what you have, not what is loaded on the machine. The page says how many are in stock and in how
many lots, and lists each open lot: its **Lot** name, **Packaging**, how many it **Holds**, the feeder
it is loaded **On** (see [Feeders](feeders.md#the-feeders-setup)), **Where kept**,
**Date code** and **Note** (the name, where kept, date code and note are changed here and kept at once).
**Receive Stock…** adds a lot: its name on the shelf, packaging, **Supplier** and **SKU** (the part's
first offer is filled in), **Date code**, **Lot code**, **Where kept**, **Note**, and how it came:
**Received** (an order: its **Quantity**, the **Cost** of all of them and the **Order**) or **Counted**
(found on the shelf). **Receive** is offered once the lot has a name and a quantity.

How many a lot holds is its ledger's: **Ledger…** lists every change to the lot in order, **When**, the
**Entry**, the **Quantity**, what it **Holds after**, the **Cost**, the **Reference** and the **Note**, so
a wrong figure shows where it went wrong. Below the list, add an entry: **Used** (taken for a job),
**Lost** (dropped, mis-picked, thrown away), **Counted** (what it holds, counted now), **Adjusted** (more,
12, or fewer, -12, by hand) or **Received** (more came in), with its quantity, a reference and a note;
**Add to Ledger** (or Return) keeps it at once. **Close the Lot** takes a lot used up or thrown away out of
stock, its ledger kept (the Stock page counts the closed lots); **Reopen the Lot** puts it back.

**Attrition** is how many to allow for parts lost to mis-picks and drops, as a share of those placed;
a job's shortages add it to what the job needs. Type the part's own **Attrition [%]**, or leave it empty to
use what the ledger measured: lost of all taken (used and lost), once any have been used. The page says what
was measured.

Stock is kept in the library's file as each change is made, without saving.

<!-- src: src/ui/JPPartsPanel.cpp (stockPage, stockAct); src/app/JPlacerStockReceiveDialog.cpp; src/app/JPlacerLotLedgerDialog.cpp; src/model/JPStockStore.cpp; src/model/JPLedgerEntry.h (apply); src/model/JPStockLot.h -->

**Settings** holds its **Pick Conditions**: **Feed & Pick Retry
Count**, how many times the feed and pick is tried again for each placement (the nozzle is cleared, and
the part discarded, after each failed try).

**Bottom Vision Settings** and **Fiducial Vision Settings** show the vision settings the part uses (its
own, else its package's, else the machine's), as on the [Vision](vision.md#the-settings) tab: a change is a
change to those settings, for everything that uses them. While the part uses settings it shares,
**Specialize for** the part makes a copy of them, named after the part, for this part alone. Once it has
its own, **Use Package *package*'s Settings** (or **Use the Machine's Default Settings**, its package having
none) puts it back on those, after asking; its own stay on the Vision tab, used by nothing.

<!-- src: src/ui/JPPartsPanel.cpp (updateWizards, formFor, libraryPage, libraryAct, act); src/app/JPlacerManufacturersDialog.cpp; src/model/JPPart.h (Packaging, Offer, kPackagingKinds); src/model/JPConfiguration.cpp (manufacturerName, sameManufacturer); src/model/JPPartMatcher.cpp (candidates); src/model/JPLibraryJson.cpp (fingerprint); src/setup/JPVisionForms.cpp (addPage, manageFor, act) -->

## Undo and Redo

Every change to the library is a step **Edit ▸ Undo** (Ctrl+Z) takes back and **Edit ▸ Redo** (Ctrl+Y)
makes again: a part or package made, changed or deleted (several deleted at once are one step), a field
changed on a part's or package's pages, a footprint added, changed or deleted, the manufacturers' names,
and what choosing a part for a board taught the library (a name remembered, a part added). Each says
what it would undo or redo: "Undo Delete Part R0603-10k", "Undo Change Package SOT-23", "Undo Delete 3
Parts", "Undo New Footprint SOT-23-3". A part or package put back by Undo is the same one, its place in the
list kept. Undo and Redo go through the library's changes and Machine Setup's together, the last made
first. The last 200 steps are kept. Stock (lots and their ledger) is kept as it is made and is not undone.

<!-- src: src/setup/JPLibraryHistory.cpp (note, describe, undo, redo); src/model/JPConfiguration.cpp (librarySnapshot, restoreLibrary); src/app/JPlacerOpenPnpTabs.cpp (libraryChanged, changed); src/app/JPlacerUndo.cpp; src/app/JPlacerApp.cpp -->
