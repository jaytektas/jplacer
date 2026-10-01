# Preferences

**Edit ▸ Preferences…** opens the settings. Each one takes effect and is saved the moment you change it,
so there is no Apply button: close the window with **Close** when you are done.

<!-- src: src/app/JPlacerPreferencesDialog.cpp (each row stores and saves on change) -->

## General

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

## Updates

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
