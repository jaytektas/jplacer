# Panels

A panel is the job's, as a board is. On the [Job tab](jobs.md#the-job-tab), choosing one of its panels
shows, beside its **Placements** in the job, the **Panel** tab: the panel's definition, what every use of it
shares. A panel holds boards (and other panels), each where it lies on the panel, and the fiducials or
placements the panel is aligned by. It comes into the job by the Job tab's **Add Board/Panel**: **New
Panel...**, **Existing Panel...** (a `.jppanel`, or OpenPnP's `.panel.xml`), or **Copy of the Chosen
Panel...** (a copy saved as a new file, and added). Each panel is its own `.jppanel` file. OpenPnP's
`.panel.xml` is read, and saved beside it as a `.jppanel` as a board is (see [Saving
boards](boards.md#saving-boards)): the job and the panels that use it follow the new file, and OpenPnP's is
left as it was. The panels open are the open job's; leaving the job closes them, as it closes boards (see
[Boards](boards.md)).

<!-- src: src/ui/JPJobPanel.cpp (showDefinition, showAddMenu, copyChosen); src/app/JPlacerOpenPnpTabs.cpp (confirmSave, holderMoved); src/app/JPlacerJob.cpp (leave); src/model/JPConfiguration.cpp (closeAll, panel, savePanel); src/model/JPPanel.h (kExtension) -->

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

Right-click for **Set Side...** and **Set Enabled...**. A fiducial's **Part** is chosen from a table of
the library's parts, as a feeder's is (see [Choosing a part](feeders.md#choosing-a-part)).

<!-- src: src/ui/JPPlacementsTableModel.cpp (choiceSearch); src/ui/JPPanelDefinitionPanel.cpp; src/app/JPlacerChildFiducialsDialog.cpp; src/model/JPPanel.cpp (pseudoPlacements) -->

## Arrays

**Create array of children...** lays out copies of the chosen child. **Rectangular**: columns and rows,
each a step apart; alternate rows can have a column more or less and be offset. **Circular**: round a
centre (given on the panel, or on the child) in angular steps, and outward in radial steps, the angular
steps growing with the radius when asked. The copies are named `Brd1[row,column]`, the child itself
becoming `[1,1]`. Each change shows at once on the panel drawn below it (the child dashed long, its
copies short); **OK** keeps them, **Cancel** puts the panel back as it was.

<!-- src: src/app/JPlacerPanelArrayDialog.cpp -->

## The viewer

**View Panel** (and a board's **View Board**) opens the viewer, as OpenPnP's Panel Viewer and Board
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

A board's viewer lists its photos under **Photo**, the newest first, each by when it was taken, why (**Job
start**, **Job end**, **Asked for**) and the board's id in the job ([Board photos](jobs.md#board-photos)).
The one chosen lies under the drawing where it was taken, its placements over it; it shows from the side
that was up, the viewer turning to that side. What was said of it is below the list. **Delete Photo…**
deletes it, once you say so (it cannot be brought back); the last one gone, its folder goes too.

<!-- src: src/ui/JPPlacementsViewer.cpp (photosChanged, showPhoto, deletePhoto); src/ui/JPPlacementsViewerCanvas.cpp (drawPhoto, photoSeen); src/model/JPBoardPhoto.cpp (of, label, remove) -->

## Saving panels

As boards: **File > Save Configuration**, and quitting, ask about each panel with changes. A change to a
panel is made to every use of it, in the job and on other panels.

<!-- src: src/app/JPlacerOpenPnpTabs.cpp (saveConfiguration); src/model/JPDefinitionChanges.h -->
