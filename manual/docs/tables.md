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

## Search

**Search**, at the right of a toolbar, shows only the rows with the text typed anywhere in them, in any
column, a hidden one's too, whatever its case. The text is a regular expression: `^C0402` finds rows with
something starting C0402. Beside it, while it searches, how many rows it leaves of how many (*12 of 340*),
and in each cell what it matched is marked. A row found only in a column the table does not show has a mark
at its start, and pointing at it says which column it was found in. The **✕** at its right end empties it.

<!-- src: src/ui/JPUiParts.cpp (search); src/ui/JPSearchField.cpp (attach, updateCount); src/ui/JPTable.cpp (setFilter, rowPasses, unsearchedRowCount, populateRenderPrimitives: markMatch, the hidden mark; handleMouseMove) -->

A column's heading and a colon first searches that column alone: `lcsc: C15` finds the rows whose LCSC
starts with or holds C15, and `lcsc:` alone the rows with anything in it. As you type the start of a
column's heading, the rest of it shows greyed after what you typed; **Tab** (or **→** at the end) takes it,
with the colon. A colon after something that is no column's heading (`http://`) is part of the search.

<!-- src: src/ui/JPSearchField.cpp (completion, handleKeyEvent, populateRenderPrimitives); src/ui/JPTable.cpp (setFilter, columnNames) -->

## Moving and widening columns

Drag a heading's edge to widen a column; the columns after it give way. Drag a heading sideways to move its
column. While it is dragged the table shows how it will look: the column floats with the pointer and the
others move aside to make its place. Letting go keeps it there; **Escape** puts every column back as it was.
Each table keeps its columns' order, widths and sort as you leave them, the next time jplacer starts too.
They are kept by what each column is, not by its heading, so a column you rename keeps its place.

<!-- src: src/ui/JPTable.cpp (setColumnWidth, previewDrop, endColumnDrag, applyState, keepState, order); src/ui/JPTableModel.h (Column::key, keyOf); src/app/JPlacerApp.cpp (s_loadState, s_saveState); src/app/JPlacerSettings.h (kTableState) -->

## Views

A **view** is a table's layout kept under a name: which columns it shows, their order and widths, and its
sort. Each table has its own, and switching between them changes the whole layout in one click: a simple
view, a supplier's, a production view, your own. Each starts with two: **Standard**, the table as jplacer
ships it, and **All Fields**, every column, always (those you add later too; its ticks in **Columns** are
greyed, as it hides none). Whatever you change in a view (a column shown or hidden, moved, widened, a sort)
is kept in that view as you change it: there is nothing to save.

<!-- src: src/ui/JPTable.cpp (readViews, saveViews, showView, keepState, order: All Fields, setColumnShown); src/ui/JPTable.h (View) -->

Beside **Columns** on the toolbar, each view has a button showing its icon, its name in its tooltip; the view
shown is lit. Click one to show it, or press **Ctrl+1** to **Ctrl+9** in the table for its first nine (the
keys can be changed in **Edit ▸ Preferences…**, as **Show View 1** to **9**). Drag a button along the row to
move its view there. Right-click it for its own menu:

| Entry | |
|---|---|
| **Show "*name*"** | Shows it. |
| **Rename…** | Gives it another name. |
| **Icon** | The icon its button shows: List, Columns, Star, Truck, Wrench, Eye, Person, Clipboard, Tag, Flag, Box, Gear or Check. |
| **Duplicate** | A copy of it beside it (as shown now, changes and all), shown. |
| **Locked** | Keeps the view as it is: a change while it is shown lasts only until another view is shown, its button marked with a dot meanwhile. Unlocked while changed, the change is its own. |
| **Keeps Its Search** | The view keeps the search typed while it is shown, and puts it back in **Search** whenever it is shown again: a production view of feeders showing only those enabled, a view of the parts with no height. |
| **Revert View** | (the view shown) Puts it back as it was kept, the changes since it was shown undone. |
| **Delete View** | Takes it away (a table keeps one view at least). |

<!-- src: src/ui/JPTableTools.cpp (ViewButton, rebuild, dropIndex); src/ui/JPTable.cpp (viewMenu, moveView, duplicateView, deleteView, setViewLocked, setViewKeepsSearch, revertView, renameView, setViewIcon); src/ui/JPViewIcons.cpp; src/app/JPlacerApp.cpp (table.view) -->

## Columns

