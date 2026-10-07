# Preferences

**Edit ▸ Preferences…** opens the settings, on three tabs: **General**, **Keys** and **Jog**. Each one
takes effect and is saved the moment you change it (a text box when you press Return or Tab, or leave it;
Escape puts back what it had), so there is no Apply button: close the window with **Close** when you are
done. The window can be made bigger by dragging its edge.

<!-- src: src/app/JPlacerPreferencesDialog.cpp (each row stores and saves on change) -->

## General

### Appearance

**Theme**
:   **Dark** (as jplacer is first installed), **Light**, or **As the desktop is set**: dark or light as
    the desktop's own setting is when jplacer opens or the theme is chosen (GNOME's colour scheme, or
    Windows' app mode); dark when the desktop does not say.

**Interface scale**
:   How big the whole interface is: its controls and the text in them grow together, and the panels
    make room, so nothing is magnified or blurred. **As the screen asks** (as first installed) follows
    the screen's pixel density; or choose 100 % to 200 %.

<!-- src: src/app/JPlacerAppearance.cpp (themes, scales, applyTheme, applyScale, desktopPrefersDark); src/app/JPlacerSettings.cpp (theme and uiScale default to 0); src/app/JPlacerPreferencesDialog.cpp (the Appearance rows); src/app/JPlacerApp.cpp (applied at start) -->

### General

**Tear-off menus (drag a menu off into its own window)**
:   When ticked, a menu can be dragged off the menu bar into a small window of its own that stays open,
    which is handy for a menu you use often. Off when jplacer is first installed. The change applies
    the next time you open a menu.

**Show jplacer in the applications menu**
:   When ticked, jplacer keeps an entry for itself in your desktop's applications menu (see
    [Getting started](getting-started.md#your-applications-menu)). Unticking it removes the entry and
    its icon. On when jplacer is first installed. This row only appears when jplacer is running as an
    AppImage.

<!-- src: src/app/JPlacerSettings.cpp (tearOffMenus defaults to false, launcher to true); src/app/JPlacerPreferencesDialog.cpp (the General rows); src/app/JPlacerLauncher.cpp (supported) -->

### Backups

**Backups kept**
:   Each time jplacer starts, before it changes anything, it copies its settings (`jplacer.json`) and every
    machine (the `cells` folder) into `backups/<date and time>/` beside them, a rolling set: past this
    many, the oldest copy is let go. 20 when jplacer is first installed; 0 takes none. The log says where
    each copy went. To go back to one, copy its files over those beside the backups folder while jplacer
    is closed.

<!-- src: src/common/JPBackups.cpp; src/app/JPlacerApp.cpp (the backup at start); src/app/JPlacerPreferencesDialog.cpp (Backups); src/app/JPlacerSettings.h (kBackupsKept) -->

### Debugging

**Save vision pictures for debugging**
:   What OpenPnP does at its Debug log level. While ticked, every vision pipeline run (bottom vision, fiducials,
    feeders, a camera's or a nozzle tip's calibration) keeps each of its stages' pictures in a folder of its
    own, `log/vision/<date and time>_<what it was for>/` beside jplacer's settings, numbered in the stages'
    order (`01_<stage>.png`, `02_<stage>.png`…), with `stages.txt` saying each stage's class, how long it took
    and what it found. A pipeline's **ImageWriteDebug** stages write too, into
    `org.openpnp.vision.pipeline.stages.ImageWriteDebug/` there, as OpenPnP's do. It fills the disk quickly:
    tick it while looking into a problem, then untick it. Off when jplacer is first installed.

<!-- src: src/pipeline/JPVisionDebug.cpp; src/pipeline/JPPipeline.cpp (process); src/pipeline/JPStagesImage.cpp (ImageWriteDebug); src/app/JPlacerPreferencesDialog.cpp (Debugging); src/app/JPlacerApp.cpp; src/app/JPlacerSettings.h (kVisionDebug) -->

### Updates

**Check for updates when jplacer opens**
:   When ticked, jplacer looks for a newer version each time it starts, and tells you only if it finds
    one. On when jplacer is first installed.

**Include beta versions**
:   When ticked, you are offered beta versions as well as full releases. Beta versions get new features
    first and have had less testing. Off when jplacer is first installed.

**Check Now**
:   Closes Preferences and looks for a newer version straight away, as **Help ▸ Check For Updates…**
    does.

<!-- src: src/app/JPlacerSettings.cpp (updatesAtStartup defaults to true, updatesBeta to false); src/app/JPlacerPreferencesDialog.cpp (the Updates rows, Check Now) -->

More about how updates work is in [Updates and betas](updates.md).

## Keys

Every function that can be given a key is listed, by menu: **File**, **Edit**, **Machine**, **Jog** (the
[Jog panel](machine.md#jog)'s buttons), **Help**, and the Jog panel's steps (**Jog Distance**, **Jog
Speed**; see [Jog](#jog) below). Each has a box showing its key, or **None**.

- **Click a function's box**, and it reads "Press a key...": press the key, with Ctrl, Alt or Shift if you
  want them. Escape leaves the key as it was. A plain key (an arrow, a digit, a letter) can be given, as
  well as one with Ctrl, Alt or Shift; Tab cannot, as it moves between fields.
- **A key belongs to one function.** Given to another, it is taken from the one that had it, and the line
  under the list says so.
- **Clear** takes a function's key off. **Reset All** puts every function's own key back.

The keys start as OpenPnP's: Ctrl+Right / Left / Up / Down for X and Y, Ctrl+' and Ctrl+/ for Z, Ctrl+,
and Ctrl+. to turn, Ctrl+= and Ctrl+- for the distance, Ctrl+Shift+P to park, and so on, with Ctrl+Z and
Ctrl+Y for Undo and Redo, Ctrl+H to home, and Escape to stop. **Emergency Stop** has no key until you give
it one, so a slip of the finger cannot reset the controllers.

A key goes to the box you are typing in first: a digit or an arrow given to a jog function still types
or moves the cursor there, and does its function only where nothing else uses it. A menu shows each
entry's key, and the Jog panel's buttons say theirs when you hover over them.

<!-- src: src/app/JPKeyMap.cpp (add, assign, reset, resetAll, parse); src/app/JPlacerMenuBuilder.cpp (entry, the default keys); src/app/JPlacerPreferencesDialog.cpp (keysPage, fillKeys); src/app/JPlacerSettings.cpp (keyFor) -->

## Jog

The [Jog panel](machine.md#jog)'s steps, numbers apart, smallest first:

**Distance steps (mm or degrees)**
:   What the distance slider steps through, and **Raise** / **Lower Jog Increment** go to (and the First to Fifth Jog Increment pick from). 0.001 to 1000.
    It starts as 0.01 0.1 1 10 25 50 100.

**Speed steps (%)**
:   Marked on the speed slider; **Faster** and **Slower** go to the next one up or down from the speed
    now. 1 to 100. It starts as 10 25 50 75 100.

Each step is also listed on the Keys tab (**Distance 1**, **Speed 25%**) to be given a key: the digit 1
for 1 mm and 2 for 10 mm, say. Steps that make no sense (a word, a step smaller than the one before) are
not taken, and the tab says why. Emptied, a box goes back to the steps jplacer starts with.

<!-- src: src/app/JPlacerPreferencesDialog.cpp (jogPage); src/app/JPlacerMachine.cpp (jogDistances, jogSpeeds); src/ui/JPJogPanel.cpp (parseSteps, defaultDistances, defaultSpeeds, setSteps); src/app/JPlacerApp.cpp (addJogStepKeys) -->
