#!/usr/bin/env bash
# Gather the packaged files for $VERSION (or $JPLACER_VERSION) into dist/release-<version>/ with SHA256SUMS.
#
# SHA256SUMS is not optional: the updater refuses to install a release without it, because the checksum
# is what proves the file that arrived is the file that was published. The update picks its file by name
# (-x86_64.AppImage on Linux, -setup.exe on Windows), so those names are part of the contract; the Windows zip
# (to unzip and run, not installed) goes beside them.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/packaging/version.sh"
OUT="$ROOT/dist/release-$VERSION"
APPIMAGE="$ROOT/dist/jplacer-$VERSION-x86_64.AppImage"
SETUP="$ROOT/dist/jplacer-$VERSION-setup.exe"
ZIP="$ROOT/dist/jplacer-$VERSION-windows-x64.zip"
for f in "$APPIMAGE" "$SETUP" "$ZIP"; do
    [ -f "$f" ] || { echo "gather-release: no $f" >&2; exit 1; }
done
rm -rf "$OUT" && mkdir -p "$OUT"
cp "$APPIMAGE" "$SETUP" "$ZIP" "$OUT/"
( cd "$OUT" && sha256sum jplacer-* > SHA256SUMS )
echo "release $VERSION: $OUT"
( cd "$OUT" && ls -l | sed 's/^/  /' && sed 's/^/  /' SHA256SUMS )
