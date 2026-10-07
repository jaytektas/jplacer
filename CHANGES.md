# Changes

What changed in each version, in plain words for the people using jplacer: no commit hashes, no file
names. The manual's What's New page (Help > What's New) is generated from this file, and each GitHub
release carries its version's section as its notes.

Every change a user would notice adds a line under Unreleased, in the same commit as the change.
`packaging/build-release.sh` turns Unreleased into the new version's section; a beta carries it as its
notes.

## Unreleased

- A nozzle tip's calibration is one step to undo, and the machine takes it once (its runout and its background
  were two, each remaking Machine Setup and the panels); putting a tip on likewise. Homing forgets other tips'
  runout in one step too.

- Expose each picture tries up to six exposures, not four: a camera opened after a restart, starting far from its
  exposure, ran out of tries a level short of the brightness wanted (and warned). Each try is in the log at Debug.

## 0.1.17

- jplacer no longer crashes when the nozzle tip menu is open while the Jog panel is made again (after a
  calibration's result, say) and an entry with a submenu, such as Manual Change, is pointed at: the menu now
  closes instead.
- A panel's menus open beside it when the panel is floating (the nozzle tip menu of a floating Jog opened
  near the main window's corner); the Console's and the job tabs' tables' menus too.
- A button's tip goes away when the button is pressed (it stood over the menu the button opened) and stays away
  until the pointer leaves the button; in a narrow floating window a tip wraps to fit instead of being cut off.

## 0.1.16

- Calibrating a nozzle's precise offsets logs each angle's estimate and says in its result how closely they agree,
  so an inconsistent run shows as one.
- The bottom camera's calibration steps no longer put a box over the camera: what to do is on the camera's line,
  Next is a green start button beside Calibrate (the red X cancels), and the Detection Diameter is a field under
  the line, saying beside it whether the tip is found.
- A floating camera window's right-click menu now really opens and stays open (0.1.14 fixed only half of it: the
  menu still closed the moment it opened); drop-downs in a floating window too.
- A nozzle tip's Tool Changer tab is a table of steps, as many as the change takes (not OpenPnP's four places):
  each row a move (or safe Z, an actuator, a wait, a question) with its own add, delete, up and down buttons;
  unloading can have its own table, to go round the holders (to the middle of the machine first, say).
- Whole-number settings are sized to their range (a percentage's field is narrower).
- Nozzle tip loading and unloading no longer make backlash compensation's extra moves: a one-sided axis went its
  Backlash Offset past each changer place and back, which could drive the tip into the slot's wall.

## 0.1.15

- The Jog panel's nozzle tip button shows whether the tip on the chosen nozzle is calibrated: green when it is
  (or its calibration is off), red when its calibration is on and it is not; its tooltip says which and when.
- A failed visual homing fails the homing, as OpenPnP's: the machine is not homed and Home turns red (it showed
  green, homed by the switches alone). Home stays busy until the whole homing has finished.
- Parking the head (X and Y) switches every camera's light off, until you next do something at a camera.
- A camera's Device Settings keep their Min, Max, Default and sliders after Defaults, then Auto-Tune (or any
  camera setting changed) while the camera is not on screen; they went blank until the camera next ran.

## 0.1.14

- Auto-Tune when calibrating? now applies to every calibration that looks through the camera: backlash, the
  precise nozzle offsets, Feature Diameter, Auto-Detect Next and its measuring, capturing a fiducial and Auto
  Focus's Test, as well as the camera's and the nozzle tip's own (it tunes once where it looks, not on every
  click of Auto-Detect Next).
- A camera's light comes on when you move a tool to it (a jog, Position Tool, Move Selected Nozzle to Camera, a
  click in its picture), as OpenPnP's User Camera Action does, even when the camera is not on screen or its
  Auto Camera View is off; it stays on until you switch it off.
- A floating camera window's right-click menu now opens and stays open (in 0.1.13 it closed at once, leaving a
  resize pointer); menus and drop-downs opened while a floating window has the focus stay open too.
- Dragging (or double-clicking) in the bottom camera's picture moves the nozzle so that point comes to the
  middle, as OpenPnP's, also while its calibration waits for the tip to be jogged into the circle.

## 0.1.13

- Preferences > Debugging > Save vision pictures for debugging: while ticked, every vision pipeline run keeps each
  stage's picture in a folder of its own (log/vision, beside the settings) with what each stage found, as OpenPnP
  does at its Debug log level; ImageWriteDebug stages write too (they never did).
- The bottom camera's Calibrate goes step by step as OpenPnP's: load the smallest tip; jog it into the green
  circle; at the calibration height turn it 360 degrees to see it stays in; set the Detection Diameter until the
  red circle turns green with a + on the tip (it starts at the tip's size); then the moves run by themselves.
  At the second height, the same again. The line over the picture says why the tip is not found at that size.
- Closing jplacer during a camera calibration cancels it (it stops before its next move) and quits, rather than
  waiting for the whole calibration to finish first.
- A camera in a floating window has its right-click menu again (reticles, zoom, and the rest), as when docked.
- The bottom camera's calibration looks for the nozzle tip at its own size (its Vision Diameter, else its
  Diameter), and only about that size, so the nozzle's base round it is no longer taken for the tip; the second
  height starts from the first's scale. A tip measuring the wrong size stops the calibration and says so.
- Nozzle tip calibration (and Calibrate Camera Position and Rotation) shows each found tip on the camera, a
  green circle and cross, as the camera's own calibration does.

## 0.1.12

- A floating camera window stays where you put it: it no longer jumps back into the dock when a camera setting
  changes (a light switched from it, a calibration recorded).
- jplacer no longer quits (crashes) at the end of the bottom camera's calibration: when calibrating moved the
  camera's location, the camera panels were made again while the calibration was still finishing.
- Cancel during a camera task is a red X button beside Calibrate (live only while the task runs), in place of
  the box of instructions, which said the step twice.
- Calibration steps say which pass as well as which move: "pass 1 of 2, measuring, move 14 of 38".

## 0.1.11

- A nozzle tip's Calibration tab is OpenPnP's: no camera button on it (the bottom camera's Calibrate does that
  job), OpenPnP's colour wheel and value bar of the background colours found, with its findings beside them, and
  settings greyed when they do nothing (calibration off, or the background method not using them).
- A camera with no picture shows OpenPnP's picture for it: dark grey with a red X in the corner. An uncalibrated
  live camera gets the same X in its corner, and not while it is being calibrated (it flashed on and off).
- Camera calibration (and every camera task) shows its steps with a Cancel button, as OpenPnP's; Cancel stops it
  before the next move and lifts the nozzle. The bottom camera's Calibrate no longer asks first.
- Greyed-out fields and drop-downs now look greyed.

## 0.1.10

- The bottom camera's Calibrate does the whole job as one: the camera's scale and lens with the tip over it, then
  the tip's runout, then the camera's true position and rotation (about the nozzle's axis, not the tip's end),
  asking only once. With the tip's calibration not enabled it stops after the first and says so.
- Calibrations show when they were done and how long ago: a nozzle's offsets (new: Offsets Calibrated on its
  page and in its precise-offsets issue), a tip's runout (its Status), a camera's calibration and an axis's backlash.
- Cameras can Auto-Tune when calibrating (their own calibration and a nozzle tip's, with the mark or tip over them:
  the time a bottom camera has something to tune on) and for each part in a job: the first part of each kind is
  tuned on and its settings kept for the rest of that kind for the run, without tuning again
  (Device Settings: Auto-Tune when calibrating?, Auto-Tune for each part?).
- Changing a camera's settings no longer brings the other camera forward (the camera panels, made again, kept the one
  you were on in front); before, ticking Auto on the bottom camera put the top camera in front and turned its light on.

## 0.1.9

- A camera view with no picture is crossed out in red, as in OpenPnP; a live camera not calibrated for its picture
  size is crossed out too, saying so (camera setting: Warn if camera calibration is not completed, on by default).
- Camera calibration shows each find on the camera's view as it goes: a green circle where the mark (or the nozzle
  tip, for the bottom camera) was found, and which move of how many.
- Moving a nozzle by hand (Position Tool, Move Selected Nozzle to Camera, a jog) brings forward the camera looking
  at where it goes (the nearest within 50 mm) when it has Auto Camera View, as in OpenPnP: over the bottom camera,
  its view.

## 0.1.8

- The strip across the top of the window now also shows FAILED when a camera task (a calibration, the precise
  nozzle offsets) fails, with why, until the next task starts; and WAITING while the pump comes up to pressure.
