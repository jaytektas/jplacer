# jplacer design

How jplacer is organised: the domain model, where data lives, the machine and
job layers, and the GUI. OpenPnP (`reference/openpnp`) is the functional
reference; this document says where jplacer deliberately differs from it.

## What OpenPnP gets wrong (and we fix)

| OpenPnP | Consequence | jplacer |
|---|---|---|
| One global `Configuration` holds every `Part` and `Package`; importers call `cfg.addPart` / `cfg.addPackage` because there is nowhere else to put them | Every board ever imported leaves its parts behind; the library grows into thousands of half-configured entries | Imported parts belong to the board they came with. The library is a separate, curated collection that boards are matched against, and it changes only when the user creates or learns |
| Part id is synthesised as `package + "-" + value` (`KicadPosImporter`) | The id is the only link; a typo or a different CAD naming makes a duplicate part | A board part keeps everything the CPL, BOM and other files said; matching it to a library part (by MPN, supplier PN or AKA) is a separate, visible choice |
| Parts and Packages tabs always list the whole library | When working on a job you scroll past 10,000 things it does not use | The job shows its own parts only. The library is opened on purpose, to find replacements, edit, or add to it |
| Placed / enabled / check-fiducials state lives in `Job` and `BoardLocation` maps beside the definition | Saving a job saves run state; definition and progress are tangled | A job's definition and its run (progress, failures) are separate objects |
| Feeder → part by global id; a job cannot tell you what is loaded for it | The user cross-checks Feeders tab against Job tab by eye | The job's parts list shows which feeder carries each part and flags parts with none |
| Ten peer tabs (Job, Panels, Boards, Parts, Packages, Vision, Feeders, Machine Setup, Issues, Log) | Constant tab hopping; no tab knows the job's context | Three workspaces (Job, Library, Machine) with an inspector that follows the selection |

## Domain vocabulary

- **Library**: the user's maintained collection, shared by every job and by
  each of the user's machines. Curated, never a dumping ground: nothing is
  added to it except by a deliberate act (create, learn).
- **Library part**: a buyable thing. A permanent id (UUID); a **name** that
  need not be unique; description; value (kept normalised as well as typed:
  100n = 100nF = 0.1µF); its package and footprint; height; speed, pick
  retries, vision overrides; its packagings; datasheet (link, and optionally
  a local copy); its **identifiers** and **AKAs** (below); its **supplier
  offers**; attrition. (OpenPnP calls this a Part.)
- **Identifier**: a typed, exact name for a library part: manufacturer + MPN
  (several per part: second sources, alternates), supplier + supplier PN
  (LCSC `C12234`). Manufacturer names have AKAs of their own (TI = Texas
  Instruments = Texas Instruments Inc.).
- **AKA** ("also known as"): a free string a CAD file or BOM may call the
  thing, and which field it matches: the CAD value ("100n"), the CAD
  footprint ("C_0603_1608Metric"), or the two together; where it was learned
  (which board, when). Parts, packages and footprints all have AKAs.
- **Package**: the physical body: body size and height default, compatible
  nozzle tips, bottom vision settings.
- **Footprint**: a land pattern of a package, its own entity (one 0603 body,
  several footprints): pads, pin 1, courtyard, and its **zero rotation** in
  jplacer's convention (IPC-7351: pin 1 top left; how it sits in tape is the
  packaging's). It is what the placement reticle draws and what a
  down-looking check fits against.
- **Packaging**: one way a part comes: tape (width 8/12/16/…, pitch, paper /
  embossed, **rotation of the part in the tape**), tube, tray. Set once in the
  library, not every time a strip goes on a feeder (OpenPnP keeps it on the
  feeder, where it is easy to forget).
- **Supplier offer**: where a library part is bought: supplier, SKU, price
  breaks, MOQ, link, the last price seen.
- **Stock lot**: one physical lot of a library part: a reel, tray, tube or
  bag, its quantity, supplier, date code, where it is kept. **Stock** is
  "do we have it"; it is not "is it loaded".
