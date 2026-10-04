# jplacer design

How jplacer is organised: the domain model, where data lives, the machine and
job layers, and the GUI. OpenPnP (`reference/openpnp`) is the functional
reference; this document says where jplacer deliberately differs from it.

## What OpenPnP gets wrong (and we fix)

| OpenPnP | Consequence | jplacer |
|---|---|---|
| One global `Configuration` holds every `Part` and `Package`; importers call `cfg.addPart` / `cfg.addPackage` because there is nowhere else to put them | Every board ever imported leaves its parts behind; the library grows into thousands of half-configured entries | Imported parts and packages belong to the job. The library is a separate, curated collection the job draws from |
| Part id is synthesised as `package + "-" + value` (`KicadPosImporter`) | The id is the only link; a typo or a different CAD naming makes a duplicate part | A job part keeps what the CAD file said (footprint, value); linking it to a library component is a separate, visible choice |
| Parts and Packages tabs always list the whole library | When working on a job you scroll past 10,000 things it does not use | The job shows its own parts only. The library is opened on purpose, to find replacements, edit, or add to it |
| Placed / enabled / check-fiducials state lives in `Job` and `BoardLocation` maps beside the definition | Saving a job saves run state; definition and progress are tangled | A job's definition and its run (progress, failures) are separate objects |
| Feeder → part by global id; a job cannot tell you what is loaded for it | The user cross-checks Feeders tab against Job tab by eye | The job's parts list shows which feeder carries each part and flags parts with none |
| Ten peer tabs (Job, Panels, Boards, Parts, Packages, Vision, Feeders, Machine Setup, Issues, Log) | Constant tab hopping; no tab knows the job's context | Three workspaces (Job, Library, Machine) with an inspector that follows the selection |

## Domain vocabulary

- **Package**: the physical body: footprint geometry (body, pads), height
  default, compatible nozzle tips, vision settings.
- **Component**: a buyable thing: value, MPN, supplier numbers (e.g. LCSC
  `C12234`), its package, height, speed, pick retries, vision overrides,
  and its packagings. (OpenPnP calls this a Part.)
- **Packaging**: one way a component comes: tape (width 8/12/16/…, pitch,
  paper / embossed, **rotation of the part in the tape**), tube, tray. A
  component may have several. Rotation in the tape is a fact about the
  packaging, so it is set once in the library, not every time a strip goes
  on a feeder (OpenPnP keeps it on the feeder, where it is easy to forget).
- **Library**: the user's maintained collection of packages and components,
  shared by all jobs. Curated, not a dumping ground.
- **Job part**: a component as one job uses it: the group of placements
  sharing (footprint, value) from the CAD file. It is either *local* (its own
  data, as imported) or *linked* (it uses a library component). Its package is
  likewise local or a library package.
- **Board**: a PCB design: outline, fiducials, placements. Reusable across
  jobs.
- **Placement**: one designator on a board: position, rotation, side, type
  (place / fiducial), the CAD's footprint and value strings, and the job part
  it belongs to.
- **Panel**: an arrangement of board instances (rows × columns, gaps, or
  free), with its own fiducials; panels may nest.
- **Job**: what is on the machine bed: panel / board instances with their
  positions, per-instance enables, the job's parts and local packages, job
  options. Self-contained: it opens and shows correctly with no library.
- **Run**: one execution of a job: per placement instance placed / failed /
  skipped, timings, errors. Kept apart from the job.

### Relations

```
Job (.jpjob)                                         Library (library.db)
  Board instances ── Placement *──1 JobPart ─linked?─► Component *──1 Package
                                      │                     ▲
                                      ├─ local data          │ carries
                                      └─ Package (local or ──┘   Feeder (machine.json)
                                         linked library one)
```

Everything a job screen shows is reached from the job: placement → job part
→ (library component) → package. The rest of the library stays out of sight
until the user goes looking.

### Job parts and the library

Import creates job parts and local packages only; the library is not
touched. Then, per job part, the user (or a remembered rule) decides:

- **Link to a library component**: the job part uses the library's data
  (package, height, vision, speed). Library improvements reach the job.
- **Keep local**: the job part carries its own data. Fine for one-offs.
- **Add to library**: turn a local job part (and its package) into library
  entries and link to them. A deliberate act, never automatic.
- **Detach**: copy a linked component's data into the job and edit it there
  without changing the library.

Swapping a part is the everyday operation. From a job part, **Swap…** offers
candidates from three sources, best first:

1. **On a feeder now**: components loaded on the machine, matching package /
   value. Swapping to one of these needs no reel change.
2. **Library, in stock first**: search pre-filtered to the same package (or
   its aliases) and value; parts marked in stock (with where the reel is
   kept) rank above the rest; free search for alternatives.
3. **Other jobs' local parts**, to reuse a part never added to the library.

**Change packaging…** does the same for how the part arrives. Example:
the library says 8 mm tape, 4 mm pitch, but that is out of stock and this
build uses another source on 12 mm tape, 8 mm pitch. The job part gets a
packaging override for this job only; the run, the lane width and the pick
pitch all follow it. Because the new source is real, the same dialog offers
"also add as a packaging of this part in the library" (with its supplier
number), so next time it is a choice rather than an override.

**Change package** does the same for the footprint: pick a library package,
another local package, or edit the local one. The CAD footprint string is
kept, so the change is visible and reversible.

Choices are remembered in the library as **rules** (`footprint+value →
component`, `footprint → package` alias, e.g. `R_0603_1608Metric → 0603`);
the next import from the same CAD tool links itself, and the user sees which
parts were auto-linked and can undo it.

Feeders hold library components (they outlive any job). So loading a local
job part onto a feeder offers to add it to the library first. Pre-flight
lists job parts with no feeder, parts with no package geometry, and parts
whose nozzle tip is not on the machine.

### Settings cascade

Machine defaults → Package → Component. Every inheritable field shows whether
it is inherited or overridden, and from where; clearing an override returns to
the inherited value. (OpenPnP's `PartSettingsHolder` does this for vision only
and hides which level is in effect.)

## Storage

| What | Where | Why |
|---|---|---|
| Library: packages, footprints, components, link rules, vision settings | `library.db`, SQLite through `JDatabase` | Thousands of rows, searched and filtered, never loaded wholesale |
| Board | `*.jpboard`, JSON | Shared between jobs, diffable |
| Job | `*.jpjob`, JSON; boards by relative path | Holds its job parts and local packages, so it is self-contained; links are library ids |
| Run history | `runs.db`, SQLite | Append-only progress; resume after a crash |
| Cells | `cells/<name>.json`; lines in `lines.json` | Hardware configuration, calibration and feeders, one file per machine; which cells are joined by conveyor |
| Preferences | `JSettings`, keys in `JPlacerSettings` | As now |

Library ids are UUIDs (`JUuid`), never names, so renaming never breaks a job.

## Machine layer

There are thousands of machine designs, so nothing above this layer knows
which one it drives: machines are described by configuration (axes, heads,
nozzles, cameras, feeder banks and slots), not by code per model.

A **cell** is one machine with its own configuration, feeders, lanes and
state (`cells/<name>.json`), on its own thread. jplacer can run **several
cells at once**, e.g. two side-by-side machines with ~80 feeder slots each.
Cells are always independent, even when placing the same job: a job is run
*on* a cell, and its plan is made for that cell's configuration and loaded
feeders, so the same job on two differently configured cells is two plans
and two runs.

Cells joined by a conveyor form a **line** (e.g. two machines, 80 feeders
each, board passed from the first to the second). A line lets one board see
the feeders of every cell on it; each cell still has its own configuration.
A simple job (≤ 80 unique parts) needs one cell; the other cell is free for
another job.

Within a machine, same capability as OpenPnP's `spi` + `machine/reference`:

- `Machine`: owns axes, drivers, heads, machine-mounted cameras and actuators,
  nozzle tips, feeders, signalers. Lifecycle: disconnected → connected →
  homed; simulation mode.