**Columns** (the table icon), at the end of a toolbar's icons after a divider, starts with the table's
views: each one (the one shown ticked, *(changed)* after a locked one changed), **New View…** (a view made
from the one shown, named as you say, with an icon no other view has yet) and **This View**, the view shown's
own menu. Then it chooses which columns the table shows: a tick for each column shown; click one to hide it
or to show it again in its place, at the width it had. The last column showing cannot be hidden. **Reset Columns** at
the bottom shows them all again, as they first were. Which columns are shown is kept with the rest of the
table's layout, each table its own. The tables on the **Job**, **Boards**, **Panels**, **Parts**,
**Packages**, **Vision**, **Feeders** and **Issues & Solutions** tabs have it and their views, the
footprint's pads too, and the table a feeder's **Part** or a part's **Package** drops has them beside its filter (its views its own,
though no fields are added or edited from it, as it is only chosen from). A table with no view buttons (those
in a window of their own) is a plain table: its **Columns** and its headings' menu show and hide its columns,
and offer no views and no fields to add or edit.

<!-- src: src/ui/JPUiParts.cpp (tableTools); src/ui/JPTableTools.cpp; src/ui/JPTable.cpp (columnsMenu, setColumnShown, columnsShown, allColumns, resetColumns); src/ui/JPIssuesPanel.cpp; src/ui/JPTable.h (setHasTools, setOffersFields); src/ui/JPTableTools.cpp (attach); src/ui/JPChooserTable.cpp (barControl); src/ui/JPPartsPanel.cpp; src/ui/JPPackagesPanel.cpp; src/ui/JPJobPanel.cpp; src/ui/JPJobPlacementsPanel.cpp; src/ui/JPBoardTools.cpp; src/ui/JPPanelDefinitionPanel.cpp; src/ui/JPFeedersPanel.cpp; src/ui/JPVisionSettingsPanel.cpp -->

Right-click a heading for that column's menu: **Hide "*name*"**, **Show in Every View** (shown in each of
the table's views, not only the one shown), **Add Field After "*name*"…**, **Edit Field "*name*"…** and
**Reset Columns**.

<!-- src: src/ui/JPTable.cpp (buildHeaderMenu, prepareContextMenu, showInEveryView) -->

## Editing many rows, pasting and undo

A value typed into a cell, a tick or a choice, in a row that is one of several chosen, goes into every row
chosen: choose 50 parts, type *LCSC* in one row's **Supplier**, and it is in all 50. Choose the rows, then
type (or press **F2**) in the cell, tick it, or pick its choice; a double-click in one of the chosen rows edits
them all too, where a plain click on one chooses it alone. A row that cannot take the value (one whose cell
cannot be changed) is left as it is; a value refused by one row is taken by none, the cell staying open to put
it right. The status line says how many were changed.

<!-- src: src/ui/JPTable.cpp (editRows, setCells, stopEditing, toggle, openChoices, handleMousePress, handleMouseRelease); src/app/JPlacerApp.cpp (s_status) -->

**Ctrl+V** pastes what **Ctrl+C** copied, or a block of cells copied from a spreadsheet (a tab between
cells, a line a row): from the chosen cell, down the rows and along the columns as the table shows them. One
value goes into every row chosen. A cell that cannot be changed is passed over, and the status line says how
many. A value a cell cannot take (a word for a number, a choice it does not have) and nothing at all is
pasted, the first few refused named.

<!-- src: src/ui/JPTable.cpp (paste, setCellValue) -->

Each change is one step **Edit ▸ Undo** (Ctrl+Z) takes back and **Edit ▸ Redo** (Ctrl+Y) makes again,
however many cells it changed, named for it ("Undo Change Rank of 12 Placements", "Undo Change 3 Cells of 2
Rows"). The library's tables (parts, packages and their pads) are undone as the library's changes are (see
[Parts](parts.md#undo-and-redo)); the others' steps are kept with Machine Setup's and the library's, the last
made first, the last 200 of them.

<!-- src: src/ui/JPTableHistory.cpp (record); src/ui/JPTable.cpp (putCells, setCells); src/ui/JPPartsTableModel.h (ownHistory); src/app/JPlacerApp.cpp (m_tableEdits); src/app/JPlacerUndo.cpp -->

## Fields

Each column of a table of things (parts, placements, feeders…) is one of their **fields**. jplacer has its
own, the columns each table has always had, and you can add as many of your own as you want: an LCSC part
number, a supplier, a reel's size, a customer's order number. A field belongs to the kind of thing, not to
one table: a field added to placements is a column of the Job tab's placements, a panel's
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
| Feeders | ID, Name, Part, Type, Enabled |
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
