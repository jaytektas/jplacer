#!/usr/bin/env bash
# Build every file a jplacer release carries, for the version in CMakeLists.txt:
#
#   dist/release-<version>/
#       jplacer-<version>-x86_64.AppImage   Linux, updates itself
#       SHA256SUMS                          `sha256sum` of the above
#
#   packaging/build-release.sh             build only
#   packaging/build-release.sh --publish   build, tag v<version>, push the tag, and create the GitHub release
#
# SHA256SUMS is not optional: the updater refuses to install a release without it, because the checksum
# is what proves the file that arrived is the file that was published. The update picks its file by name
# (-x86_64.AppImage on Linux), so that name is part of the contract.
#
# A version with a pre-release (JPLACER_PRERELEASE in CMakeLists.txt) is published as a GitHub
# PRE-RELEASE. /releases/latest never returns one, so only people who ticked Preferences > Include beta
# versions are offered it.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT/dist"
source "$ROOT/packaging/version.sh"

fail() { echo "build-release: $*" >&2; exit 1; }

PUBLISH=0
[ "${1:-}" = "--publish" ] && PUBLISH=1

OUT="$DIST/release-$VERSION"
TAG="v$VERSION"

# A release is cut from committed work, so the tag points at exactly what was built.
git -C "$ROOT" diff-index --quiet HEAD -- || fail "uncommitted changes -- commit first"

cmake -S "$ROOT" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$ROOT/build" --target jplacer --parallel
"$ROOT/packaging/build-appimage.sh"

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$DIST/jplacer-$VERSION-x86_64.AppImage" "$OUT/"
( cd "$OUT" && sha256sum jplacer-* > SHA256SUMS )

echo
echo "release $VERSION: $OUT"
( cd "$OUT" && ls -l | sed 's/^/  /' && sed 's/^/  /' SHA256SUMS )

[ "$PUBLISH" = 1 ] || exit 0

command -v gh >/dev/null || fail "no gh on PATH"
git -C "$ROOT" rev-parse -q --verify "refs/tags/$TAG" >/dev/null || git -C "$ROOT" tag -a "$TAG" -m "jplacer $VERSION"
git -C "$ROOT" push origin "$TAG"
PRE=()
[ -n "$PRERELEASE" ] && PRE=(--prerelease)
( cd "$OUT" && gh release create "$TAG" "${PRE[@]}" --verify-tag --title "jplacer $VERSION" \
      --generate-notes jplacer-* SHA256SUMS )
