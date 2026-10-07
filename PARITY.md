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
| Machine Setup tree: OpenPnP's order, "Class Name" titles and icons; Feeders (each its Feeders tab page); ReferenceNozzle / ContactProbeNozzle | done | |
| Machine Setup tools: Delete X, Permutate Up / Down, New X..., a nozzle tip's Unload / Load, with OpenPnP's icons and tips; delete asks first | done | per kind as OpenPnP's; a head and a changer step (jplacer's) have them too |
| Home after enabled, Park after homed | done | Machine › Configuration |
| Park all at Safe Z, Auto tool select | done | Machine › Configuration; Jog Z park |
| Unsafe Z Roaming | done | |
| Discard location, Default Board Location | done | |
| Location buttons: Position Camera / Tool / Actuator, Get Coordinates, Position Tool (Without Safe Z), Contact Probe Tool | done | on the rows OpenPnP shows each on |
| Auto-load most recent job | done | on for a new cell; as OpenPnP's for an imported one |
| Simulation Mode's Replace Drivers (OpenPnP's GcodeServer for its G-code controllers; Communications simulated, TCP to GcodeServer) | done | jplacer's own controllers keep a simulated grblHAL |
| Motion planner: continuous motion | done | Machine › Motion Planner; waits where the machine must stand still (actuator coordination, pick and place, homing, each operation's end) |
| Motion Control Type (all seven), OpenPnP's motion profiles and Motion (coordinated, synchronized), interpolation (GcodeAsyncDriver Advanced Settings), Interpolation Retiming, minimum speed and rates | done | OpenPnP's MotionProfile and Motion ported and checked against its AdvancedMotionTest; imported |
| Motion planner: Allow uncoordinated? (uncoordinated motion blending, the path planner) | done | OpenPnP's AbstractMotionPath ported, passing its testMotionPaths; a safe Z sequence planned and blended with continuous motion |
| GcodeAsyncDriver: Confirmation Flow Control?, Location Confirmation? | missing | |
| Motion planner test motion (4 locations) and diagnostics | done | planned time from feed rates and accelerations, actual time, each axis's location and velocity from the controllers' reports |
| Issues & Solutions (guided setup, milestones, auto-fixes) | done | the tab, milestones (any one chosen from a box, beyond OpenPnP's step at a time); Actuator, Axis (rotation, limited articulation, align with part, pre-rotate), Calibration (backlash, camera), Camera (preview, device properties), ContactProbeNozzle, GcodeDriver (generic), Head, HttpActuator, Kinematic, NozzleTip, ReferenceMachine (auto tool select) and Vision (primary calibration fiducial and initial camera calibration, visual homing, rig, tables) solutions, solved on Accept where OpenPnP's are; own way: firmware-specific driver solutions are jplacer's profiles, VisionSolutions' camera-guided calibration is jplacer's own calibration, motion control type and GcodeAsyncDriver conversion are jplacer's movement |
| Issues & Solutions: nozzle solution (Standalone, DualNegated, DualCam, units), NullDriver and camera replacements | done | |
| First start: OpenPnP's default machine, packages, parts and vision settings | done | shipped in openpnp-defaults with OpenPnP's test picture; missing configuration files taken from them |
| Log panel (filterable log) | done | Log tab |
| Signalers (sound, actuator on error / job done, Neoden4Signaler) | done | Neoden4Signaler: the first NeoDen 4 controller's buzzer, beeping until confirmed; its sound ticks are honoured (OpenPnP's ignores them) |
| View: System Units (inches) | done | every length shown and typed in mm or inches (forms, tables, readout, Jog), kept in mm; on restart, as OpenPnP |
| View: Language | done | OpenPnP's translations (ru, es, fr, it, de, zh_CN), applied to whatever jplacer names as OpenPnP does; on restart, as OpenPnP |
| View: Selections in Tables (linked tables) | done | |
| Help: Submit Diagnostics (and the log file it includes) | done | written to a file to attach, not uploaded to Pastebin; no window screenshot or vision debug images |
| Window: Multiple Window Style, Change Appearance (theme, font size, alternating rows) | done | on restart, as OpenPnP |
| Scripting (events, Python/JS scripts, Pool scripting engines, Clear Scripting Engine Pool) | done | the Scripts menu and every OpenPnP event; scripts run as programs of their own, or, pooled, by Python and node interpreters kept for the next (a pooled node script's asynchronous work is not waited for); OpenPnP's object model (machine, config, scripting, gui.jobTab; Location, LengthUnit, UiUtils, Utils2D, VisionUtils.readQrCode, JOptionPane, javax.script) for Python and for JavaScript run as Nashorn runs it (print, load, Packages, JavaImporter, with, for each), CvPipeline run on the head camera with its results and showFilteredImage, so OpenPnP's scripts and its Examples run as they are |
| ContactProbeNozzle (probing pick and place heights, nozzle tip Z calibration) | done | contact sense actuator and vacuum sniffle probing, feeder and placement heights with their triggers, part height probing, Z calibration by touch; discard probing; probed heights kept while jplacer runs (OpenPnP keeps them in its file) |
| Jog panel: feeder take back (Recycle) | done | |
| Jog panel: Safety tab, Board Protection | done | jogs checked against the job's enabled boards, every nozzle and Z actuator on the head |

## Controllers (GcodeDriver / GcodeAsyncDriver)

| OpenPnP | Status | jplacer |
|---|---|---|
| Serial port settings, DTR / RTS, line endings | done | |
| TCP communications | done | |
| Keep Alive | done | per controller: Disconnect leaves its connection open, Connect takes it up as it is; imported; Issues & Solutions' warning |
| Sync Initial Location, Allow Unhomed Motion | done | unhomed jogs and moves allowed per controller, refused with OpenPnP's words; imported |
| Firmware detection, generic G-code proposal | done | firmware profiles with `auto` detection: Grbl, grblHAL, Generic, and OpenPnP's Smoothieware, Marlin, RepRapFirmware (Duet) and TinyG set up as its GcodeDriverSolutions proposes; its firmware issues (Smoothieware PnP build and PAXIS, RepRapFirmware 3.3, Marlin rotation axes, unknown firmware) from the M115 reply |
| Command timeout, connect wait, max feed rate, log G-code | done | |
| Compress G-code, remove comments, backslash escapes | done | Driver Settings |
| Send feed rate, acceleration, jerk on change only | done | Driver Settings |
| Units (millimetres or inches) | done | Driver Settings |
| Letter variables off, pre-move commands | done | Driver Settings; a Pre-Move Command per axis |
| Gcode tab: every command, per head-mountable | done | per controller; empty uses the profile's |
| Gcode tab: Import / Export (Export Gcode File, Copy Gcode to Clipboard) | done | the controller's settings as jplacer keeps them (JSON); OpenPnP's Load, Paste and Reset are not on its form |
| Driver settings: $-Command Wait Time, Detect Firmware (and the firmware's answer) | done | Detect Firmware asks a connected controller only |
| Gcode console (send to a controller, Force Upper Case, history) | done | each driver's Console tab in Machine Setup, as OpenPnP's; and the Console dock, choosing the controller |
| Confirmation flow control, location confirmation | own way | the driver waits for each `ok` and reads status reports |
| Interpolation (max steps, jerk steps, min step time) | own way | as above: the controller plans the motion |
| Console | done | Console dock |
| NeoDen4Driver (home, moves, air, lights, rails, feeders; Home Coordinate and Scale Factor), NeoDen4FeederActuator (Change Feeder ID) | done | Communications Type neoden4 with the neoden4 profile: the driver's lines done in the NeoDen's binary protocol; the actuators by OpenPnP's names; Change Feeder ID updates the actuator's Feeder ID; a Blow actuator switched off sets no air (OpenPnP's does nothing) |
| NullDriver (simulated controller), old single-driver machine.xml migration | done | jplacer's simulated controller, axes given letters; an old `<driver>` NullDriver migrated as OpenPnP's load does |

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
| Linear transform axis | done | any linear transform (inputs X/Y/Z/Rotation, factors, offset), the move's linear axes solved onto their inputs as OpenPnP inverts its affine transform; non-squareness kept as jplacer's squareness (measured from board fiducials; Square the Machine) |
| Cam axes (clockwise / counter-clockwise, shared Z) | done | a cam axis kind, clockwise or not |
| Simulation Mode (SimulationModeMachine) | done | mode, Replace Drivers?, runout and phase, non-squareness, camera lag and noise, vibration, homing error, Set Machine Table Z, Reset Feeders, Pick & Place Checking (on the image camera's picture, OpenPnP's tolerances), imported |
| Switch linear / rotational | done | and the feed rate as G-code reads F (linear path, else rotational) |

## Head

| OpenPnP | Status | jplacer |
|---|---|---|
| Homing fiducial, Visual Test, Visual Home | done | |
| Park location | done | |
| Calibration rig: Primary Fiducial, Secondary Fiducial, Test Object, each with its Diameter (OpenPnP's layout and tips) | done | fiducials used by two-height calibration; the test object by a nozzle's Calibrate Precise Offsets |
| Z probe actuator | done | Capture Camera Location probes Z |
| Pump: actuator, control mode, on-wait | done | OpenPnP's tips on Pump Control and Pump On Wait |

## Nozzles

| OpenPnP | Status | jplacer |
|---|---|---|
| Name, axes, offsets | done | |
| Rotation mode (absolute part angle, ...), align with part | done | the four modes as OpenPnP's rotation mode offset on the nozzle's rotation (moves and readout), set at pick, gone with the part; Align with Part adds bottom vision's turn |
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
| Part on / part off vacuum sensing (methods, ranges, probing, Establish Level, Perform Checks, last readings, vacuum graph) | done | |
| Tool changer locations, speeds, post actuators | done | OpenPnP's form (First…Last Location, speeds 1↔2…3↔4, Post 1–3 Actuators) over the tip's loading steps; steps of jplacer's own beyond it in the tree |
| Template / clones | done | the template's changer steps, moved by the difference of the first moves |
| Clone options (Locations?, Z Calibration?, Vision Calibration?), Calibrate all Touch Locations' Z to Template | done | the touch location cloned with the places |
| Touch Location, Auto Z Calibration (trigger, calibrated offset, Reset, Calibrate now), Fail Homing? | done | OpenPnP's row and tips; Fail Homing? shown when not Manual |
| Tool changer Vision Calibration (template images empty / occupied, trigger, Z adjust, test; Vision Calibration? on cloning) | done | the slot offset is worked out again after jplacer starts (OpenPnP keeps it in its configuration) |
| Runout calibration pipeline (Edit / Reset, nozzleTip properties) | done | OpenPnP's default, imported; the find refined to a fraction of a pixel |
| Runout calibration (circle divisions, misdetects, offset threshold, Z offset, vision diameter, compensation, Position Tool) | done | measured and compensated; Auto Recalibration on tip change, in jobs and on homing, with Fail Homing; to be tried on the bench with the user there |
| Runout compensation algorithms (Model, ModelAffine, ModelNoOffset, ModelNoOffsetAffine, ModelCameraOffset, ModelCameraOffsetAffine, Table; the camera offset for its nozzle) | done | imported with OpenPnP's migration and what OpenPnP measured; chosen on the tip's Calibration tab (OpenPnP has no field for it) |
| Runout: Calibrate Camera Position and Rotation | done | Affine or circle fit, as the algorithm; the turn into jplacer's calibration (else the picture's rotation, as OpenPnP); Excenter Ratio imported; done by the bottom camera's Calibrate, not on the tip's tab (OpenPnP hides it there with Advanced Calibration, as jplacer's cameras always have) |
| Nozzle offsets in Issues & Solutions (offsets for the primary/secondary fiducial; Calibrate precise camera ↔ nozzle offsets with Feature diameter, Auto-Detect Next, results) | done | plus Capture Test Object Z (jplacer's own: a test object thicker than paper) |
| Background calibration (HSV, detail size; HsvIndicator wheel and value bar, diagnostics beside it; adaptDialog greying) | done | with runout calibration; Show Problems as one picture of pairs |

## Cameras

| OpenPnP | Status | jplacer |
|---|---|---|
| Name, looking, preview FPS, suspend during tasks, auto camera view, multi-camera view | done | runs while on screen (own way); each camera its own tab, so no multi-camera view |
| Light actuator and when it is on | done | |
| Units per pixel (measure) | own way | from Calibrate |
| Settling (methods incl. Motion, threshold, timeout, debounce, mask, colour, edges, contrast, denoise, test moves incl. Rotate and Up, diagnostics graph and replay) | done | |
| Show in multi camera view? | done | a camera window of its own each: off, it starts closed |
| OpenPnpCaptureCamera: properties with Min/Max/Default, Freeze Properties, Reapply to Camera, Capture FPS test | done | properties ticked are always set on opening (frozen) |
| Device settings and properties table | done | |
| White balance (balance, gamma, Overall, Brightest, Mapped Roughly / Finely, curve plot) | done | OpenPnP's sliders (percent) and tips, the graph live; imported from OpenPnP too |
| Position (head offsets, fixed location, safe Z, roaming radius) | done | |
| Lens calibration | own way | fitted by Calibrate |
| Image transforms (rotate, offset, flip, crop, scale, de-interlace) | done | all of OpenPnP's, in its order, before calibration (calibrate after changing them); imported unless OpenPnP's advanced calibration overrides them |
| Advanced calibration: pipeline (DetectCircularSymmetry on OpenCV) | done | OpenPnP's default, imported, editable from the page (Edit / Reset Pipeline; OpenPnP has no button for it); the find refined to a fraction of a pixel |
| Advanced calibration: settings | done | grid, reach, outliers, worst fit, two heights; General Settings (deinterlace, cropped width/height, Default Working Plane Z, the scale taken there); detection diameter: the mark's size is measured from its first find (and checked against the head's mark), never asked for |
| Advanced calibration: results | done | units per pixel, accuracy, FOV mm and degrees, turn, height, focal length; Camera Mounting Error about X, Y and Z, and Calibrated Head Offsets / Camera Location at the Default Working Plane Z (from where the picture's middle looked at the two heights; the lean between heights applied) |
| Advanced calibration: plots | done | in order, X against Y, map |
| Camera view: zoom, reticles (cross, grid, ruler, circle / square), drag / Shift+click to move | done | |
| Camera view: footprint reticle, image info and histogram, light toggle, Estimate Z, Move Selected Nozzle to Camera, Zoom Sensitivity | done | reticles: none, cross, grid, ruler, circle, square |
| Camera view: Rendering Quality | done | Low (sharp pixels), High (smoothed), Highest (best scale: whole-number scale, zoom by 2); per camera |
| Camera view: capture error picture (no image: dark grey, red X top left); camera calibration's Cancel | done | the X also over a live uncalibrated camera (jplacer's, can be turned off), not during its own calibration; Cancel a red X beside Calibrate, for every camera task, with the step (pass, move) on the camera's line rather than an instructions box |
| Auto focus (up-looking) | done | Focus Sensing Method, the Auto Focus tab, part height by focus in bottom vision |
| Capture backends (OpenPnpCapture, Webcam, OpenCv, GStreamer, MJPG, ONVIF, Image, Switcher, Neoden4Camera, Neoden4SwitcherCamera) | done | V4L2 (OpenPnpCapture, Webcam, OpenCvCamera by its index and OpenCV properties), MJPG, Image, Switcher, ONVIF, GStreamer (the system's gst-launch-1.0), NeoDen 4 (libneodencam.so loaded at run time; Width, Height and Shift set on the camera, as OpenPnP's fields suggest though its code leaves them), simulated |
| ImageCamera: Camera Simulation (pixel dimension, units per pixel, offset, Z and Y rotation, viewing scale, distortion, mirrored, source with Browse) and Simulated Calibration Rig (focal length, sensor diagonal, primary and secondary fiducials, focal blur) | done | |
| SimulatedUpCamera: Camera Location, Pixel Dimension, Simulated Units per Pixel, Focal Length, Sensor Diagonal (perspective and shade), Background Scenario, Pick Error Offsets, View mirrored?, Simulate Focal Blur? | done | the nozzle tip drawn at its tip's diameter (OpenPnP's: 1 mm with a bore); a camera looking up sees the machine mirrored (View mirrored? turns it back), as jplacer's calibration expects |

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
| ThermistorToLinearSensorActuator (thermistor, ADC, linear transform) | done | its Transforms tab; R1 kept but not used, as OpenPnP |
| Actuators panel (switch, read) | done | |

## Fiducials and boards

| OpenPnP | Status | jplacer |
|---|---|---|
| Fiducial locator: passes, max linear offset, parallax | done | Machine › Fiducials |
| Averaging | done | Fiducal Locator › Average Matches? |
| Tolerances (scaling, shearing, board offset) | done | OpenPnP's fixed 0.05, 0.05 and 5 mm, plus a fit spread limit |
| Fiducial vision pipeline | done | the fiducial's vision settings' pipeline, as OpenPnP's (fiducial.center, MaskCircle.center, the nearest result) |
| Multi-placement manual locate | done | Job tab › Multiple Point Board Location |
| Board location, side, rotation | done | from fiducials |
| Panels (arrays, nested) | done | Panels tab |
| Boards tab (board definitions, placements editing) | done | |
| Board, panel and job viewers (outlines, origins, fiducials, placements, reticle; right-click Enabled?, Check Fids?, Placed?, Center Camera, Run Fiducial Check) | done | |

## Parts, packages, vision, feeders, jobs

| OpenPnP | Status |
|---|---|
| Parts (height, speed, package, pick retries) | done |
| Packages (footprint, body, compatible tips, vision) | done |
| Vision settings, pipelines and the pipeline editor | done |
| Bottom vision (pipeline, pre-rotate, size check, max rotation, vision offset, pick tolerance; OpenPnP's findOffsets, passing its ReferenceBottomVision tests) | done |
| Vision compositing (multi-shot bottom vision, the package's preview) | done |
| Feeders: strip, tray, rotated tray, push-pull, drag, auto, slot auto, lever, heap, loose part, advanced loose part, tube, blinds, Schultz, slot Schultz, Photon, Rapid, Bamboo | done |
| Job: placements table, start / pause / step / stop, job order, nozzle tip strategy, retries, optimisation, pre-rotation as a subordinate move (gone with the next move at safe Z; OpenPnP's BasicJobTest's moves and actuations, in order, on the cell) | done |
| Importers: KiCad, Eagle (board and mountsmd), Diptrace, Altium, Proteus, named CSV | done (OpenPnP's solder paste Gerber importer is skipped by OpenPnP itself) |

## Order of work

Every area above is done or covered its own way; what is left is checking against OpenPnP itself:

1. Side by side with OpenPnP (run headless), panel by panel: layouts, words, tooltips and icons, put right
   where they differ.
2. On the bench, with real hardware: camera Defaults, then Auto-Tune (and Auto-Tune when homing, jplacer's own), runout calibration, nozzle tip Z
   calibration and Load / Unload from Machine Setup.
