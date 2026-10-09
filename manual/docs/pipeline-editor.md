# Pipeline Editor

Some of OpenPnP's vision is done by a **pipeline**: a list of stages that each do one thing to the
camera's picture (blur it, threshold it, find circles in it, draw what was found) and hand the result to
the next. jplacer runs OpenPnP's pipelines stage for stage and writes them back as OpenPnP does, so a pipeline
brought over from OpenPnP runs here, and one tuned here runs in OpenPnP. Every stage OpenPnP's editor offers can be added and run.

<!-- src: src/pipeline/JPPipeline.cpp (process); src/pipeline/JPStageRegistry.cpp (the stage groups) -->

Everything jplacer finds with a camera is found by a pipeline, as OpenPnP finds it: fiducials, the homing
mark, strip feeders' holes, parts in bottom vision, nozzle tips and calibration marks.

<!-- src: src/tasks/JPAlignRequests.cpp; src/tasks/JPPipelineMarkFinder.cpp; src/tasks/JPFiducialLocator.cpp; src/tasks/JPFeederFeed.cpp; src/tasks/JPRunoutCalibrator.cpp -->

## Opening it

A strip feeder's page has **Edit Pipeline...** and **Reset Pipeline** under **Vision**. **Edit
Pipeline...** opens the editor on the feeder's pipeline (OpenPnP's default strip feeder pipeline when the
feeder has none), with the head camera's picture. **Reset Pipeline** puts the default pipeline back.

<!-- src: src/setup/JPFeederForms.cpp (stripForm); src/tasks/JPFeederPipelines.cpp; src/pipeline/JPDefaultPipelines.cpp; src/app/JPlacerOpenPnpTabs.cpp (pipelineAction) -->

Bottom vision and fiducial settings have it too, under **Pipeline ▸ Edit...** on their pages (the Vision
tab, a part's or a package's): see [Vision](vision.md#the-pipeline).

The editor opens in a window of its own, nine tenths of the main window's size. The pipeline is run once
as it opens, and again after every change, so the result of each stage is always the current one. Its
camera runs, as every camera does while the machine is on, its picture on screen or not.

<!-- src: src/app/JPlacerPipelineEditorDialog.cpp; src/ui/JPPipelineEditor.cpp (process); src/app/JPlacerPipelines.cpp (useCamera); src/ui/JPCameraPanel.cpp (setPowered) -->

## The picture it is given

As in OpenPnP, a pipeline is given the camera's picture corrected, once the camera is calibrated: the
lens's bending taken out and the machine square to the picture, at one scale both ways, centred on the
point the camera looks at, as the camera's straightened view shows it (its **Crop All Invalid Pixels**
the same). Edges straight on the board are straight in it, and what the camera does not see is black.
What a pipeline finds is placed on the machine through that corrected picture. This is so wherever a
pipeline runs: in a job (fiducials, bottom vision, feeders), in a test, from a script and in this editor.
A camera not yet calibrated gives its picture as taken. A camera's own calibration pipeline is the
exception: it measures the lens, so it is given the picture as taken.

<!-- src: src/tasks/JPPipelineCamera.cpp; src/pipeline/JPStraightPicture.cpp; src/tasks/JPCellJobMachine.cpp (headCameraPipeline, cameraPipeline, align); src/tasks/JPPipelineMarkFinder.cpp -->

## The stages

The left side lists the stages in the order they run: **Enabled** (an unticked stage is skipped),
**Name** (double-click to rename; other stages refer to it by this name) and **Stage**, its kind. Drag a
stage up or down to run it at another place. Below the list, the chosen stage's description; below
that, its settings, each with its own description as its tooltip. A setting the feeder or vision
operation sets each time the pipeline runs is shown, not edited, with "Controlled by pipeline caller"
and the value it was given.

<!-- src: src/ui/JPPipelinePanel.cpp (Model, refreshProperties); src/ui/JPTable.cpp (row dragging) -->

| Tool | |
|---|---|
| Update picture from current view | Runs the pipeline again on a new picture from the camera. |
| New stage... | Adds a stage of the kind chosen at the end, named so no other stage has its name. |
| Delete Stage | Removes the chosen stage. |
| Copy pipeline to clipboard | The whole pipeline as OpenPnP's text, for pasting into OpenPnP or another feeder. |
| Create pipeline from clipboard | Replaces the stages with a pipeline copied from OpenPnP or jplacer. |

<!-- src: src/ui/JPPipelinePanel.cpp (newStage, deleteStage, copyPipeline, pastePipeline) -->

## The results

The right side shows a stage's result: its name, how long it took and how long the whole pipeline took;
its picture; and, below, what it found (circles, rectangles, key points…), one to a line. The arrows show
the first, previous, next and last stage. **Pin** keeps showing the pinned stage's result while you
choose others and change their settings. The colour button switches between true colours (a picture in
HSV or HLS shown as it looks) and the picture's numbers shown as if they were blue, green, red.

Over the picture, the line under it gives the colour of the pixel under the mouse (as RGB and as
full-range HSV, the numbers a mask stage wants) and its place in pixels; over something found, what was
found there. For an AffineWarp stage it also gives the place in the stage's length units from the
camera's centre.

When the pipeline stops (no picture from its camera, say), the text below the picture begins with
why; nothing else interrupts you, so holding a setting's arrow keeps changing it.

<!-- src: src/ui/JPPipelineResultsPanel.cpp (update, hover, refresh); src/pipeline/JPStageUtil.h (toRgba); src/ui/JPPipelineEditor.cpp (process) -->

## Closing it

Closed after a change, the editor asks **Save pipeline changes?**: **Yes** keeps them, **No** puts the
pipeline back as it was, **Cancel** goes back to editing with the changes still there.

<!-- src: src/app/JPlacerPipelines.cpp (edit) -->
