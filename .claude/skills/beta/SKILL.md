---
name: beta
description: Publish a jplacer beta (vX.Y.Z-beta.N) as a GitHub pre-release, offered only to users with Preferences > Include beta versions. Use when the user asks for a beta, pre-release, or test build of jplacer.
---

# Beta jplacer

A beta is a GitHub PRE-release. `/releases/latest` never returns one, so only
users who ticked Preferences > Include beta versions are offered it (their
updater reads the full release list). It orders before the release it leads
to: 0.2.0-beta.1 < 0.2.0-beta.2 < 0.2.0.

Argument (optional): the target version (`0.2.0`), or `next` for the next
beta of the version already in beta. With none, work it out:

- `JPLACER_PRERELEASE` in `CMakeLists.txt` is `beta.N` and v<X.Y.Z>-beta.N is
  already published → next beta of the same version: `beta.N+1`.
- Otherwise the beta leads up to a version not yet released: ask whether it is
  the next minor (new features, the usual case) or the next patch, then use
  `beta.1`.

## 1. Preconditions — stop and report if any fail

- `git status --porcelain` is empty and the branch is `main`, up to date with
  `origin/main` (`git fetch origin && git status -sb`).
- `gh auth status` succeeds. If the token is invalid, log in from the token
  file on the NFS, never printing it:
  `grep -oE "ghp_[A-Za-z0-9_]+" "/mnt/nfs/github access token" | head -1 | gh auth login -h github.com --with-token`
- `appimagetool` and `rsvg-convert` are on PATH.
- The tag `vX.Y.Z-beta.N` does not already exist (`gh release view` must fail),
  and `vX.Y.Z` itself has not been released — a beta of a shipped version
  would never be offered to anyone.

## 2. Set the version

In `CMakeLists.txt`:
- `project(jplacer VERSION X.Y.Z ...)` — the version the beta leads up to.
- `set(JPLACER_PRERELEASE "beta.N")`.

Build to make sure it compiles before anything is committed:

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
    cmake --build build

## 3. Commit and push

Commit only the version change, with a subject `X.Y.Z-beta.N: <what is being
tried out>` — summarise from the log since the previous release or beta tag.
Then:

    git push origin main
    git push backup main

## 4. Build and publish

    packaging/build-release.sh --publish

The script sees the pre-release in `CMakeLists.txt` and creates the GitHub
release with `--prerelease`. Then mirror the tag to the backup:

    git push backup vX.Y.Z-beta.N

## 5. Verify — report each result

- `gh release view vX.Y.Z-beta.N --json isPrerelease,assets -q '{pre: .isPrerelease, assets: [.assets[].name]}'`
  shows `pre: true` and both `jplacer-X.Y.Z-beta.N-x86_64.AppImage` and `SHA256SUMS`.
- `gh api repos/jaytektas/jplacer/releases/latest -q .tag_name` is still the
  last FULL release, not the beta — otherwise the beta went out to everyone.

Finish with the release URL. Leave `JPLACER_PRERELEASE` set: the next beta
bumps N, and the `release` skill clears it when the version ships.
