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
| Connect | A controller or a camera still simulated. |
| Basics | An axis without a controller or a letter (set right in the issue), the letter E, two axes of one controller with the same letter; a nozzle without a Z or a rotation axis; nozzles sharing one. |
| Kinematics | The machine not homed (Accept homes it); a Z axis's Safe Z zone invalid, or not set (Accept takes where the nozzle is as its Safe Z); an X or Y axis without soft limits (Accept takes where it is); an axis without a feed rate or acceleration; a nozzle's rotation not wrapping around, or not limited to ±180° (Accept sets it). |
| Vision | A camera settling by a fixed time (Accept sets the adaptive Euclidean method), one not calibrated, one without a white balance. |
| Calibration | A nozzle tip that no nozzle takes. |
| any | A Photon feeder's slot without a location, or without an offset from it. |

Choosing an issue selects its part in Machine Setup, ready there when you open that tab.

<!-- src: src/setup/JPIssueChecks.cpp; src/app/JPlacerMachine.cpp (showSetupNode, changeSetup) -->