- **Axes**: `ControllerAxis` (a driver letter), `VirtualAxis`, `MappedAxis`,
  `LinearTransformAxis`, `CamAxis` (shared-Z). Soft limits, backlash, safe Z.
- **Head mountables**: `Nozzle`, `Camera`, `Actuator`, each with a head offset
  and axis assignment. A `Location` ↔ raw-axis transform belongs to the axis
  chain, not to the mountable.
- **Drivers**: a cell has any number of controllers (e.g. three 6-axis CNC
  boards: one for X, one for Y and Z, one for the conveyor and feeders).
  Each axis and each actuator names the driver that owns it; nothing above
  the driver layer knows which controller does what. Each driver has its own
  connection and I/O thread, so a slow controller does not stall the others.
- **G-code driver and firmware profiles.** One `GcodeDriver` owns the
  transport (`SerialPort` / TCP / simulated), the command queue, flow
  control and reply matching. What it knows about a particular firmware
  comes from a **firmware profile** plugged into it, not from a subclass:
  - *detect*: identify the firmware and version (e.g. `$I` for Grbl /
    grblHAL, `M115` for Marlin, RepRapFirmware, Smoothieware);
  - *dialect*: the command table (move, home, set position, actuator
    templates, status query) and the patterns for its replies and status
    reports;
  - *settings*: read and write the controller's own configuration (Grbl
    `$$`: steps/mm, max rate, acceleration, travel; Marlin / RRF `M503`),
    shown in the inspector as the controller's values.
  The controller is the single source of truth for what it stores: axis
  feed rate, acceleration and travel limits are read from it, not typed in
  twice (OpenPnP's `GcodeDriverSolutions` detects the firmware via `M115`
  and proposes commands, but never reads the settings back, so the user
  enters them again on each axis). Values the firmware does not hold (jerk,
  safe Z, backlash) are jplacer's.
  The **Generic** profile is a hand-edited command table and no settings
  access, for anything not yet profiled. Profiles are data where possible
  (command tables, reply patterns, setting maps), so adding a firmware or a
  version is mostly a description, not code. `NullDriver` for simulation.
- **Motion**: `MotionPlanner` with jerk-limited profiles, speed factors,
  `moveTo` / `moveToSafeZ`, wait-for-completion. A move that spans several
  controllers is split per driver; the planner gives each part a profile of
  the same duration, so the axes start and finish together and the path
  stays straight, and waits for every driver involved before the next move.
  Homing, enable and emergency stop go to all drivers. This follows
  OpenPnP's `AbstractMotionPlanner`: one planned `Motion` is turned into
  interpolated move commands per driver (`interpolatedMoveToCommands`), and
  unhomed motion is allowed per driver only where that driver permits it.
- **Conveyor**: axes / actuators on whichever driver runs them, plus board
  sensors; a line's hand-off between cells is a conveyor action on each.
- **Nozzles**: pick / place sequence, vacuum part-on / part-off checks,
  tip changer, runout calibration, contact probing.
- **Cameras**: capture (V4L2 first), units-per-pixel, lens calibration,
  rotation / flip, settle, light actuators.
- **Feeders**: one class per type: strip, tray, rotated tray, push-pull, drag,
  tube, auto / slot (banks), loose-part. A feeder holds a component *and the
  packaging it was loaded in*; pick rotation and pitch come from the
  packaging. Strip feeders remember how many parts are left in the strip.
- **Bus feeders** (e.g. Photon on RS-485) speak their own protocol over a
  **pass-through channel**. A channel is provided by a driver (a G-code
  command that carries the packet and returns the reply, such as `M485
  <hex>` → `rs485-reply: <hex>`, described in the firmware profile) or by a
  serial port of its own. The feeder protocol does not know which.
- **Lanes**: hand-loaded positions (e.g. a bank of strip lanes 8 / 12 / 16 mm
  wide) are a pool, not fixed assignments. A lane knows its width and
  geometry; what is in it is decided by the job as it runs (below).

