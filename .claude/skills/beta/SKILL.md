---
name: beta
description: Build and publish a jplacer beta (<next patch>-beta.N) as a GitHub pre-release for jplacers with Include beta versions ticked, numbering it automatically. Use when asked to "push a beta", "publish a beta", "make a beta" or "a test build".
---

# A jplacer beta

A beta is a GitHub **pre-release**. `/releases/latest` never returns one, so only jplacers with
**Preferences ▸ Include beta versions** are offered it. It sorts below the release it leads to
(0.1.2-beta.3 < 0.1.2), so that release later supersedes it everywhere. For a release, use the
`release` skill. Being asked for a beta is the go-ahead to publish it.

## 0. Before anything

- Everything is **committed**; `git status --short` is empty (any branch).
- `gh auth status` works (see the `release` skill for logging in from the NFS token file).
- `## Unreleased` in `CHANGES.md` says what the beta changes, and the manual describes it (the
  `release` skill's steps 1 and 2) — the beta carries both: the notes become the pre-release's notes,
  and the manual inside the AppImage shows them under What's New as this beta.

## 1. Build and publish

    packaging/build-beta.sh --publish

It does, in order:
1. **The version**: `<next patch>-beta.N`, the patch after CMakeLists.txt's (the last release) with N
   one past every beta of it already published — a beta numbered below one already out reaches nobody.
   It refuses when that patch is already released (run a release first).
2. Builds jplacer AS that version (`JPLACER_VERSION_OVERRIDE`), the manual, the AppImage, and
   `dist/release-<beta>/` + SHA256SUMS, then puts the build back to the plain version. **Nothing is
   committed**: CMakeLists.txt keeps the last release's version.
3. Pushes the commit to the **`beta`** branch on origin (never `main`'s history is changed), and
   creates the pre-release with the Unreleased notes.

## 2. Verify

    gh release view v<beta> --json isPrerelease,assets --jq '{pre:.isPrerelease, assets:[.assets[].name]}'
    gh api repos/jaytektas/jplacer/releases/latest --jq .tag_name

`pre` must be true and Latest must still be the last full release — otherwise the beta went out to
everyone. Never publish a beta without `--prerelease`.

## 3. Tell the user

The beta's version and URL, that only jplacers with Include beta versions ticked are offered it, and
anything not verified.
