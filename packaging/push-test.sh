#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>
#
# Put this build on a test machine: build the AppImage (as a test version, below) and copy it to <host>:~/Downloads, replacing the
# jplacer AppImage already there, so the machine keeps one copy. The applications-menu entry jplacer
# writes for itself points at the AppImage's path; if the new file's name differs (another version),
# the entry is pointed at it here, because the old file it named is gone and could not start to fix it.
#
#   packaging/push-test.sh <host>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST="${1:?usage: push-test.sh <host>}"
fail() { echo "push-test: $*" >&2; exit 1; }

# A TEST BUILD'S VERSION sorts just above the newest published beta of the next patch
# (0.1.1-beta.1.test.202610021530 > 0.1.1-beta.1, < 0.1.1-beta.2), so the machine's update check never
# offers an older build in its place, and does offer the next real beta. Like a beta, nothing is
# committed: the version is handed to the build and the build is put back to the plain version after.
cd "$ROOT"
source packaging/version.sh
NEXT=$(next_patch "$PLAIN_VERSION")
TAGS=$(gh release list -L 100 --json tagName --jq '.[].tagName') || fail "could not list the published releases"
N=$(sed -n "s/^v$NEXT-beta\.\([0-9]*\)$/\1/p" <<< "$TAGS" | sort -n | tail -1)
export JPLACER_VERSION="$NEXT-beta.${N:-0}.test.$(date -u +%Y%m%d%H%M)"
restore() { cmake -S . -B build -DJPLACER_VERSION_OVERRIDE= >/dev/null 2>&1 || true; }
trap restore EXIT
cmake -S . -B build -DJPLACER_VERSION_OVERRIDE="$JPLACER_VERSION" >/dev/null
cmake --build build >/dev/null || fail "build failed"
manual/tools/build.sh >/dev/null 2>&1 || fail "manual failed to build -- run manual/tools/build.sh"
OUT="$(packaging/build-appimage.sh | sed -n 's/^built: //p')"
[ -f "$OUT" ] || fail "no AppImage built"
NAME="$(basename "$OUT")"

scp -q "$OUT" "$HOST:Downloads/$NAME.new"
ssh "$HOST" bash -s -- "$NAME" <<'REMOTE'
set -euo pipefail
NAME="$1"
cd ~/Downloads
for old in jplacer-*.AppImage; do [ -e "$old" ] && [ "$old" != "$NAME" ] && rm -f "$old"; done
mv -f "$NAME.new" "$NAME"
chmod +x "$NAME"
ENTRY=~/.local/share/applications/jplacer.desktop
if [ -f "$ENTRY" ]; then
    sed -i "s|^Exec=.*|Exec=\"$HOME/Downloads/$NAME\"|" "$ENTRY"
    command -v update-desktop-database >/dev/null && update-desktop-database ~/.local/share/applications || true
fi
echo "on $(hostname): ~/Downloads/$NAME"
REMOTE
