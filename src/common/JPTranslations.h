// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's View > Language: its translations (translations/) given to the
// framework's translation engine, which every label, button, tab, menu and
// table draws its text through: shown in the chosen language when OpenPnP's
// English for it is jplacer's (with a trailing ':' or '…' or not). Chosen at
// start (a change takes effect the next time, as OpenPnP's).
class JPTranslations {
public:
    struct Language {
        const char* code;   // "ru", "zh_CN"; "en" for English
        const char* name;   // as the menu shows it
    };
    // English first, then OpenPnP's others.
    static const std::vector<Language>& languages();

    // Take the language `code`'s translations from `dir` (translations*.properties)
    // into the translation engine; false with why. English needs none.
    static bool load(const std::string& dir, const std::string& code, std::string& why);
    // The folder they are in: beside the executable, or the source tree's beside the build.
    static std::string directory();

    // Every codepoint the chosen language's text uses, for the font.
    static std::vector<uint32_t> codepoints();

    // A .properties file's entries (a value continued over lines with '\',
    // \uXXXX and the other escapes undone; UTF-8 kept).
    static std::map<std::string, std::string> parse(const std::string& text);
};

} // inline namespace jf
