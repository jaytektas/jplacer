// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's LocalisationTest: the translations file (translations.properties, OpenPnP's, shipped with jplacer) is in
// normalised form: read as Java's Properties.load reads it and written back as Java's Properties.store(Writer)
// writes it, keys sorted, it is the same, its comments aside.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "common/JPTranslations.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace jf;

namespace {

// Java's Properties.saveConvert, as store(Writer) calls it (no \u escapes): `escapeSpace` for a key.
std::string saveConvert(const std::string& s, bool escapeSpace) {
    std::string out;
    for (size_t x = 0; x < s.size(); ++x) {
        const char c = s[x];
        if (static_cast<unsigned char>(c) > 61 && static_cast<unsigned char>(c) < 127) {
            if (c == '\\') out += "\\\\";
            else out += c;
            continue;
        }
        switch (c) {
            case ' ': out += (x == 0 || escapeSpace) ? "\\ " : " "; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\f': out += "\\f"; break;
            case '=': case ':': case '#': case '!': out += '\\'; out += c; break;
            default: out += c;
        }
    }
    return out;
}

} // namespace

int main() {
    const std::string filename = std::string(JPLACER_TESTDATA_DIR) + "/../../translations/translations.properties";
    std::ifstream in(filename);
    assert(in);
    // The body, its comments taken out.
    std::string original, line;
    while (std::getline(in, line))
        if (line.rfind("#", 0) != 0) original += line + "\n";
    // Read, then written back sorted, as Java does.
    std::string normalised;
    for (const auto& [key, value] : JPTranslations::parse(original)) normalised += saveConvert(key, true) + "=" + saveConvert(value, false) + "\n";
    if (normalised != original) {
        size_t i = 0;
        while (i < normalised.size() && i < original.size() && normalised[i] == original[i]) ++i;
        auto lineAt = [](const std::string& s, size_t at) {
            const size_t a = s.rfind('\n', at == 0 ? 0 : at - 1), b = s.find('\n', at);
            return s.substr(a == std::string::npos ? 0 : a + 1, b == std::string::npos ? std::string::npos : b - (a == std::string::npos ? 0 : a + 1));
        };
        std::fprintf(stderr, "%s is not in normalised form; first found:\n%s\nexpected:\n%s\n", filename.c_str(), lineAt(original, i).c_str(),
                     lineAt(normalised, i).c_str());
    }
    assert(normalised == original);
    return 0;
}
