// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A camera's reticle is kept as text with its style (Reticle Style…): its colour and line width and a
// footprint's, written only as they differ from the theme's, so a reticle kept before it had a style reads
// back as it was; a bad width or colour is the theme's.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPReticle.h"

#include <j/core/JStyle.h>

using namespace jf;

int main() {
    JPReticle r;
    r.kind = JPReticle::Kind::Grid;
    r.spacingMm = 0.5;
    assert(r.toText() == "grid 0.5 1");   // the theme's style: nothing more written

    r.lineWidth = 3;
    r.colour = "#ff8000";
    r.footprintWidth = 2;
    r.footprintColour = "#00ff00";
    const JPReticle back = JPReticle::fromText(r.toText());
    assert(back.kind == JPReticle::Kind::Grid && back.spacingMm == 0.5 && back.sizeMm == 1.0);
    assert(back.lineWidth == 3 && back.colour == "#ff8000" && back.footprintWidth == 2 && back.footprintColour == "#00ff00");

    // Kept before there was a style.
    const JPReticle old = JPReticle::fromText("circle 1 2");
    assert(old.kind == JPReticle::Kind::Circle && old.sizeMm == 2 && old.lineWidth == 1 && old.colour.empty());

    // Bad words: the theme's.
    const JPReticle bad = JPReticle::fromText("cross 1 1 line=9 colour=red footprint-line=0 footprint-colour=#12345");
    assert(bad.lineWidth == 1 && bad.colour.empty() && bad.footprintWidth == 1 && bad.footprintColour.empty());

    // A colour named, else the theme's.
    const JColor named = JPReticle::ink("#102030", Colors::Accent);
    assert(named.r == 0x10 && named.g == 0x20 && named.b == 0x30);
    const JColor theme = JPReticle::ink("", Colors::Accent);
    assert(theme.r == Colors::Accent[0] && theme.g == Colors::Accent[1] && theme.b == Colors::Accent[2]);
    return 0;
}
