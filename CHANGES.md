# Changes

What changed in each version, in plain words for the people using jplacer: no commit hashes, no file
names. The manual's What's New page (Help > What's New) is generated from this file, and each GitHub
release carries its version's section as its notes.

Every change a user would notice adds a line under Unreleased, in the same commit as the change.
`packaging/build-release.sh` turns Unreleased into the new version's section; a beta carries it as its
notes.

## Unreleased

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

## 0.1.0

- The first release: the jplacer window and its menus, Preferences, and updates that install
  themselves from Help > Check for Updates and when jplacer opens.
