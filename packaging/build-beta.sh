#!/usr/bin/env bash
# Cut a jplacer BETA: build and package <next patch>-beta.N, and (with --publish) put it on GitHub as a
# PRE-RELEASE, which only jplacers with Preferences > Include beta versions are offered.
#
#   packaging/build-beta.sh              build and gather dist/release-<beta>/
#   packaging/build-beta.sh --publish    ...and push the beta branch, create the pre-release, mirror the tag
#
# THE VERSION is the patch after CMakeLists.txt's (the last release), with N one past every beta of it
# already published -- the updater offers by version alone, so a beta that sorts below one already out
# reaches nobody. It sorts below the release it leads to (0.1.2-beta.3 < 0.1.2), so that release later
# supersedes it everywhere.
#
# NOTHING IS COMMITTED. The version is handed to the build (JPLACER_VERSION_OVERRIDE) and the build is
# put back to the plain version however this ends, so a later `cmake --build build` cannot say beta.
# The tag's commit goes to GitHub on the `beta` branch, never main.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
say()  { printf '  beta: %s\n' "$*"; }
fail() { printf '  beta: %s\n' "$*" >&2; exit 1; }

PUBLISH=0
[ "${1:-}" = "--publish" ] && PUBLISH=1

# 1. committed: the beta must name a commit
[ -z "$(git status --porcelain)" ] || { git status --short | head -10 >&2; fail "commit everything first"; }

# 2. the version
source packaging/version.sh
NEXT=$(next_patch "$PLAIN_VERSION")
TAGS=$(gh release list -L 100 --json tagName --jq '.[].tagName') || fail "could not list the published releases"
grep -qx "v$NEXT" <<< "$TAGS" && fail "v$NEXT is already released -- run a release first so CMakeLists.txt says $NEXT"
N=$(sed -n "s/^v$NEXT-beta\.\([0-9]*\)$/\1/p" <<< "$TAGS" | sort -n | tail -1)
export JPLACER_VERSION="$NEXT-beta.$(( ${N:-0} + 1 ))"
TAG="v$JPLACER_VERSION"
# The beta's notes are what is under ## Unreleased in CHANGES.md -- a beta of nothing is refused.
packaging/changes.py unreleased >/dev/null || fail "nothing under '## Unreleased' in CHANGES.md -- write what this beta changes"
say "jplacer $JPLACER_VERSION"

# 3. build as the beta, package, gather -- and put the build back to the plain version however this ends
restore() { cmake -S . -B build -DJPLACER_VERSION_OVERRIDE= >/dev/null 2>&1 || true; }
trap restore EXIT
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DJPLACER_VERSION_OVERRIDE="$JPLACER_VERSION" >/dev/null
cmake --build build --target jplacer --parallel
manual/tools/build.sh
packaging/build-appimage.sh
packaging/gather-release.sh

[ "$PUBLISH" = 1 ] || exit 0

# 4. publish. --prerelease is the whole point: without it the beta becomes Latest and reaches everyone.
COMMIT=$(git rev-parse HEAD)
git push -q --force origin "$COMMIT:refs/heads/beta"
git push -q --force backup "$COMMIT:refs/heads/beta"
gh release create "$TAG" --prerelease --target "$COMMIT" --title "jplacer $JPLACER_VERSION (beta)" \
    --notes "$(packaging/changes.py unreleased; printf '\nBeta \xE2\x80\x94 for testing.\n')" "dist/release-$JPLACER_VERSION"/*
git fetch -q origin tag "$TAG"
git push -q backup "$TAG"
say "published: $(gh release view "$TAG" --json url --jq .url)"
