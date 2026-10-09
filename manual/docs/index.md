# jplacer

jplacer controls pick-and-place machines: the machines that pick electronic parts from feeders and place
them on a circuit board. It works as OpenPnP does and with OpenPnP's own files: a machine set up in OpenPnP
is brought in as it is, jobs, boards, panels, parts and packages open from OpenPnP's files and are saved
back to them, and parts and fiducials are found by OpenPnP's vision pipelines.

<!-- src: src/openpnp/JPOpenPnpMachineImporter.cpp; src/model/JPConfiguration.cpp (load); src/pipeline/JPPipeline.cpp -->

## Getting a machine going

1. [Getting started](getting-started.md): download jplacer, run it, and find it in your applications menu.
2. [Bring in your machine](machine.md#bringing-in-a-machine-set-up-in-openpnp) from OpenPnP, or start from
   OpenPnP's simulated default machine to try things out.
3. [Connect](machine.md#connecting) and [home](machine.md#homing) it.
4. Work through [Issues & Solutions](issues.md), a milestone at a time: it says what is not set up yet,
   from the controllers to the cameras' calibration and the nozzles' offsets, and sets most of it up with
   you. [Machine Setup](machine-setup.md) holds every setting it touches.
5. Set up the [feeders](feeders.md), and the [parts](parts.md), [packages](packages.md) and
   [vision settings](vision.md) they need.
6. Open or make a [job](jobs.md), check its boards' fiducials, and run it.

<!-- src: src/app/JPlacerMachine.cpp (importOpenPnp, startWithDefault, connect, home); src/setup/JPIssueChecks.cpp (Milestone) -->

## The chapters

- [Getting started](getting-started.md): installing, the applications menu, where settings are kept,
  starting from a terminal.
- [Menus](menus.md): what each menu holds, and its keys.
- [Machine](machine.md): cells, bringing in an OpenPnP machine, connecting, homing, the Machine, Jog,
  Actuators and Console panels, and the cameras.
- [Machine Setup](machine-setup.md): every part of the machine and how it is set up.
- [Jobs](jobs.md): jobs, their boards and panels, fiducial checks, and running a job.
- [Panels](panels.md) and [Boards](boards.md): panels, boards and their placements.
- [Parts](parts.md) and [Packages](packages.md): the parts library, stock, and packages' footprints.
- [Vision](vision.md): bottom vision and fiducial settings, and testing them.
- [Pipeline Editor](pipeline-editor.md): OpenPnP's vision pipelines and their stages.
- [Feeders](feeders.md): every kind of feeder, and setting a strip up with Auto Setup.
- [Issues & Solutions](issues.md): what is not set up yet, and what to do about it.
- [Log](log.md): what jplacer says it is doing.
- [Preferences](preferences.md): every setting, its keys and its jog steps.
- [Updates and betas](updates.md): how jplacer keeps itself up to date.
- [What's new](whats-new.md): the changes in each version.

<!-- src: manual/mkdocs.yml (nav) -->
