# OpenPnP parity

What OpenPnP does, area by area, and where jplacer stands: the groundwork list.
OpenPnP (`reference/openpnp`) has about 1,400 saved settings across 96
configuration pages; this list follows its features, not each field, and names
fields where they matter. jplacer's own way is kept where it is better (see
DESIGN.md, "What OpenPnP gets wrong"); covering a feature here means covering
what it is for, not copying how OpenPnP does it.

Status: **done**, **partial** (what is missing is said), **missing**,
**own way** (covered differently, on purpose).

## Machine

| OpenPnP | Status | jplacer |
|---|---|---|
| Home after enabled, Park after homed | done | Machine › Configuration |
| Park all at Safe Z, Auto tool select | done | Machine › Configuration; Jog Z park |
| Unsafe Z Roaming | done | |
| Discard location, Default Board Location | done | |
| Auto-load most recent job | done | on for a new cell; as OpenPnP's for an imported one |
| Motion planner: continuous motion | done | Machine › Motion Planner; waits where the machine must stand still (actuator coordination, pick and place, homing, each operation's end) |
| Motion planner: uncoordinated moves, interpolation retiming, minimum speed | missing | these shape OpenPnP's own 3rd-order motion profiles, sent as interpolated moves; jplacer leaves acceleration and jerk to the controller |
| Motion planner test motion (4 locations) and diagnostics | done | planned time from feed rates and accelerations, actual time, each axis's location and velocity from the controllers' reports |
| Issues & Solutions (guided setup, milestones, auto-fixes) | partial | the tab, milestones and the checks jplacer has so far |
| Log panel (filterable log) | done | Log tab |
| Signalers (sound, actuator on error / job done) | done | Neoden4Signaler left out (Neoden4 driver) |
| View: System Units (inches), Language | missing | millimetres and English only, greyed out in View |
| View: Selections in Tables (linked tables) | done | |
| Scripting (events, Python/JS scripts) | partial | the Scripts menu and events, scripts run as programs of their own told what they run for (JSON); no machine API for them; camera, nozzle tip and calibration events not fired yet |

## Controllers (GcodeDriver / GcodeAsyncDriver)

| OpenPnP | Status | jplacer |
|---|---|---|
| Serial port settings, DTR / RTS, line endings | done | |
| TCP communications | done | |
| Keep Alive | own way | it keeps a disabled machine's connection; jplacer's Connect and Disconnect open and close it, with no disabled-but-connected state |
| Firmware detection, generic G-code proposal | own way | firmware profiles (Grbl, grblHAL, Generic) with `auto` detection |
| Command timeout, connect wait, max feed rate, log G-code | done | |
| Compress G-code, remove comments, backslash escapes | done | Driver Settings |
| Send feed rate, acceleration, jerk on change only | done | Driver Settings |
| Units (millimetres or inches) | done | Driver Settings |
| Letter variables off, pre-move commands | done | Driver Settings; a Pre-Move Command per axis |
| Gcode tab: every command, per head-mountable | done | per controller; empty uses the profile's |
| Confirmation flow control, location confirmation | own way | the driver waits for each `ok` and reads status reports |
| Interpolation (max steps, jerk steps, min step time) | missing | |
| Console | done | Console dock |

## Axes

| OpenPnP | Status | jplacer |
|---|---|---|
| Controller axis: type, name, driver, letter, home coordinate, resolution / steps per mm | done | |
| Rotation: limit to range, wrap around | done | |
| Soft limits, safe zone (each with Enabled?) | done | |
| Feed rate, acceleration, jerk | done | |
| Capture / move buttons on limits | done | |
| Backlash: methods (one-sided, directional, directional sneak-up) with offset, sneak-up and speed factor | done | |
| Backlash calibration ("Calibrate now", with graphs of backlash against speed and sneak-up distance) | done | tolerance from the measuring's own noise (8 pictures a measurement); graphs of play by distance, by speed, and errors after; plus jplacer's DistanceAware method for stretching drives |
| Virtual axis | done | |
| Mapped axis (two map points) | done | |
| Linear transform axis (non-squareness) | own way | squareness measured from board fiducials; Square the Machine |
| Cam axes (clockwise / counter-clockwise, shared Z) | done | a cam axis kind, clockwise or not |
| Vibration / chassis resonance | missing | the settle graph shows ringing below 15 Hz (30 fps); frame rates above that, or an accelerometer, needed for chassis modes |
| Switch linear / rotational | done | and the feed rate as G-code reads F (linear path, else rotational) |

## Head

| OpenPnP | Status | jplacer |
|---|---|---|
| Homing fiducial, Visual Test, Visual Home | done | |
| Park location | done | |
| Calibration rig: primary, secondary marks, test object | partial | marks used by two-height calibration; test object unused |
| Z probe actuator | done | Capture Camera Location probes Z |
| Pump: actuator, control mode, on-wait | done | |

## Nozzles

| OpenPnP | Status | jplacer |
|---|---|---|
| Name, axes, offsets | done | |
| Rotation mode (absolute part angle, ...), align with part | done | the four modes; Align with Part only changes what OpenPnP's DRO shows, and jplacer shows the axes |
| Safe Z, dynamic safe Z | done | safe Z from the axes' safe zones; dynamic safe Z for a nozzle on a Z of its own |
| Pick / place dwell | done | |
| Compatible / loaded tips table | done | |
| Vacuum, blow-off, sensing actuators | done | |
| Tool changer enabled, change on manual pick, manual change location | done | |
| Nozzle offset wizard | done | |
| Z home command (own) | done | jplacer's addition |

## Nozzle tips

| OpenPnP | Status | jplacer |
|---|---|---|
| Name, pick / place dwell | done | |
| Place blow-off level | done | and the package's pick vacuum and blow-off levels |
| Push and drag (allowed, outside diameter) | done | used by blinds feeders |
| Part dimensions: min / max part diameter, max part height, max pick tolerance | done | and Issues & Solutions' checks of them |
| Part on / part off vacuum sensing (methods, ranges, probing) | done | |
| Tool changer locations, speeds, post actuators | own way | changer steps, a list per tip |
| Template / clones | done | the template's changer steps, moved by the difference of the first moves |
| Runout calibration (circle divisions, misdetects, Z offset, vision diameter, compensation) | partial | measured and compensated; auto recalibration (on tip change, on home) and fail homing missing; to be tried on the bench with the user there |
| Background calibration (HSV, detail size) | done | with runout calibration; Show Problems as one picture of pairs |

## Cameras

| OpenPnP | Status | jplacer |
|---|---|---|
| Name, looking, preview FPS, suspend during tasks, auto camera view, multi-camera view | done | runs while on screen (own way); each camera its own tab, so no multi-camera view |
| Light actuator and when it is on | done | |
| Units per pixel (measure) | own way | from Calibrate |
| Settling (methods, threshold, timeout, debounce, mask, test moves, diagnostics graph) | done | |
| Device settings and properties table | done | |
| White balance (balance, gamma, Overall, Brightest) | partial | Mapped Roughly / Finely and the curve plot missing |
| Position (head offsets, fixed location, safe Z, roaming radius) | done | |
| Lens calibration | own way | fitted by Calibrate |
| Image transforms (rotate, offset, flip, crop, scale, de-interlace) | done | crop and de-interlace, as under OpenPnP's advanced calibration; straightening covers the rest |
| Advanced calibration: settings | partial | grid, reach, outliers, worst fit, two heights; crop size, default working plane Z, detection diameter missing |
| Advanced calibration: results | partial | units per pixel, accuracy, FOV mm and degrees, turn, height, focal length; head offsets and tilt about X / Y missing |
| Advanced calibration: plots | done | in order, X against Y, map |
| Camera view: zoom, reticles (cross, grid, ruler, circle / square), drag / Shift+click to move | done | |
| Camera view: footprint reticle, image info and histogram, light toggle, Estimate Z, Move Selected Nozzle to Camera, Zoom Sensitivity | done | reticles: none, cross, grid, ruler, circle, square |
| Camera view: Rendering Quality | missing | the framework's image drawing has no filtering choice |
| Auto focus (up-looking) | missing | |
| Capture backends (OpenPnpCapture, Webcam, GStreamer, MJPG, ONVIF, Image, Switcher) | partial | V4L2 (OpenPnpCapture, Webcam), MJPG, Image, simulated; GStreamer, ONVIF, Switcher missing |

## Actuators

| OpenPnP | Status | jplacer |
|---|---|---|
| Driver, name, head, offsets | done | |
| Value type (boolean, double, string, profile), actuator profiles | done | |
| Actuation per machine state (enabled, homed, disabled) | done | |
| Machine coordination (before / after actuation, before read) | done | Machine Coordination group, as OpenPnP's |
| Axis interlock | done | |
| HTTP actuators | done | |
| Script actuators | done | the script runs as a program told actuateBoolean, actuateDouble or actuateString |
| Actuators panel (switch, read) | done | |

## Fiducials and boards

| OpenPnP | Status | jplacer |
|---|---|---|
| Fiducial locator: passes, max linear offset, parallax | done | Machine › Fiducials |
| Averaging | done | Fiducal Locator › Average Matches? |
| Tolerances (scaling, shearing, board offset) | own way | stricter fixed limits, plus a fit spread limit |
| Fiducial vision pipeline | own way | round-mark finder, no pipeline to tune |
| Multi-placement manual locate | done | Job tab › Multiple Point Board Location |
| Board location, side, rotation | done | from fiducials |
| Panels (arrays, nested) | done | Panels tab |
| Boards tab (board definitions, placements editing) | done | |

## Parts, packages, vision, feeders, jobs

| OpenPnP | Status |
|---|---|
| Parts (height, speed, package, pick retries) | done |
| Packages (footprint, body, compatible tips, vision) | done |
| Vision settings, pipelines and the pipeline editor | done |
| Bottom vision (pipeline, pre-rotate, size check, max rotation) | done |
| Vision compositing (multi-shot bottom vision, the package's preview) | done |
| Feeders: strip, tray, rotated tray, push-pull, drag, auto, slot auto, lever, heap, loose part, blinds, Schultz, Photon, Rapid, Bamboo | done |
| Job: placements table, start / pause / step / stop, job order, nozzle tip strategy, retries, optimisation | done |
| Importers: KiCad, Eagle (board and mountsmd), Diptrace, Altium, Proteus, named CSV | done (OpenPnP's solder paste Gerber importer is skipped by OpenPnP itself) |

## Order of work

Breadth first, with jplacer's own methods where they are better:

1. Nozzle tips: runout recalibration triggers; part dimensions and push and drag with the job that uses them.
2. Cameras: remaining calibration settings and results (head offsets, tilt); image transforms; preview FPS cap.
3. Actuators: profiles, interlocks.
4. Machine: motion planner settings (continuous motion).
5. View: System Units (inches throughout), languages; Scripting.
