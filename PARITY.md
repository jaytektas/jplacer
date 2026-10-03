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
| Park all at Safe Z, Auto tool select, Unsafe Z Roaming | missing | |
| Discard location | done | |
| Default Board Location | missing | a board starts from Camera Is on It instead |
| Auto-load most recent job | missing | no jobs yet |
| Motion planner: continuous motion, uncoordinated moves, interpolation retiming, minimum speed | missing | moves are sent one at a time, each waited for |
| Motion planner test motion (4 locations) and diagnostics graphs | missing | |
| Issues & Solutions (guided setup, milestones, auto-fixes) | missing | a calibration checklist is planned (DESIGN.md build order 2) |
| Log panel (filterable log) | partial | Console shows controller traffic; the log is a file |
| Signalers (sound, actuator on error / job done) | missing | |
| Scripting (events, Python/JS scripts) | missing | |

## Controllers (GcodeDriver / GcodeAsyncDriver)

| OpenPnP | Status | jplacer |
|---|---|---|
| Serial port settings, DTR / RTS, line endings | done | |
| TCP communications | missing | serial and simulated links only |
| Keep Alive | missing | |
| Firmware detection, generic G-code proposal | own way | firmware profiles (Grbl, grblHAL, Generic) with `auto` detection |
| Command timeout, connect wait, max feed rate, log G-code | done | |
| Units, letter variables, pre-move commands, compress G-code, remove comments, backslash escapes, send rate on change only | missing | profiles decide the dialect |
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
| Backlash calibration ("Calibrate now", with graphs of backlash against speed and sneak-up distance) | done | tolerance from the measuring's own noise; graphs of play by distance, by speed, and errors after |
| Virtual axis | done | |
| Mapped axis (two map points) | done | |
| Linear transform axis (non-squareness) | own way | squareness measured from board fiducials; Square the Machine |
| Cam axes (clockwise / counter-clockwise, shared Z) | missing | |
| Switch linear / rotational | missing | |

## Head

| OpenPnP | Status | jplacer |
|---|---|---|
| Homing fiducial, Visual Test, Visual Home | done | |
| Park location | done | |
| Calibration rig: primary, secondary marks, test object | partial | marks used by two-height calibration; test object unused |
| Z probe actuator | missing | |
| Pump: actuator, control mode, on-wait | done | |

## Nozzles

| OpenPnP | Status | jplacer |
|---|---|---|
| Name, axes, offsets | done | |
| Rotation mode (absolute part angle, ...), align with part | missing | |
| Safe Z, dynamic safe Z | partial | safe Z from the axes' safe zones; dynamic safe Z missing |
| Pick / place dwell | done | |
| Compatible / loaded tips table | done | |
| Vacuum, blow-off, sensing actuators | done | |
| Tool changer enabled, change on manual pick | missing | tips change through each tip's changer steps, on request |
| Nozzle offset wizard | done | |
| Z home command (own) | done | jplacer's addition |

## Nozzle tips

| OpenPnP | Status | jplacer |
|---|---|---|
| Name, pick / place dwell | done | |
| Place blow-off level | missing | |
| Push and drag (allowed, outside diameter) | missing | |
| Part dimensions: min / max part diameter, max part height, max pick tolerance | partial | diameter only |
| Part on / part off vacuum sensing (methods, ranges, probing) | done | |
| Tool changer locations, speeds, post actuators | own way | changer steps, a list per tip |
| Template / clones | missing | |
| Runout calibration (circle divisions, misdetects, Z offset, vision diameter, compensation) | partial | measured and compensated; auto recalibration (on tip change, on home) and fail homing missing; to be tried on the bench with the user there |
| Background calibration (HSV, detail size) | missing | |

## Cameras

