# CLAUDE.md

## Project

`jplacer` — pick-and-place machine control, our own take on OpenPnP, built on
the JFramework toolkit (C++20, CMake, Vulkan; no Qt/GTK).

OpenPnP's source is cloned into `reference/openpnp` (git-ignored) as the domain
reference for machines, heads, nozzles, feeders, vision and jobs. Read it for
concepts and behaviour; jplacer is not a port of its Java code.

## The framework's rules are this project's rules

`/home/jay/workspace/JFramework/CLAUDE.md` applies here in full: one public
class per header, file name == class name; no hardcoded visual constants (they
come from `JStyle`); no shims, stubs, TODO placeholders or dead code; widgets
never position themselves; no platform types in public headers; `JLOGC` only,
never `printf` or `std::cout`.

jplacer is DOWNSTREAM of the shipped SDK (`$HOME/jframework-sdk`). It consumes
JFramework through `find_package(JFramework CONFIG)` and never edits it.

## Additions specific to this project

**Menu skeleton.** Menu entries for features not built yet are present but
disabled (`JPlacerMenuBuilder`), never wired to a handler that does nothing.
Enable an entry in the same change that implements it.

**Settings.** Preferences live in `JSettings::instance()`, backed by the file
`JPlacerSettings` names. Key names are constants in `JPlacerSettings` only.

**App shell stays small.** `JPlacerApp` wires things together; features get
classes of their own.

## Build

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
    cmake --build build
    ./build/jplacer [--verbose] [--trace <category>] [--settings <file>]

## Releases and updates

The version is `project(jplacer VERSION x.y.z)` plus `JPLACER_PRERELEASE` in
`CMakeLists.txt`. `packaging/build-release.sh --publish` builds the AppImage and
SHA256SUMS, tags `v<version>` and creates the GitHub release; a version with a
pre-release (e.g. `beta.1`) is published as a GitHub pre-release, which only
users with Preferences > Include beta versions are offered.

`JAppUpdater` checks at startup and on Help > Check for Updates. Set
`JPLACER_UPDATE_URL` to point it at a test releases URL.