- Switching the vacuum pump on now says in the log and Console how long it waits for it to come up to pressure (the
  head's Pump On Wait) before the valve opens; a long first wait no longer looks like a stall.
- Issues & Solutions: Auto-Detect Next (or anything that changes an issue's values) no longer rebuilds the issue's
  page: its values are read again where they are, and the page stays scrolled where you left it.
- A controller's serial Port is a list of the ports there now, as in OpenPnP (each by its stable name; the one set
  stays in the list though it is unplugged; another can still be typed).
- Camera calibration says how many moves in all ("move 12 of 38"), and precise nozzle offsets how many angles
  ("2 of 6").
- Updating disconnects the machine before the new version starts, so the new version can connect to it (the old
  one still held the controller's port).
- The Log tab is readable on the dark theme: information in the normal text colour (it was OpenPnP's blue, made
  for a white background), warnings and errors in the theme's warning and danger colours.

## 0.1.7

- Issues & Solutions: an issue whose solution runs on the machine (a calibration, the nozzle offsets) is marked
  Solved only once that work has succeeded, as in OpenPnP. While it runs, and if it fails, it stays open, so
  Accept tries it again; before, it showed Solved at once and then fell back to open with its buttons wrong.
- Each time jplacer starts it copies its settings and every machine into backups/<date and time>/ beside them,
  a rolling set of the last 20 (Preferences > Backups kept; 0 for none).
- Saving the settings never takes anything out of the file: every setting already there is kept, and only what
  this run changed is written. A settings file that cannot be read is left alone instead of being set aside.
- A calibrated camera is shown straightened to begin with, as in OpenPnP (it was shown as taken, bent by the
  lens, until the eye button was turned off; settings lost in an update put it back to that).
- A camera that does not settle within its Settle Timeout says why: the least difference it saw against its
  threshold, and when the picture's own noise is above the threshold (so it can never settle), that the threshold
  needs raising: Denoise (Pixel) first. OpenPnP only notes a time-out in its debug log.

## 0.1.6

- Calibrate precise nozzle offsets starts where you sized the test object (Feature diameter or Auto-Detect Next):
  the camera goes back there to measure it and look for it. It looked on the primary fiducial before, where the
  object need not be.
- The controller traffic is written to the log file too, and every Console line starts with the time it came
  (to the millisecond), so a step that takes long shows.
- A controller's Log G-code? writes the G-code sent to a file of its own (GcodeDriver/<name>-<time>.g beside the
  settings), as in OpenPnP.

## 0.1.5

- Fixed: an update could lose jplacer's settings and replace your machine with OpenPnP's default machine. The
  new version started while the old one was still writing the settings, found them empty, and took the bench
  for a new install. Settings are now written whole and kept before the new version starts; jplacer never
  writes the default machine over one you have (it opens the machine changed last); and a settings file that
  cannot be read is put aside as jplacer.json.unreadable instead of being replaced.

## 0.1.4

- A nozzle tip's Calibrate runs at once, as in OpenPnP; it no longer asks first.
- The nozzle tip Calibration tab is laid out as OpenPnP's: Enable? with Position Tool, the Calibrate, Reset and
  Calibrate Camera Position and Rotation buttons, Auto Recalibration, Fail Homing?, then Nozzle Tip Calibration
  with its Status line.
- The Jog panel's nozzle tip menu has Calibrate.
- Putting a tip on by hand (Manual Change) now follows its Auto Recalibration: NozzleTipChange calibrates it
  every time, MachineHome when it is not yet calibrated on that nozzle.
- Controller traffic in the Console starts with [GCODE] and the controller's name, like the log's lines.

## 0.1.3

- Each log line in the Console starts with its level and category, as in the log file: [INFO][machine.cell] ...
- The Console's G-code, Log and Categories choices now apply to the lines already shown, not only to new ones:
  choose Errors and only the errors stay.
- Machine Setup's Search also finds a part by the settings on its page: "motion" finds the controller (Motion
  Control Type) and the machine (its Motion Planner tab), and choosing it opens that tab.

## 0.1.2

- jplacer opens as it was last closed: the window's place and size, every panel where you left it (docked,
  tabbed or in a window of its own), the front tabs, the splits' sizes, and closed panels still closed.
- The Console's lines are one text you can copy from: drag over them (or Ctrl+A), then Ctrl+C, or right-click for
  Copy, Select All and Clear.
- The Jog panel's nozzle tip menu has Manual Change > Move to Manual Change Location: the nozzle goes up to safe Z,
  across, and down to where its tip is changed by hand.
- Every issue in Issues & Solutions now says plainly what it is about and what Accept does, instead of OpenPnP's
  wording (which names its own classes and settings): for example "Controller N is simulated: make it the real
  controller", "Measure the play (backlash) in axis X". What you had solved or dismissed stays so.
- The coarse nozzle offset issues say what they do: "Set nozzle LEFT approximate offsets and capture the primary
  fiducial height", "Set nozzle RIGHT approximate offsets and match its Z to nozzle LEFT", and "Set the secondary
  fiducial height" (which changes no offsets), each explained plainly. What was accepted under OpenPnP's names stays
  accepted.
- Issues & Solutions' bar is tidier: the Milestone box is as wide as its longest milestone, the checkboxes sit
  beside their labels, and the wiki button is at the right.

## 0.1.1

- Issues & Solutions' Milestone is now a box: choose any milestone to go straight to it (back to Calibration
  after a crash, or to where an imported OpenPnP machine already was), not only one step at a time.
- A nozzle whose Z is a mapped axis (as an imported OpenPnP machine's ZL) now gets its Safe Z from the axis it is
  mapped from, so its nozzle offset issues show in Issues & Solutions.
- Nozzle offsets are calibrated in Issues & Solutions, as in OpenPnP: "Nozzle N offsets for the primary fiducial"
  (jog the nozzle tip onto the fiducial and Accept; the first nozzle also sets the fiducials' Z), then "Calibrate
  precise camera <-> nozzle N offsets", with OpenPnP's Feature diameter (each change shows the circle found on the
  camera, which should hug the test object) and Auto-Detect Next; Accept measures the test object, then picks,
  turns and places it, and shows the result. The test object's height can be captured with the nozzle tip touching
  it (Capture Test Object Z), for anything thicker than paper; it is kept on the head's Calibration Rig. Calibrate
  Precise Offsets has left the nozzle's Machine Setup page.
- Changing a nozzle's X/Y offsets (Calibrate Precise Offsets, or typing them) takes along what depends on them, as
  in OpenPnP: the nozzle tips' runout is forgotten (it was measured against the old offsets), the manual tip change
  location moves with it, an actuator fastened to it gets the new offsets, and for the head's first nozzle the
  camera looking up moves with it. Undo takes them back together.
- Calibrate Precise Offsets lets the test object go if it fails while holding it, instead of lifting it on vacuum.
- A nozzle tip's Calibration tab has OpenPnP's Calibrate Camera Position and Rotation: the tip, its runout
  measured, is sent round a circle over the camera looking up, and the camera's position and turn are set from
  where it is seen.
- A nozzle tip's runout is fitted and compensated by OpenPnP's algorithms, chosen on its Calibration tab as
  Compensation Algorithm: Model (the axis's offset compensated too), NoOffset (the swing alone), CameraOffset (the
  swing alone, and the camera looking up taken to be off by the axis's offset for that nozzle, where bottom
  vision puts the nozzle), each fitted as a circle or, the Affine ones, by an affine transform; or Table (the
  measured offsets, interpolated). A new tip uses OpenPnP's default, ModelCameraOffsetAffine; a tip set up before
  keeps how it was compensated (ModelNoOffset). An OpenPnP import brings the algorithm and the runout OpenPnP
  measured on each nozzle.
- An OpenPnP machine runs in Simulation Mode as it does in OpenPnP: a G-code controller brought from OpenPnP is
  simulated by OpenPnP's GcodeServer, which takes the commands that controller is set up with (G28 to home, M114,
  M400, ...). A controller OpenPnP simulates (Communications "simulated", or TCP to "GcodeServer") is simulated so
  too. Before, such a machine could not home in Simulation Mode. Checked against OpenPnP's SampleJobTest and
  SamplePanelizedJobTest: its imperfect simulated machine homes (visually), finds every board's fiducials, and
  places all of OpenPnP's sample job, each pick and place checked against the table's picture.
- On a controller whose firmware reports no position (generic G-code), the axes are taken to be where they were
  sent, as in OpenPnP; before, they seemed never to move.
- Importing from OpenPnP: a linear transform axis (such as a non-squareness correction) now brings its inputs, as
  OpenPnP writes them; it was read wrongly, and a move through it failed. An actuator naming a controller the
  machine does not have uses the first controller, as in OpenPnP.
- Visual homing finds the homing mark up to the Fiducial Locator's Max. Distance from where it should be (4 mm to
  begin with), as in OpenPnP; it gave up beyond 2 mm.
- In Simulation Mode, the camera looking up sees a nozzle where its axes put it, as in OpenPnP; after visual
  homing it saw the nozzle off by the homing correction, so nozzle tip calibration failed.
- A job's pre-rotation (Pre-Rotate All Nozzles) is no longer a move of its own: as in OpenPnP, each nozzle's turn
  goes with the next move made with the head at safe Z, so the nozzles turn while the head travels. Checked against
  OpenPnP's BasicJobTest: every move and switching of its two-nozzle job, in order.
- An OpenPnP machine imported with no park location set parks at the origin, as in OpenPnP (it could not park).
  A nozzle from an older OpenPnP naming only one vacuum actuator uses it for both vacuum and sensing, as OpenPnP
  does, so it picks (it said it had no vacuum actuator).
- A simulated controller made from OpenPnP's NullDriver names its axes as a grblHAL with that many axes does
  (two nozzles: Z, A, B, C), so where each axis is reads back right; a second nozzle's Z was U, which was never
  read back, so it was not raised after a pick. The grblHAL profile reads U and V axes too.
- A new feeder's Pick Retry Count starts at 0, as a new OpenPnP feeder's does (it was 3). Checked against OpenPnP's
  job retry tests (feed and pick retries, an empty feeder failed over, faults counted with Defer), all of which pass.
- The job planner gives a second nozzle its placement as OpenPnP's does: by the time the head takes to get there
  (from the axes' speed and acceleration), to where the head goes for that nozzle (it was the straight distance to
  where the part goes). Checked against OpenPnP's JobProcessorTest: the same tip changes, cycles, planning cost
  and part changes for every job order, strategy and ranking it tries.
- A script can run an event's scripts or another script, as in OpenPnP: `scripting.on(event, globals)` and
  `scripting.execute(script, globals)`.
- Bottom vision on a camera looking up (which sees the machine mirrored) now tells its pipeline the part's angle,
  footprint and, for vision compositing, the edges each shot looks for as the picture shows them. A composite shot
  looked for the wrong side of a pad, so a part seen in several shots came out turned by up to a few tenths of a
  degree. Checked against OpenPnP's VisionCompositingTest on its simulated camera: all its parts pass.
- A pipeline setting that is a whole number, given a length or a number by OpenPnP, is rounded as OpenPnP rounds it
  (it was cut down: 12.7 pixels became 12, now 13).
- Translated texts read as OpenPnP reads them: a "\r" in a tooltip no longer shows as a stray "r", and a text
  ending in a space keeps it.
- The PhotonFeederData actuator jplacer makes is read through the first G-code controller (as OpenPnP: a simulated
  one has none), its reply pattern OpenPnP's `rs485-reply: (?<Value>.*)`. Checked against OpenPnP's Photon feeder
  tests, all of which now pass.
- An actuator's Read Reply Pattern takes OpenPnP's `(?<Value>...)` group as OpenPnP does: a pattern typed so was
  refused, and one brought in from OpenPnP with groups before the Value group read the wrong one. OpenPnP's
  patterns are now brought in as they are.
