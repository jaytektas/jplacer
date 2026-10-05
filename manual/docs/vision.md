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
(see [Running the job](jobs.md#running-the-job)).

**Test Alignment** aligns the part on the nozzle chosen on the Jog panel over the camera looking up, as a
job would at the **Placement Angle** (the machine's test alignment angle), and shows what it found on the
camera ("R1 | X:0.012 Y:-0.034 C:0.512 Δ:0.036"); with **Center After Test** it then moves the part over
the camera's centre, turned to the angle. The nozzle must hold a part (picked with **Pick** on the Feeders
or Parts tab, or by a job), of the part or package the page is for, whose bottom vision these settings
are; else it says what is wrong ("Nozzle N1 does not have a part loaded"). **Detect Offsets** is for an
asymmetric part: centre it over the camera by hand first; it aligns and centres the part at 0°, and adds
the difference to the **Vision Center Offsets**.

**Fiducial Vision Settings** add the **Fiducial Locator**: **Max. Vision Passes**, **Max. Linear Offset**,
**Parallax Diameter** and **Parallax Angle**. A fiducial check uses them (see [Jobs](jobs.md#running-the-job)):
the fiducial is looked at again, centred, up to the passes, until a look moves it less than the max
linear offset; with a parallax diameter, it is looked at from either side of it, that far apart and
turned by the angle, and the middle taken. Settings not enabled stop the check. **Test Fiducial Locator**
finds the fiducial nearest where the head camera is (the part's or package's footprint; on the Vision
tab, a round 1 mm fiducial) as a fiducial check would, and moves the camera onto it.

A part uses its own settings, else its package's, else the machine's (OpenPnP's part alignment and
fiducial locator, brought in with an OpenPnP machine).

## The pipeline

Both kinds keep OpenPnP's vision pipeline with them (OpenPnP's stock pipeline for the kind until they
have one of their own). The **Pipeline** row works it as OpenPnP's does: **Edit...** opens it in the
[Pipeline Editor](pipeline-editor.md); **Reset** puts back the machine's default settings' pipeline (or,
for the machine's default settings themselves, the stock one), after asking; the copy button puts the
pipeline on the clipboard as OpenPnP's text, and the paste button replaces it with one from the
clipboard, after asking.

<!-- src: src/setup/JPVisionForms.cpp (pipelineControls); src/ui/JPVisionPipelineActions.cpp; src/setup/JPVisionPipelines.cpp -->

Under it, a slider for each of the pipeline's parameters (its **ParameterNumeric** and **ParameterBool**
stages: a threshold, a least detail size, a search distance), named and explained as the pipeline names
them. The value is kept with these settings, not in the pipeline, so settings that share a pipeline can
each tune it. Moving a slider runs the pipeline on the camera and shows the stage the parameter affects,
then the result, on the camera's view for three seconds each, with the parameter's name and value over
them ("Threshold = 204").

<!-- src: src/pipeline/JPPipelineParameter.cpp; src/pipeline/JPPipelineAssignments.cpp; src/app/JPlacerPipelines.cpp (previewVision); src/ui/JPCameraView.cpp (showPicture) -->

The pipeline is run as OpenPnP prepares it: a fiducial's on the head camera, with its package's footprint
(on the Vision tab, a round 1 mm fiducial); bottom vision's on the camera looking up, with the package of
the part or package the page is for (on the Vision tab, the part chosen on the Parts tab, else the package
chosen on the Packages tab) turned by the machine's test alignment angle, over the camera's centre.

<!-- src: src/tasks/JPVisionPipelinePrep.cpp; src/app/JPlacerPipelines.cpp (prepared) -->

<!-- src: src/setup/JPVisionForms.cpp; src/tasks/JPJobProcessor.cpp (align); src/tasks/JPAlignRequests.cpp; src/tasks/JPFiducialLocator.cpp (FiducialLook, lookFor); src/app/JPlacerJobMachine.cpp (locateFiducial); src/app/JPlacerVisionTests.cpp; src/app/JPlacerMachine.cpp (nozzlePart); src/model/JPConfiguration.cpp (inheritedVision); src/machine/JPVisionConfig.h -->
