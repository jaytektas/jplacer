# The jplacer User Manual — writing standard

Every page is written to this standard. It is the brief each author (person or agent) works from, and
the checklist a page is reviewed against. Modelled on the jayecu manual's standard.

## 1. What the manual is

The reference for using jplacer. It is a static HTML site (MkDocs + Material) that ships inside the
AppImage — **Help ▸ User Manual** opens the local copy in the browser — and is published on the
project's GitHub Pages (https://jaytektas.github.io/jplacer/).

## 2. It is kept in step with the code

The manual describes the jplacer it ships with. **A change a user would notice updates the manual in
the same commit** — a new menu entry, a setting, a changed behaviour — and adds a line under
`## Unreleased` in `CHANGES.md` (shown as **Help ▸ What's New** and used as the release notes). A
release refuses to build while the manual fails to build.

## 3. Voice

- Plain English, second person ("you"), present tense, active voice. Short sentences.
- Explain **why** before **how**. A setting described only by its name is not described.
- Name things exactly as jplacer shows them, in **bold** (**Edit ▸ Preferences…**, **Include beta
  versions**).
- No marketing language, and no comparisons with other software. Naming OpenPnP is fine only where
  jplacer works with it.

## 4. Accuracy — the rule that overrides every other rule

**Nothing is written from general knowledge where the code decides the answer.** Every behaviour,
default, label and sequence is taken from this repository (`src/`), or from JFramework where the
framework does the work (the updater, dialogs).

Each factual paragraph carries a hidden source note for maintainers:

    <!-- src: src/app/JPlacerSettings.h (the defaults); src/app/JPlacerPreferencesDialog.cpp (the rows) -->

A note names the FILE, with its full path from the repository root, and says in words what to look for
in it. A file in JFramework is prefixed `JFramework ` (`JFramework include/j/app/JAppUpdater.h`).
**Never a line number.** `manual/tools/build.sh` runs `manual/tools/check_src.py`, which fails the
build if a note cites a line number or a file that does not exist.

When something cannot be confirmed from the source, it is not guessed: the page says only what is
certain.

## 5. Features that do not exist yet

Menu entries for unbuilt features are shown greyed out in jplacer. The manual lists them as "not yet
available" and describes nothing about how they will work.

## 6. Building

    manual/tools/build.sh       # venv on first run, source-note check, mkdocs build --strict
    manual/tools/publish.sh     # build, then push manual/site to the gh-pages branch

`--strict` makes a broken link or anchor fail the build.
