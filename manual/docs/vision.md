# Vision

The **Vision** tab (in the work area, after Packages, as in OpenPnP) lists the vision settings parts,
packages and the machine use: **Bottom Vision Settings** (how a part on the nozzle is looked at from
below) and **Fiducial Vision Settings** (how a fiducial is found). They are kept in `vision-settings.xml`
in jplacer's configuration folder, each exactly as OpenPnP keeps it, pipeline and all: OpenPnP's file can
be copied there as it is.

<!-- src: src/ui/JPVisionSettingsPanel.cpp; src/model/JPVisionSettings.cpp; src/model/JPConfiguration.cpp (save, kVisionFile); src/app/JPlacerOpenPnpTabs.cpp (the Vision dock) -->

## The toolbar and table

| Button | |
|---|---|
| **New Settings** (plus) | Makes new settings of the type shown, named after their kind, and chooses them. |
| **Delete Settings** (cross) | Deletes the chosen settings, after asking. Settings in use are not deleted: it says what uses them. |
| **Copy Vision Settings to Clipboard** | Puts the chosen settings on the clipboard as text. |
| **Create Vision Settings from Clipboard** | Makes settings from what the clipboard holds, each with a new id; a name already used gets " (Copy)". |

**Type**, at the right, chooses which are shown: **BottomVision** or **FiducialVision**.

The table has **Name** (changed in place, except the stock settings', whose id says "Stock") and
**Assigned To**: what uses them. That is the stock settings themselves, **Bottom Vision** or **Fiducal
Locator** for the machine's default (as OpenPnP spells it), then the packages, then the parts, by ID.

<!-- src: src/ui/JPVisionSettingsPanel.cpp (newSettings, deleteSettings, copySettings, pasteSettings, usedIn); src/ui/JPVisionSettingsTableModel.cpp; src/model/JPConfiguration.cpp (visionUsedIn) -->

## The settings

Under the table, the chosen settings' page. **General**: **Name**, **Assigned to**, **Enabled?**, and
**Reset to Default**, which makes them as the stock settings of their kind (keeping their name), after
asking. **Specialize** and **Generalize** are for settings shown from a part or package, so they are
greyed here.

**Bottom Vision Settings** add **Pre-rotate** (Default, AlwaysOn, AlwaysOff), **Rotation** (Adjust, Full),
**Part size check** (Disabled, BodySize, PadExtents) with its **Size tolerance (%)**, and **Vision Offsets**:
**Asymmetric?** (the contacts are off the part's centre by design) and the **Vision Center Offsets**.
A job uses **Enabled?**, **Pre-rotate** (Default: as the machine's bottom vision says) and **Rotation**
(**Adjust**: within the machine's max angular offset; **Full**: all the way round). jplacer finds the part
by its footprint, so the size check and the vision offsets are kept as OpenPnP wrote them but not needed
(see [Running the job](jobs.md#running-the-job)). **Edit Pipeline**, **Test Alignment** and **Detect
Offsets** are not yet available.

**Fiducial Vision Settings** add the **Fiducial Locator**: **Max. Vision Passes**, **Max. Linear Offset**,
**Parallax Diameter** and **Parallax Angle**. A fiducial check uses them (see [Jobs](jobs.md#running-the-job)):
the fiducial is looked at again, centred, up to the passes, until a look moves it less than the max
linear offset; with a parallax diameter, it is looked at from either side of it, that far apart and
turned by the angle, and the middle taken. Settings not enabled stop the check. **Test Fiducial Locator**
is not yet available.

A part uses its own settings, else its package's, else the machine's (OpenPnP's part alignment and
fiducial locator, brought in with an OpenPnP machine).

<!-- src: src/setup/JPVisionForms.cpp; src/tasks/JPJobProcessor.cpp (align); src/tasks/JPFiducialLocator.cpp (FiducialLook); src/app/JPlacerJobMachine.cpp (locateFiducial); src/model/JPConfiguration.cpp (inheritedVision); src/machine/JPVisionConfig.h -->