- **Stock ledger**: every change to a lot as an entry: received (order,
  quantity, cost, date), used by a run, lost to a mis-pick or discard,
  counted and corrected. On hand is the ledger's sum, so a wrong figure shows
  why. Attrition is measured from it, per part and per feeder.
- **Board**: a PCB design and everything needed to build it, in one
  self-contained file: outline, fiducials, placements, its **board parts**,
  and the **import provenance**. It works on a machine whose library has
  never seen it.
  It keeps its **revisions** (rev A, rev B, …), each an upgrade of the one
  before (see Board revisions).
- **Board part**: one line of the board's parts list: a BOM line, or (no
  BOM) the placements sharing CAD value and footprint, or one made by hand.
  It keeps every field it was given, as given (value, footprint, MPN,
  manufacturer, description, supplier PN, any extra column by its own name),
  and its **resolution**:
  - *unmatched*: only what the files said;
  - *matched*: a library part's id, the chosen alternates, and a **copy** of
    that part's package, footprint and settings as they were, with a
    **fingerprint** of the copy;
  - *local*: defined in this board only, on purpose.
- **Placement**: one designator on a board: position, rotation, side, type
  (place / fiducial), do-not-place, the board part it belongs to, and
  **verified**: whether its position and rotation have been checked, and
  by whom (operator, automatic check, position only).
- **Import provenance**: each source file the board was made from (CPL, BOM,
  others), its raw rows, the column mapping used and the profile it came
  from, the importer, the date. Kept, so a mapping can be changed and the
  matching run again without importing again.
- **Mapping profile**: how one CAD tool's or supplier's columns map to
  jplacer's fields, guessed from header synonyms and confirmed once; and
  that source's per-footprint rotation corrections, learned from verifying.
- **Panel**: an arrangement of board instances (rows × columns, gaps, or
  free), with its own fiducials; panels may nest. Carries its boards as
  boards do.
- **Job**: what is on the machine bed: panel / board instances with their
  positions and per-instance enables, carrying **copies** of its boards and
  panels (with a note of each source file, to pull updates from on request),
  job options and the plan. Self-contained: given a job file, it opens and
  runs.
- **Run**: one execution of a job: per placement instance placed / failed /
  skipped, timings, errors, parts used. Kept apart from the job; its parts
  used go to the stock ledger.

### Relations

```
Job (.jpjob)                                   Library (library.db)
  Board copies ── Placement *──1 BoardPart ─matched─► LibraryPart *──1 Package
                                   │  (id + copy +       │  identifiers, AKAs      └─ Footprints
                                   │   fingerprint)      │  offers, packagings
                                   ├─ fields as imported │  StockLot *── ledger
                                   └─ local data         ▲
                                                         │ carries a lot of
                                                    Feeder (cell json)
```

A job runs from its copies, never from the live library: a library edit
cannot change a job behind the user's back. Everything a job screen shows is
reached from the job: placement → board part → its copy (and, matched, the
library part it came from).

### Import

From the bare minimum to as much as the user has:

1. **Sources.** A CPL (placement file) is the minimum: designator, X, Y,
   rotation, side are the fields to count on; anything more is a bonus. A
   BOM, and any other table (a supplier order, a second BOM), can be added
   at import time or later. Without a BOM the user can build the board's
   parts by hand, as in OpenPnP; with one, nothing has to be typed.
2. **Mapping.** Each source's columns are mapped to jplacer's fields with a
   drop-down per column (as LCSC's BOM tool does: a "Provider" column can
   be the Manufacturer): Designator, X, Y, Rotation, Side, Do Not Place;
   Value, Footprint, Package, Description, Manufacturer, MPN, Supplier,
   Supplier PN, Height, Datasheet, Quantity; or **keep as extra** under its
   own name. Guessed from the headers (a synonym list), confirmed, and saved
   as a mapping profile, so the next file from the same tool maps itself.