- Bottom vision works out a part's offsets as OpenPnP's does, step for step: the Vision Center Offsets are now
  taken off (they were kept but not used), the Part size check is made, offsets beyond the nozzle tip's Max. Pick
  Tolerance stop the placement, and a part not pre-rotated is looked at with the nozzle at 0° (it was looked at as
  picked). Checked against OpenPnP's own bottom vision tests on its simulated camera.
- Reset to Default on vision settings gives them the machine's default settings, as OpenPnP's (it gave the stock
  ones); the machine's default itself is reset to the stock settings.
- An image camera's offset, rotation, scale, distortion and Y rotation, its fiducials, a simulated camera's focal
  length and frame rate, and a switcher camera's actuator value kept only their whole numbers when read back from
  the cell (12.5 became 12). They keep their fractions now.
- A pipeline length given in mm becomes pixels at the mean of the camera's mm per pixel, as OpenPnP works it out
  (it was the mean of its pixels per mm, a hair different on a camera whose X and Y scales differ).
- Routes through feeders and placements are found by OpenPnP's own travelling salesman (simulated annealing, seeded
  as OpenPnP's, each hop timed by the camera axes' speed and acceleration), so a job takes the route OpenPnP would.
- The Motion Planner's Allow uncoordinated?, as OpenPnP's: with continuous motion, a move by way of safe Z is planned
  as one sequence and blended, the head moving on while the nozzle is still rising or already falling (for a
  controller with 3rd order motion control, simulated or true).
- OpenPnP's motion control: each controller's Motion Control Type (ToolpathFeedRate, EuclideanAxisLimits,
  ConstantAcceleration, ModeratedConstantAcceleration, SimpleSCurve, Simulated3rdOrderControl, Full3rdOrderControl),
  with OpenPnP's motion planning behind it, the interpolation settings (a GcodeAsyncDriver's Advanced Settings) and
  Interpolation Retiming; brought in from OpenPnP. Existing cells keep how their moves were sent (EuclideanAxisLimits).
- A backlash overshoot stops on a whole step of the axis, so the approach after it is never lost.
- Issues & Solutions starts vision as OpenPnP's does: Primary calibration fiducial position and initial camera
  calibration (jog the camera over the fiducial, Accept). A new machine's camera can now be calibrated before
  any homing mark is set; it works out its first scale itself.
- The precise nozzle offsets calibration picks its test object at as many angles as OpenPnP's machine was set to
  (its nozzle-offset-angles, brought in by an import; six otherwise).
- Calibrating a fixed camera (the one looking up) moves its location to where it was measured to be, as OpenPnP
  applies its calibration; calibrate the nozzle offsets first, as the first nozzle's tip is what it is measured by.
- Machine Setup's tree highlights what is shown when it is brought up from elsewhere (a camera's settings button, say)
  even when its name holds a "/" (a nozzle tip named "0805 / 0603").
- Save the picture on a camera that is not running starts it and saves a fresh, lit picture, not the last one from
  when it stopped (which could be dark).
- Camera calibration measures its mark with jplacer's own round mark finder again, now as a pipeline stage of its
  own (DetectRoundMark) in the calibration pipeline, still editable; Reset Pipeline puts it in place of an imported
  OpenPnP one.
- A camera's Device Settings have Expose each picture? with a Brightness: every picture taken for vision is taken
  with the exposure set first for that brightness under the light there is then, for a cell whose light changes.
- Defaults, then Auto-Tune switches the camera's light on first (and other cameras' Anti-Glare lights off), so the
  camera is tuned for the pictures vision takes; with the machine off it says to connect first, rather than tuning
  a dark camera.
- Auto-Tune waits for the camera's own automatic exposure to settle (up to 6 seconds) before taking it as the aim, so a
  camera starting far off (very bright or very dark) is no longer tuned to a picture it was still passing through; it
  looks at each value it tries only once the camera shows it, and checks the values it finds give the picture the
  camera gave by itself (not tuned, and homing stops, when they do not).
- Auto-Tune sets the camera as OpenPnP's Issues & Solutions recommends for vision: sharpness at its least (the
  camera's own sharpening upset the measuring of marks), white balance at its default (jplacer's White Balance does
  the colour), the rest at their defaults; only exposure is tuned. Issues & Solutions and Auto-Tune now agree.
- Camera calibration again measures a mark towards the picture's corners when only half its edge looks round, as it
  did before it used the calibration pipeline.
- Visual homing looks again as OpenPnP's does: up to the FIDUCIAL-HOME part's Max Vision Passes, until a look corrects
  by less than its Max Linear Offset, the last look's correction standing; it no longer fails a home that is a few
  hundredths of a millimetre off after three looks.
- A camera's Device Settings have Auto-Tune when homing?: each visual homing first tunes the camera over the head's
  primary fiducial with its light on, then finds the homing fiducial, then goes on as before (nozzle tips, park).
- Home after enabled? is the machine's setting alone, as in OpenPnP; the controllers' own Home after connected? is
  gone (a cell that had it ticked keeps it on the machine).
- Visual homing and Visual Test look for the FIDUCIAL-HOME part with its fiducial vision pipeline, as OpenPnP's
  visual homing does.
- The cameras are opened only while the machine is on, and closed and let go of (for other programs) when it is
  turned off.
- A nozzle tip's runout calibration finds the tip with its own OpenPnP pipeline, editable on its Calibration tab
  (Pipeline: Edit, Reset) and brought in from OpenPnP.
- A camera's calibration finds its mark with OpenPnP's Advanced Calibration pipeline (DetectCircularSymmetry, on
  OpenCV), editable from the Advanced Calibration page (Edit Pipeline, Reset Pipeline) and brought in from OpenPnP;
  the mark's centre is still measured to a fraction of a pixel.
- A camera's Image Transforms have OpenPnP's Rotation, Offset X and Y, Flip Vertical? and Flip Horizontal?, and Scale
  Width and Height, besides the crop and De-Interlace?, brought in from OpenPnP too.
