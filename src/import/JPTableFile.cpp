// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTableFile.h"

#include "JPImportField.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

inline namespace jf {

namespace {

constexpr int kHeaderSearch = 50;        // lines the header is looked for in
constexpr int kKnownForHeader = 2;       // known columns that make a row the header

void appendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
    else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
}

bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        const int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + size_t(n) >= s.size()) return false;
        for (int k = 1; k <= n; ++k)
            if ((uint8_t(s[i + size_t(k)]) >> 6) != 2) return false;
        i += size_t(n) + 1;
    }
    return true;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && uint8_t(s[b]) <= ' ') ++b;
    while (e > b && uint8_t(s[e - 1]) <= ' ') --e;
    return s.substr(b, e - b);
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> v;
    std::string line;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n' || c == '\r') {
            v.push_back(line);
            line.clear();
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            line += c;
        }
    }
    if (!line.empty()) v.push_back(line);
    return v;
}

// A line's text without a leading comment mark ("# Ref Val …" is a header in KiCad's .pos).
std::string uncommented(const std::string& line) {
    std::string t = trim(line);
    while (!t.empty() && t[0] == '#') t.erase(0, 1);
    return trim(t);
}

int known(const std::vector<std::string>& cells) {
    int n = 0;
    for (const std::string& c : cells) {
        const JPImportField::Id id = JPImportField::guess(c);
        n += id != JPImportField::Id::Extra && id != JPImportField::Id::Ignore;
    }
    return n;
}

} // namespace

std::string JPTableFile::utf8(const std::string& bytes) {
    if (bytes.size() >= 2 && uint8_t(bytes[0]) == 0xFF && uint8_t(bytes[1]) == 0xFE) {
        std::string out;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            uint32_t c = uint8_t(bytes[i]) | (uint32_t(uint8_t(bytes[i + 1])) << 8);
            if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes.size()) {
                const uint32_t lo = uint8_t(bytes[i + 2]) | (uint32_t(uint8_t(bytes[i + 3])) << 8);
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
            appendUtf8(out, c);
        }
        return out;
    }
    if (bytes.size() >= 3 && uint8_t(bytes[0]) == 0xEF && uint8_t(bytes[1]) == 0xBB && uint8_t(bytes[2]) == 0xBF)
        return bytes.substr(3);
    if (validUtf8(bytes)) return bytes;
    std::string out;   // Latin-1
    for (const char c : bytes) appendUtf8(out, uint8_t(c));
    return out;
}

std::vector<std::string> JPTableFile::split(const std::string& line, char separator) {
    std::vector<std::string> cells;
    std::string cell;
    bool quoted = false, any = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cell += '"'; ++i; }
            else if (c == '"') quoted = false;
            else cell += c;
            continue;
        }
        const bool sep = separator == ' ' ? (c == ' ' || c == '\t') : c == separator;
        if (c == '"') { quoted = true; any = true; }
        else if (sep) {
            if (separator == ' ' && !any && trim(cell).empty()) continue;   // runs of spaces are one
            cells.push_back(trim(cell));
            cell.clear();
            any = false;
        } else {
            cell += c;
            any = any || uint8_t(c) > ' ';
        }
    }
    if (separator != ' ' || any || !trim(cell).empty()) cells.push_back(trim(cell));
    return cells;
}

bool JPTableFile::read(const std::string& path, JPTableFile& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "Cannot read " + path;
        return false;
    }
    const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!parse(utf8(bytes), out, error)) return false;
    out.path = path;
    return true;
}

bool JPTableFile::parse(const std::string& text, JPTableFile& out, std::string& error) {
    const std::vector<std::string> all = lines(text);
    // The separator: of those the header row splits into the most known columns by, else the one that
    // splits the first rows most evenly into the most cells.
    char best = 0;
    int bestKnown = -1, bestLine = -1;
    for (const char sep : { ',', ';', '\t', ' ' })
        for (int i = 0; i < kHeaderSearch && size_t(i) < all.size(); ++i) {
            const std::string t = uncommented(all[size_t(i)]);
            if (t.empty()) continue;
            const int k = known(split(t, sep));
            if (k >= kKnownForHeader && k > bestKnown) {
                best = sep;
                bestKnown = k;
                bestLine = i;
            }
        }
    if (bestLine < 0) {
        // No header of known names: the first row that is not a comment, by the separator giving most cells.
        for (int i = 0; size_t(i) < all.size(); ++i)
            if (!trim(all[size_t(i)]).empty() && trim(all[size_t(i)])[0] != '#') {
                bestLine = i;
                break;
            }
        if (bestLine < 0) {
            error = "No table in it: every line is empty or a comment";
            return false;
        }
        size_t most = 0;
        for (const char sep : { ',', ';', '\t', ' ' }) {
            const size_t n = split(trim(all[size_t(bestLine)]), sep).size();
            if (n > most) {
                most = n;
                best = sep;
            }
        }
    }
    out.separator = best;
    out.headerLine = bestLine;
    out.header = split(uncommented(all[size_t(bestLine)]), best);
    out.rows.clear();
    for (size_t i = size_t(bestLine) + 1; i < all.size(); ++i) {
        const std::string t = trim(all[i]);
        if (t.empty() || t[0] == '#') continue;
        std::vector<std::string> row = split(t, best);
        if (row.size() < out.header.size()) row.resize(out.header.size());
        out.rows.push_back(std::move(row));
    }
    if (out.header.empty()) {
        error = "No columns in its header";
        return false;
    }
    return true;
}

} // inline namespace jf
