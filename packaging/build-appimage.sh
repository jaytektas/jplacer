#!/usr/bin/env bash
# Package the Linux build as the release file: dist/jplacer-<version>-x86_64.AppImage.
#
#   packaging/build-appimage.sh        (after: cmake --build build)
#
# The AppImage is the Linux release that UPDATES ITSELF: JFramework's JAppUpdater swaps the file named by
# $APPIMAGE, and picks the release asset ending in -x86_64.AppImage. So the name here is the name a
# release must carry.
#
# Needs appimagetool (github.com/AppImage/appimagetool) on PATH, and rsvg-convert for the icon.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/jplacer"
DIST="$ROOT/dist"
source "$ROOT/packaging/version.sh"

fail() { echo "build-appimage: $*" >&2; exit 1; }

command -v appimagetool >/dev/null || fail "no appimagetool on PATH"
command -v rsvg-convert >/dev/null || fail "no rsvg-convert (apt install librsvg2-bin)"
[ -x "$BIN" ] || fail "not built: $BIN -- run cmake --build build first"

OUT="$DIST/jplacer-$VERSION-x86_64.AppImage"
APPDIR="$DIST/jplacer.AppDir"
rm -rf "$APPDIR" "$OUT"
mkdir -p "$APPDIR/usr/bin"

install -m755 "$BIN" "$APPDIR/usr/bin/jplacer"
# The unstripped executable kept here (never published), by version: a crash's addresses on a machine running
# this AppImage (the jplacer (gdb) launcher's backtrace) read back to functions with it. Same build ID.
mkdir -p "$ROOT/dist/symbols"
cp "$BIN" "$ROOT/dist/symbols/jplacer-$VERSION"
strip "$APPDIR/usr/bin/jplacer"

# The user manual travels beside the executable: Help > User Manual opens usr/bin/manual/index.html.
# Not optional -- an AppImage without it has a Help menu that cannot help.
[ -f "$ROOT/manual/site/index.html" ] || fail "no built manual -- run manual/tools/build.sh first"
cp -r "$ROOT/manual/site" "$APPDIR/usr/bin/manual"
# Firmware profiles travel beside the executable too: JPFirmwareProfile reads usr/bin/profiles.
cp -r "$ROOT/profiles" "$APPDIR/usr/bin/profiles"
# OpenPnP's translations too: View > Language reads usr/bin/translations (JPTranslations).
cp -r "$ROOT/translations" "$APPDIR/usr/bin/translations"
# OpenPnP's defaults: its configuration and test picture, for a first start (JPlacerPaths::bundled).
cp -r "$ROOT/openpnp-defaults" "$APPDIR/usr/bin/openpnp-defaults"
# Only the files kept in the repository: a library or runs database left by a configuration read there is not.
find "$APPDIR/usr/bin/openpnp-defaults" \( -name '*.db' -o -name '*.db-wal' -o -name '*.db-shm' \) -delete
# OpenPnP's icons too: JPOpenPnpIcons reads usr/bin/icons.
cp -r "$ROOT/icons" "$APPDIR/usr/bin/icons"
# OpenPnP's illustrations too: JPIllustrations reads usr/bin/illustrations.
cp -r "$ROOT/illustrations" "$APPDIR/usr/bin/illustrations"
# And its BlindsFeeder OpenSCAD models: a blinds feeder's Extract 3D-Printing Files reads usr/bin/openscad.
cp -r "$ROOT/openscad" "$APPDIR/usr/bin/openscad"

# The icon twice: the PNG is what the AppImage itself shows, and both are what jplacer copies into the
# icon theme when it adds itself to the applications menu (src/app/JPlacerLauncher.cpp).
rsvg-convert -w 256 -h 256 -o "$APPDIR/jplacer.png" "$ROOT/packaging/jplacer.svg"
cp "$ROOT/packaging/jplacer.svg" "$APPDIR/jplacer.svg"
cp "$ROOT/packaging/jplacer.desktop" "$APPDIR/jplacer.desktop"

cat > "$APPDIR/AppRun" <<'RUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/jplacer" "$@"
RUN
chmod 755 "$APPDIR/AppRun"

ARCH=x86_64 appimagetool --no-appstream "$APPDIR" "$OUT" >/dev/null 2>&1 \
    || { ARCH=x86_64 appimagetool --no-appstream "$APPDIR" "$OUT"; fail "appimagetool failed"; }
rm -rf "$APPDIR"
echo "built: $OUT"