- Defaults, then Auto-Tune finds exposure and white balance from the picture when a camera does not say what its
  automatic modes chose (as on the bench's cameras), in about two seconds; it, Reapply to Camera and the Capture
  FPS Test now work while the camera's picture is hidden behind its settings page.
- A nozzle's, camera's or actuator's Coordinate System shows OpenPnP's Safe Z (from its Z axis), under the Z column.
- A capture camera's properties are in OpenPnP's columns (Auto, Min, Value, Max, Default), each value with OpenPnP's
  slider from the camera's least to its most.
- A camera's General Configuration pairs its settings as OpenPnP's (Preview FPS with Suspend during tasks?, Auto
  Camera View? with Show in multi camera view?), its light's OFF ticks are named beside the ON ones, and its Camera
  Settling is laid out in OpenPnP's columns.
- A push-pull feeder's Tape Settings, Vision and Clone Settings, a Bamboo feeder's Vision, a heap feeder's, a drag or
  lever feeder's and a blinds feeder's settings are laid out in OpenPnP's columns.
- A rotated tray feeder's Tray Parameters are laid out in OpenPnP's columns.
- A strip feeder's Tape Settings are laid out in OpenPnP's columns, Auto Setup across them.
- New Axis… asks which of OpenPnP's axis classes, as OpenPnP's does; the class dialogs are worded as OpenPnP's.
- A controller in Machine Setup has OpenPnP's Console tab: its G-code traffic as it happens, and a command line to
  send it a line (Force Upper Case as OpenPnP's).
- The Help menu's What's New is OpenPnP's Change Log, and Check for Updates its Check For Updates….
- A new configuration's vision settings are made in OpenPnP's order: the Default Machine Bottom Vision before
  the Whole Part Body settings.
- Machine Setup lists the feeders, as OpenPnP's: choosing one shows its page there, the same as on the Feeders tab.
- The machine's General settings are in OpenPnP's order, with its Home after enabled? (every controller homing
  the machine once it connects).
- Machine Setup's tree is OpenPnP's: Axes, Signalers, Heads, Nozzle Tips, Cameras, Actuators, Drivers, each part
  named by its class and name (ReferenceHead H1), with OpenPnP's icons. A new nozzle is a ReferenceNozzle or a ContactProbeNozzle, as
  OpenPnP's, and only a ContactProbeNozzle has the Contact Probe tab.
- A camera's White Balance is laid out as OpenPnP's: a slider for each colour's balance and gamma, in percent, with
  its value to type; the Color Balance graph follows them as they move, and the Auto White-Balance buttons say how
  each works.
- Machine Setup's tools are OpenPnP's icons for what is chosen: Delete (asking first), Permutate Up and Down, the
  group's New, and a nozzle tip's Unload and Load (on the Jog panel's nozzle).
- More of Machine Setup says what each setting does, in OpenPnP's words: the GcodeDriver's settings and
  line-endings, an axis's letter, resolution, rotation limits and backlash Calibrate, an actuator's actuation and
  value type, a camera's light switching and Capture FPS, and a nozzle tip's runout calibration; the Jog panel's
  Recycle too, and bottom vision's Rotation and the fiducial locator's Parallax Angle.
- A nozzle tip's Auto Z Calibration is laid out as OpenPnP's, with the nozzle's calibrated Z offset beside it; Fail
  Homing? shows only when the calibration is automatic, and the Tool Changer's settings say what they do.
- The head's Calibration Rig is laid out as OpenPnP's: Primary Fiducial, Secondary Fiducial and Test Object, each
  with its Diameter; the head's buttons and Pump settings say what they do, in OpenPnP's words.
- The Issues & Solutions tab shows OpenPnP's dot, coloured by the severest open issue.
- A capture camera's Device Settings have Defaults, then Auto-Tune: every property to the camera's own default, the
  automatic ones left to settle for a moment, then held and kept as its settings.
- The Motion Planner's Minimum Speed, as OpenPnP's: the Jog panel's speed goes no lower (5% to begin with).
- An axis's soft limit and safe zone buttons say which limit they take or go to, as OpenPnP's.
- A controller's Sync Initial Location and Allow Unhomed Motion, as OpenPnP's: an unhomed machine can be jogged on
  controllers that say where they are, and moved at all where they allow it. A tick box that cannot be changed now
  is shown greyed and stays as it is.
- Machine Setup has OpenPnP's Expand tick box over the tree (every branch opened, or closed). The Boards tab's
  placement menus say what each entry does, as OpenPnP's.
- OpenPnP's keys: Ctrl+Shift+F1 to F5 choose the First to Fifth Jog Increment, Shift makes a jog two steps finer,
  and Ctrl+Shift+R, S and A start, step and stop the job (Save Job As no longer has Ctrl+Shift+S, as OpenPnP's).
  The jog functions have OpenPnP's names.
- Menu entries say what they do, as OpenPnP's: the Job menu, Add Board/Panel, the boards', placements' and panel
  children's Set Side, Set Enabled, Set Check Fids, Set Placed and Set Error Handling, each value's own; the tip
  now shows beside the menu instead of over its entries.
- A SimulatedUpCamera has OpenPnP's Camera Simulation settings on its Device Settings: Camera Location, Pixel
  Dimension, Simulated Units per Pixel, Focal Length and Sensor Diagonal (nozzles and parts higher or lower seen
  smaller and darker), Background Scenario (coloured backgrounds and tips), Pick Error Offsets, View mirrored? and
  Simulate Focal Blur?. It now shows the nozzles even when the machine is not in Simulation Mode.
- An image camera has all of OpenPnP's Camera Simulation settings (Y Rotation, Distortion, Browse for the picture)
  and its Simulated Calibration Rig: two fiducials drawn into the picture, the second at another height, blurred.
- The machine's Pool scripting engines?, as OpenPnP's: Python and JavaScript scripts run by interpreters kept from
  one script to the next, faster to start; Scripts > Clear Scripting Engine Pool ends them. Issues & Solutions
  suggests it at Advanced.
- An actuator can be OpenPnP's ThermistorToLinearSensorActuator: a temperature read turned into what a linear sensor
  would read, set on its Transforms tab. Number boxes are wide enough for all their decimal places.
- A rotated tray feeder's page shows OpenPnP's Tray Illustration of its three points and offsets.
- Adding a controller on Machine Setup asks which kind, as OpenPnP's: NullDriver (simulated), GcodeDriver,
  GcodeAsyncDriver or NeoDen4Driver.
- A NeoDen 4 can be driven, as OpenPnP's NeoDen4Driver drives it: Communications Type neoden4, its scale factors
  and home coordinates, its nozzles' vacuum and blow, lights, rails and feeders by their names; its NeoDen 4 feeder
  actuators (with Change Feeder ID) and its Neoden4Signaler, the buzzer beeping until a job's end is confirmed.
  Its cameras are OpenPnP's Neoden4Camera and Neoden4SwitcherCamera, through the NeoDen's camera library. An
  OpenPnP NeoDen 4 machine comes in with all of these.
- Issues & Solutions' Welcome milestone has OpenPnP's Create nozzles for this head: so many standalone nozzles,
  negated pairs or cam pairs, with their axes and vacuum actuators, made at once.
- The console's G-code box has OpenPnP's Force Upper Case and its history (Up and Down through the last 50 lines).
- A capture camera's properties show the camera's Min, Max and Default, with OpenPnP's Reapply to Camera, and any
  camera's Capture FPS can be measured.
- A controller's Driver Settings have OpenPnP's $-Command Wait Time and Detect Firmware, with what the firmware said.
- A simulated controller can be made a real one: its Communications Type on Machine Setup, or Issues & Solutions'
  Replace with GcodeDriver, as OpenPnP's NullDriver; a picture or simulated camera a real one by Replace with
  OpenPnpCaptureCamera.
- The job viewer's right-click menu has OpenPnP's Placed?, Center Camera on a placement, fiducial, board or panel,
  and Run Fiducial Check; the viewers open as a tab in the work area, wide enough to see.
- A camera's Show in multi camera view?, as OpenPnP's: off, its window starts closed; and its light chosen from its
  head's actuators, the machine's too with Allow Machine Actuators?.
- Runout calibration has OpenPnP's Offset Threshold (a tip found too far off is a misdetect) and Position Tool.
- Camera settling has all of OpenPnP's options: the Motion method, Color Sensitive, Edge Sensitive, Enhance Contrast,
  Denoise and Diagnostics (every settle graphed, its pictures replayed), and a fixed camera's Rotate and Up tests.
  OpenPnP cameras that settle by Motion come in as they are.
- Part detection works as OpenPnP's: Establish Level, Perform Checks (after pick, alignment, before place, after
  place, before pick), a Difference measured from the end of the pick's dwell, and the last readings and a graph of
  the vacuum and valve on the nozzle tip's Part Detection tab. The checks need the nozzle's vacuum sense actuator.
- The Jog panel's Safety tab, with OpenPnP's Board Protection: a jog that would take a nozzle below safe Z into one
  of the job's boards is refused.
- OpenPnP's Vision Calibration of a nozzle tip's changer slot: two template pictures of the slot, empty and
  occupied; before each tip change the slot is found by them, checked empty or occupied as it should be, and the change
  moved by how far off it was. Cloning can take it too, and OpenPnP machines bring it with their template pictures.
- A nozzle tip's Tool Changer tab has OpenPnP's Calibrate all Touch Locations' Z to Template, and the Locations? and
  Z Calibration? choices for cloning; cloning also takes the touch location. Contact Probe Tool on a touch location now
  asks first and probes with the default probing nozzle, as OpenPnP's.
- A controller's Gcode tab has OpenPnP's Export Gcode File and Copy Gcode to Clipboard.
- OpenPnP's other location buttons: Position Tool (Without Safe Z) on a motion test's stops, a tool changer's
  locations and a push-pull feeder's places, and Contact Probe Tool on a tool changer's Touch Location, which
  probes its Z.
- OpenPnP's linear transform axes: an axis can be its inputs (X, Y, Z, rotation) times factors plus an offset, for a
  turned or skewed head; OpenPnP machines with them come in as they are.
- A controller's Keep Alive, as OpenPnP's: Disconnect leaves its connection open, so a board that resets when its port
  opens is not reset by the next Connect.
- A script's pipeline looks with the camera it is given (OpenPnP's `camera` property), a fixed one included.
- Scripts can run OpenPnP vision pipelines as OpenPnP's do (CvPipeline): on the head camera, with each stage's
  results, and the working image shown on the camera.
- Controllers running Smoothieware, Marlin, RepRapFirmware (Duet) or TinyG are recognised and driven as OpenPnP sets them
  up, and Issues & Solutions checks their firmware as OpenPnP does (Smoothieware's PnP build, RepRapFirmware 3.3,
  Marlin's rotation axes, an unknown firmware).
- A nozzle tip's Tool Changer tab has OpenPnP's form: First, Second, Third and Last Location, the speeds between them,
  and the Post 1 to 3 Actuators, over the tip's loading steps.
- OpenPnP's Python and JavaScript scripts run in jplacer as they are: they find OpenPnP's `machine`, `config`,
  `scripting` and `gui` (its Job tab's boards), and its Location, LengthUnit, UiUtils, Utils2D, QR code reading and
  message dialog; JavaScript as OpenPnP's Java JavaScript has it (print, load, JavaImporter, for each). OpenPnP's
  Example scripts are put in the scripts folder's Examples, as OpenPnP does. jplacer's own helper modules moved out of
  the Scripts menu.
- A camera that a task needs takes its pictures whether or not it is in front.
- OpenPnP's sample job runs on OpenPnP's default machine as it does in OpenPnP: a first start puts it in the samples
  folder beside the settings; OpenPnP's stock vision settings are made and kept up to date; a machine from OpenPnP
  finds fiducials and parts with its pipelines; its up-looking simulated camera shows the part on the nozzle; and
  with no discard location set it is at the origin.
- A vision task on a camera that is not in front waits for it to start instead of failing.
- A nozzle turns parts as OpenPnP's does: while it holds a part its rotation reads the part's angle (the Jog panel
  and the status bar show it), its axis turned by the rotation mode offset. New: Align with Part?, bottom vision's
  turn of the part taken into that offset, and Issues & Solutions suggests it. A pick from the Feeders tab gets the
  offset too, as OpenPnP's does (against bottom vision's Test Alignment Angle).
- A strip feeder from an older OpenPnP file (without the EIA-481 flag) is turned as OpenPnP turns it, so its parts are
  no longer picked 90° off.
- The simulated controller reports its axes in Grbl's order: a machine whose rotation axis came before Z in its list
  no longer showed Z and the rotation swapped.
- Simulation Mode's Pick & Place Checking, as OpenPnP's: with an image camera on the head, each pick must find a part
  in the picture where the nozzle is, and each place its pads, or it fails. OpenPnP's default machine now feeds and
  picks out of the box: its camera counts as calibrated by its picture, and its actuators switch on its simulated
  controller. A pick from the Feeders tab gives the nozzle its part before the vacuum, as OpenPnP does.
- As OpenPnP, a first start brings in OpenPnP's own default machine (a simulated controller, a camera over
  OpenPnP's test picture of the table, its strip feeders) and its default packages, parts and vision settings.
  OpenPnP's NullDriver comes in as a simulated controller, and an old machine.xml with a single NullDriver is
  brought up to date as OpenPnP does. A camera showing a still picture (an image camera, a simulated one) is
  no longer taken to have hung while the machine stands still.
- Issues & Solutions, as OpenPnP's: a nozzle that can turn less than a full turn must use the LimitedArticulation
  rotation mode, and bottom vision must then pre-rotate parts (Accept sets both). A contact probing nozzle needs its
  probing actuator, on the same controller as its Z, with a probing command (a G38.2 suggested for a Grbl). Auto tool
  select off is suggested on, and an HTTP actuator reading a URL is given a pattern to read the value by.
- OpenPnP machines with an OpenCvCamera (by its device index and OpenCV properties) or a Webcam bring them in as capture
  devices; a capture device can be named by its node (/dev/video2). A SimulatedUpCamera comes in as a simulated camera
  that sees the nozzle tips in Simulation Mode.
- Help > Submit Diagnostics…, as OpenPnP's: what helps with a problem put in one file to attach to an issue (nothing is
  uploaded). The log is now also kept in a file, log/jplacer.log.
- The Window menu, as OpenPnP's: Multiple Window Style (the cameras and the machine controls each in a window of their
  own, from the next start) and Change Appearance… (theme, font size, alternating table rows).
- Scripts can drive the machine, as OpenPnP's can: `import jplacer` (or `require("jplacer")`) to move tools, home,
  switch and read actuators, send G-code and show a message.
- View > Language, as OpenPnP's: Russian, Spanish, French, Italian, German or Chinese, from OpenPnP's own translations,
  from the next start.
- Issues & Solutions checks a camera's own settings as OpenPnP does (brightness, contrast, gamma, gain, hue,
  saturation, white balance, sharpness, auto exposure) and sets them right on Accept.
