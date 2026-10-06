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

**Icon buttons say where a click leads** (JPIconButton::setLeads). One that
opens a menu shows a small down-triangle in its bottom-right corner; one that
leads somewhere else (another window, a dialog, Machine Setup) shows "…"
there, as text buttons and menu entries end in "…"; one without either acts
at once. Every icon button chooses; none is left ambiguous.

**Settings.** Preferences live in `JSettings::instance()`, backed by the file
`JPlacerSettings` names. Key names are constants in `JPlacerSettings` only.

**The manual is kept in step with the code.** `manual/` is the user manual
(MkDocs), written to `manual/STANDARD.md`: every fact comes from the code and
carries a `<!-- src: -->` note naming the file it comes from (never a line
number). A change a user would notice updates the manual IN THE SAME COMMIT,
and adds a plain-words line under `## Unreleased` in `CHANGES.md`. The manual
ships inside the AppImage (Help > User Manual), the Change Log (What's New) is generated from
`CHANGES.md`, and `manual/tools/build.sh` fails on a stale source note or a
broken link.

**App shell stays small.** `JPlacerApp` wires things together; features get
classes of their own.

## Build

    packaging/build-opencv.sh      # once: the static OpenCV OpenPnP's vision pipelines run on
    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
    cmake --build build
    ./build/jplacer [--verbose] [--trace <category>] [--settings <file>]

## Manual

    manual/tools/build.sh      # check source notes, build manual/site (--strict)
    manual/tools/publish.sh    # build and push to GitHub Pages (done by a release)

## Releases and betas

Use the `release` and `beta` skills; they run `packaging/build-release.sh` and
`packaging/build-beta.sh`. CMakeLists.txt holds the last release's version: a
release raises the patch (a hand-raised version is kept) and commits it with
the CHANGES.md notes; a beta is built as `<next patch>-beta.N` without
committing any version, and published as a GitHub pre-release from the `beta`
branch. `JAppUpdater` checks at startup and on Help > Check For Updates…; set
`JPLACER_UPDATE_URL` to point it at a test releases URL.
