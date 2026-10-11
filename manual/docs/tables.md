# Tables

Every table in jplacer works the same way: the parts, packages and their pads, boards, panels and their
placements, the job's boards, feeders and vision settings. You can sort it, choose rows, move, widen and hide
its columns, and add fields of your own to what its rows are.

## Sorting, choosing and copying

Click a column's heading to sort by it; click it again to turn it round. Clicking another heading sorts by
that first and by the earlier ones after it (up to three); the later ones' arrows are fainter. Choose
several rows with **Shift**+click and **Ctrl**+click; **Ctrl+A** chooses them all, and **Ctrl+C** copies the
chosen rows, a tab between cells (in the columns' order as shown).

<!-- src: src/ui/JPTable.cpp (clickHeader, selectView, handleKeyEvent, copySelection) -->

## Moving and widening columns

Drag a heading's edge to widen a column; the columns after it give way. Drag a heading sideways to move its
column. While it is dragged the table shows how it will look: the column floats with the pointer and the
others move aside to make its place. Letting go keeps it there; **Escape** puts every column back as it was.
Each table keeps its columns' order, widths and sort as you leave them, the next time jplacer starts too.
They are kept by what each column is, not by its heading, so a column you rename keeps its place.

<!-- src: src/ui/JPTable.cpp (setColumnWidth, previewDrop, endColumnDrag, applyState, keepState, order); src/ui/JPTableModel.h (Column::key, keyOf); src/app/JPlacerApp.cpp (s_loadState, s_saveState); src/app/JPlacerSettings.h (kTableState) -->

## Columns

**Columns** (the table icon), at the end of a toolbar's icons after a divider, chooses which columns the
table shows. It opens a list of every column with a tick for each one shown: click one to hide it or to show
it again in its place, at the width it had. The last column showing cannot be hidden. **Reset Columns** at
the bottom shows them all again, as they first were. Which columns are shown is kept with the rest of the
table's layout, each table its own. The tables on the **Job**, **Boards**, **Panels**, **Parts**,
**Packages**, **Vision** and **Feeders** tabs have it, the footprint's pads too, and so does the table a
feeder's **Part** drops.

<!-- src: src/ui/JPUiParts.cpp (columnsButton); src/ui/JPTable.cpp (columnsMenu, setColumnShown, columnsShown, allColumns, resetColumns); src/ui/JPPartsPanel.cpp; src/ui/JPPackagesPanel.cpp; src/ui/JPJobPanel.cpp; src/ui/JPJobPlacementsPanel.cpp; src/ui/JPPlacementsHoldersGroup.cpp; src/ui/JPBoardPlacementsPanel.cpp; src/ui/JPPanelDefinitionPanel.cpp; src/ui/JPFeedersPanel.cpp; src/ui/JPVisionSettingsPanel.cpp -->

Right-click a heading for that column's menu: **Hide "*name*"**, **Add Field After "*name*"…**, **Edit Field
"*name*"…** and **Reset Columns**.

<!-- src: src/ui/JPTable.cpp (buildHeaderMenu, prepareContextMenu) -->

## Fields

Each column of a table of things (parts, placements, feeders…) is one of their **fields**. jplacer has its
own, the columns each table has always had, and you can add as many of your own as you want: an LCSC part
number, a supplier, a reel's size, a customer's order number. A field belongs to the kind of thing, not to
one table: a field added to placements is a column of the Boards tab's placements, the Job tab's, a panel's
fiducials, and every other table of placements. And the import offers each one to fill (see
[Importing a board](boards.md)).

<!-- src: src/model/JPFieldSet.h; src/model/JPFieldSet.cpp (builtIns, kinds); src/ui/JPFieldsModel.h; src/ui/JPTable.cpp (setModel) -->

**Add Field…**, in **Columns** and on a heading's menu, adds one. Give it a **Name** (unique among that kind's
fields, and without a colon), a **Type**, and the **Import matches**: the headings of a CAD file's or BOM's
column that the import takes for it, commas between (its name always counts, and upper and lower case,
spaces and punctuation do not matter). It shows in the table you added it from, at the end or after the
column whose heading you right-clicked, and is hidden in the other tables of its kind until you tick it in
their **Columns**.

