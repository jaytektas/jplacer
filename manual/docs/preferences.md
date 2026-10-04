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

### Parts library

**Parts library**
:   The folder the [parts library](jobs.md#the-parts-library) is kept in: type it, or **Choose…** it.
    Empty (as it starts) is jplacer's own data folder, `~/.local/share/jplacer/library`. A git
    repository or a shared drive works, to keep it safe or share it between machines. Choosing another
    folder opens the library there: nothing is copied or moved, and jobs keep their own parts. A folder
    with no library in it starts one with the standard packages.

<!-- src: src/app/JPlacerPreferencesDialog.cpp (generalPage, Parts library); src/app/JPlacerJob.cpp (setLibraryFolder); src/library/JPLibrary.cpp (open) -->

### Updates

**Check for updates when jplacer opens**
:   When ticked, jplacer looks for a newer version each time it starts, and tells you only if it finds
    one. On when jplacer is first installed.

**Include beta versions**
:   When ticked, you are offered beta versions as well as full releases. Beta versions get new features
    first and have had less testing. Off when jplacer is first installed.

**Check Now**
:   Closes Preferences and looks for a newer version straight away, as **Help ▸ Check for Updates**
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
:   What the distance slider steps through, and **Larger** / **Smaller Distance** go to. 0.001 to 1000.
    It starts as 0.01 0.1 1 10 25 50 100.

**Speed steps (%)**
:   Marked on the speed slider; **Faster** and **Slower** go to the next one up or down from the speed
    now. 1 to 100. It starts as 10 25 50 75 100.

Each step is also listed on the Keys tab (**Distance 1**, **Speed 25%**) to be given a key: the digit 1
for 1 mm and 2 for 10 mm, say. Steps that make no sense (a word, a step smaller than the one before) are
not taken, and the tab says why. Emptied, a box goes back to the steps jplacer starts with.

<!-- src: src/app/JPlacerPreferencesDialog.cpp (jogPage); src/app/JPlacerMachine.cpp (jogDistances, jogSpeeds); src/ui/JPJogPanel.cpp (parseSteps, defaultDistances, defaultSpeeds, setSteps); src/app/JPlacerApp.cpp (addJogStepKeys) -->
