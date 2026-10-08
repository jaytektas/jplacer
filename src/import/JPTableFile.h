// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// A CAD tool's table as text: a CPL, a BOM, a supplier's export. Read
// whatever it is: separated by commas, semicolons, tabs, or runs of spaces
// (KiCad's .pos); quoted or not; UTF-8, UTF-16 (with its mark) or Latin-1.
// Its header is the first row (of the first 50) naming two or more known
// columns (JPImportField), a "#" before it allowed (KiCad's "# Ref Val …");
// else the first row that is not a comment. Rows after it, comments ("#")
// and empty ones passed over, each padded to the header's width.
class JPTableFile {
public:
    std::string                           path;
    char                                  separator = ',';   // ' ' for runs of spaces and tabs
    int                                   headerLine = -1;   // its line in the file (0 based)
    std::vector<std::string>              header;
    std::vector<std::vector<std::string>> rows;
    // Its comment lines ("#"), their text after the marks: what a tool says of the table ("Unit = inches").
    std::vector<std::string>              comments;

    // False (`error`) when it cannot be read or holds no table.
    static bool read(const std::string& path, JPTableFile& out, std::string& error);
    // From text, as read() reads a file's.
    static bool parse(const std::string& text, JPTableFile& out, std::string& error);
    // The text of a file of any of those encodings, as UTF-8.
    static std::string utf8(const std::string& bytes);
    // A line split by `separator` (' ': runs of whitespace), quotes honoured, each cell trimmed.
    static std::vector<std::string> split(const std::string& line, char separator);
};

} // inline namespace jf
