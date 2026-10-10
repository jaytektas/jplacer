// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardImporter.h"

#include "JPPartMatcher.h"

#include "JPAltiumCsvImporter.h"
#include "JPDipTraceImporter.h"
#include "JPEagleBoardImporter.h"
#include "JPEagleMountsmdUlpImporter.h"
#include "JPKicadPosImporter.h"
#include "JPLabcenterProteusImporter.h"
#include "JPReferenceCsvImporter.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

inline namespace jf {

namespace {

void appendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    } else {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
}

std::string fileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(path + " (No such file or directory)");
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string latin1ToUtf8(const std::string& bytes) {
    std::string out;
    for (unsigned char b : bytes) appendUtf8(out, b);
    return out;
}

} // namespace

bool JPBoardImporter::read(const std::vector<std::string>& files, const std::vector<bool>& options,
                           JPConfiguration& config, JPBoard& out, std::string& error) const {
    // A file for each it reads (empty: none), an option for each it has.
    if (files.size() != this->files().size() || options.size() != this->options().size()) {
        error = name() + ": " + std::to_string(this->files().size()) + " files and " + std::to_string(this->options().size())
              + " options expected";
        return false;
    }
    try {
        parse(files, options, config, out);
        // The rotation each was given, kept apart from the one placed with (corrections on the machine).
        for (JPPlacement& p : out.placements)
            if (!p.cadRotation) p.cadRotation = p.location.rotation();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::vector<std::unique_ptr<JPBoardImporter>> JPBoardImporter::all() {
    // OpenPnP finds them by class name, so in its menus they stand in that
    // order; it leaves its Gerber solder paste importer out.
    std::vector<std::unique_ptr<JPBoardImporter>> v;
    v.push_back(std::make_unique<JPAltiumCsvImporter>());
    v.push_back(std::make_unique<JPDipTraceImporter>());
    v.push_back(std::make_unique<JPEagleBoardImporter>());
    v.push_back(std::make_unique<JPEagleMountsmdUlpImporter>());
    v.push_back(std::make_unique<JPKicadPosImporter>());
    v.push_back(std::make_unique<JPLabcenterProteusImporter>());
    v.push_back(std::make_unique<JPReferenceCsvImporter>());
    return v;
}

bool JPBoardImporter::exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(path, ec);
}

std::vector<std::string> JPBoardImporter::splitLines(const std::string& text) {
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

std::vector<std::string> JPBoardImporter::lines(const std::string& path, bool latin1) {
    const std::string bytes = fileBytes(path);
    return splitLines(latin1 ? latin1ToUtf8(bytes) : bytes);
}

std::string JPBoardImporter::csvText(const std::string& path) {
    const std::string bytes = fileBytes(path);
    if (bytes.size() < 2 || uint8_t(bytes[0]) != 0xFF || uint8_t(bytes[1]) != 0xFE) return latin1ToUtf8(bytes);
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

std::string JPBoardImporter::trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && uint8_t(s[b]) <= ' ') ++b;
    while (e > b && uint8_t(s[e - 1]) <= ' ') --e;
    return s.substr(b, e - b);
}

std::vector<std::string> JPBoardImporter::split(const std::string& s, char separator) {
    std::vector<std::string> v;
    size_t start = 0;
    for (;;) {
        const size_t at = s.find(separator, start);
        v.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) break;
        start = at + 1;
    }
    while (v.size() > 1 && v.back().empty()) v.pop_back();
    if (v.size() == 1 && v[0].empty() && !s.empty()) v.clear();
    return v;
}

std::vector<std::string> JPBoardImporter::splitWhitespace(const std::string& s) {
    std::vector<std::string> v;
    std::string field;
    bool inSpace = false;
    for (char c : s) {
        if (std::isspace(uint8_t(c))) {
            if (!inSpace) {
                v.push_back(field);
                field.clear();
            }
            inSpace = true;
        } else {
            field += c;
            inSpace = false;
        }
    }
    v.push_back(field);
    while (v.size() > 1 && v.back().empty()) v.pop_back();
    return v;
}