<!-- src: src/app/JPlacerFieldDialog.cpp; src/model/JPField.cpp (matchesHeading); src/model/JPFieldSet.cpp (nameFree, add); src/ui/JPTable.cpp (fieldAdded, order); src/ui/JPFieldsModel.cpp (column: hiddenAtFirst) -->

The type says how its values are typed and shown:

| Type | Typed as | Shown |
|---|---|---|
| **Text** | anything | as typed |
| **Number** | a number (`12`, `-0.5`, `12,5`) | as typed, on the right, sorted as numbers |
| **Length** | a length in any units (`1.2`, `40mil`, `0.5in`); without units, the System Units | in the System Units, sorted by length |
| **Yes/No** | | a tick box |
| **Choice** | one of its **Choices** (commas between when you make it) | picked from its list; the empty one at the end takes it out |

A value that is not one of its type is refused, and the cell stays open to put it right. Emptying a cell
takes its value out.

<!-- src: src/model/JPField.cpp (parse, text); src/ui/JPFieldsModel.cpp (column, text, setText, setChoice, setChecked, compare); src/ui/JPTable.cpp (stopEditing) -->

**Edit Fields…** (in **Columns**) lists every field of that kind: its name, its type (jplacer's own for its
fields), whose it is, whether jplacer needs it, its import matches, and how many have a value in each of
yours. **Edit…**, or a double-click, or **Edit Field "*name*"…** on a heading, opens the field: rename any
field, and change the import matches of one an import fills; a field of yours also its type and choices.
Changing a field of yours to another type reads its values again as that type. Those it cannot read are
named first, and emptied once you agree.

<!-- src: src/app/JPlacerFieldsDialog.cpp; src/app/JPlacerFieldDialog.cpp (keep); src/model/JPFieldRecords.cpp (unconvertible, convert, count) -->

**Delete…** takes a field of yours away with its values (how many have one is said first). A field of
jplacer's you have no use for is **Removed…**: it leaves every table, the import and the search, its values
staying as they are (jplacer uses some of them itself), until **Restore** in **Edit Fields…** brings it back.
The fields jplacer cannot work without have no **Delete…**:

| Table of | Kept always |
|---|---|
| Parts | ID, Height, Package |
| Packages | ID |
| Pads | Name, X, Y, Width, Length, Rot. |
| Placements | ID, Part, Side, X, Y, Rot. |
| Boards and panels where they lie | Board/Panel Id, Side, X, Y, Z, Rot., Enabled? |
| Boards, Panels | Board Name, Panel Name |
| Feeders | Name, Part, Type, Enabled |
| Vision settings | Name |

<!-- src: src/model/JPFieldSet.cpp (builtIns: required; remove; restore); src/app/JPlacerFieldDialog.cpp (remove); src/app/JPlacerFieldsDialog.cpp (Restore) -->

The fields are kept in `fields.json` in the configuration folder. Each thing keeps its own values with it:
a part's, a package's and its pads' in the library, a placement's and a board's in the board's file, a
panel's in its file, the job's boards and the job's own in the job, a feeder's in `feeders.json`, vision
settings' in `vision-settings.json`. A placement's values are its board's: on the **Job** tab, a board used
more than once keeps no values of its own, so there they are shown and not changed.

<!-- src: src/model/JPFields.cpp; src/model/JPFieldValues.cpp (write, read); src/model/JPLibraryStore.cpp (custom); src/model/JPPlacement.cpp; src/model/JPBoard.cpp; src/model/JPPanel.cpp; src/model/JPPlacementsHolderLocation.cpp; src/model/JPJob.cpp; src/model/JPFeeder.cpp; src/model/JPConfiguration.cpp (save, load); src/ui/JPPlacementsTableModel.cpp (customEditable) -->

## Job fields

A job has no table of its own, so its fields are in **Job ▸ Job Fields…**: a row for each field added to
jobs, its value changed in place, and **Add Field…** and **Edit Fields…** as every table has them. The job's
values are kept in its file.

<!-- src: src/app/JPlacerJobFieldsDialog.cpp; src/app/JPlacerMenuBuilder.cpp (job.fields); src/app/JPlacerOpenPnpTabs.cpp (openJobFields) -->
