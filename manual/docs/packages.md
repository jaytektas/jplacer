# Packages

The **Packages** tab (after Parts, as in OpenPnP) lists every package jplacer knows, as OpenPnP's
Packages tab does. Packages are kept in the library (`library.db`, see [Parts](parts.md)); OpenPnP's own
`packages.xml` copied into jplacer's configuration folder brings the packages the library lacks. A package
is the part's body (SOIC-8, R0603, SOT-23); its footprints, the land patterns, are the library's too (see
the **Footprints** tab below).

<!-- src: src/ui/JPPackagesPanel.cpp (footprintsTab); src/app/JPlacerOpenPnpTabs.cpp; src/model/JPConfiguration.h (kPackagesFile); src/model/JPLibraryFootprint.h; src/model/JPConfiguration.cpp (footprintNamed, packageNamed, defaultFootprint, load) -->

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

- **Units** the footprint is in: another unit chosen, its numbers are converted, so it keeps its size
  (OpenPnP keeps the numbers); **Body Width** and **Body Length**.
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

A package chosen, its footprint's pads are drawn over every calibrated camera's picture, centred where the
camera looks and turned by the rotation of the tool chosen in Jog (its C, as OpenPnP's reticles turn), so
a part can be held up to it. The cameras show one footprint, the last chosen: a
package's, or a placement's on the Job tab (choosing a placement chooses its package too when **View ▸
Selections in Tables** is **Linked**, as OpenPnP's). It stays when another tab is shown.

**Footprints** lists the package's footprints in the library: land patterns of it, several to a package
(an R0603's nominal one, a CAD library's). Each has its **Name** (unique in the package), its **CAD
Names**, what CAD files call it, commas between (a board's footprint by one of them is this package's:
`R_0603_1608Metric`), its **Zero Rotation**, how far a CAD tool's 0° is turned from jplacer's (pin 1 top
left), and its pads (how many, and where they came from). **Use as the Package's** makes the package's own
footprint (the Footprint tab, what vision measures the part by) this one's pads and body; **Delete** takes
it out of the library. **From the Package's Footprint** makes one from the package's own, named after the
package; **Import KiCad Footprint…** reads one from a `.kicad_mod` file, named and known by its file's
name. The names boards' files give a footprint are learned too, as parts are chosen (see [Choosing a
part](boards.md#choosing-a-part)). A library from before footprints had its packages' CAD names moved onto
a footprint of each, made from the package's own.

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

<!-- src: src/ui/JPPackagesPanel.cpp (nozzleTipsTab, settingsTab, footprintTab, generatePads, compositingTab, showFootprint); src/ui/JPFootprintTableModel.cpp; src/model/JPFootprint.cpp (generate, inUnits); src/model/JPKicadModImporter.cpp; src/ui/JPFootprintOverlay.cpp; src/app/JPlacerMachine.cpp (selectedToolRotation); src/ui/JPCompositingPreview.cpp; src/tasks/JPVisionComposite.cpp (compute, composeShots, travel, interpret); src/tasks/JPVisionPipelinePrep.cpp (composite, bottom, shot); src/tasks/JPCellJobMachine.cpp (alignPart, alignComposite); src/app/JPlacerOpenPnpTabs.cpp (computeComposite) -->

**Bottom Vision Settings** and **Fiducial Vision Settings** show the vision settings the package uses (its
own, else the machine's), as on the [Vision](vision.md#the-settings) tab. **Specialize for** the package,
shown while it uses the machine's, makes a copy for it alone; **Generalize for** the package, shown while
some of its parts have settings of their own, takes those off each of them (after saying which), so they
use the package's again.

<!-- src: src/ui/JPPackagesPanel.cpp (visionTab, visionAct); src/setup/JPVisionForms.cpp (act, specializedIn) -->

Every change to a package, its footprint and the library's footprints, a package made or deleted, can be
taken back with **Edit ▸ Undo** and made again with **Edit ▸ Redo** (see [Parts](parts.md#undo-and-redo)).

<!-- src: src/setup/JPLibraryHistory.cpp; src/app/JPlacerOpenPnpTabs.cpp (libraryChanged) -->
