# Rotations

A pick-and-place file gives each placement an angle, but that angle is measured from the CAD library's
idea of where the footprint's 0° is, and CAD libraries do not agree (a SOT-23 drawn with pin 1 at the
bottom, say). A package whose 0° differs from the CAD's would put every part of that footprint down
turned 90°, 180° or 270°, and nothing would say so until the board was looked at. The rotation check
finds that before the first part goes down.

<!-- src: src/library/JPRotationCheck.h -->

## The package's turn

Each package has a **Turn** (on its page on the [Parts](jobs.md#editing) panel): what is added to the
imported rotation of every placement using it. Setting it once puts the whole footprint right. A
placement's rotation is the one the file gave plus its package's turn, unless you set one on the
placement itself (see [Rotation](jobs.md#rotation)).

<!-- src: src/library/JPPlacementRotation.cpp (of); src/library/JPPackage.h (turnDeg) -->

## How each package is checked

Each package the job places is checked once, not each placement, the cheapest way that settles it:

1. **Cannot matter**: two pads alike turned half round, on parts with no polarity, told by their
   designators (**R**, **C**, **L**, **FB**). No check is needed.
2. **By the file's pad 1**: where the pick-and-place file gives pad 1's position (EasyEDA's Pad X / Pad Y),
   where the package's footprint puts pad 1 at each placement's rotation is compared with it. Every
   placement agreeing: checked, and nothing moves. Every one off by the same quarter turn: the panel says
   so, and **Set Turn from the File** sets the package's turn by that much. Placements that disagree
   with each other are named, and nothing is set: either the CAD turned those parts on purpose or the
   file is wrong.
3. **By vision**: a footprint that looks different at each quarter turn (a SOT-23, an odd shape) is
   matched against the bare board's copper at the placement's angle and at each further quarter turn,
   the camera over the first of its placements on the side that is up. One angle matching clearly
   better than the others (by 0.1 of a perfect match) is the answer: the package's turn is set to it.
4. **By you, on the board**: pads alike turned half round on a polarised part (a SOIC, a QFP, a diode):
   vision cannot tell pin 1, so you decide (below).

A package stays checked until its footprint or its turn is changed.

<!-- src: src/library/JPRotationCheck.cpp (way, byFile, symmetric, checked); src/tasks/JPRotationLook.cpp (run, kClearMargin); src/app/JPlacerRotations.cpp (checkAll, nextVision) -->

## The Rotations panel

**Job ▸ Rotations** lists each package the job places (fiducials and parts not to be placed are left
out): its footprint, how many placements use it, how its turn can be **Settled**, and whether it is
**Checked** (and how). The page below it holds the rule and the actions.

The rotation check is one rule of the job's checklist, and the job says how it is used:

| Setting | |
|---|---|
| **Check rotations** | The rule itself. Off, nothing is checked. |
| **Skip where it cannot matter** | Packages that cannot matter are marked checked without a look. |
| **By the file's pad 1** | The file's pad 1 is used where it is given. |
| **By vision** | The camera settles what its four angles can. |
| **By you, on the board** | You are asked for the rest. |

A way turned off is passed over, and what it would have settled falls to the next. Each job keeps its
own; [Preferences](preferences.md#new-jobs) holds what a new job starts with (all on).

- **Check Rotations** runs the ways allowed over every package not yet checked: the file at once, then
  the camera, one package after another (the board must be [located](board.md#locating-the-board)). The
  status bar says how many were settled.
- **Check by Vision** runs the camera's check on the chosen package alone.
- **Check on Board…** draws the chosen package's footprint over the head camera's live picture, at the
  first of its placements on the side that is up: each pad outlined, a dot on pin 1. Turn it with
  **Turn 90° Anticlockwise** and **Turn 90° Clockwise** until the dot sits on the board's pin-1 mark,
  and press **Accept**: the package's turn is set to that, and it is checked. **Cancel** leaves it as it
  was.
- **Uncheck** marks the chosen package as not checked again.

The [Parts](jobs.md#the-parts-panel) panel's **Turn Checked** column shows each placement's package's
check.

<!-- src: src/app/JPlacerRotations.cpp; src/job/JPRotationRules.h; src/app/JPlacerBoard.cpp (showFootprint, marks); src/ui/JPCameraView.cpp (outlines) -->
