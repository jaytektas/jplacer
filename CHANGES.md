# Changes

What changed in each version, in plain words for the people using jplacer: no commit hashes, no file
names. The manual's What's New page (Help > What's New) is generated from this file, and each GitHub
release carries its version's section as its notes.

Every change a user would notice adds a line under Unreleased, in the same commit as the change.
`packaging/build-release.sh` turns Unreleased into the new version's section; a beta carries it as its
notes.

## Unreleased

- Jobs: File > New Job, Open Job…, Save Job and Save Job As… keep a board and the parts it needs in a
  .jpjob file. The job open last is opened again at start, and you are asked to save changes before
  they would be lost.
- Reading a pick-and-place file now keeps everything it says about each part (supplier part numbers,
  MPN, ratings, package, pin count, pad 1) and gives each placement its part: by part number or MPN
  where the parts library knows it, as a guess to confirm from its value and package, or as a new part.
- A parts library, kept from job to job: jobs take copies from it and never change it. A new library
  starts with the common packages (chip sizes, SOT, SOD, SMA/B/C, SOIC, TSSOP, QFN, LQFP) and their
  footprints, known by their KiCad, EasyEDA and supplier names.
- A Parts panel (Job > Parts) lists the job's placements with their parts and whether each can be
  placed: as a table sorted by any column, or as a tree grouped by any column, with a filter that
  narrows it as you type.
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
- Finding a fiducial on a real board: several round things near where it should be are measured and the
  best kept, so holes, vias, round letters and reflections are not mistaken for it, and shiny copper is
  found whether it shows bright or with the lens's dark reflection in its middle.
- Pictures measured after a move are ones taken after the move ended.
- A mark not found in a picture is looked for again with the room's light taken out (the camera's light
  off and on, one picture taken from the other).
- A camera that drops off USB or hangs is noticed, shown as lost over its last picture, and opened again
  until it is back.
- A Board panel: import a board's pick-and-place file (EasyEDA, JLCPCB, KiCad and other CSV), choose the
  side that is up, put the camera on one fiducial, and Locate Board finds it exactly by all its
  fiducials. Double-click a part to look at it.
- Squaring the machine: a located board shows how far the machine's Y axis leans from square, and
  Square the Machine corrects every move for it from then on.
- Importing an OpenPnP machine brings its non-squareness correction, and each camera's settings
  (exposure, white balance and the rest), which are set again every time the camera is opened.
- Importing an OpenPnP machine again keeps the camera calibrations and squareness measured in jplacer.
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
- The board is drawn over the live camera picture: each part's designator and each fiducial where the
  board's place puts it.
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
  under them, Board, Machine Setup and Machine tabbed across the rest, and the Console along the
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
- Panels short of room shrink their lists and boxes first, so buttons and input lines (the console's,
  the Board panel's rows) are no longer squeezed or lost.
- Clicking, dragging and scrolling work in the panels to the right of the cameras (Board, Machine Setup,
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
- A move can be stopped while it runs: Stop (Escape, or the Jog panel) slows it to rest and keeps the
  position (a controller that does not come to rest in time is reset anyway, and the machine must be
  homed again);
  E-STOP (the Jog panel, the red toolbar button, or the Machine menu; no key) resets the controllers at once, and the
  machine must be homed again.
- A nozzle's Z can be homed on its own (Home Z, in the nozzle's tip menu), for when forcing a tip on made
  its motor slip: the head parks, then the nozzle's own home G-code runs (set on its Homing tab in Machine
  Setup). Nozzles sharing a Z motor are homed together. Importing an OpenPnP machine again keeps it.
- Machine Setup shows its tree beside the chosen part's settings rather than over them, so both have
  the panel's full height.
- A controller left in alarm (after an emergency stop, say) connects instead of refusing with error 9; Home
  unlocks it and homes.
- Visual homing, Visual Test, camera calibration, locating the board and the nozzle offset wizard move at
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
- Locate Board can look at each fiducial from both sides (parallax) for shiny fiducials; the passes, how centred, and the parallax are in Machine Setup on the Machine's Fiducials tab, and come across from OpenPnP.
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
- Buttons and other controls are no longer clipped by a pixel along an edge, and the Jog panel's tip, Stop
  and E-STOP buttons stay whole in a narrow dock (a long tool name is cut short instead).

## 0.1.0

- The first release: the jplacer window and its menus, Preferences, and updates that install
  themselves from Help > Check for Updates and when jplacer opens.
