# Board

The **Board** panel (a dock on the right, with Machine, Jog and Actuators) holds the open
[job](jobs.md)'s board on the machine: what is on it, which side is up, and exactly where it is.

<!-- src: src/ui/JPBoardPanel.cpp; src/app/JPlacerBoard.cpp; src/app/JPlacerMachine.cpp (buildPanels) -->

## Reading the board

**Import Pick-and-Place…** (or **File ▸ Import Pick-and-Place File…**) reads the board's
pick-and-place file (also called a centroid, CPL or position file) into the open job, as your PCB tool
writes it, as CSV: EasyEDA and JLCPCB's, KiCad's, and others. It is reviewed on the Import panel and
reaches the job when you accept it (see [Reading a board into the job](jobs.md#reading-a-board-into-the-job)). jplacer finds the columns by their headings
(designator, X and Y, side, rotation, footprint, value, and what else the file says about each part; see
[Reading a board into the job](jobs.md#reading-a-board-into-the-job)), whatever the tool calls them, and
reads positions in mm, mil or inches, with the unit in the number or in the heading. Where a tool gives
several positions for a part, the part's centre is used. Fiducials are told by their designator (FID…) or
footprint (…FIDUCIAL…). Nothing is added to the parts library.

The panel then names the board and counts the parts and fiducials on the side that is up. The board is
kept in the job; its side and where it was found are kept too, and are there again the next time jplacer
opens. Opening another job, or reading another board in, starts its position again.

<!-- src: src/import/JPCsvTable.cpp (classify, length, sideOf); src/app/JPlacerImport.cpp (choosePlacements, accept); src/app/JPlacerBoard.cpp (newBoard, save) -->

## Which side is up

Choose **Top** or **Bottom**. A board bottom side up is seen mirrored, as if turned over about its Y
axis; jplacer works that out from the file, so the positions are the file's as they are. Just after
importing, the side with the fiducials is chosen for you if only one side has them. Changing the side
starts the board's position again.

<!-- src: src/job/JPBoardSide.h; src/app/JPlacerBoard.cpp (newBoard, setSide) -->

## Finding the board

1. With the machine homed, jog the camera on the head roughly over one of the board's fiducials (within
   a few millimetres).
2. Choose that fiducial from the list, and press **Camera Is on It**. That is the starting point: the
   board unturned (or turned as it was last found), with that fiducial where the camera is looking.
3. Press **Locate Board**. The camera visits every fiducial on the side that is up: first the one nearest
   it, then the one furthest from that, then each nearest the last. It looks widely (10 mm) for the
   first two (until two are found, how far the board is turned is only a guess), then closely (2 mm) for
   the rest, centres on each one and measures it in the middle of the
   picture, where the lens bends nothing. Each find makes the board's position better for the next.

How each fiducial is measured is set on the Machine's **Fiducials** tab in
[Machine Setup](machine-setup.md#settings). Each **vision pass** finds the fiducial and moves the camera
over where it was found; the passes stop once one moves the camera less than **Centred To** (0.01 mm to
begin with), or after **Vision Passes** of them (4 to begin with). With a **Parallax Diameter**, each pass
looks at the fiducial from two places that far apart, either side of it along the **Parallax Angle** (0
along X, 90 along Y), the nearer first, and takes the midpoint of the two. A shiny (HASL) fiducial seen
straight on can mirror the camera peeking through its light and look dark or misshapen; seen from the
side it mirrors the bright light, and what looking from one side puts out, looking from the other takes
back. 0 (as to begin with) looks straight down. An OpenPnP import brings these across from the fiducial
locator's vision settings (`vision-settings.xml` beside `machine.xml`), Max. Linear Offset becoming
Centred To.

With two fiducials found, the board is moved and turned to fit them; with three or more it is fitted
fully, which also takes up a machine whose axes are not quite square or not quite to scale. The list
shows each fiducial: found, and how far it sits from the fit, or why it was not found. The panel then
says where the board is (its origin and how far it is turned) and that it was found by its fiducials.

A fit is refused when the fiducials disagree with each other by more than 0.1 mm, or when they would
stretch the board or skew it by more than 1%: one of them was then something else.

Fiducials are found as bright copper on darker solder mask, the size their footprint's name gives (the
first size in it: `FIDUCIAL_1MM` is 1 mm, `Fiducial_0.75mm_Mask1.5mm` is 0.75 mm), or 1 mm where it gives
none. The camera must be calibrated (see [Calibrating the head camera](machine.md#calibrating-the-head-camera)).

<!-- src: src/tasks/JPBoardLocator.cpp (run, kMaxStretch, Options); src/machine/JPFiducialConfig.h; src/openpnp/JPOpenPnpMachineImporter.cpp (fiducial-locator); src/import/JPCplImporter.cpp (sizeInName); src/app/JPlacerBoard.cpp (cameraOn, locate); src/app/JPlacerCameraTasks.cpp (locateBoard) -->

## Squaring the machine

A gantry's Y axis is rarely exactly square to its X: moving along Y carries the head a little along X
too. A board is made far squarer than that, so when **Locate Board** finds three or more fiducials, the
line beside **Square the Machine…** says how far the machine's axes lean: so many millimetres of X per
100 mm of Y, and the angle out of square. One finding gives it to some tens of percent; each Locate Board
after that adds a finding, and the line gives their mean and range. **Square the Machine…** corrects by
the mean from then on: jplacer's coordinates
become square, and each move tells the axes what that means for them (a move along Y moves X a
little too). The correction is kept in the cell file, and is made about the Y of the head's homing mark,
so the homing mark's coordinates do not change.

Every other coordinate changes a little, so afterwards home the machine again, calibrate the camera
again and locate the board again. The board should then measure square to within a few hundredths of
a millimetre per 100 mm; a measurement this small is the fiducials' own scatter, and squaring by it again
changes nothing worth having. The correction adds to the one already made, so squaring twice from the
same board is the same as squaring once.

<!-- src: src/tasks/JPBoardLocator.cpp (xPerY); src/app/JPlacerBoard.cpp (square, show); src/app/JPlacerMachine.cpp (squareMachine); src/machine/JPSquarenessConfig.h; src/machine/JPCell.cpp (toAxes, updatePositions, setSquareness) -->

## Looking at a placement

Once the board has a place (a starting point or found by its fiducials), the live picture of a
calibrated head camera shows the board over itself: a small circle and the designator at each part on
the side that is up, and a ring the size of a fiducial at each fiducial. A board in its right place has
each mark on its part, wherever the camera looks.

Double-click a placement in the list (the parts on the side that is up), or a fiducial in the list of
the last finding (one that was not found, to see why), and the camera on the head goes to look at it,
wherever the board is, its tab brought to the front. The status bar names it, its footprint and value, and where it is.

<!-- src: src/app/JPlacerBoard.cpp (goTo, marks); src/app/JPlacerCameraTasks.cpp (lookAt, cameraLook); src/ui/JPCameraView.cpp (marks); src/machine/JPCameraCalibration.cpp (pixelFor) -->