- Issues & Solutions points out a nozzle, actuator or camera on other X or Y axes than its head's camera, and fixes
  it on Accept.
- Issues & Solutions: each nozzle's Safe Z dynamic or fixed, an unconventional Safe Z, the tallest part against the
  safe zone, the manual tip change location (captured on Accept) and a tip's background calibration method (calibrated
  on Accept), as OpenPnP's Kinematic and NozzleTip solutions.
- Issues & Solutions checks the controllers as OpenPnP's GcodeDriverSolutions does: serial flow control on a Grbl,
  pre-move commands and letter variables, the Maximum Feed Rate, G-code compression and comments.
- Issues & Solutions, as OpenPnP's VisionSolutions: Enable Visual Homing (Accept finds the mark under the head camera
  and makes it the homing mark), the calibration rig's heights against each other and against Safe Z, and linking the
  tables in Production.
- Issues & Solutions calibrates on Accept, as OpenPnP does: a camera not calibrated, and each head's X and Y backlash.
- Issues & Solutions checks the actuators as OpenPnP does (vacuum, blow off, sensing, pump, Z probe, camera lights and
  switchers: assigned, on a controller, with their commands, typed in the issue) and the cameras' previews (rate,
  suspended in tasks, Auto Camera View, Rendering Quality).
- The Machine has OpenPnP's Simulation Mode tab: simulated imperfections (homing error, non-squareness, nozzle tip runout,
  camera lag, noise and vibration) on the simulated cameras, Replace Drivers? to run a real machine's settings on
  simulated controllers, Set Machine Table Z and Reset Feeders. OpenPnP's SimulationModeMachine comes in with them.
- View > System Units > Inches, as OpenPnP's: every length shown and typed in inches (rotations in degrees), from the
  next start. The Jog distances are kept apart for inches.
- A camera can be OpenPnP's GstreamerCamera: any GStreamer pipeline, as gst-launch-1.0 is given one (GStreamer must be
  installed). OpenPnP machines bring theirs in.
- A camera slow to give its first picture is no longer taken for hidden and stopped before it shows.
- A camera can be OpenPnP's OnvifIPCamera: an IP camera set up over ONVIF (user and password, resolution, resize),
  its snapshots its pictures. OpenPnP machines bring theirs in.
- A camera's picture menu has OpenPnP's Rendering Quality: Low (sharp pixels, to begin with), High (smoothed) and
  Highest (best scale).
- Calibrating a camera at two heights now also shows how far it is tipped (OpenPnP's Camera Mounting Error about X, Y
  and Z) and where it looks at its Default Working Plane Z (Calibrated Head Offsets, or Camera Location for a fixed
  camera); a tipped head camera's lean is allowed for at that height.
- A camera's Advanced Calibration has OpenPnP's General Settings: Deinterlace, Cropped Width and Height, and Default
  Working Plane Z, the height a head camera's scale is taken at once calibrated at two heights. OpenPnP's comes in.
- A camera can be OpenPnP's SwitcherCamera: one of several analog cameras on one capture device through a multiplexer,
  switched in by an actuator when vision takes its picture. OpenPnP machines bring theirs in.
- Camera white balance has OpenPnP's Mapped Roughly and Mapped Finely and its color balance curve, and an
  OpenPnP import brings a camera's white balance.
- A nozzle's Offset Wizard has OpenPnP's precise offsets calibration: a test object picked, turned and placed
  at six angles, the camera finding where it went.
- A contact probing nozzle can probe the discard place too (Discard Probing), brushing the part off there.
- Nozzle tips have OpenPnP's Auto Recalibration and Fail Homing: their runout is measured again on a tip
  change, before a job's picks, or once the machine is homed, as each tip is set.
- The camera looking up can auto focus, as OpenPnP's: a part whose height is not known is measured by bringing
  it into focus, and its Auto Focus tab tests it and can set the camera's Z.
- Nozzles can probe by touch, as OpenPnP's ContactProbeNozzle: a job finds feeder and placement heights (and a
  part's height when it is not known) with a contact sensing actuator, and a nozzle tip's Z can be calibrated
  at its touch location.
- The Jog panel's Special tab has OpenPnP's Recycle: the part on the nozzle put back into a feeder that holds
  it. Its buttons flow onto as many lines as the panel's width needs, so none is cut off.
- More of OpenPnP's scripting events run: the camera's settle, capture and position events, nozzle tip
  calibration, part alignment, discards, feeder faults, and Machine.AfterDriverHoming.
- Nozzle tips have OpenPnP's Background Calibration: measured along with the runout, it finds how the
  background round the tip looks, says what could be better, and bottom vision masks it out.
- Nozzle tips have OpenPnP's Cloning Settings: one tip is the template, and the others' tool changer steps
  can be cloned from it, moved to their own slot.
- A move's feed rate is now worked out as OpenPnP does: over the path of its linear axes (a diagonal move is
  no longer slowed to one axis's rate), as long as its slowest axis takes; a turn alone in degrees. Axes have
  OpenPnP's Switch Linear ↔ Rotational for a controller axis used the other way round.
- Bottom vision sees a part too big for one picture in several shots, as OpenPnP's Vision Compositing:
  the Packages tab's Vision Compositing works out the shots and draws them, and a camera fixed to the
  machine has OpenPnP's Roaming Radius.
- The Machine has OpenPnP's Motion Planner tabs: Allow continuous motion, so the moves of one operation go to the
  controller back to back, and Test Motion through up to four places with how long it was planned to take and
  took. Actuators have OpenPnP's Machine Coordination: whether to wait for the machine before and after actuating
  and before reading.
- Actuators can be OpenPnP's ScriptActuator: switching or setting one runs a script of the scripts folder, told
  whether it is on or the value it is set to.
- OpenPnP's vision pipelines run in jplacer, every stage of OpenPnP's editor included, and a Pipeline Editor as
  OpenPnP's: stages added, removed, renamed, dragged and switched off, their settings, each stage's picture and
  what it found, the pixel under the mouse, pin, true colours, copy and paste. A strip feeder's Edit Pipeline
  and Reset Pipeline work.
- Bottom vision and fiducial settings have OpenPnP's pipeline controls: Edit, Reset, Copy and Paste, and a slider
  for each of the pipeline's parameters, its effect shown on the camera as it moves.
- Machine Setup has OpenPnP's Vision nodes: Bottom Vision and Fiducal Locator with their settings, and a choice of
  finding parts and fiducials with jplacer's own finders (the default) or with the vision settings' pipelines.
  Fiducials are averaged when Average Matches? is set.
- Test Alignment (with Center After Test), Detect Offsets and Test Fiducial Locator work on the vision settings' pages,
  as OpenPnP's; jplacer remembers which part each nozzle holds after a pick, as OpenPnP does.
- Loose part feeders work as OpenPnP's (ReferenceLoosePartFeeder and AdvancedLoosePartFeeder): their pipelines find the
  part nearest the camera in three looks, picked from on top of it; their pipelines (an advanced one's training pipeline
  too) edited and reset.
- A strip feeder's Auto Setup works as OpenPnP's: click two parts on the camera's view, and its sprocket holes, part
  pitch and feed count are set, the holes it sees shown while it waits.
- Push-pull feeders (ReferencePushPullFeeder) work as OpenPnP's: the lever pushed and pulled through the start, mid and
  end locations ticked, each at its speed with its delay, the auxiliary (peel) actuator, multiple actuations, additive
  rotation; their sprocket holes calibrated by vision as Bamboo feeders' are, Auto-Setup (trying the stock pipelines when
  the feeder's own fails), Preview Vision Features, Discard Parts and the Push-Pull Motion tab.
- Machine Setup's Bottom Vision and Fiducal Locator have OpenPnP's second tab: the default vision settings' page.
- A Rapid feeder's Scan works as OpenPnP's: the camera along the scan reads the feeders' QR codes, and finds or makes
  each feeder, setting its place and address.
- Blinds feeders (BlindsFeeder) work as OpenPnP's: the holder's fiducials and their calibration, the tapes on one holder
  sharing its settings and numbered across it, Auto Setup of the pockets, Show Features, the covers opened and closed by
  a nozzle tip that may push (Open/Close Cover, Open/Close All Covers, Calibrate Cover Edges, opened on first use or on
  job start), push covers, OCR of the part's label, feeder groups, the pipeline and OCR settings set to all, and Extract
  3D-Printing Files.
- Nozzle tips have OpenPnP's Push and Drag Usage: whether a tip may push, and its outside diameter.
- Push-pull feeders read the part in them by OCR as OpenPnP's: Setup OCR Region on the camera's view, Part by OCR,
  All Feeder OCR with its report, the wrong part actions (swap feeders, swap or create one, change the part, with or
  without cloning), Stop after wrong part, and the check on job start. Their Clone Settings: a feeder used as a
  template, Clone from Template and Clone to Feeders with the template's places moved to each tape, and + for one more
  feeder in the row, set up there.
- Heap feeders (ReferenceHeapFeeder) work as OpenPnP's: parts fetched from the heap by the nozzle's vacuum (stirred or
  poked), dropped into a drop box, the ones the right way up found by the feeder's pipeline and the others turned by
  dropping them again; drop boxes made, named, deleted and cleaned, their pipelines and dummy part, GetSamples for the
  template pipeline.
- Bamboo feeders (BambooFeederAutoVision) work as OpenPnP's: fed by their feed actuator, the parts of a feed picked in turn,
  their sprocket holes found by vision to keep the pick location true (Calibration Trigger, precision statistics),
  Auto-Setup with the camera at the pick location, Preview Vision Features, Discard Parts, their pipelines by Vision Type.
