# Issues & Solutions

The **Issues & Solutions** tab, as OpenPnP's, lists what is wrong with, or could be better in, the
machine's setup, each with what to do about it. The machine is set up a **milestone** at a time —
Welcome, Connect, Basics, Kinematics, Vision, Calibration, Production, Advanced — and only what belongs
to the target milestone and those before it is checked.

<!-- src: src/setup/JPSolutions.cpp; src/ui/JPIssuesPanel.cpp -->

**Find Issues & Solutions** checks again (it is also done once when jplacer starts). Beside it, the
target **Milestone** and what it is for; the ⓘ button opens OpenPnP's wiki page about it. **Include
Solved?** and **Include Dismissed?** show the issues solved or dismissed before; otherwise they stay away.

The table lists each issue's **Subject** (what it is about), **Severity** (Information, Suggestion,
Warning, Error, Fundamental; fundamentals first), **Issue**, **Solution** and **State** (Open, Solved,
Dismissed). The chosen issue is shown in full under it, with what it offers to set and its choices:

| | |
|---|---|
| Accept | The solution done (one issue at a time). Some issues only explain: they cannot be accepted. |
| Dismiss | Put away: not shown again (and a solution just accepted undone). |
| Reopen | Back to open (a solution just accepted undone). |
| ⓘ | The issue's page in OpenPnP's wiki. |

After each round, a reminder says to search again: one solution often brings out the next issue.

The last issue is always **Complete milestone**: accepting it goes on to the next milestone (when issues
are still open, you are asked first) or, choosing so, back to the previous one. The milestone and what was
solved or dismissed are kept between sessions.

<!-- src: src/setup/JPSolutions.cpp (find, setState, addMilestoneIssue); src/app/JPlacerOpenPnpTabs.cpp (the settings kept) -->

## What is checked

jplacer checks its own Machine Setup where OpenPnP checks its drivers' settings:

| Milestone | Checked |
|---|---|
| any | Machine Setup's own problems: a part naming another that is not there. |
| Welcome | A head without nozzles. |
| Connect | A controller or a camera still simulated. As OpenPnP's GcodeDriverSolutions: a Grbl or grblHAL controller on a serial port with flow control (Accept turns it off). |
| Basics | An axis without a controller or a letter (set right in the issue), the letter E, two axes of one controller with the same letter; a nozzle without a Z or a rotation axis; nozzles sharing one. The actuators, as OpenPnP's ActuatorSolutions: each nozzle's vacuum valve (and blow off, when it has one; and, when a nozzle tip senses the vacuum, something to read it), the head's pump control (and Z probe, when it has one), each camera's light (and a switcher camera's switcher): one assigned, a controller for it (unless HTTP or a script works it), and the commands for what it is asked to do (switch, set, read), typed in the issue and set on Accept; a profile actuator's own actuators each. A controller allowing pre-move commands, or else not using letter variables (Accept changes it). As OpenPnP's HeadSolutions: on a head, a nozzle (an error), an actuator or another camera on other X or Y axes than the head's first camera (Accept assigns the camera's). |
| Kinematics | The machine not homed (Accept homes it); a Z axis's Safe Z zone invalid, or not set (Accept takes where the nozzle is as its Safe Z); an X or Y axis without soft limits (Accept takes where it is); an axis without a feed rate or acceleration; a nozzle's rotation not wrapping around, or not limited to ±180° (Accept sets it). A controller's Maximum Feed Rate limiting the axes (Accept removes it). As OpenPnP's KinematicSolutions: each nozzle's Safe Z, dynamic (lifted by the part's height) or fixed, chosen in the issue; a Safe Z above 2 mm (unconventional); with dynamic Safe Z, a compatible tip's Max. Part Height taking the nozzle past its Safe Z zone. As OpenPnP's NozzleTipSolutions: a nozzle without a manual tip change location (Accept takes where the nozzle is). |
| Vision | A camera settling by a fixed time (Accept sets the adaptive Euclidean method), one not calibrated (Accept calibrates it, as its Calibrate button does), one without a white balance. As OpenPnP's VisionSolutions: a head without visual homing (Accept finds the round mark under its camera, makes it the homing mark, its place and width, and turns visual homing on: jog the camera over it first), the calibration rig's two fiducials less than 2 mm apart in Z, and the head's first nozzle's Safe Z not above a rig fiducial. As OpenPnP's CameraSolutions: a Preview FPS over 15 (Accept sets 5), a preview not suspended during tasks (an error for a switcher camera; Accept suspends it), Auto Camera View off (Accept turns it on), and Rendering Quality Low (Accept sets High). |
| Calibration | A nozzle tip that no nozzle takes. As OpenPnP's CalibrationSolutions: each head's X and Y backlash, once its camera is calibrated and its homing mark set (Accept measures it with the camera over the mark, and keeps what it finds). Work on the machine that cannot start makes Accept fail, saying why; work that fails later leaves the issue open again. A nozzle tip without a background calibration method: chosen in the issue (Brightness and Key-Color, or Brightness), then Accept calibrates the tip on the nozzle it is loaded on. |
| Production | The tables not linked between tabs (Accept sets View > Selections in Tables to Linked), as OpenPnP's VisionSolutions. |
| Advanced | G-code not compressed, comments not removed (Accept turns each on); before Advanced, the other way, offered only (never counted as open). |
| any | A Photon feeder's slot without a location, or without an offset from it. |

Choosing an issue selects its part in Machine Setup, ready there when you open that tab.

<!-- src: src/setup/JPIssueChecks.cpp; src/app/JPlacerMachine.cpp (showSetupNode, changeSetup) -->
