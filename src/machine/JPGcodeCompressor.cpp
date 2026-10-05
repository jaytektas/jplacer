// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPGcodeCompressor.h"

#include <cstdlib>

inline namespace jf {

namespace {

// A number's trailing zeros (and a point left last) cut away, when compressing
// and there is a digit before them.
int cutTrailingZeros(int trailing, std::string& out, bool compress) {
    if (compress && trailing > 0 && int(out.size()) - trailing > 0) {
        const char c = out[out.size() - size_t(trailing) - 1];
        if (c >= '0' && c <= '9') out.erase(out.size() - size_t(trailing));
    }
    return 0;
}

} // namespace

std::string JPGcodeCompressor::process(const std::string& line, const Settings& s) {
    std::string command = line;
    if (s.removeComments || s.compress) {
        bool insideComment = false, decimal = false;
        int trailing = 0;
        std::string out;
        for (size_t col = 0; col < command.size();) {
            const char ch = command[col++];
            if (ch == ' ') {
                // Spaces may stand inside a number: gone, compressing.
                if (s.compress) continue;
            } else if (s.excludes.find(ch) != std::string::npos) {
                trailing = cutTrailingZeros(trailing, out, s.compress);
                decimal = false;
                // From the left-most to the right-most exclude character, as it is.
                size_t pos = col - 1;
                for (const char e : s.excludes)
                    if (e != ' ')
                        if (const size_t p = command.rfind(e); p != std::string::npos && p >= pos) pos = p;
                if (pos < col) {   // no partner: the rest of the line as it is
                    out += command.substr(col - 1);
                    break;
                }
                out += command.substr(col - 1, pos + 1 - (col - 1));
                col = pos + 1;
                continue;
            } else if (ch == '(') {
                trailing = cutTrailingZeros(trailing, out, s.compress);
                decimal = false;
                insideComment = true;
                if (s.removeComments) continue;
            } else if (ch == ')') {
                insideComment = false;
                if (s.removeComments) continue;
            } else if (insideComment) {
                if (s.removeComments) continue;
            } else if (ch == ';') {
                trailing = cutTrailingZeros(trailing, out, s.compress);
                decimal = false;
                if (!s.removeComments) out += command.substr(col - 1);
                break;
            } else if (ch == '.') {
                decimal = true;
                trailing = 1;   // the point counts as one to cut
            } else if (ch >= '1' && ch <= '9') {
                trailing = 0;
            } else if (ch == '0') {
                if (decimal) ++trailing;
            } else {
                trailing = cutTrailingZeros(trailing, out, s.compress);
                decimal = false;
            }
            out += ch;
        }
        cutTrailingZeros(trailing, out, s.compress);
        command = out;
    }
    if (s.backslashEscapes) command = unescape(command);
    return command;
}

std::string JPGcodeCompressor::unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '\\' && i < s.size()) {
            c = s[i++];
            const char lower = char(c | 0x20);
            if (lower == 'u') {
                const std::string hex = s.substr(i, 4);
                char* end = nullptr;
                const long code = hex.size() == 4 ? std::strtol(hex.c_str(), &end, 16) : -1;
                if (code >= 0 && end == hex.c_str() + 4) {
                    i += 4;
                    // The character, as UTF-8.
                    if (code < 0x80) {
                        out += char(code);
                    } else if (code < 0x800) {
                        out += char(0xC0 | (code >> 6));
                        out += char(0x80 | (code & 0x3F));
                    } else {
                        out += char(0xE0 | (code >> 12));
                        out += char(0x80 | ((code >> 6) & 0x3F));
                        out += char(0x80 | (code & 0x3F));
                    }
                    continue;
                }
                out += '\\';   // not four hex digits: passed along as written
            } else if (lower == 't') {
                c = '\t';
            } else if (lower == 'b') {
                c = '\b';
            } else if (lower == 'n') {
                c = '\n';
            } else if (lower == 'r') {
                c = '\r';
            } else if (lower == 'f') {
                c = '\f';
            } else {
                out += '\\';
            }
        }
        out += c;
    }
    return out;
}

} // inline namespace jf