- Nozzle tips have OpenPnP's Max. Part Diameter and Max. Pick Tolerance, imported from OpenPnP and used by bottom vision
  pipelines.
- An Issues & Solutions tab as OpenPnP's: milestones, Find Issues & Solutions, Accept, Dismiss, Reopen, Include
  Solved and Dismissed, with checks of jplacer's Machine Setup (axes, letters, nozzles' axes, homing, Safe Z, soft limits, feed rates,
  rotation), cameras, nozzle tips and Photon feeders.
- A Log tab as OpenPnP's: the log's entries coloured by level, its global level, and search, level and system
  output filters, clear, copy and scroll down.
- Photon feeders work as OpenPnP's: found and set up on their bus, fed by their pitch, their slots' locations,
  Global Config's Search and the Program Feeder Slot Wizard; the PhotonFeederData actuator made when missing.
- Neoden 4 feeders work as OpenPnP's: actuated with their pitch, turned by their rotation in the tape, Actuate
  and Reset, and their template vision.
- Slot feeders work as OpenPnP's: banks of feeders loaded into slot auto and slot Schultz feeders, taken from
  OpenPnP's machine, with their offsets from the slot, parts, banks, and a slot Schultz feeder's fiducial check.
- Schultz and Rapid feeders work as OpenPnP's: a Schultz feeder's actuators with its feeder number, its Get ID,
  feed count, pitch and status read and shown, its test buttons; a Rapid feeder's address and pitch sent to RAPIDFEEDER.
- Lever feeders work as OpenPnP's: the lever pushed once for every 4 mm of the pitch, the take up run while it
  comes back, and the same template image vision as a drag feeder.
- Drag feeders work as OpenPnP's: the pin dragging the tape (with peel off and back off), two parts a drag at a
  2 mm pitch, and the template image vision, with its template and area of interest selected on the camera.
- Rotated tray, auto and tube feeders work as OpenPnP's: their setup pages, feeds (an auto feeder's feed and
  post-pick actuators, Test feed), and picks.
- The Parts and Packages tabs show each one's Bottom and Fiducial Vision Settings, with Specialize and
  Generalize as OpenPnP has them.
- Bottom vision: a job aligns each part over the camera looking up, found by its footprint's pads (no
  pipeline to tune), pre-rotated and checked again as the vision settings say, and places it corrected.
- A Vision tab, as OpenPnP's: bottom and fiducial vision settings, with Assigned To, New, Delete, copy and
  paste, and their settings pages. Vision settings are now saved, pipelines and all. A fiducial check
  looks as its fiducial vision settings say (passes, max linear offset, parallax). Importing an OpenPnP
  machine brings its bottom vision and fiducial locator settings.
- Strip feeders with Use Vision? find their sprocket holes with the camera at each feed (extrapolation
  distance and parallax as OpenPnP has them) and pick where the holes are; a missing hole ends the strip.
- Machine Setup > Job Processors > ReferencePnpJobProcessor: the job order, tip loading strategy, attempts,
  Step Next Motion, nozzle optimizing and pre-rotation, and feeder fault limits, as OpenPnP has them
  (imported with an OpenPnP machine).
- The Job tab's Multiple Point Board Location: a board located by jogging the camera over two or more of
  its placements, step by step as in OpenPnP.
- Jobs run, as OpenPnP runs them: Start (Pause, Resume), Step and Stop on the Job tab and in the Job menu.
  The setup is checked, boards are located by their fiducials, placements are planned by nozzle tip,
  feeder and place, tips are changed, parts fed, picked and placed, and the head parked at the end;
  errors pause the job (or are deferred), their board, part or feeder shown. Job > Reset All Placed.
- The Job tab's Fiducial Check locates the chosen board or panel by its fiducials.
- A Feeders tab, as OpenPnP's: the feeders table (Name, Part, Type, Priority, Faults, Enabled, Feed),
  changed in place, with Set Enabled and Set Feed option on the right-click menu; New Feeder (OpenPnP's
  nineteen kinds), Delete Feeder, Pick, Feed, Move Camera and Move Tool; each feeder's setup, with
  OpenPnP's location buttons. Strip and tray feeders are fed and picked from. Importing an OpenPnP
  machine brings its feeders in. Parts > Pick Part picks from a feeder, and the Job tab's Edit Placement
  Feeder opens the placement's feeder.
- A double-click in the Add existing board or panel list takes the one clicked; a single click only
  chooses it.
- File > Open Recent Job (the last ten jobs); the save question on leaving a changed job now has Cancel;
  saving over a file that is there asks first.
- A Job tab, as OpenPnP's: the job's boards and panels, nested, where each lies, side, enabled and
  fiducial check; add and remove boards and panels; move the camera or nozzle to a board or placement and
  capture where they are; Alert or Defer errors; each board's placements with Placed and Status; the job
  viewer; and the placements done in the status line.
- A Panels tab, as OpenPnP's: add, create, copy, remove and clean up panels; each panel's children
  (boards and panels, where they lie, side, enabled, fiducial check) with Add Child, Remove, Replace and
  the right-click menu; its alignment fiducials and pseudo-placements, chosen from its boards' with Use
  Children Fiducials (hull, Auto Select); and arrays of a child, rectangular or circular, previewed as
  they are set.
- The board and panel viewer (View Board, View Panel): outlines by side, placements, fiducials, origins,
  locations and a reticle, viewed from the top or bottom, zoomed and panned, with a right-click menu.
- A Boards tab, as OpenPnP's: add, create, copy, remove and clean up boards; each board's placements
  (Enabled, ID, Part, Side, X, Y, Rot., Type, Error Handling, Rank, Comments) changed in place, with
  OpenPnP's right-click menu and Space to turn one on or off. Placements are imported with OpenPnP's
  importers: Altium, DipTrace, EAGLE board and mountsmd, KiCad .pos, Proteus and named-column CSV, merged
  into a board or replacing what it had. File > Save Configuration and quitting ask about each changed
  board, as OpenPnP does.
- A Parts tab, as OpenPnP's: the parts table (ID, Description, Height, Through-Board Depth, Package,
  Speed %, BottomVision, FiducialVision, Placements, Feeders), changed in place, sorted by up to three
  columns, searched as you type; New Part, Delete Part, Pick Part, copy and paste a part; each part's pick
  retries on its Settings tab. Buttons show OpenPnP's own icons.
- A Packages tab, as OpenPnP's: the packages table, New/Delete/copy/paste, and each package's Nozzle Tips,
  Settings (vacuum and blow off), Footprint (Dual, Quad and BGA generators, KiCad footprint import, the
  pads table) and Vision Compositing settings. The chosen package's footprint is drawn over the cameras.
- Jobs, boards, panels, parts and packages are kept as OpenPnP keeps them, in its own files (.job.xml,
  .board.xml, .panel.xml, parts.xml, packages.xml): open an OpenPnP job and it is the same job, with its
  boards and panels, what was placed and what was enabled. A job from an older OpenPnP is converted as
  OpenPnP converts it, a copy of the old file kept beside it. File > New Job, Open Job…, Save Job and
  Save Job As… work on these; the job open last is opened again at start.
- jplacer can now talk to a machine. A machine is described by a cell file, and
  Machine > Import OpenPnP Machine… makes one from an OpenPnP machine.xml.
- Machine > Connect connects to the machine's controllers, recognises grblHAL, Grbl
  and other G-code firmware, and reads the settings the controller stores.
- A Machine panel shows the live position of every axis, switches and reads the
  actuators (lights, valves, vacuum sensors), and has a console for sending G-code.
- Backlash is taken out of every move: an axis always arrives travelling the same way, going past and
  coming back slowly when it would otherwise arrive the other way. An imported machine keeps OpenPnP's
  measured backlash.
- Importing an OpenPnP machine brings the head's homing fiducial, park location, calibration rig and
  pump. OpenPnP's camera calibration is not brought across: jplacer will measure its cameras itself.
- Importing an OpenPnP machine brings its whole home command (every line, in order), and importing
  again keeps the port you chose.
- The machine can be homed (Home, or Machine > Home All Axes) and moved by hand from the Jog panel:
  choose a nozzle or camera, then step it, or type where it should go. Soft limits are checked, and
  nothing moves until the machine is homed.
- The machine's panels are docks of their own (Machine, Jog, Actuators, Console, Axes), to arrange,
  stack or tear out as you like.
- Positions shown are the coordinates moves use, taking the controller's work offset into account.
- A controller's port belongs to jplacer while connected: another program (or a second jplacer) trying to
  open it is refused, instead of the two silently sharing and each losing part of what the controller says.
- Save Picture keeps what a camera is showing as a PNG.
- Calibrate measures the camera on the head: it goes to the homing mark, moves the head by known amounts
  and works out how big a pixel is on the machine and which way the camera is turned. The result is
  saved in the cell.
- Visual Test looks at the homing mark through a calibrated camera and says how far, in mm, it is from
  where the head's settings put it. Nothing is changed.
- Round marks are measured from their edge all the way round, so where a mark is found no longer depends
  on where the search began, and its size is measured to a fraction of a pixel.
- Calibrate on a fixed camera (one looking up) holds a nozzle's tip over it and moves the nozzle instead of
  the camera, asking first.
- Calibrate measures across the whole picture, out to its edges, with the lens's bending towards the
  corners as well, and leaves out a measurement far from the rest.
- Calibrate also measures the camera's lens (how it pulls the edges of the picture in, and about which
  point), and everything measured in a picture is straightened through it.
- Pictures measured after a move are ones taken after the move ended.
- A mark not found in a picture is looked for again with the room's light taken out (the camera's light
  off and on, one picture taken from the other).
- A camera that drops off USB or hangs is noticed, shown as lost over its last picture, and opened again
  until it is back.
- Importing an OpenPnP machine brings its non-squareness correction, and each camera's settings
  (exposure, white balance and the rest), which are set again every time the camera is opened.
- Importing an OpenPnP machine again keeps the camera calibrations measured in jplacer.
- Machine > Park Head: Z up into its safe zone, then the head to its park place (as near as the soft
  limits allow).
