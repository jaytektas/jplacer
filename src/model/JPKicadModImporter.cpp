// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPKicadModImporter.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>

inline namespace jf {

namespace {

// Each "(pad …)" expression whole, its whitespace run into single spaces.
std::vector<std::string> padExpressions(const std::string& text) {
    std::vector<std::string> out;
    for (size_t at = text.find("(pad "); at != std::string::npos; at = text.find("(pad ", at + 1)) {
        int depth = 0;
        bool quoted = false;
        std::string e;
        size_t i = at;
        for (; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '"' && (i == 0 || text[i - 1] != '\\')) quoted = !quoted;
            if (!quoted && c == '(') ++depth;
            if (!quoted && c == ')') --depth;
            const bool space = std::isspace(static_cast<unsigned char>(c));
            if (space) {
                if (!e.empty() && e.back() != ' ') e += ' ';
            } else {
                e += c;
            }
            if (depth == 0) break;
        }
        // KiCad writes "( size" never, but closing brackets with a space before them are tidied.
        std::string tidy;
        for (size_t k = 0; k < e.size(); ++k)
            if (!(e[k] == ' ' && k + 1 < e.size() && e[k + 1] == ')')) tidy += e[k];
        out.push_back(tidy);
        at = i;
    }
    return out;
}

double number(const std::string& s) {
    return std::strtod(s.c_str(), nullptr);
}

} // namespace

std::vector<JPFootprint::Pad> JPKicadModImporter::parse(const std::string& text) {
    // OpenPnP's expressions, as its KicadModImporter has them.
    static const std::regex head(R"re(^\(pad\s"?(\w*)"?\s(\w*)\s(\w*))re");
    static const std::regex size(R"re(\(size ([\-0-9.]*) ([\-0-9\.]*)\))re");
    static const std::regex at(R"re(\(at ([\-0-9.]*) ([\-0-9.]*)\s?([^\)]*)\))re");
    static const std::regex ratio(R"re(\(roundrect_rratio ([\-0-9.]*)\))re");
    static const std::regex layers(R"re(\(layers ([^\)]*)\))re");
    std::vector<JPFootprint::Pad> pads;
    for (const std::string& def : padExpressions(text)) {
        std::smatch m;
        if (!std::regex_search(def, m, head)) continue;
        const std::string name = m[1], type = m[2], shape = m[3];
        std::smatch l;
        const bool topCu = std::regex_search(def, l, layers)
                           && (l[1].str().find("F.Cu") != std::string::npos || l[1].str().find("*.Cu") != std::string::npos);
        if (type != "smd" || !topCu) continue;
        JPFootprint::Pad pad;
        pad.name = name;
        if (std::regex_search(def, m, size)) {
            pad.width = number(m[1]);
            pad.height = number(m[2]);
        }
        if (std::regex_search(def, m, at)) {
            pad.x = number(m[1]);
            pad.y = number(m[2]) * -1;   // KiCad's Y runs down
            if (m[3].length() > 0) pad.rotation = number(m[3]);
        }
        if (shape == "rect") pad.roundness = 0;
        else if (shape == "circle" || shape == "oval") pad.roundness = 100;
        else if (shape == "roundrect") pad.roundness = std::regex_search(def, m, ratio) ? number(m[1]) * 100 : 0;
        else continue;   // a shape OpenPnP does not take
        pads.push_back(pad);
    }
    return pads;
}

bool JPKicadModImporter::read(const std::string& path, std::vector<JPFootprint::Pad>& pads, std::string& error) {
    std::ifstream f(path);
    if (!f) {
        error = "Kicad Footprint Load Error" + path + ": cannot be read";
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    pads = parse(text);
    return true;
}

} // inline namespace jf
