// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// OpenPnP's GcodeDriver.preProcessCommand: a line made ready to send, as the
// controller's settings say. Comments removed ("(...)" and from ";" on);
// compressed (spaces and a number's trailing zeros and point taken out:
// "G1 X100.0000 Y20.1000" goes as "G1X100Y20.1"), except from the first to
// the last of the exclude characters (quotes, brackets); and backslash
// escapes (\t \b \n \r \f, \uXXXX) made the characters they stand for.
class JPGcodeCompressor {
public:
    struct Settings {
        bool        removeComments = false;
        bool        compress = false;
        std::string excludes = "[]\"";
        bool        backslashEscapes = false;
    };
    static std::string process(const std::string& line, const Settings& settings);
    static std::string unescape(const std::string& s);
};

} // inline namespace jf