- The Machine panel lists what the machine has been calibrated for: each camera, the squareness, homing.
- The Jog panel shows and takes the chosen tool's own coordinates (its offset on the head included).
- Home waits for the controller to report the home coordinates before calling the machine homed.
- The camera's picture can be shown straightened: the lens's bending out, square to the machine, with a
  slider for how much of the bent edge to show. Drawn by the graphics card, or the processor without one.
- Double-click the live picture of a calibrated head camera and it moves to look there.
- Each camera has a tab of its own in the middle of the window, instead of buttons to choose one: put
  them side by side or tear one out to see two at once. A camera runs, with its light on, while its
  picture is on screen, and keeps its own straightened or as-taken choice.
- Machine Setup (Machine > Machine Setup…): the machine as a tree of its parts (controllers, axes,
  heads with their nozzles, cameras and actuators), each part's settings to change, parts to add,
  remove and reorder, and Apply to put the changes to use.
- A camera keeps a calibration for each picture size it was calibrated at, and uses the one for the size
  it is taking.
- Visual homing: Home finishes by finding the homing mark with the calibrated head camera and
  correcting the position to it, as OpenPnP did for an imported machine.
- When a move finishes, the positions shown are where the machine stopped, not where it was a moment
  before.
- The cameras, live, in the middle of the window: choose a camera, see what it sees, with a cross at its
  centre. A camera is found by its own name, whichever USB socket it is in, and its light is switched
  on while it is shown and the machine is connected.
- Two toolbar icons show and change the machine's state: a chip for the connection, a house for homing,
  each grey, amber while working, green when done, red when it failed. The red strip across the window
  is kept for what is critical: an alarm, or a connection lost while working. Connecting to a port
  where nothing answers now fails instead of pretending.
- The Machine panel lists the serial devices plugged in, so you can pick the controller's port; it is
  saved by the device's permanent name, which does not change when USB devices start in another order.
- Dialog buttons are always wide enough for their labels.
- The file dialog can show hidden folders, such as OpenPnP's .openpnp2: tick Show hidden, or press
  Ctrl+H.
- A new icon. Run as an AppImage, jplacer adds itself to your applications menu with it.
- Preferences has a General section: tear-off menus (off by default) and whether jplacer appears in
  the applications menu.
- A user manual: Help > User Manual opens it in your browser, and Help > What's New shows what changed
  in each version.
- Nozzle tips: Machine Setup lists them, each with its diameter seen from below and the nozzles it
  fits, and each nozzle says which tip is on it. Importing an OpenPnP machine brings its nozzle tips.
- A nozzle tip's changer is taught as steps (moves, safe Z, actuators, waits, a message to you), edited
  in Machine Setup under the tip; unloading is loading run backwards, or steps of its own. Importing an
  OpenPnP machine brings each tip's tool changer as steps.
- Preferences has an Appearance section: a dark or light theme (or as the desktop is set) and the
  interface scale, how big the whole interface is.
- The window is laid out as in OpenPnP: the cameras top left, the machine controls (Jog, Actuators)
  under them, Machine Setup and Machine tabbed across the rest, and the Console along the
  bottom. Splits can be dragged past half way. View has a tick for every panel, to close it or bring it
  back.
- The status bar shows where the tool chosen in Jog is; click it to measure from where it is now, click
  again to go back. It takes the place of the Axes panel.
- A camera's tools are icons in its tab: an eye to see the picture as taken (lit) or straightened, save
  the picture, calibrate, and the visual test. The picture gets the room the buttons took.
- A camera's gear icon opens its settings in Machine Setup. How much of a straightened picture's edge
  shows is now one of those settings, no longer a slider over the picture.
- Machine Setup's tree has buttons to open and close every branch, a search box with a clear button,
  and a right-click menu: open or close a branch, open or close all, add, remove.
- Applying Machine Setup no longer disconnects the machine: it takes the new settings as it runs and
  stays homed. A controller's connection settings (port, profile) are used the next time you connect,
  and a change to the axes needs a home.
- Machine Setup has a divider between the tree and the settings, to drag; where it is is kept.
- Panels short of room shrink their lists and boxes first, so buttons and input lines (the console's) are no longer squeezed or lost.
- Clicking, dragging and scrolling work in the panels to the right of the cameras (Machine Setup,
  Machine): tree branches open, scroll bars drag. Tooltips show on the camera icons, the toolbar's icons
  and the position readout.
- Machine Setup has no Apply or Reset any more: each change goes to the machine (and is saved) as you
  make it, and Undo and Redo (also in the Edit menu, Ctrl+Z and Ctrl+Shift+Z) step back and forward
  through them. A text field's value changes when you press Return or Tab or leave it, and Escape puts
  the old value back. A setup with something wrong in it is kept from the machine until it is put right.
- Machine Setup is laid out as OpenPnP lays out each part: tabs, titled groups, controls the size of what
  they hold, and coordinates in X / Y / Z / Rotation columns. A nozzle's tips are a table (Compatible?,
  Loaded?). Places have buttons to set them from where the camera or nozzle is, or to go there; soft
  limits and safe zones the same for their axis. A head's homing has Visual Test and Visual Home beside
  it, and a camera's calibration Start Calibration.
- A controller's settings gain what OpenPnP has: parity, data bits, stop bits, Set DTR / Set RTS, line
  endings, a maximum feed rate, Log G-code, and a Gcode tab to replace any of the firmware's commands
  (several lines if need be). Brought in from OpenPnP with the rest of the machine.
- The machine's own settings, as in OpenPnP: Home after connected, Park after homed (after visual
  homing), and a Discard Location. Brought in from OpenPnP.
- An axis's Resolution (Steps / mm) rounds every move to a whole step; a rotation axis can be limited to
  -180..180 and turn the short way round; acceleration and jerk can be sent with each move. As in
  OpenPnP, and brought in from it.
- A nozzle with a vacuum has Pick and Place on the Jog panel, as in OpenPnP: the head's pump as its Pump
  Control says, the vacuum, the blow-off and the dwell times. Machine Setup has the nozzle's Vacuum tab,
  its and its tip's dwell times, and the head's Pump; all brought in from OpenPnP.
- An actuator can be switched on or off by itself once the machine is connected, once it is homed, and
  before you disconnect (a pump off as the machine is let go), as in OpenPnP and brought in from it.
- A camera's Device Settings show its own properties (exposure, white balance, focus, gain and the rest)
  as OpenPnP does: set to a value or automatic when the camera opens, or left as the camera has them.
  Zoom is now brought in from OpenPnP too.
- Camera Settling as in OpenPnP: a picture for vision waits a fixed time after a move, or until the
  picture stops changing (Maximum, Mean, Euclidean or Square difference, threshold, debounce, timeout,
  centre mask). Brought in from OpenPnP.
- A camera's white balance as in OpenPnP: each colour's balance and gamma on every picture, worked out
  with Overall or Brightest, and brought in from OpenPnP (the bench's top camera has one).
- A camera's light as in OpenPnP: on before a picture for vision and/or while you look at the camera,
  off after the picture and/or while another camera takes one. Brought in from OpenPnP.
- Part detection by the vacuum, as in OpenPnP: Pick checks a part is on and Place that it is off, by the
  vacuum level (Absolute) or its change (Difference), as each nozzle tip is set; brought in from OpenPnP.
- A nozzle's Offset Wizard, as in OpenPnP: a mark left by the nozzle, the camera over it, and the
  nozzle's offset corrected (one step to undo).
- Importing an OpenPnP machine again keeps each nozzle tip's loading and unloading steps, which tip is
  on each nozzle, and each camera's straightened-picture setting, as set here, and takes OpenPnP's new serial settings while
  keeping the port chosen here.
- The Jog panel is laid out as OpenPnP's: an X/Y arrow pad with Park, Z and C with Park, put the nozzle
  where the camera looks and the camera over the nozzle, Distance and Speed sliders standing beside the
  pad, which grows and shrinks with its dock; a Special tab with
  Head Safe Z, Discard, Pick and Place. Each has OpenPnP's key (Machine ▸ Jog); the choices are kept for
  next time, and the panel scrolls when its dock is short. The left column gives the machine controls
  more of its height, and the console a little less of the window's, so the pad shows whole.
- Menu keys work: Edit ▸ Undo (Ctrl+Z) and Redo (now Ctrl+Y), Home All Axes (Ctrl+H).
- A move to a soft limit itself (the park place at X 390, say) is no longer refused because the nearest
  whole motor step lies a hair past it: it goes to the step on this side.
- The Jog panel's Speed is the machine's speed, as in OpenPnP: every move is scaled by it (jogs, parks,
  camera tasks, nozzle tip changer steps), not only jogs.
- Nozzle tips are loaded and unloaded from the Jog panel: a tip button beside the tool opens the chosen
  nozzle's tips. Loading another unloads the one on it first; Step Through asks before each changer step
  (on by default); Manual Change says which tip was put on by hand, moving nothing. Each step's move goes at its speed
  times the machine's.
- A move can be stopped while it runs: Stop (Escape, or the Machine menu) slows it to rest and keeps the
  position (a controller that does not come to rest in time is reset anyway, and the machine must be
  homed again);
  Emergency Stop (the Machine menu; no key unless you give it one) resets the controllers at once, and the
  machine must be homed again.
- A nozzle's Z can be homed on its own (Home Z, in the nozzle's tip menu), for when forcing a tip on made
  its motor slip: the head parks, then the nozzle's own home G-code runs (set on its Homing tab in Machine
  Setup). Nozzles sharing a Z motor are homed together. Importing an OpenPnP machine again keeps it.
- Machine Setup shows its tree beside the chosen part's settings rather than over them, so both have
  the panel's full height.
- A controller left in alarm (after an emergency stop, say) connects instead of refusing with error 9; Home
  unlocks it and homes.
- Visual homing, Visual Test, camera calibration and the nozzle offset wizard move at
  the machine's speed (the Jog panel's Speed) instead of always a tenth of it.
