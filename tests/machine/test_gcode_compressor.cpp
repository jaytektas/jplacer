// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A line made ready to send as OpenPnP's GcodeDriver makes it: comments
// removed, compressed (outside the exclude characters), backslash escapes
// made characters; with nothing asked, as it is.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPGcodeCompressor.h"

using namespace jf;

int main() {
    using S = JPGcodeCompressor::Settings;
    const std::string line = "G1 (move to) X100.0000   Y20.1000 F0.50 ; move to the given X, Y";
    assert(JPGcodeCompressor::process(line, S {}) == line);
    // OpenPnP's own examples.
    S remove;
    remove.removeComments = true;
    // (A space inside a comment stays, as OpenPnP's does: spaces are looked at first.)
    assert(JPGcodeCompressor::process("G1 (move to) X100 Y20 ; move to the given X, Y", remove) == "G1   X100 Y20 ");
    S compress;
    compress.compress = true;
    assert(JPGcodeCompressor::process("G1  X100.0000     Y20.1000    ", compress) == "G1X100Y20.1");
    S both = compress;
    both.removeComments = true;
    assert(JPGcodeCompressor::process(line, both) == "G1X100Y20.1F0.5");
    // A whole number's zeros stay; a point with nothing after goes.
    assert(JPGcodeCompressor::process("G0 X10 Y100. Z0.0", compress) == "G0X10Y100Z0");
    // Between the exclude characters: as written.
    assert(JPGcodeCompressor::process("M118 \"Hello 1.50 world\" X1.50", both) == "M118\"Hello 1.50 world\"X1.5");
    assert(JPGcodeCompressor::process("G1 X[1.0 + 2.0] Y2.0", both) == "G1X[1.0 + 2.0]Y2");
    // Escapes.
    S esc;
    esc.backslashEscapes = true;
    assert(JPGcodeCompressor::process("A\\tB\\nC\\u0041\\x", esc) == "A\tB\nCA\\x");
    assert(JPGcodeCompressor::unescape("\\u00e9") == "\xC3\xA9");
    assert(JPGcodeCompressor::unescape("\\u12") == "\\u12");
    return 0;
}
