# Sourced by the packaging scripts. Sets:
#   PLAIN_VERSION  the version in CMakeLists.txt -- the last release, or the one being cut
#   VERSION        what is being packaged: $JPLACER_VERSION when a beta build sets it, else PLAIN_VERSION
#   next_patch     a function: next_patch 0.1.4 -> 0.1.5
_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLAIN_VERSION="$(sed -n 's/^project(jplacer VERSION \([0-9.]*\).*/\1/p' "$_root/CMakeLists.txt")"
[ -n "$PLAIN_VERSION" ] || { echo "could not read the version out of CMakeLists.txt" >&2; exit 1; }
VERSION="${JPLACER_VERSION:-$PLAIN_VERSION}"
unset _root
next_patch() { local IFS='.'; read -r a b c <<< "${1%%[-+]*}"; echo "$a.$b.$((c + 1))"; }
