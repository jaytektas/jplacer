---
name: release
description: Build and publish a jplacer release (AppImage + SHA256SUMS + manual) to github.com/jaytektas/jplacer, raising the version automatically. Use when asked to "release", "cut a release", "ship it" or "push a release".
---

# Releasing jplacer

A release is a normal GitHub release: `/releases/latest` returns it, so every jplacer is offered it.
Being asked to release is the go-ahead to publish. Never delete or overwrite a published release; a
mistake is fixed by the next release.

## 0. Before anything

- Everything to ship is **committed** on `main`; `git status --short` is empty.
- `gh auth status` works. If the token is invalid, log in from the NFS token file without printing it:
  `grep -oE "ghp_[A-Za-z0-9_]+" "/mnt/nfs/github access token" | head -1 | gh auth login -h github.com --with-token`
- The SDK is current: `cat ~/jframework-sdk/jframework.commit` against `git -C ~/workspace/JFramework rev-parse HEAD`.
  If they differ, say so — jplacer would ship against an older framework.

## 1. Release notes — CHANGES.md

`## Unreleased` must hold a plain-words line for every change a user would notice since the last
release: no hashes, no file names. Check it against what actually changed:

    git log --oneline v<last>..HEAD

Write any missing lines and commit them. The build script refuses a release with no notes, and prints
those commits.

## 2. The manual says what this release does

The manual ships inside the AppImage and on GitHub Pages, so a stale manual ships with the release.
For each change in the notes, search `manual/docs` for the labels, settings and source files it touches
and make the pages describe the release, to `manual/STANDARD.md` (facts from the code, a
`<!-- src: -->` note per paragraph, labels in **bold**). Things that are gone must not be named anywhere.
Commit it (`manual: <what>`).

`manual/tools/build.sh` must pass: it checks every source note and builds `--strict`.

## 3. Build and publish

    packaging/build-release.sh --publish

It does, in order:
1. **The version**: one past the last published release (patch), committed with the notes as
   `jplacer x.y.z`. A version already raised past the last release (a hand-raised minor/major, or a
   re-run after a failure) is kept, so re-running is safe.
2. Builds jplacer, the manual, the AppImage (manual inside), and `dist/release-<version>/` + SHA256SUMS.
3. Pushes `main` to origin and backup, creates the release with the version's CHANGES.md section as its
   notes, mirrors the tag to backup, and publishes the manual to GitHub Pages.

## 4. Verify

    gh release view v<version> --json isPrerelease,assets --jq '{pre:.isPrerelease, assets:[.assets[].name]}'
    gh api repos/jaytektas/jplacer/releases/latest --jq .tag_name

`pre` is false, the assets are the AppImage and SHA256SUMS, and Latest is the new tag.

## 5. Tell the user

The release URL, the version, the manual URL (https://jaytektas.github.io/jplacer/), and anything not
verified (for example, an update from the previous version not tried).
