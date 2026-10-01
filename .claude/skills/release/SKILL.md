---
name: release
description: Publish a full jplacer release (vX.Y.Z) to GitHub that every user's updater is offered. Use when the user asks to release, ship, or publish a version of jplacer (not a beta).
---

# Release jplacer

A full release is a normal (non-pre-release) GitHub release. `/releases/latest`
returns it, so every jplacer offers it on its next update check.

Argument (optional): the version (`0.2.0`) or the part to bump (`patch`,
`minor`, `major`). With none, ask which; suggest `patch` for fixes only and
`minor` when there are new features.

## 1. Preconditions — stop and report if any fail

- `git status --porcelain` is empty and the branch is `main`, up to date with
  `origin/main` (`git fetch origin && git status -sb`).
- `gh auth status` succeeds. If the token is invalid, log in from the token
  file on the NFS, never printing it:
  `grep -oE "ghp_[A-Za-z0-9_]+" "/mnt/nfs/github access token" | head -1 | gh auth login -h github.com --with-token`
- `appimagetool` and `rsvg-convert` are on PATH.
- The tag `v<version>` does not already exist locally or on GitHub
  (`gh release view v<version>` must fail).

## 2. Set the version

In `CMakeLists.txt`:
- `project(jplacer VERSION X.Y.Z ...)` — the new version. It must be newer than
  the latest release (`gh release list --limit 5`).
- `set(JPLACER_PRERELEASE "")` — empty. Promoting a beta to its release means
  clearing this and keeping X.Y.Z (0.2.0-beta.3 → 0.2.0).

Build to make sure it compiles before anything is committed:

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
    cmake --build build

## 3. Commit and push

Commit only the version change, with a subject `X.Y.Z: <what this release
brings>` — summarise from `git log v<previous>..HEAD --oneline`. Then:

    git push origin main
    git push backup main

## 4. Build and publish

    packaging/build-release.sh --publish

It builds the AppImage and SHA256SUMS, tags `vX.Y.Z`, pushes the tag to
origin and creates the GitHub release with generated notes. Then mirror the
tag to the backup:

    git push backup vX.Y.Z

## 5. Verify — report each result

- `gh release view vX.Y.Z --json isPrerelease,assets -q '{pre: .isPrerelease, assets: [.assets[].name]}'`
  shows `pre: false` and both `jplacer-X.Y.Z-x86_64.AppImage` and `SHA256SUMS`.
- `gh api repos/jaytektas/jplacer/releases/latest -q .tag_name` is `vX.Y.Z`.

Finish with the release URL. Never delete or re-tag a published release to
fix a mistake; publish the next patch version instead.
