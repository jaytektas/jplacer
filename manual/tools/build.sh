#!/usr/bin/env bash
# Build the manual: manual/site/, which the AppImage ships and Help > User Manual opens.
#
#   manual/tools/build.sh
#
# What's New is generated from CHANGES.md; a beta build (JPLACER_VERSION set) heads the unreleased
# lines with the beta's version. The tools come from a venv made on the first run (manual/.venv, pinned by requirements.txt). The
# source notes are checked first, and mkdocs builds --strict, so a broken link fails the build.
set -euo pipefail
MANUAL="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -x "$MANUAL/.venv/bin/mkdocs" ] || { python3 -m venv "$MANUAL/.venv" && "$MANUAL/.venv/bin/pip" install -q -r "$MANUAL/requirements.txt"; }
python3 "$MANUAL/../packaging/changes.py" page ${JPLACER_VERSION:+"$JPLACER_VERSION"} > "$MANUAL/docs/whats-new.md"
python3 "$MANUAL/tools/check_src.py"
( cd "$MANUAL" && .venv/bin/mkdocs build --strict --quiet )
echo "  manual: $MANUAL/site/index.html"
