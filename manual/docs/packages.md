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

**Vision Compositing** holds how bottom vision puts several pictures of a big part together:
**Method** (None, Restricted, Body, Automatic, SingleCorners), **Extra Shots**, **Max. Pick Tolerance**
(zero: the nozzle tip's), **Min. Angle Leverage**, and **Allow inside corner?**. **Compute** works it out
and shows the result; it comes with bottom vision.

<!-- src: src/ui/JPPackagesPanel.cpp (nozzleTipsTab, settingsTab, footprintTab, generatePads, compositingTab, showFootprint); src/ui/JPFootprintTableModel.cpp; src/model/JPFootprint.cpp (generate); src/model/JPKicadModImporter.cpp; src/ui/JPFootprintOverlay.cpp -->

**Bottom Vision Settings** and **Fiducial Vision Settings** show the vision settings the package uses (its
own, else the machine's), as on the [Vision](vision.md#the-settings) tab. **Specialize for** the package
makes a copy for it alone; **Generalize for** the package takes off the settings of its own of each part
of the package (after saying which), so they use the package's.

<!-- src: src/ui/JPPackagesPanel.cpp (visionTab, visionAct); src/setup/JPVisionForms.cpp (act, specializedIn) -->
