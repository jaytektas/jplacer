// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's View > Language: its .properties read (escapes, continued lines,
// UTF-8 or \u escapes), and a label shown in the chosen language where
// OpenPnP's English for it is jplacer's; English, or text it has no
// translation for, as it is.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "common/JPTranslations.h"

#include <j/core/TranslationEngine.h>

#include <string>

using namespace jf;

int main() {
    {
        const auto p = JPTranslations::parse("# a comment\n"
                                             "A.b=Line one \\\n"
                                             "    and two\n"
                                             "C.d = \\u0416 colon\\: here\n"
                                             "E.f:value\n");
        assert(p.at("A.b") == "Line one and two");
        assert(p.at("C.d") == "\xD0\x96 colon: here");
        assert(p.at("E.f") == "value");
    }
    const std::string dir = std::string(JPLACER_TESTDATA_DIR) + "/../../translations";
    std::string why;
    assert(JPTranslations::load(dir, "en", why) && tr("Machine Setup") == "Machine Setup");
    assert(JPTranslations::load(dir, "ru", why));
    const std::string setup = tr("Machine Setup");
    assert(setup != "Machine Setup" && !setup.empty());
    assert(tr("Machine Setup:") == setup + ":");   // its ending kept
    assert(tr("No such words in OpenPnP") == "No such words in OpenPnP");
    assert(!JPTranslations::codepoints().empty() && JPTranslations::codepoints().front() >= 0x400);
    assert(!JPTranslations::load(dir, "xx", why) && !why.empty());
    return 0;
}