- Keys can be chosen for every menu entry and Jog panel button in Preferences > Keys: click a function's
  box and press the key (plain keys such as arrows and digits included). A key given to one function is
  taken from the one that had it; Reset All puts back the keys jplacer starts with (OpenPnP's).
- Preferences > Jog sets the Jog panel's distance and speed steps, and each step can be given a key (1 for
  1 mm, say). Faster and Slower step the speed; Pick, Place, Turn to 0 and the nozzle and camera
  positioning can be given keys too.
- Preferences is on tabs (General, Keys, Jog) and can be made bigger.
- Opening jplacer from the applications menu no longer leaves a busy cursor spinning for half a minute
  after its window is up.
- The mouse wheel zooms a camera's picture in and out, up to 64 times, about the cross in the middle.
- Right-click a camera's picture for a reticle: a grid, a ruler, or a circle or square of a size, in millimetres through the camera's calibration.
- Shift+click a camera's picture, or drag in it, to move the camera to look there.
- Buttons and labels no longer go missing after the window is made small and then big again.
- Camera calibration comes to each place the same way (a lead-in), so the drives' play no longer spreads the fit, and finds each in several pictures.
- A camera's Advanced Calibration tab has options for how it is calibrated (the grid's size and reach, the outlier limit, the worst fit taken), and shows the results with graphs of the measurements: in the order made, X against Y, and as a map over the picture.
- Calibrate measures a camera at a second height too (a head camera over the calibration rig's secondary mark, a fixed one with the nozzle raised), giving where the camera is, its focal length and field of view in degrees; the rig's marks are on the head in Machine Setup.
- Backlash: DirectionalCompensation and DirectionalSneakUp as well as one-sided, imported from OpenPnP as they are; Calibrate on an axis's Backlash tab measures the play with the head camera, chooses the method and shows graphs of what it measured.
- Changing an axis's speed, limits or backlash no longer needs the machine homed again.
- Machine Setup: the controller's Gcode tab is grouped by what each command is for, with what its placeholders mean, the profile's commands shown once, and scrolls to the end; Home after connected is a controller's setting; the tree's search has its × on the right, Open All and Close All on its right-click menu; a page's groups line up.
- The console shows the newest line at the bottom and follows it, unless you have scrolled back. It shows jplacer's log as well as the G-code, with how much of each: G-code on or off, the log's level, and each category's own level; kept for next time.
- Icon buttons show where a click leads: a small down-triangle for a menu (the nozzle tip button), "…" for another place (a camera's settings).
- DistanceAware backlash keeps each drive's lag as it goes, so a move after a short one the other way (the drive only partly wound) is sent the right amount too.
- Machine Setup: Undo and Redo are on the Edit menu (Ctrl+Z, Ctrl+Y) only, the space given to the settings; its tabs wrap onto more rows when narrow, and its notes and graphs follow its width.
- Backlash: DistanceAware, jplacer's own method for a drive whose play keeps growing with the move: each move is sent the lag measured for how far it travelled since the axis last turned. Calibrate tries it against one-sided and keeps the better.
- OneSidedPositioning now ends every move the same way, as OpenPnP's does; OneSidedOptimizedPositioning keeps the fewer moves. Backlash calibration averages 8 pictures a measurement and chooses one-sided for play that keeps growing with the move (a stretching belt).
- A camera's Camera Settling tab can test the settling (one jog step out and back, or standing still) and graphs how the camera came to rest against the threshold; for the camera looking up, the head moves over it with the nozzle held there.
- Nozzle tip runout: Calibrate on a tip's Calibration tab measures how its end swings as the nozzle turns, with the camera looking up; compensated, the tip's centre lands where it is sent at any angle.
- Actuators that take a value (Double or String): set from the Actuators panel, their value type and commands in Machine Setup, imported from OpenPnP.
- A camera that hangs showing the same picture over and over is noticed as
  hung, like one that stops sending pictures. Work looking through a lost
  camera (calibrations, tests) waits a little for it to come back and carries
  on where it was; if it does not, it says the camera was lost. How long
  each takes is set per camera in Machine Setup (When the Camera Is Lost).
- The Jog pad's park buttons show a parking sign that grows with the pad.
- Buttons and other controls are no longer clipped by a pixel along an edge, and the Jog panel's tip
  button stays whole in a narrow dock (a long tool name is cut short instead).
- The Edit menu has OpenPnP's Add Board/Panel, Remove Board(s)/Panel(s) and Capture Tool Location, and Help
  has Quick Start and Setup and Calibration, in OpenPnP's order. OpenPnP's Scripts and Window menus are there,
  greyed out until they are built.
- View has OpenPnP's Selections in Tables: set to Linked, choosing a board, placement, part or feeder in one tab
  chooses what goes with it on the others (its board, its part, the part's package, feeder and vision settings).
  View also has OpenPnP's System Units and Language, with only millimetres and English for now.
- A row chosen in a table on a tab not yet shown is scrolled into view when the tab is shown.
- Machine Setup has OpenPnP's Signalers: a SoundSignaler plays a sound when a job meets an error or is finished,
  and an ActuatorSignaler switches an actuator (a beacon, a buzzer) while a job is in a state you choose.
  Signalers come in with an OpenPnP machine.
- The Jog pad's Z park takes the tool to its safe Z as OpenPnP's does, and the machine's Park all at Safe Z
  (on to begin with) says whether every other Z on the head goes up first.
- Machine Setup's machine page has OpenPnP's Auto tool select (the camera or tool another panel moves is chosen
  on the Jog panel), Auto-load most recent job and Default Board Location (where a board added to a job
  starts). They come in with an OpenPnP machine.
- A camera's picture has OpenPnP's light toggle (a sun at its top right, for a camera with a light) and Show
  Image Info in its right-click menu: the picture's size, zoom, pictures a second and a colour histogram. A
  fixed camera's menu has Move Selected Nozzle to Camera, and one calibrated at two heights Estimate Z
  Coordinate of Object, which measures how high a feature is from two clicks on it.
- A camera's picture menu has OpenPnP's Zoom Sensitivity (High, Medium, Low): how much the wheel zooms.
- A camera's Machine Setup has OpenPnP's Image Transforms: crop width and height, and de-interlace. They come
  in with an OpenPnP machine.
- Actuators can be OpenPnP's Profile actuators: named profiles that set up to six other actuators at once
  (lights, valves), with Default ON and Default OFF profiles, chosen on the Actuators panel. They come in with
  an OpenPnP machine.
- A controller can be reached over TCP (an IP address or host name and a port), as OpenPnP's can: a grblHAL
  board on Ethernet, for one. An OpenPnP machine with a TCP controller now comes in connected that way.
- A controller's Driver Settings have OpenPnP's Remove Comments, Compress G-code (with its exclude characters)
  and Backslash Escaped Characters, and they come in with an OpenPnP machine.
- Axes can be OpenPnP's cam axes: a Z (or a pair of nozzles' Zs, one each way) that a cam on a rotation axis
  drives. They come in with an OpenPnP machine, where they used to be left out.
- A camera has OpenPnP's Preview FPS, Suspend during tasks and Auto Camera View, brought in with an OpenPnP
  machine.
- A nozzle can have OpenPnP's Dynamic Safe Z: carrying a part, it goes up higher by the part's height when it
  goes to safe Z. It comes in with an OpenPnP machine.
- Actuators can have OpenPnP's Axis Interlock: switched as axes move (moving, standing still, in or out of the
  safe zone, parked), or read to confirm it is safe to move before or after a move, the machine stopped if not.
  It comes in with an OpenPnP machine.
- The machine has OpenPnP's Unsafe Z Roaming: a tool left below safe Z goes up to safe Z once it is jogged more
  than that far away.
- A nozzle has OpenPnP's Rotation Mode: the part's own angle (as before), the placement's angle, minimal
  rotation, or limited articulation for a nozzle that only turns so far. It comes in with an OpenPnP machine.
- A controller's Driver Settings have OpenPnP's Send FeedRate, Acceleration and Jerk On Change Only: a move
  leaves out what the controller already has. They come in with an OpenPnP machine.
- A controller can work in inches (Driver Settings' Units, as OpenPnP's): what is sent to it and what it reports
  converted. It comes in with an OpenPnP machine.
- Actuators can be OpenPnP's HttpActuator: switched, set and read through web addresses (a smart plug, a relay
  board on the network). They come in with an OpenPnP machine.
- A controller can work with OpenPnP's Letter Variables off and Pre-Move Commands: several axes sharing one
  output, each switched to by its pre-move command. They come in with an OpenPnP machine.
- Placing blows off as OpenPnP does: only at a level, the part's package's Blow Off Level or else the nozzle
  tip's new Place Blow-Off Level; with neither, the vacuum just goes off. A package's Vacuum Level sets a vacuum
  actuator that takes a value at pick.
- A nozzle has OpenPnP's Tool Changer settings: with the automatic tool changer off (or a tip without load and
  unload steps) a tip change is asked to be done by hand, at the Manual Change Location when set; Change On Manual
  Pick puts a fitting tip on for a pick from the Feeders tab, which now refuses a pick with a tip that does not
  fit, as OpenPnP does.
- A head can have OpenPnP's Z Probe actuator: Capture Camera Location then probes the place and fills in its Z.
  It comes in with an OpenPnP machine.
- A nozzle tip has OpenPnP's Min. Part Diameter and Max. Part Height (the height taken for a part whose height is
  not known, for Dynamic Safe Z), and Issues & Solutions checks its part diameters and pick tolerance as OpenPnP
  does.
- A camera can be OpenPnP's ImageCamera: it shows the part of a picture of the table under where it looks, for
  trying jobs and vision with no camera. It comes in with an OpenPnP machine.
- A camera can be OpenPnP's MjpgCaptureCamera: a network camera streaming JPEGs over HTTP. It comes in with an
  OpenPnP machine.
- The Scripts menu works, as OpenPnP's: the scripts folder's Python, JavaScript and shell scripts, run from the
  menu, and its Events folder's run at OpenPnP's events (Startup, homing, the job starting, finishing and failing,
  each placement, feed, pick and place). Scripts are told what they run for, but cannot reach into the machine as
  OpenPnP's can.

## 0.1.0

- The first release: the jplacer window and its menus, Preferences, and updates that install
  themselves from Help > Check for Updates and when jplacer opens.