**Threads.** The GUI never touches hardware and never waits on it.

| Thread | Owns | Does |
|---|---|---|
| Main (GUI) | widgets, rendering, input | posts requests to a cell; shows state from events |
| Cell, one per cell | the cell's sequence | runs jobs, moves, homing, calibration steps in order; waits on drivers without blocking anyone else |
| Driver I/O, one per controller | the connection (serial port claimed with `JSerialPort::claim()`, socket, simulator) | writes queued commands, reads every line, matches replies to commands, polls and parses status reports, keeps reading while the cell thread waits |
| Camera, one per camera | the capture device | grabs frames; the latest frame is handed over, never queued |

Requests go down as queued commands that complete later (a callback or a
future the cell thread waits on). State comes up as events (`JSignal`),
re-posted to the main thread with `JMainThreadDispatcher` before any widget
sees them. Position, status and readings are published by the driver
threads as they arrive, so the DRO and vacuum readouts stay live while a
long move or a job runs. A slow or stuck controller stalls only the moves
that need it, and its timeout is reported, not hidden.

**Calibration** (OpenPnP's Issues & Solutions) becomes a checklist in the
Machine workspace: each item checks the machine, explains, and offers its
guided fix.

## Job execution

- `JobPlanner`: produces a **plan**: groups of identical parts in order,
  each assigned to a machine, feeder or lane, packaging, nozzle and tip;
  within a group, travel via nearest-neighbour / TSP. The plan is an object
  the user sees and edits (the planner view below), not something hidden
  inside the runner. Editing it never changes the job's definition.
- **Stages.** A plan is a sequence of stages; each stage runs on one cell
  with one set of loaded feeders. Between stages the board either moves on
  the conveyor to the next cell of a line, or stays put while the feeders
  are changed over for another pass (more unique parts than slots on a
  single cell). The planner chooses the split: parts already loaded stay
  where they are, the fewest changeovers, big or tall parts last. Each
  stage starts with a fiducial check, since the board has moved or been
  touched. A changeover is one prompt with the whole list ("Pass 2: load
  these 31 parts into these slots"), with the same calibration as a single
  load. Load-as-you-go is the same thing at its finest grain: one stage per
  part group.
- `JobRunner`: a state machine on the machine thread: PreFlight → Fiducials
  → [Plan → Pick → Align → Place]* → Finish, with Pause, Step, Stop, Abort.
  Per-placement error policy: retry n, skip, defer to end, stop and ask.
- **Every step can be retried.** A step (pick, align, place, a fiducial)
  starts from what the machine and the run record say, never from state
  left half-done by the step before, so repeating it is always safe. When
  a step fails the job pauses on that step and puts up a requester (as
  OpenPnP's does: its job pauses on an error and Start runs the failed step
  again) with Retry, Skip and Stop; Retry runs the same step again, and the
  run carries on from there. The failure says what actually went wrong: a
  lost or hung camera ("Bottom camera lost: plug it in again, then Retry")
  is reported as itself, never as "part not found", and does not use up the
  step's vision retries (each would fail the same way). A brief drop-out
  never reaches the requester: a task waits the camera's own Lost time for
  it first (`JPCameraConfig::Lost`, `JPCameraLook`).
- Board transform: placement → board instance (position, rotation, bottom-side
  mirror) → panel → fiducial affine correction. One `PlacementTransform` does
  this maths.
- Results are written to the `Run`, never into the job.

### Load as you go (hand-loaded feeders)

For low-volume work with a few lanes and reels on a shelf. The scenario: a
repeat job of ~500 placements, more distinct parts than lanes. In OpenPnP the
user stops, looks for what is unplaced, finds a reel, tells the machine which
feeder holds it, re-enables the placements, and restarts, over and over.

In jplacer the run does the bookkeeping:

1. **Order by groups.** The job is sorted into groups of identical parts by
   a sort rule the user picks and can reorder by drag: e.g. resistors 0402 →
   0603 → 0805, then capacitors, then ICs (smallest / lowest first, so tall
   parts do not get in the nozzle's way). Within a group, travel is optimised.
2. **Place what is loaded.** Groups whose part is already on a feeder run
   first, without asking.
3. **Ask for the next part.** When the next group's part is not loaded, the
   run picks a free lane of the right width (or one whose part is finished,
   and says so) and pauses with one prompt:

   > Load **C12234 (LCSC) 10n 0603 capacitor**, 8 mm paper tape, into
   > **lane 3** (remove 10k 0603 first). Rotation in tape: 90°.
   > [Continue] [Use another lane…] [Skip this part] [Stop]

   The lane LED / camera can point at the lane. No feeder setup, no
   enabling placements, no restart.
4. **Calibrate the strip.** On Continue: auto-calibrate (vision finds the
   sprocket holes and the first part) if enabled for that lane, otherwise
   the camera moves to the lane and the user jogs onto the first part and
   confirms. Pitch and rotation come from the packaging, so nothing else is
   asked.
5. **Place the group**, then go to 3 for the next one.

Skipped parts are listed at the end with a "load and place now" action. If a
strip runs out mid-group, the same prompt asks to top it up. A repeat job
remembers its swaps and order, so the second build is: open, Start, load
when asked.

Fixed feeders (auto feeders, reels that stay on the machine) take part in
the same run; their groups simply never prompt.

**Several boards.** The normal build is one board or one panel per run;
to build several at once, panelise (e.g. 4 × the same board) and every group
is placed on all of them before the next load.

**Swap boards** is a possible later addition, not part of the first design:
place a group on board 1, "Insert board 2 of 5 [Continue]", re-check its
fiducials, place, and so on. It is only worth it if fiducial re-checking is
fast enough not to become the bottleneck, and it carries a real risk:
repeatedly handling a board with hundreds of 0402s sitting in wet paste.

## Vision

OpenPnP's vision works when tuned and stops working when the light changes:
hand-set thresholds, cameras left on auto exposure, pipelines per part to
edit, and a bare "not found" when it fails. jplacer's rule is **no tuning**:

- **The machine says what to look for.** Size from the CAD data or package,
  approximate place from the board's position or the feeder, millimetres per
  pixel from the camera's calibration. A detector takes those, not knobs.
- **Detectors without thresholds** where possible: circular symmetry for
  round marks (bright on dark or dark on bright alike), matching a drawn
  shape of the expected size, edges and fits for refinement to a fraction
  of a pixel. Two methods must agree before an answer is taken.
- **Measure what does not change.** A real board is full of round things
  (holes, vias, pads, round letters, reflections), and shiny copper looks
  different from place to place (straight under the camera it reflects the
  lens, dark in the middle). So the round-mark finder keeps the roundest few
  places, measures each by its EDGE alone (the steepest change of the asked
  polarity along narrow strips, placed at the halfway crossing, a circle
  fitted), accepts an edge round nearly all the way and standing above the
  ground's grain, and prefers the most convincing. The first fiducial is
  looked for widely, the rest close by; each is centred before it is
  measured, where the lens bends nothing.
- **Own the picture.** Exposure, gain and white balance are locked per camera
  and per task; a picture is used only once it is still after a move.
- **Cancel the ambient light.** A frame with the camera's light on, one with
  it off, subtracted: what is left is what jplacer lit, whatever the sun or
  the room is doing. Too much light with the camera's light off is reported,
  not silently failed.
- **Colour as well as brightness.** Chromaticity (how green, not how bright)
  separates a green background or nozzle from a part in any light.
- **Try harder before failing:** another look, another exposure, a wider
  search. Then, rather than tuning, **teach by clicking**: the picture with
  its candidates, one click on the right one, and the mark's look is
  learned for that board or feeder.
- **Every answer shows its working:** the detection drawn on the live
  picture, a confidence, and on failure the picture kept with why.
- **AI where appearance varies** (part present, polarity, empty pockets,
  odd parts, reading reel labels): a model stage (ONNX) finds and
  classifies; classical measurement gives the position. Fiducials, homing
  and calibration stay classical: they need a fraction of a pixel and the
  same answer every time.

Cameras are calibrated by jplacer itself, not imported: scale, rotation,
mirroring and the lens from known moves of the head over a mark (a 5 x 5
grid across the middle of the picture, every move arriving from the same
side); tilt from the calibration rig's two fiducials at two heights, so the
point a camera looks at is computed for the height being looked at. The lens
is a radial bend about its own centre (a small camera's sensor is rarely on
the lens's axis: bench's top camera bends about a point 30 px off the
picture's middle). A picture is used only if it was TAKEN after the move
ended (its capture time, not its arrival: a driver queues a few).
Everything is first proven on a simulated camera that draws the machine
with a hidden scale, rotation, lens and tilt the calibration must recover.

The machine is calibrated by what it can see. Visual homing corrects the
switches' home by the homing mark. A board is square to far better than a
gantry, so a board located by three or more fiducials measures how far the
machine's Y axis leans from square; the cell then works in square
coordinates and each move tells the axes their own (the correction pivots
at the homing mark, which keeps its coordinates). A camera that drops off
or hangs is opened again by its name.

## GUI

Three workspaces, switched from the toolbar; each a `JDockManager` layout the
user can rearrange.

### Principles

- **The job is the centre.** You open a job and everything you need for it is
  one click from it; nothing unrelated is on screen.
- **Plain status, one next step.** Every part says in words what it is
  (*From library*, *This job only*) and what it lacks (*no feeder*, *no
  footprint*, *tip not fitted*), with the button that fixes it beside it.
  The words "linked" and "local" are for this document, not the screen.
- **Select once, see everywhere.** Selecting a part, placement or feeder
  highlights it on the board view, in the lists and in the inspector, and the
  camera can go to it.
- **Ready to run is visible.** The job shows a running count of what stops it
  from running; pre-flight is that same list, not a surprise at Start.
- **Edit where you look.** Fields are edited in the inspector; there are no
  separate configuration tabs to hunt for.
- **Undo** (`JUndoStack`) for every edit to a job or the library.

### Job workspace (the default)

```
┌ Job tree ─────────┬ Board view / Camera ───────────────┬ Inspector ───────┐
│ Job               │ PCB rendered from footprints,      │ the selection:   │
│ ├ Panel 2×3       │ placements coloured by state       │ placement →      │
│ │ └ Board A ×6    │ (to fix, ready, placed, failed)   │  component →     │
│ └ Fiducials       │ click = select; double = move cam  │   package        │
│                   │                                    │ inherited/       │
│                   │                                    │ overridden fields│
├ Parts (this job only)────────────────────────────────── ┤ [Swap…]         │
│ 10k  0603 ×24  RC0603-10K  From library  ✔ feeder S12 │ [Change package…]│
│ 100n ?    ×8   This job only  ⚠ no footprint [Fix…]  │ [Add to library] │
│ 4u7  0805 ×2   GRM21-4u7   From library  ⚠ no feeder  │                  │
├ Placements (filter by part / board / state) ─────────── ┴──────────────────┤
│ Run bar: Start · Pause · Step · Stop   12/180 placed   Log                 │
└────────────────────────────────────────────────────────────────────────────┘
```

- The Parts list is the job's own parts, linked or local; selecting one
  highlights its placements on the board and in the list.
- **Swap…** and **Change package…** open the picker: feeder-loaded
  candidates first, then the library pre-filtered by package and value, with
  free search. It is the only place the wider library shows inside a job.
- Feeders for the job: the machine's feeders filtered to the job's parts,
  with "load onto feeder" from a part.

### Planner view (in the Job workspace)

The plan made visible and editable; the place to prepare a build.

```
┌ Groups (in run order, drag to reorder) ────────┬ Feeders / lanes ───────────┐
│ 1 ✔ 10k 0402 ×42   lane 1 (8mm)    loaded      │ Cell: Bench, bank 1        │
│ 2 ✔ 1k  0402 ×18   lane 2 (8mm)    loaded      │  lane 1 8mm  10k 0402  ✔   │
│ 3 ⏵ 10n 0603 ×36   lane 3 (8mm)    load        │  lane 2 8mm  1k 0402   ✔   │
│ 4 ⏵ 4u7 0805 ×6    lane 4 (12mm)   override    │  lane 3 8mm  (free)        │
│ 5 ✔ STM32 QFP48    tray 1          loaded      │ Bank 2                     │
│ …                                              │  slot 14 …                 │
├ Sort: [package size ▾] [type ▾]   Est. 34 min, 9 loads ───────────────────────┤
└──────────────────────────────────────────────────────────────────────────────┘
```

- Choose a sort rule, then drag groups to adjust; split or merge groups.
- Each group shows where its part comes from and whether a load prompt will
  happen; drag a group onto a feeder or lane to pin it there; pick the
  cell the plan is for.
- Selecting a group highlights its placements on the board view.
- Stages are shown as columns (cell A → conveyor → cell B, or pass 1 →
  changeover → pass 2); drag groups between stages.
- An estimate of time and number of loads updates as the plan changes.
- The plan is saved with the job, so a repeat build starts from it.

### Library workspace (opened on purpose)

Search-first: the list is empty until you type or choose a filter (package
family, "used by open job", "unused", "incomplete"). Package editor with a
footprint preview; component editor; link rules; **where used**
(which boards and jobs reference this). Import and export of library packs.

### Machine workspace

Machine tree (axes, drivers, heads, nozzles, tips, cameras, actuators,
feeders) with the inspector for the selection; camera view; jog panel;
calibration checklist; driver console.

Jog and camera are dock widgets available in every workspace.

## Source layout

```
src/
  app/        JPlacerApp, menus, preferences, settings (as now)
  geometry/   Length, Location, units, PlacementTransform, affine
  library/    Package, Footprint, Component, LinkRule, LibraryStore
  job/        Board, Placement, Panel, Job, JobPart, PartLinker, JobFile
  import/     KiCadPosImporter, CsvImporter, EagleImporter, ... (no library writes)
  machine/    Machine, axes, drivers, motion, Head, Nozzle, NozzleTip, Camera, Actuator
  feeders/    one class per feeder type
  vision/     pipeline, stages, BottomVision, FiducialLocator
  run/        JobPlanner, Plan, JobRunner, Run, RunStore
  ui/         workspaces, BoardView, JobPartsPanel, Inspector, PartPicker, JogPanel, CameraView
```

Model code (`geometry`, `library`, `job`, `import`) has no GUI or hardware
dependency and gets unit tests.

## Build order

Each step ships something usable, with its manual pages. Development and
tests use the simulated controller and camera; real machines are
configurations to check against, never inputs to the design.

1. **Machine basics**: one cell (the model allows several from the start):
   `cells/<name>.json`, Machine workspace (tree, inspector, undo), G-code
   driver with the Generic and Grbl / grblHAL profiles (detect, dialect,
   settings), any number of controllers, axes, connect / home / jog / DRO,
   actuators (on/off, values, readings), camera backends and camera view.
2. **Calibration**: head offsets, units per pixel, lens, nozzle runout; the
   calibration checklist.
3. **Library and job model**: geometry, library store, packagings, board /
   job files, KiCad and CSV import, job parts, linking and swapping. Job and
   Library workspaces with board view.
4. **Feeders and running**: strip lanes and tray feeders first, planner and
   planner view, runner, runs, pre-flight, load-as-you-go, stages.
5. **Vision**: fiducials, bottom alignment, feeder vision.
6. **Breadth**: remaining feeder types and firmware profiles, panels, other
   importers, lines (conveyor hand-off between cells).

## Open questions

- Domain class naming: plain names (`Component`, `Package`) in a `jplacer`
  namespace, or a prefix to stay clear of JFramework's `J*` names.
- OpenCV as a dependency (vision) and V4L2 for capture.
- Import of an existing OpenPnP configuration (parts.xml, packages.xml,
  machine.xml) to migrate users.
