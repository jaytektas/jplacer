# Packages

The **Packages** tab (after Parts, as in OpenPnP) lists every package jplacer knows, as OpenPnP's
Packages tab does. Packages are kept in `packages.xml` in jplacer's configuration folder; OpenPnP's own
`packages.xml` can be copied there as it is.

<!-- src: src/ui/JPPackagesPanel.cpp; src/app/JPlacerOpenPnpTabs.cpp; src/model/JPConfiguration.h (kPackagesFile) -->

## The toolbar and the table

| Button | |
|---|---|
| **New Package...** (plus) | Asks for the new package's ID and makes it. An ID already used is refused, and asked for again. |
| **Delete Package** (cross) | Deletes the chosen packages, after asking. A package a part uses is not deleted: it says which part. |
| **Copy Package to Clipboard** | Puts the chosen package on the clipboard as text. |
| **Create Package from Clipboard** | Asks for an ID and makes a package from what the clipboard holds. |

**Search** works as on the [Parts](parts.md) tab, and the table sorts, chooses and edits the same way.

| Column | |
|---|---|
| **ID** | The package's ID. |
| **Description** | What it is. |
| **Tape Specification** | Text some feeders read; see the feeder's own notes. |
| **BottomVision**, **FiducialVision** | The vision settings it uses, chosen from a list. |

<!-- src: src/ui/JPPackagesPanel.cpp (newPackage, deletePackages, copyPackage, pastePackage); src/ui/JPPackagesTableModel.cpp -->

## The package's tabs

Under the table, the chosen package's tabs.

**Nozzle Tips** lists the machine's nozzle tips; tick **Compatible** beside each tip that can pick the
package.

**Settings** holds its **Vacuum & Blow Off**: the **Vacuum Level** a pick must reach and the **Blow Off
Level** for placing.

**Footprint** holds the footprint's **Settings** and its **Pads**:

- **Units** the footprint is in; **Body Width** and **Body Length**.
- **Generate** makes the pads from the numbers beside it: **Dual** (two rows; **Pad count** a multiple of
  2), **Quad** (four sides; a multiple of 4), **BGA** (a square grid; a square number, those within the
  **Inside dimension** left out), or **KiCad**, read from a KiCad footprint file (`.kicad_mod`): its SMD
  pads on the top copper. **Outside dimension** is the footprint's overall width (Dual) or width and
  height (Quad); **Pad pitch** the distance between pads; **Pad Across** a pad's size along the pitch;
  **% Roundness** how rounded the pads are, in percent of their smaller side (negative: only the inner
  side). Pads already there are deleted first, after asking.
- **Pads** lists each pad: **Name**, **Mark** (O on the pad marked as pin 1 or the cathode), **X**, **Y**,
  **Width**, **Length**, **Rot.** and **% Round**, each changed in the table. The buttons above add a pad
  (asking its name), delete the chosen one (after asking), and move the mark to the chosen pad (or take
  it off).

While the Packages tab shows and a package is chosen, its footprint is drawn over every calibrated
camera's picture, centred where the camera looks, so a part can be held up to it.

**Vision Compositing** is how bottom vision sees a part too big for one picture, as OpenPnP's: in
several shots, each of a few of the part's corners, put together into its centre, angle and size.
**Method**: None (always one shot), Restricted (several only when the part is too big for the camera
or not symmetric), Body (the body's corners, not the pads'), Automatic (always several) or
SingleCorners (each corner a shot of its own). **Extra Shots**: how many of the optional shots are
taken too. **Max. Pick Tolerance**: how far off the part may be picked (zero: the nozzle tip's).
**Min. Angle Leverage**: how far apart, as a share of the part's size, corners must be to give its
angle. **Allow inside corner?**: corners facing the part's centre may be used too. The camera's
Roaming Radius (Machine Setup) limits how far the part may be carried over it; without one, there is
one shot.

The tab works it out when shown, when its settings or the footprint change, and on **Compute**, with
the camera looking up and the first nozzle tip that can take the package; the line beside Compute
says the solution (Square, Box, Z, Arrow, Figure7, Angle, Trapezoid, or Small for one shot) and the
fewest and most shots, or why there is none. The picture under it shows the footprint and each shot:
needed shots in red, optional ones in blue, each with its two mask circles and its corners' edges.
With the mouse over a shot, it alone in yellow, what the camera sees there and the roaming radius
round it; pressed, the pads as compositing joins them. A solution that cannot work is crossed out.

In a job, a part whose solution is one of corners is aligned shot by shot: the nozzle takes each
shot's corners over the camera (without going up to safe Z within the roaming radius), the pipeline
finds the corners there, and what the shots found is put together.

<!-- src: src/ui/JPPackagesPanel.cpp (nozzleTipsTab, settingsTab, footprintTab, generatePads, compositingTab, showFootprint); src/ui/JPFootprintTableModel.cpp; src/model/JPFootprint.cpp (generate); src/model/JPKicadModImporter.cpp; src/ui/JPFootprintOverlay.cpp; src/ui/JPCompositingPreview.cpp; src/tasks/JPVisionComposite.cpp (compute, composeShots, travel, interpret); src/tasks/JPVisionPipelinePrep.cpp (composite, bottom, shot); src/app/JPlacerJobMachine.cpp (alignPart, alignComposite); src/app/JPlacerOpenPnpTabs.cpp (computeComposite) -->

**Bottom Vision Settings** and **Fiducial Vision Settings** show the vision settings the package uses (its
own, else the machine's), as on the [Vision](vision.md#the-settings) tab. **Specialize for** the package
makes a copy for it alone; **Generalize for** the package takes off the settings of its own of each part
of the package (after saying which), so they use the package's.

<!-- src: src/ui/JPPackagesPanel.cpp (visionTab, visionAct); src/setup/JPVisionForms.cpp (act, specializedIn) -->
