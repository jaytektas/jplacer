# Panels

The **Panels** tab (in the work area, before Boards, as in OpenPnP) lists the panels open and, under
them, the chosen panel's definition, as OpenPnP's Panels tab does. A panel holds boards (and other
panels), each where it lies on the panel, and the fiducials or placements the panel is aligned by. Each
panel is its own `.panel.xml` file. The panels open are the open job's and any you open here while it is
open; leaving the job closes them, as it closes boards (see [Boards](boards.md)).

<!-- src: src/ui/JPPanelsPanel.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/app/JPlacerJob.cpp (leave); src/model/JPConfiguration.cpp (closeAll) -->

## Panels

The buttons and the table work as the [Boards](boards.md#boards) tab's: **Add Panel...** (**Create New
Panel...**, **Existing Panel**), **Remove Panel**, **Copy Panel...** and **Clean Up**. A panel used by the
job, or inside another known panel, is not taken off. Clean Up goes round again while it takes any off,
since a panel may only have been used inside one that has just gone.

<!-- src: src/ui/JPPlacementsHoldersGroup.cpp -->

## Children

The boards and panels on the chosen panel: **Board/Panel Id**, **Name**, **Width**, **Length**, **Side**,
**X**, **Y**, **Rot.**, **Enabled?** and **Check Fids?**. All but the size are changed in the table. Side
turns a child over where it lies. The name is the board's or panel's own, wherever it is used.

| Button | |
|---|---|
| **Add Child...** (plus, with a menu) | **Add New Board...** or **Add New Panel...** asks where to save a new one; **Add Existing Board...** or **Add Existing Panel...** offers those known, or **Browse** for a file. A panel cannot be put on itself, or on a panel inside it. |
| **Remove Child(ren)** (cross) | Takes the chosen children off the panel, with the alignment pseudo-placements that came from them. |
| **Create array of children...** | Copies of the chosen child in an array (see below). |
| **View Panel** | Opens the panel viewer (see below). |

Right-click for **Replace child(ren)...** (another board or panel in their places, keeping where they lie;
for children that are all the same board or panel), **Set Side...**, **Set Enabled...** and **Set Check
Fids...**. **Space** turns the chosen child on or off.

<!-- src: src/ui/JPPanelDefinitionPanel.cpp; src/ui/JPLocationsTableModel.cpp; src/app/JPlacerExistingHolderDialog.cpp -->

## Alignment fiducials and placements

What the panel is aligned by: its own fiducials, and *pseudo-placements*, fiducials or placements of its
boards used as the panel's (named by where they are, `Brd1⇒FID1`). Its own are changed in the table; a
pseudo-placement is only turned on or off.

| Button | |
|---|---|
| **Add Fiducial...** (plus) | Asks for an ID and adds a fiducial of the first part, at 0, 0. |
| **Remove Fiducial(s)...** (cross) | Takes the chosen ones off. |
| **Use Children Fiducials...** | Lists every fiducial (or placement, or both) of the panel's boards where it lies on the panel; **Hull Only** shows just those on the outermost edge. **Auto Select** chooses the four on each side spanning the most area (those turned off are passed over). **OK** adds the chosen ones. |

Right-click for **Set Side...** and **Set Enabled...**.

<!-- src: src/ui/JPPanelDefinitionPanel.cpp; src/app/JPlacerChildFiducialsDialog.cpp; src/model/JPPanel.cpp (pseudoPlacements) -->

## Arrays

**Create array of children...** lays out copies of the chosen child. **Rectangular**: columns and rows,
each a step apart; alternate rows can have a column more or less and be offset. **Circular**: round a
centre (given on the panel, or on the child) in angular steps, and outward in radial steps, the angular
steps growing with the radius when asked. The copies are named `Brd1[row,column]`, the child itself
becoming `[1,1]`. Each change shows at once on the panel drawn below it (the child dashed long, its
copies short); **OK** keeps them, **Cancel** puts the panel back as it was.

<!-- src: src/app/JPlacerPanelArrayDialog.cpp -->

## The viewer

**View Panel** (and the Boards tab's **View Board**) opens the viewer, as OpenPnP's Panel Viewer and Board
Viewer: a tab in the work area (after the others), where it can be dragged out on its own, showing the chosen panel or board and
following the choice. It draws each board's and panel's outline (top side up in one colour, bottom side
up in another, struck through when it is not enabled), and as ticked its **Board/Panel Locations**,
**Board/Panel Origins**, **Fiducials** and **Placements**, and a **Reticle**. **Viewing From Top** turns to
the bottom and back; a panel can show its children only or all its descendants. The wheel zooms about
the pointer; drag to pan. Right-click a board, panel, placement or fiducial for its **Enabled?** and
**Check Fids?**. In the job's viewer, as OpenPnP's, a placement or fiducial also has **Placed?** and
**Center Camera on Placement** (or **Fiducial**), and a board or panel **Center Camera on Board Location**
(or **Panel Location**: its origin, or for one bottom side up its corner at the far X) and **Run Fiduicial
Check on Board Location** (as the Job tab's Fiducial Check: set by its fiducials, the camera taken there).

<!-- src: src/ui/JPPlacementsViewer.cpp; src/ui/JPPlacementsViewerCanvas.cpp; src/app/JPlacerViewerDock.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/app/JPlacerJobRun.cpp (fiducialCheck) -->

## Saving panels

As boards: **File > Save Configuration**, and quitting, ask about each panel with changes. A change to a
panel is made to every use of it, in the job and on other panels.

<!-- src: src/app/JPlacerOpenPnpTabs.cpp (saveConfiguration); src/model/JPDefinitionChanges.h -->
