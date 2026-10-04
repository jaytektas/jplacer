# Parts

The **Parts** tab (in the work area, after Boards, as in OpenPnP) lists every part jplacer knows, as
OpenPnP's Parts tab does. Parts are kept in `parts.xml` in jplacer's configuration folder; OpenPnP's
own `parts.xml` can be copied there as it is.

<!-- src: src/ui/JPPartsPanel.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/model/JPConfiguration.h (kPartsFile) -->

## The toolbar

| Button | |
|---|---|
| **New Part...** (plus) | Asks for the new part's ID and makes it, in the first package. There must be a package first. An ID already used is refused, and asked for again. |
| **Delete Part** (cross) | Deletes the chosen parts, after asking. |
| **Pick Part** | Feeds and picks the chosen part, as the Feeders tab's **Pick...** does, from the feeder OpenPnP would take it from: of the enabled feeders holding it, those of the highest priority, and of those the one closest to the camera. With none, it says no valid feeder was found. |
| **Copy Part to Clipboard** | Puts the chosen part on the clipboard as text (the part's line from `parts.xml`). |
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
| **Height** | The distance between the board's surface and where the nozzle picks it, in mm (in its own units, with their name, when those are not mm). |
| **Through-Board Depth** | How far any of it reaches below the board when placed; added to the height for Dynamic Safe Z. Zero for surface-mount parts. |
| **Package** | Its package, chosen from a list. |
| **Speed %** | How fast it is moved, as a share of full speed. |
| **BottomVision**, **FiducialVision** | The vision settings it uses, chosen from a list; empty: its package's. |
| **Placements** | How many placements on the known boards use it. |
| **Feeders** | How many feeders hold it. |

Everything but the ID and the two counts is changed in the table: double-click a cell, press F2, or start
typing; **Return** or **Tab** keeps the change, **Escape** puts back what was there. A height may be typed
with units (`0.5mm`, `20mil`); without, the part's own units are taken. Package and the vision settings
open their list on a click. Each change is saved at once.

Click a column's heading to sort by it; click it again to turn it round. Clicking another heading sorts by
that first and by the earlier ones after it (up to three); the later ones' arrows are fainter. Choose
several rows with **Shift**+click and **Ctrl**+click; **Ctrl+A** chooses them all, and **Ctrl+C** copies the
chosen rows, a tab between cells. Drag a heading's edge to widen a column; the columns after it give way.

<!-- src: src/ui/JPPartsTableModel.cpp; src/ui/JPTable.cpp (clickHeader, handleKeyEvent, startEditing, setColumnWidth); src/ui/JPLengthCell.cpp -->

## The part's tabs

Under the table, the chosen part's tabs. **Settings** holds its **Pick Conditions**: **Feed & Pick Retry
Count**, how many times the feed and pick is tried again for each placement (the nozzle is cleared, and
the part discarded, after each failed try).

<!-- src: src/ui/JPPartsPanel.cpp (updateWizards) -->
