// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPXmlWriter.h"

#include "common/JPWholeFile.h"

#include <charconv>
#include <cmath>
#include <cstdlib>

inline namespace jf {

namespace {

constexpr const char* kIndent = "   ";

std::string escaped(const std::string& s, bool attribute) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += attribute ? "&quot;" : "\""; break;
            case '\n': out += attribute ? "&#10;" : "\n"; break;
            default: out += c;
        }
    }
    return out;
}

void emit(const JPXmlNode& n, int depth, std::string& out) {
    for (int i = 0; i < depth; ++i) out += kIndent;
    out += "<" + n.name;
    for (const auto& [k, v] : n.attributes) out += " " + k + "=\"" + escaped(v, true) + "\"";
    if (n.children.empty() && n.text.empty()) {
        out += "/>\n";
        return;
    }
    out += ">";
    if (n.children.empty()) {
        out += escaped(n.text, false) + "</" + n.name + ">\n";
        return;
    }
    out += "\n";
    for (const JPXmlNode& c : n.children) emit(c, depth + 1, out);
    for (int i = 0; i < depth; ++i) out += kIndent;
    out += "</" + n.name + ">\n";
}

} // namespace

std::string JPXmlWriter::text(const JPXmlNode& root) {
    std::string out;
    emit(root, 0, out);
    return out;
}

bool JPXmlWriter::write(const std::string& path, const JPXmlNode& root, std::string& error, bool finalNewline) {
    std::string t = text(root);
    if (!finalNewline && !t.empty()) t.pop_back();
    return JPWholeFile::write(path, t, error);
}

std::string JPXmlWriter::number(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
    if (v == 0) return std::signbit(v) ? "-0.0" : "0.0";
    const double a = std::fabs(v);
    char buf[64];
    if (a >= 1e-3 && a < 1e7) {
        // Plain: the shortest digits that read back the same, with a ".0" when whole.
        const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
        std::string s(buf, r.ptr);
        if (s.find('.') == std::string::npos) s += ".0";
        return s;
    }
    // Java's computerized scientific notation: "1.0E-4", "1.2345E7".
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
    std::string s(buf, r.ptr);
    const size_t e = s.find('e');
    std::string mantissa = s.substr(0, e);
    if (mantissa.find('.') == std::string::npos) mantissa += ".0";
    const int exponent = std::atoi(s.c_str() + e + 1);
    return mantissa + "E" + std::to_string(exponent);
}

} // inline namespace jf
