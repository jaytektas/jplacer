# Sourced by the packaging scripts: sets VERSION (e.g. 0.2.0 or 0.2.0-beta.1) and PRERELEASE from
# CMakeLists.txt, the one place the version is written.
_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
_base="$(sed -n 's/.*project(jplacer VERSION \([0-9.]*\).*/\1/p' "$_root/CMakeLists.txt")"
PRERELEASE="$(sed -n 's/^set(JPLACER_PRERELEASE "\(.*\)")$/\1/p' "$_root/CMakeLists.txt")"
[ -n "$_base" ] || { echo "could not read the version out of CMakeLists.txt" >&2; exit 1; }
VERSION="$_base${PRERELEASE:+-$PRERELEASE}"
unset _root _base
