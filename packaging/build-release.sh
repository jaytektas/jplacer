#!/usr/bin/env bash
# Cut a jplacer RELEASE: raise the version, build, package, and (with --publish) put it on GitHub.
#
#   packaging/build-release.sh             raise the version, build and gather dist/release-<version>/
#   packaging/build-release.sh --publish   ...and push main, create the GitHub release, mirror the tag,
#                                          publish the manual
#
# THE NOTES come from CHANGES.md: everything under ## Unreleased becomes this version's section, the
# GitHub release's notes and the manual's What's New. No notes, no release.
#
# THE VERSION. Every release is newer than the last one published. CMakeLists.txt holds the last
# release's version, so this raises the patch and commits it with the notes ("jplacer x.y.z"). A version
# already raised past the last release -- a hand-raised minor or major, or a re-run after a failure --
# is kept, not raised again.
#
# A release is a normal GitHub release: /releases/latest returns it, so every jplacer is offered it.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
say()  { printf '  release: %s\n' "$*"; }
fail() { printf '  release: %s\n' "$*" >&2; exit 1; }

PUBLISH=0
[ "${1:-}" = "--publish" ] && PUBLISH=1

# 1. committed, on main
[ -z "$(git status --porcelain)" ] || { git status --short | head -10 >&2; fail "commit everything first"; }
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || fail "releases are cut from main"

# 2. the version, and its notes
source packaging/version.sh
LAST=$(gh release list -L 100 --exclude-pre-releases --json tagName --jq '.[].tagName' | sed 's/^v//' | sort -V | tail -1) \
    || fail "could not list the published releases"
newer() { [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ]; }
NEW="$PLAIN_VERSION"
[ -n "$LAST" ] && ! newer "$PLAIN_VERSION" "$LAST" && NEW=$(next_patch "$LAST")
# The notes are written by hand, as the changes are made (CHANGES.md, ## Unreleased). A release with none
# is refused, with the commits to write them from.
if ! packaging/changes.py unreleased >/dev/null && ! packaging/changes.py notes "$NEW" >/dev/null 2>&1; then
    git log --oneline ${LAST:+"v$LAST..HEAD"} | sed 's/^/    /' >&2
    fail "nothing under '## Unreleased' in CHANGES.md -- write what this release changes, from the commits above"
fi
sed -i -E "s/^project\(jplacer VERSION [0-9.]+/project(jplacer VERSION $NEW/" CMakeLists.txt
packaging/changes.py prepare "$NEW"
if [ -n "$(git status --porcelain CMakeLists.txt CHANGES.md)" ]; then
    git commit -q -m "jplacer $NEW" CMakeLists.txt CHANGES.md
    say "committed: jplacer $NEW (last release: ${LAST:-none})"
fi
source packaging/version.sh
TAG="v$VERSION"
say "jplacer $VERSION"

# 3. build, package, gather -- always as the plain version
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="${JFRAMEWORK_SDK:-$HOME/jframework-sdk}" -DCMAKE_BUILD_TYPE=Release -DJPLACER_VERSION_OVERRIDE= >/dev/null
cmake --build build --target jplacer --parallel
manual/tools/build.sh
packaging/build-appimage.sh
packaging/gather-release.sh

[ "$PUBLISH" = 1 ] || exit 0

# 4. publish: the commit first, so the tag GitHub makes points at something it has
git push -q origin main
git push -q backup main
gh release create "$TAG" --target "$(git rev-parse HEAD)" --title "jplacer $VERSION" \
    --notes "$(packaging/changes.py notes "$VERSION")" "dist/release-$VERSION"/*
git fetch -q origin tag "$TAG"
git push -q backup "$TAG"
manual/tools/publish.sh
say "published: $(gh release view "$TAG" --json url --jq .url)"