| OpenPnP | Status | jplacer |
|---|---|---|
| Name, looking, preview FPS, suspend during tasks, auto camera view, multi-camera view | partial | runs while on screen (own way); FPS cap and auto view missing |
| Light actuator and when it is on | done | |
| Units per pixel (measure) | own way | from Calibrate |
| Settling (methods, threshold, timeout, debounce, mask, test moves, diagnostics graph) | done | |
| Device settings and properties table | done | |
| White balance (balance, gamma, Overall, Brightest) | partial | Mapped Roughly / Finely and the curve plot missing |
| Position (head offsets, fixed location, safe Z, roaming radius) | partial | roaming radius missing |
| Lens calibration | own way | fitted by Calibrate |
| Image transforms (rotate, offset, flip, crop, scale, de-interlace) | partial | straightening covers rotation and lens; crop, flip, scale, de-interlace missing |
| Advanced calibration: settings | partial | grid, reach, outliers, worst fit, two heights; crop size, default working plane Z, detection diameter missing |
| Advanced calibration: results | partial | units per pixel, accuracy, FOV mm and degrees, turn, height, focal length; head offsets and tilt about X / Y missing |
| Advanced calibration: plots | done | in order, X against Y, map |
| Camera view: zoom, reticles (cross, grid, ruler, circle / square), drag / Shift+click to move | done | |
| Camera view: footprint reticle, image info and histogram, light toggle, measure rectangle, Estimate Z | missing | |
| Auto focus (up-looking) | missing | |
| Capture backends (OpenPnpCapture, Webcam, GStreamer, MJPG, ONVIF, Image, Switcher) | partial | V4L2 and simulated |

## Actuators

| OpenPnP | Status | jplacer |
|---|---|---|
| Driver, name, head, offsets | done | |
| Value type (boolean, double, string, profile), actuator profiles | partial | boolean, double and string, set and read; profiles missing |
| Actuation per machine state (enabled, homed, disabled) | done | |
| Machine coordination (before / after actuation, before read) | own way | every actuation and read waits for moves before it to end, and moves wait for it |
| Axis interlock | missing | |
| HTTP, script actuators | missing | |
| Actuators panel (switch, read) | done | |

## Fiducials and boards

| OpenPnP | Status | jplacer |
|---|---|---|
| Fiducial locator: passes, max linear offset, parallax | done | Machine › Fiducials |
| Averaging | missing | |
| Tolerances (scaling, shearing, board offset) | own way | stricter fixed limits, plus a fit spread limit |
| Fiducial vision pipeline | own way | round-mark finder, no pipeline to tune |
| Multi-placement manual locate | missing | |
| Board location, side, rotation | done | from fiducials |
| Panels (arrays, nested) | missing | |
| Boards tab (board definitions, placements editing) | partial | CPL import and list; no editing |

## Parts, packages, vision, feeders, jobs

All in DESIGN.md's build order 3 to 6, none started:

| OpenPnP | Status |
|---|---|
| Parts (height, speed, package, pick retries) | missing |
| Packages (footprint, body, compatible tips, vision) | missing |
| Bottom vision (pipeline, pre-rotate, size check, max rotation) | missing |
| Feeders: strip, tray, rotated tray, push-pull, drag, auto, slot auto, lever, heap, loose part, blinds, Schultz, Photon (Lumen) | missing |
| Job: placements table, start / pause / step / stop, job order, nozzle tip strategy, retries, optimisation | missing |
| Importers: KiCad, Eagle, Diptrace, named CSV, ... | partial | one CSV / POS importer |

## Order of work

Breadth first, with jplacer's own methods where they are better:

1. Nozzle tips: runout recalibration triggers; part dimensions and push and drag with the job that uses them.
2. Cameras: remaining calibration settings and results (head offsets, tilt); image transforms; preview FPS cap.
3. Actuators: profiles, interlocks.
4. Machine: motion planner settings (continuous motion), Unsafe Z roaming, Default Board Location.
5. Then DESIGN.md's build order 3 onward: library and job model, feeders and running, vision.
