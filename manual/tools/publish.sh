#!/usr/bin/env bash
# Build the manual and publish it to GitHub Pages (the gh-pages branch): https://jaytektas.github.io/jplacer/
#
#   manual/tools/publish.sh
#
# Public: run it with a release, from the commit the release was built from.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
"$ROOT/manual/tools/build.sh"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cp -r "$ROOT/manual/site/." "$T/"
touch "$T/.nojekyll"
git -C "$T" init -q -b gh-pages
git -C "$T" add -A
git -C "$T" -c user.name="$(git -C "$ROOT" config user.name)" -c user.email="$(git -C "$ROOT" config user.email)" \
    commit -q -m "manual from $(git -C "$ROOT" rev-parse --short HEAD)"
git -C "$T" push -q -f "$(git -C "$ROOT" remote get-url origin)" gh-pages
# Pages is switched on the first time, serving the gh-pages branch.
gh api "repos/jaytektas/jplacer/pages" >/dev/null 2>&1 \
    || gh api -X POST "repos/jaytektas/jplacer/pages" -f "source[branch]=gh-pages" -f "source[path]=/" >/dev/null
echo "  manual published: https://jaytektas.github.io/jplacer/  (from $(git -C "$ROOT" rev-parse --short HEAD))"
