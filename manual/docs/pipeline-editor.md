# Pipeline Editor

Some of OpenPnP's vision is done by a **pipeline**: a list of stages that each do one thing to the
camera's picture (blur it, threshold it, find circles in it, draw what was found) and hand the result to
the next. jplacer runs OpenPnP's pipelines stage for stage and writes them back as OpenPnP does, so a pipeline
brought over from OpenPnP runs here, and one tuned here runs in OpenPnP. Every stage OpenPnP's editor offers can be added and run.

<!-- src: src/pipeline/JPPipeline.cpp (process); src/pipeline/JPStageRegistry.cpp (the stage groups) -->

jplacer's own finders (fiducials, strip feeder holes, bottom vision) do not use pipelines and need no
tuning. A pipeline is used where a feeder or setting says so.

## Opening it

A strip feeder's page has **Edit Pipeline...** and **Reset Pipeline** under **Vision**. **Edit
Pipeline...** opens the editor on the feeder's pipeline (OpenPnP's default strip feeder pipeline when the
feeder has none), with the head camera's picture. **Reset Pipeline** puts the default pipeline back.

<!-- src: src/setup/JPFeederForms.cpp (stripForm); src/tasks/JPFeederPipelines.cpp; src/pipeline/JPDefaultPipelines.cpp; src/app/JPlacerOpenPnpTabs.cpp (pipelineAction) -->

The editor opens in a window of its own, nine tenths of the main window's size. The pipeline is run once
as it opens, and again after every change, so the result of each stage is always the current one.

<!-- src: src/app/JPlacerPipelineEditorDialog.cpp; src/ui/JPPipelineEditor.cpp (process) -->

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

<!-- src: src/ui/JPPipelineResultsPanel.cpp (update, hover); src/pipeline/JPStageUtil.h (toRgba) -->

## Closing it

Closed after a change, the editor asks **Save pipeline changes?**: **Yes** keeps them, **No** puts the
pipeline back as it was, **Cancel** goes back to editing with the changes still there.

<!-- src: src/app/JPlacerPipelines.cpp (edit) -->
