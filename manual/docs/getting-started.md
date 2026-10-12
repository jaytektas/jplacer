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

jplacer for Windows (10 or 11, 64-bit) comes as an installer, `jplacer-<version>-setup.exe`: run it, and
jplacer is installed in Program Files with an entry in the Start menu (and, if you tick it, an icon on the
desktop), then started. Each version installs over the one before. `jplacer-<version>-windows-x64.zip`
is the same jplacer to unzip anywhere and run (`jplacer.exe`) without installing; that copy does not
update itself. Its USB cameras are read through Windows' own camera support (Media Foundation); a
computer whose graphics have no Vulkan (a virtual machine's, say) still runs it, drawing more slowly.

<!-- src: packaging/build-windows.sh (the installer and the zip); packaging/windows/jplacer.iss (Program Files, the Start menu, the desktop icon, started when done); src/camera/JPMediaFoundationSource.cpp; packaging/build-windows-deps.sh (the Vulkan loader beside jplacer.exe) -->

## Your applications menu

On Windows the installer puts it in the Start menu. On Linux, the first time the AppImage runs, jplacer adds itself to your desktop's applications menu, with its
icon, so after that you can start it like any other program. Each time it starts, it points that menu
entry at wherever the AppImage is now, so moving the file does not leave a launcher that no longer
works.

The entry is yours alone (it is written under `~/.local/share`), and no password is needed. To take it
out, untick **Show jplacer in the applications menu** in [Preferences](preferences.md).

<!-- src: src/app/JPlacerLauncher.cpp (install, remove, the paths under the data home); src/app/JPlacerApp.cpp (install on every start) -->

## Where your settings are kept

jplacer keeps what it knows in its configuration folder: on Linux `~/.config/jplacer` (or `jplacer`
under `$XDG_CONFIG_HOME` when that is set), on Windows `%APPDATA%\jplacer` (installing, updating and
uninstalling jplacer never touch it):

| File | What it holds |
|---|---|
| `jplacer.json` | Your preferences, and how the window was laid out. |
| `cells/` | The machine: each cell's set-up and calibration (see [Machine](machine.md)). |
| `library.db` | The library's parts and packages (see [Parts](parts.md)). |
| `feeders.json` | The feeders (see [Feeders](feeders.md)). |
| `vision-settings.json` | The vision settings (see [Vision](vision.md)). |
| `machine-properties.json` | What feeders keep on the machine: slot feeders' banks, Photon feeders' slots, heap feeders' drop boxes. |
| `runs.db` | Each run of a job: what it fed and placed, as it went. |

<!-- src: src/app/JPlacerSettings.cpp (defaultPath); src/common/JPlacerPaths.cpp (configDir); src/model/JPConfiguration.h (kFeedersFile, kVisionFile, kMachinePropertiesFile); src/model/JPLibraryStore.h (kFile); src/model/JPRunStore.h (kFile) -->

Each is written whole or not at all: the new file takes the old one's place only once it is complete, so a
crash or a full disk while saving leaves the old one as it was. The JSON files can be read in any text
editor. Each feeder has its name, part, retries, priority, feed option and pick location by name, and the
settings its kind adds under the kind's name (a tray's `"tray": {"countX": 4, "countY": 3, …}`, an auto
feeder's `"auto": {"actuator": …}`). A feeder of a class jplacer does not know keeps its settings under
`"openpnp"`, in OpenPnP's shape, as the vision settings are: an attribute of OpenPnP's is a value
(`"x": 10.25`, `"enabled": true`), what OpenPnP nests is an object, and what it repeats is a list.

<!-- src: src/common/JPWholeFile.h; src/model/JPFeeder.h (toJson, Own); src/model/JPTrayFeeder.h (kKey); src/model/JPAutoFeeder.h (kKey); src/openpnp/JPXmlJson.h -->

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
