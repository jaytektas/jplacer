# Getting started

## Download and run

jplacer for Linux is a single file, an AppImage. Download `jplacer-<version>-x86_64.AppImage` from the
[releases page](https://github.com/jaytektas/jplacer/releases), put it somewhere it can stay (for
example `~/Applications`), make it executable and run it:

    chmod +x jplacer-*-x86_64.AppImage
    ./jplacer-*-x86_64.AppImage

Keep the file where you put it. jplacer updates itself by replacing this file, so it needs to be
somewhere you can write to.

<!-- src: packaging/build-appimage.sh (the file name); JFramework include/j/update/JSelfInstaller.h (an AppImage replaces itself) -->

## Your applications menu

The first time the AppImage runs, jplacer adds itself to your desktop's applications menu, with its
icon, so after that you can start it like any other program. Each time it starts, it points that menu
entry at wherever the AppImage is now, so moving the file does not leave a launcher that no longer
works.

The entry is yours alone (it is written under `~/.local/share`), and no password is needed. To take it
out, untick **Show jplacer in the applications menu** in [Preferences](preferences.md).

<!-- src: src/app/JPlacerLauncher.cpp (install, remove, the paths under the data home); src/app/JPlacerApp.cpp (install on every start) -->

## Where your settings are kept

jplacer saves its preferences in `~/.config/jplacer/jplacer.json` (or under `$XDG_CONFIG_HOME` when that
is set).

<!-- src: src/app/JPlacerSettings.cpp (defaultPath) -->

## Starting it from a terminal

These options are for finding problems:

| Option | What it does |
|---|---|
| `--help`, `-h` | Lists these options, the log's categories and where your settings file is, then exits without starting jplacer. |
| `--verbose`, `-v` | Log more detail. |
| `--quiet`, `-q` | Log only warnings and errors. |
| `--trace <category>` | Log everything in one category, such as `updates` or `machine.traffic` (every line sent to and received from the controllers). A wildcard takes a whole area: `--trace 'machine.*'`. |
| `--settings <file>` | Use another settings file, leaving your own untouched. |

An option jplacer does not know, or `--trace` or `--settings` without what follows it, is refused with the
list of options, and jplacer does not start.

<!-- src: src/main.cpp (parseArgs); src/common/JPlacerLog.h (the categories) -->