3. **Join.** BOM rows name their designators ("R1, R2, R5-R8"); ranges are
   expanded and joined to the CPL. What does not fit is listed, not guessed:
   designators in one file and not the other; fields the files disagree on
   (CPL 10k, BOM 10k 1%), with which source wins chosen per field or row.
4. **Board parts.** Grouped by manufacturer + MPN where given, else by value
   + footprint. Nothing is thrown away: every field each board part was
   given stays on it; every source's raw rows stay in the provenance.
5. **Match**, then **approve** (below).

Importing touches only the board. The library changes only when the user
creates or learns.

### Matching

Each board part is matched against the library, strongest evidence first:

1. manufacturer + MPN exact (MPN alone when the manufacturer is missing,
   ranked lower);
2. supplier PN exact;
3. an AKA on value + footprint;
4. normalised value + a footprint AKA;
5. a footprint AKA alone; description or name, loosely.

Every candidate says why it matched and from which source ("MPN, from the
BOM"); evidence that contradicts (the MPN's part is 0402, the CPL's
footprint 0603) lowers it and is shown. With only the bare CPL, tiers 3–5
still work.

The **matching wizard** is a row per board part: a single strong candidate
chosen already, several ("5 match") left to choose from, with the smart
filter for a free search. Per row:

- **Use** a library part; **use and learn** (its CAD strings become AKAs,
  its MPN an identifier, so the next import matches by itself; shown, and
  undoable);
- **choose alternates**: approved substitutes (a second source, another
  brand of the same 0603); the run uses whichever is loaded;
- **create in library from this**: the new part filled from everything the
  board part has (MPN, manufacturer, value, description, height,
  datasheet), its package and footprint by footprint AKA, or created too;
- **keep local**; **leave unmatched** for now.

The same picker, for one board part, is what the placements' Part field
opens: changing a part changes it for all its placements. Changing the part
of one placement alone (a value swap on R7, a do-not-place) gives that
placement a board part of its own.

**Differs from library.** On opening a board or job, each matched board
part's fingerprint is compared with the library part now: "3 parts differ
from your library", each shown side by side, with *update the board*, *update
the library from the board*, or *keep both*, per item.

### Verifying placements

The footprint's zero rotation and the mapping profile's corrections give a
placement its first rotation; **the placement's own rotation is the truth**
from then on.

- **Step-through** (the existing tool): the camera goes to each placement and
  draws the footprint as a reticle. Stepping on marks it verified by the
  operator; a correction made there is saved to the placement, and offered
  to the mapping profile ("all C_0603 from this tool: +90°?").
- **Automatic check, looking down** (optional, before a run): the
  footprint's pads drawn at the placement's position and rotation, fitted
  against the board in the camera (bare or pasted, the pipeline tuned for
  which) by the same vision used to look up at parts. It verifies position
  for anything of more than one pad, and rotation where the pad pattern is
  not symmetric (SOT-23, connectors). For symmetric patterns (two-pad parts,
  SOIC, QFP, QFN) polarity needs a pin 1 pad that differs or a silkscreen
  mark the camera can see; without one the placement is marked *position
  only*, and those are what the operator steps through.
- Later, the same looking down at placed parts (the body, its polarity mark)
  checks the result.

### Board revisions

Boards are revised; a new revision is an **upgrade** of the board, never a
start from scratch. The new revision's files (CPL, BOM, …) are imported
against the board as it is, through the same mapping profiles, and
everything already decided carries over wherever it still holds, so only
what changed asks for attention.

1. **Pair the placements.** By designator first. Then the leftovers
   (in the old revision only, in the new only) are paired by footprint and
   position, which catches **renumbering** (R12 is now R15): a pair found so
   is a rename and keeps everything.
2. **Find the board's own move.** When most placements moved by the same
   offset or turn, the CAD origin moved, not the parts: that is one change
   ("origin moved 2.00, -1.50 mm"), applied to all, not 300 "moved".
