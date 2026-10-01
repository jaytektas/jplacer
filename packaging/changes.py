#!/usr/bin/env python3
"""CHANGES.md for the release scripts.

    changes.py unreleased            print the Unreleased lines; exit 1 if there are none
    changes.py prepare <version>     move Unreleased into "## <version>" (folding into it if it exists)
    changes.py notes <version>       print that version's section, for the GitHub release notes
    changes.py page [<beta>]         print the manual's What's New page (manual/tools/build.sh writes it);
                                     a beta build heads the Unreleased lines with the beta's version
"""
import re
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parent.parent / "CHANGES.md"
HEAD = re.compile(r"^## (.+?)\s*$")


def split(text):
    """(preamble lines, [(heading, [body lines])]) in file order."""
    pre, sections = [], []
    for line in text.splitlines():
        m = HEAD.match(line)
        if m:
            sections.append((m.group(1), []))
        elif sections:
            sections[-1][1].append(line)
        else:
            pre.append(line)
    return pre, sections


def items(body):
    return [l for l in body if l.strip()]


def join(pre, sections):
    out = list(pre)
    for heading, body in sections:
        out.append(f"## {heading}")
        out.append("")
        if items(body):
            out.extend(items(body))
            out.append("")
    return "\n".join(out).rstrip() + "\n"


def section(sections, heading):
    for h, b in sections:
        if h == heading:
            return b
    return None


def main(argv):
    pre, sections = split(PATH.read_text())
    cmd = argv[1] if len(argv) > 1 else ""
    unreleased = section(sections, "Unreleased")
    if unreleased is None:
        sys.exit("CHANGES.md has no '## Unreleased' section")

    if cmd == "unreleased":
        lines = items(unreleased)
        print("\n".join(lines))
        return 0 if lines else 1

    if cmd == "prepare" and len(argv) == 3:
        version = argv[2]
        new = items(unreleased)
        existing = section(sections, version)
        if existing is None:
            if not new:
                sys.exit("nothing under '## Unreleased' in CHANGES.md")
            at = next(i for i, (h, _) in enumerate(sections) if h == "Unreleased") + 1
            sections.insert(at, (version, new))
        else:
            existing[:] = items(existing) + new     # a re-run: fold any new lines into the version
        unreleased[:] = []
        PATH.write_text(join(pre, sections))
        return 0

    if cmd == "notes" and len(argv) == 3:
        body = section(sections, argv[2])
        if body is None or not items(body):
            sys.exit(f"CHANGES.md has no notes for {argv[2]}")
        print("\n".join(items(body)))
        return 0

    if cmd == "page":
        out = ["# What's new", "",
               "<!-- Generated from CHANGES.md by manual/tools/build.sh. Edit CHANGES.md, not this. -->", ""]
        for heading, body in sections:
            lines = items(body)
            if heading == "Unreleased":
                if not lines:
                    continue
                heading = f"{argv[2]} (beta)" if len(argv) == 3 else "Coming in the next version"
            out += [f"## {heading}", ""] + lines + [""]
        print("\n".join(out).rstrip())
        return 0

    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