double JPBoardImporter::number(const std::string& s) {
    const std::string t = trim(s);
    if (t.empty()) throw Failure("empty String");
    // Java takes a trailing d or f, and no hex or "inf" spelt C's way.
    std::string n = t;
    if (n.size() > 1 && std::strchr("dDfF", n.back()) && !std::isalpha(uint8_t(n[n.size() - 2]))) n.pop_back();
    bool ok = n == "NaN" || n == "Infinity" || n == "+Infinity" || n == "-Infinity";
    if (ok) return n == "NaN" ? std::nan("") : (n[0] == '-' ? -HUGE_VAL : HUGE_VAL);
    for (char c : n)
        if (!std::isdigit(uint8_t(c)) && !std::strchr("+-.eE", c)) throw Failure("For input string: \"" + s + "\"");
    char* end = nullptr;
    const double v = std::strtod(n.c_str(), &end);
    if (end != n.c_str() + n.size()) throw Failure("For input string: \"" + s + "\"");
    return v;
}

const std::string& JPBoardImporter::at(const std::vector<std::string>& fields, size_t i) {
    if (i >= fields.size())
        throw Failure("Index " + std::to_string(i) + " out of bounds for length " + std::to_string(fields.size()));
    return fields[i];
}

char JPBoardImporter::first(const std::string& s) {
    if (s.empty()) throw Failure("Index 0 out of bounds for length 0");
    return s[0];
}

std::string JPBoardImporter::upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

std::string JPBoardImporter::boardPart(JPConfiguration& config, JPBoard& out, const std::string& partId,
                                       const std::string& packageId, const std::string& value, bool create,
                                       JPPart** part, bool* made, JPPackage** madePackage) {
    if (made) *made = false;
    if (madePackage) *madePackage = nullptr;
    // A part this import made, before it is the library's.
    auto madeOne = [&out](const std::string& id) -> JPPart* {
        for (const auto& p : out.madeParts)
            if (p->id == id) return p.get();
        return nullptr;
    };
    for (JPBoardPart& bp : out.parts())
        if (bp.field("part") == partId) {
            if (part) {
                *part = nullptr;
                if (bp.state == JPBoardPart::State::Matched) {
                    *part = config.libraryPart(bp.libraryPartId);
                    if (!*part) *part = madeOne(bp.libraryPartId);
                }
            }
            return bp.key;
        }
    JPBoardPart bp;
    bp.key = out.newPartKey();
    bp.fields["part"] = partId;
    if (!packageId.empty()) bp.fields["footprint"] = packageId;
    if (!value.empty()) bp.fields["value"] = value;
    // The library's part of that name, else one it knows by these names (JPPartMatcher, strong evidence only).
    JPPart* found = config.libraryPart(partId);
    if (!found) found = JPPartMatcher::automatic(config, bp);
    if (found) {
        bp.state = JPBoardPart::State::Matched;
        bp.libraryPartId = found->id;
        bp.libraryUuid = found->uuid;
    } else if (create) {
        auto p = std::make_shared<JPPart>();
        p->id = partId;
        p->value = value;   // its value as the file wrote it
        if (const JPPackage* k = config.libraryPackage(packageId)) {
            p->packageId = k->id;
        } else {
            p->packageId = packageId;
            bool have = false;
            for (const auto& k : out.madePackages) have = have || k->id == packageId;
            if (!have) {
                auto k = std::make_shared<JPPackage>();
                k->id = packageId;
                out.madePackages.push_back(k);
                if (madePackage) *madePackage = k.get();
            }
        }
        bp.state = JPBoardPart::State::Matched;
        bp.libraryPartId = partId;
        found = p.get();
        out.madeParts.push_back(std::move(p));
        if (made) *made = true;
    }
    out.parts().push_back(bp);
    if (part) *part = found;
    return bp.key;
}

void JPBoardImporter::assign(const JPBoard& out, JPPlacement& p, const std::string& key) {
    p.boardPart = key;
    if (const JPBoardPart* bp = out.part(key)) p.partId = bp->partId();
}

} // inline namespace jf