3. **Sort what is left.** Each placement is one of:
   - *unchanged*: kept as it was, verified stays verified;
   - *moved* or *turned* (in the CAD): its part kept, its verified mark
     cleared, its rotation correction carried over (see below);
   - *part changed* (another value or MPN, same footprint): matched again
     (learned AKAs and identifiers usually do it), its position and
     verified mark kept;
   - *footprint changed*: matched again and verified again;
   - *new*: as any import; *removed*: listed, then taken out (a board part
     left with no placements is dropped from the board, never from the
     library).
4. **Board parts** follow their placements: a BOM line whose fields are the
   same keeps its resolution, alternates and decisions; a changed one is
   matched again, its old match offered first.

**Corrections are kept as differences from the CAD**, not as absolute
values: a placement's rotation correction is its verified rotation less the
CAD's. A part turned in the CAD by 90° in the new revision keeps its
correction and lands right; only its verified mark asks to be checked.

The upgrade shows one summary to work from: *182 unchanged, 4 moved (to
verify), 2 parts changed (matched), 1 new part (to match), 3 renamed, 1
removed*, each count opening its list. The data check before a run then
names exactly the placements and parts the revision left to do.

The board file keeps its **revisions**: each with its label (rev A, rev B,
from the user or the file name), its provenance and its placements and
parts, complete, so any of them can be opened, compared or built.

**Switching revisions.** A job (and the board view) has a revision chooser:
the board in the job is any of its revisions, newer or older, switched
either way at any time but during a run. Each revision keeps its own
decisions, so going back to rev A gives rev A exactly as it was left, and
forward again rev B as it was. A placement has an identity that lasts
across revisions (given when the upgrade pairs it), so work done in one
revision is offered to the others where it still holds: a rotation
corrected or a part matched on rev B, for a placement unchanged since rev A,
is applied to rev A too (shown, and undoable); where the placement differs
it stays with its own revision. Switching re-plans the job: groups whose
parts are the same keep their order and feeders, and the job's data check
names what the revision chosen has left to do. A run records the revision
it built, so its history and the parts it used stay true after a switch.

### Ready to run

Two different things:

- **The job's data** is checked before a run, as one list that names what to
  do: board parts unmatched (and not local on purpose), placements not
  verified, parts with no height, no footprint or no vision settings, no
  compatible nozzle tip on the machine.
- **Material is the feeders'.** A part does not have to be loaded to start:
  the run places what the loaded feeders hold and asks for the rest as it
  reaches them (load as you go), and a feeder running out asks the same way.
  A machine that cannot hold every part at once is normal. Stock answers
  "do we have it, and enough with attrition" in the job's shortage list
  (with where each lot is kept and, short, the supplier offers); it never
  stops a run from starting.

