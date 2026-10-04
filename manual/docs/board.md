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
axis; jplacer works that out from the file, so the positions are the file's as they are. For a new board,
the side with references is chosen for you if only one side has them. Changing the side starts the
board's position again.

<!-- src: src/job/JPBoardSide.h; src/app/JPlacerBoard.cpp (newBoard, setSide) -->

## References

A board is located by its **references**: placements you mark to locate it by. A fiducial is one, but
any placement the camera can see precisely serves as well, so a board with no fiducials (or none on the
side being placed) can still be located. When a board is read in, its fiducials start as references.
Mark others, or unmark them, on the [Parts](jobs.md#the-parts-panel) panel: choose placements and press
**Use as Reference** or **Don't Use as Reference** on its Placement page; the **Reference** column says
which are.

Each side has its own references. Two fix where the board is and how it is turned; three or more are
better, since then the fit checks itself (one wrong reference shows) and the machine's squareness is
measured. The panel says so when a side has fewer than three.

The **References on this side** list shows each one and how it is found:

- **round mark**: a fiducial, found as a round mark (below);
- **found by its pads**: a part whose package has a footprint, found by its pads, drawn from the
  footprint at the placement's rotation as the camera sees them, and matched in the picture: their size
  and shape come from the package, so there is nothing to tune. A part whose pads look the same turned
  half round (an 0603) serves as well as any, since only its centre is used;
- **found by its look**: a part with no footprint, found by the picture taken round it when it was
  recorded by hand;
- **not findable by camera**: record it by hand first.

It adds what was last captured of each: recorded where, found and how far from the fit, or not found and
why.

<!-- src: src/app/JPlacerBoard.cpp (show, footprints, record); src/tasks/JPBoardLocator.cpp (run, padPattern, lookOf); src/vision/JPPatternFinder.cpp; src/import/JPBoardBuilder.cpp (reference = fiducial); src/app/JPlacerParts.cpp (reference) -->

## Locating the board

**References** chooses how they are captured, for this job:

- **Found by Camera**: **Locate Board** sends the camera to each reference and vision finds it there.
- **Recorded by Hand**: for each reference, choose it, press **Go To** (the camera goes where the board as
  last known puts it), jog the camera onto it, and press **Record**. The next reference not yet recorded
  is then chosen and the camera goes to it. **Locate Board** fits the board to the recorded positions;
  nothing moves.

With **Recorded by Hand**, **A new board** says what **New Board on the Bed** does when you put the next
board down: **Record Again** clears the recorded positions, to be recorded again (each a small jog from
where the last board's were), the usual case; **Reuse Them** keeps them, for a fixture that puts every
board in the same place. Either way the board's position becomes a guess until it is located again.

<!-- src: src/ui/JPBoardPanel.cpp; src/app/JPlacerBoard.cpp (record, newBoardOnBed, locate); src/job/JPLocateSettings.h -->

### The starting point

Before the camera can look for references it needs to know roughly where the board is. How it gets that
is the machine's **Starting Point**, on the Machine's **Board Location** tab in
[Machine Setup](machine-setup.md#settings), since it depends on how boards are held:

- **By hand**: jog the camera roughly over one reference (within a few millimetres), choose it, and press
  **Camera Is on It**: the board unturned (or turned as it was last found), with that reference where the
  camera is looking.
- **Fixture anchor**: boards are mounted at one place (a clamp, a corner stop), each the same way round,
  one of its corners at the **Anchor X**, **Anchor Y**. For each side up, say which corner sits there
  (of the board's outline, or with none its placements' extent) and how far the board is turned on the
  machine (0, 90, 180 or 270): the board need not lie as the CAD draws it. **Locate Board** starts from
  there; a board a millimetre or two off is within the search.
- **Search a region**: **Locate Board** first scans a region picture by picture, rows from its first
  corner, for the round-mark reference nearest the board's origin, and takes the first round mark of its
  size it sees to be it. The region is **From X**, **From Y** to **To X**, **To Y**; all 0, the head's whole
  travel within its axes' soft limits.

Once the board has a position (from any of these, or found before), **Locate Board** starts from it.

<!-- src: src/machine/JPBoardStartConfig.h; src/app/JPlacerBoard.cpp (cameraOn, anchorGuess, locate); src/tasks/JPBoardLocator.cpp (searchStart); src/setup/JPSetupProperties.cpp (Board Location) -->

### Finding each reference

The camera visits the references on the side that is up: first the one nearest it, then the one
furthest from that, then each nearest the last. It looks widely (10 mm) for the first two (until two are
found, how far the board is turned is only a guess), then closely (2 mm) for the rest, centres on each
one and measures it in the middle of the picture, where the lens bends nothing. Each find makes the
board's position better for the next.

How each is measured is set on the Machine's **Fiducials** tab in
[Machine Setup](machine-setup.md#settings). Each **vision pass** finds the reference and moves the camera
over where it was found; the passes stop once one moves the camera less than **Centred To** (0.01 mm to
begin with), or after **Vision Passes** of them (4 to begin with). With a **Parallax Diameter**, each pass
looks at a fiducial from two places that far apart, either side of it along the **Parallax Angle** (0
along X, 90 along Y), the nearer first, and takes the midpoint of the two. A shiny (HASL) fiducial seen
straight on can mirror the camera peeking through its light and look dark or misshapen; seen from the
side it mirrors the bright light, and what looking from one side puts out, looking from the other takes
back. 0 (as to begin with) looks straight down. An OpenPnP import brings these across from the fiducial
locator's vision settings (`vision-settings.xml` beside `machine.xml`), Max. Linear Offset becoming
Centred To.

Fiducials are found as bright copper on darker solder mask, the size their footprint's name gives (the
first size in it: `FIDUCIAL_1MM` is 1 mm, `Fiducial_0.75mm_Mask1.5mm` is 0.75 mm), or 1 mm where it gives
none. A part's pads or look are found by how closely the picture matches them, and refused below 0.6 of a
perfect match. A look recorded at another camera scale is not used: record it again. The camera must be
calibrated (see [Calibrating the head camera](machine.md#calibrating-the-head-camera)).

<!-- src: src/tasks/JPBoardLocator.cpp (run, kLookScaleShare); src/vision/JPPatternFinder.h (minScore); src/machine/JPFiducialConfig.h; src/openpnp/JPOpenPnpMachineImporter.cpp (fiducial-locator); src/import/JPCplImporter.cpp (sizeInName); src/app/JPlacerCameraTasks.cpp (locateBoard) -->

### The fit

With two references found (or recorded), the board is moved and turned to fit them; with three or more it
is fitted fully, which also takes up a machine whose axes are not quite square or not quite to scale. The
panel then says where the board is (its origin and how far it is turned) and that it was located by its
references; with only two, the status bar says a third would check the fit.

A fit is refused when the references disagree with each other by more than 0.1 mm, or when they would
stretch the board or skew it by more than 1%: one of them was then something else.

A board's position stops being trusted when the machine changes under it: homing again, calibrating a
camera again or squaring the machine marks it **Needs locating again**, with the reason; it is kept as
the starting point for locating it again.

<!-- src: src/tasks/JPBoardLocator.cpp (fitted, kMaxStretch, fitRecorded); src/app/JPlacerBoard.cpp (found, machineChanged, show); src/app/JPlacerMachine.cpp (onHomed, onCalibration) -->

## Squaring the machine

A gantry's Y axis is rarely exactly square to its X: moving along Y carries the head a little along X
too. A board is made far squarer than that, so when **Locate Board** finds three or more references, the
line beside **Square the Machine…** says how far the machine's axes lean: so many millimetres of X per
100 mm of Y, and the angle out of square. One finding gives it to some tens of percent; each Locate Board
after that adds a finding, and the line gives their mean and range. **Square the Machine…** corrects by
the mean from then on: jplacer's coordinates
become square, and each move tells the axes what that means for them (a move along Y moves X a
little too). The correction is kept in the cell file, and is made about the Y of the head's homing mark,
so the homing mark's coordinates do not change.

Every other coordinate changes a little, so afterwards home the machine again, calibrate the camera
again and locate the board again. The board should then measure square to within a few hundredths of
a millimetre per 100 mm; a measurement this small is the references' own scatter, and squaring by it again
changes nothing worth having. The correction adds to the one already made, so squaring twice from the
same board is the same as squaring once.

<!-- src: src/tasks/JPBoardLocator.cpp (xPerY); src/app/JPlacerBoard.cpp (square, show); src/app/JPlacerMachine.cpp (squareMachine); src/machine/JPSquarenessConfig.h; src/machine/JPCell.cpp (toAxes, updatePositions, setSquareness) -->

## Looking at a placement

Once the board has a place (a starting point, or located), the live picture of a
calibrated head camera shows the board over itself: a small circle and the designator at each part on
the side that is up, and a ring the size of a fiducial at each fiducial. A board in its right place has
each mark on its part, wherever the camera looks.

Double-click a placement in the list (the parts on the side that is up), or a reference in the
references list (one that was not found, to see why), and the camera on the head goes to look at it,
wherever the board is, its tab brought to the front. The status bar names it, its footprint and value, and where it is.

<!-- src: src/app/JPlacerBoard.cpp (goTo, marks); src/app/JPlacerCameraTasks.cpp (lookAt, cameraLook); src/ui/JPCameraView.cpp (marks); src/machine/JPCameraCalibration.cpp (pixelFor) -->