A job line reads, for example: *needs 40 + 3 attrition; 3,000 in stock on 2
reels; reel A in feeder 7, 220 left*. A feeder carries a stock lot, so it
knows what is left and warns before it runs out ("feeder 7: about 15
placements left").

### Settings cascade

Machine defaults → Package → Library part → board part copy. Every
inheritable field shows whether it is inherited or overridden, and from
where; clearing an override returns to the inherited value. (OpenPnP's
`PartSettingsHolder` does this for vision only and hides which level is in
effect.)

## Storage

| What | Where | Why |
|---|---|---|
| Library: parts, identifiers, AKAs, packages, footprints, packagings, offers, stock lots and ledger, mapping profiles | `library.db`, SQLite through `JDatabase`; one per user | Thousands of rows and a growing ledger, searched and filtered, never loaded wholesale |
| Board | `*.jpboard`, JSON | Self-contained (board parts, copies, provenance); diffable; drops into any job |
| Job | `*.jpjob`, JSON | Carries copies of its boards and panels (each with its source file noted) and the plan; opens and runs anywhere |
| Run history | `runs.db`, SQLite | Append-only progress; resume after a crash; parts used, for the ledger |
| Cells | `cells/<name>.json`; lines in `lines.json` | Hardware configuration, calibration and feeders (each carrying a stock lot), one file per machine |
| Preferences | `JSettings`, keys in `JPlacerSettings` | As now |

A board file, in outline (a job carries boards in this shape):

```json
{
  "format": "jplacer-board", "version": 1,
  "name": "Controller rev B", "outline": { "...": "..." }, "fiducials": [ ],
  "parts": [
    { "key": "bp-3", "fields": { "value": "100n", "footprint": "C_0603_1608Metric",
                                 "manufacturer": "Samsung", "mpn": "CL10B104KB8NNNC",
                                 "supplierPn": "C1591", "extra": { "Voltage": "50V" } },
      "resolution": { "state": "matched", "libraryId": "5f0c…", "alternates": [ "a91e…" ],
                      "copy": { "part": { }, "package": { }, "footprint": { } },
                      "fingerprint": "sha256:…", "matchedBy": "mpn (BOM)" } }
  ],
  "placements": [
    { "designator": "C12", "x": 23.41, "y": 11.05, "rotation": 90, "side": "top",
      "part": "bp-3", "doNotPlace": false,
      "verified": { "by": "operator", "when": "2026-10-08T11:20:00" } }
  ],
  "provenance": [
    { "role": "cpl", "file": "ctrl-top-pos.csv", "importer": "KiCad", "when": "…",
      "profile": "KiCad 8 pos", "mapping": { "Ref": "designator", "PosX": "x", "…": "…" },
      "rows": [ [ "C12", "100n", "C_0603_1608Metric", "23.41", "11.05", "90", "top" ] ] },
    { "role": "bom", "file": "ctrl-bom.csv", "mapping": { "Provider": "manufacturer", "…": "…" },
      "rows": [ ] }
  ]
}
```

Library ids are UUIDs (`JUuid`), never names, so renaming never breaks a
board, a job or a feeder. OpenPnP's jobs, boards, `parts.xml` and
`packages.xml` are imported through the same mapping and matching (its part
ids become AKAs of the library parts made from them); jplacer does not write
OpenPnP's formats.

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
  tube, auto / slot (banks), loose-part. A feeder holds a library part's
  **stock lot** *and the packaging it was loaded in*; pick rotation and pitch
  come from the packaging. It counts the lot down as it feeds (the ledger),
  so it knows how many are left and warns before it runs out.
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
  (*From library*, *This board only*, *Not matched*, *Differs from library*)
  and what it lacks (*no footprint*, *not verified*, *tip not fitted*,
  *short: 12*), with the button that fixes it beside it. The words "matched"
  and "local" are for this document, not the screen.
- **Select once, see everywhere.** Selecting a part, placement or feeder
  highlights it on the board view, in the lists and in the inspector, and the
  camera can go to it.
- **Ready to run is visible.** The job shows a running count of what in its
  data stops it from running; pre-flight is that same list, not a surprise at
  Start. Parts not yet loaded are not on it: the feeders ask for them as the
  run reaches them.
- **Edit where you look.** Fields are edited in the inspector; there are no
  separate configuration tabs to hunt for.
- **Undo** (`JUndoStack`) for every edit to a job or the library.

### Job workspace (the default)

```
┌ Job tree ─────────┬ Board view / Camera ───────────────┬ Inspector ───────┐
│ Job               │ PCB rendered from footprints,      │ the selection:   │
│ ├ Panel 2×3       │ placements coloured by state       │ placement →      │
│ │ └ Board A ×6    │ (to fix, ready, placed, failed)   │  board part →    │
│ └ Fiducials       │ click = select; double = move cam  │   library part   │
│                   │                                    │ inherited/       │
│                   │                                    │ overridden fields│
├ Parts (the boards' own)──────────────────────────────── ┤ [Choose part…]  │
│ 10k  0603 ×24  RC0603-10K  From library  stock 3000 S12 │ [Alternates…]   │
│ 100n ?    ×8   Not matched  ⚠ no footprint  [Match…]   │ [Add to library] │
│ 4u7  0805 ×2   GRM21-4u7   Differs from library [Review]│                  │
├ Placements (filter by part / board / state) ─────────── ┴──────────────────┤
│ Run bar: Start · Pause · Step · Stop   12/180 placed   Log                 │
└────────────────────────────────────────────────────────────────────────────┘
```

- The Parts list is the boards' own parts (matched, local or not matched);
  selecting one highlights its placements on the board and in the list.
- **Choose part…** (and the Part field of a placement) opens the picker for
  that board part: the matcher's candidates with why each matched, then the
  library with the smart filter, in-stock and loaded parts ranked first. It
  is the only place the wider library shows inside a job.
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
footprint preview; part editor (identifiers, AKAs, offers, datasheet);
stock lots and their ledger; mapping profiles; **where used** (which boards
and jobs were matched to this). Import and export of library packs.

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
  library/    LibraryPart, Identifier, Aka, Package, Footprint, Packaging, Offer, StockLot, Ledger, LibraryStore
  job/        Board, Placement, BoardPart, Panel, Job, JobFile, PartMatcher
  import/     sources (CPL, BOM, tables), MappingProfile, the join; KiCad, CSV, Eagle, ... (no library writes)
  machine/    Machine, axes, drivers, motion, Head, Nozzle, NozzleTip, Camera, Actuator
  feeders/    one class per feeder type
  vision/     pipeline, stages, BottomVision, FiducialLocator
  run/        JobPlanner, Plan, JobRunner, Run, RunStore
  ui/         workspaces, BoardView, BoardPartsPanel, Inspector, PartPicker, MatchingWizard, JogPanel, CameraView
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
3. **Library and job model**, in stages that each leave the app working (the
   OpenPnP model it grew from is replaced, not run beside it; until
   `library.db`, `parts.xml` / `packages.xml` are the library):
   1. *Board parts and `.jpboard`* (done): a board's own parts list, each
      placement naming one; importers add nothing to the library; boards
      saved as JSON, OpenPnP's read and never written over.
   2. *CPL + BOM import* (done): sources, column mapping and profiles, the
      join, provenance. Tables as text (CSV, TSV, KiCad's .pos); spreadsheets
      (.xlsx) to follow.
   3. *The part picker / matcher* (done) for a board part, in place of the
      Part combo; the matching wizard (Board's Parts). Learning (a choice's
      CAD strings kept as AKAs) waits for `library.db`.
   4. *`library.db`* (done, in part): the library in SQLite, migrated from
      `parts.xml` / `packages.xml`; part identifiers and AKAs, package AKAs,
      learning, Add to Library; matched board parts carry copies and
      fingerprints, reviewed in Board's Parts; packagings, supplier offers,
      manufacturers' names; footprints as their own entity (land patterns of
      a package, CAD names, zero rotation), carried in board copies. The
      zero rotation is kept, not yet applied to imported rotations.
   5. *Verifying, revisions, stock*: verified marks, board revisions and
      switching, stock lots and the ledger, the looking-down check.
   Then the Job and Library workspaces with the board view.
4. **Feeders and running**: strip lanes and tray feeders first, planner and
   planner view, runner, runs, pre-flight, load-as-you-go, stages.
5. **Vision**: fiducials, bottom alignment, feeder vision.
6. **Breadth**: remaining feeder types and firmware profiles, panels, other
   importers, lines (conveyor hand-off between cells).

## Open questions

- Decided with defaults, open to change: one library per user (shared by the
  user's machines); footprints separate from packages; a job carries copies
  of its boards (source noted); OpenPnP imported, not exported.
- Online part data (LCSC, Octopart and the like) as one more import source,
  filling package, height and datasheet from an MPN: later, not first.
- Domain class naming: plain names (`Component`, `Package`) in a `jplacer`
  namespace, or a prefix to stay clear of JFramework's `J*` names.
- OpenCV as a dependency (vision) and V4L2 for capture.
- Import of an existing OpenPnP configuration (parts.xml, packages.xml,
  machine.xml) to migrate users.
