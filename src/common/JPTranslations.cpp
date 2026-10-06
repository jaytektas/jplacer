// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTranslations.h"

#include "JPlacerPaths.h"

#include <j/core/TranslationEngine.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

inline namespace jf {

namespace fs = std::filesystem;

namespace {

std::mutex                         s_lock;
std::map<std::string, std::string> s_text;   // English (bare) -> chosen language, for codepoints()

void utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// A label without its trailing ':' or ellipsis, and that ending.
std::pair<std::string, std::string> bare(const std::string& s) {
    std::string t = trim(s);
    for (const char* end : { "\xE2\x80\xA6", "...", ":" })
        if (t.size() > std::strlen(end) && t.compare(t.size() - std::strlen(end), std::strlen(end), end) == 0) {
            const std::string e = t.substr(t.size() - std::strlen(end));
            t.erase(t.size() - std::strlen(end));
            return { trim(t), e };
        }
    return { t, {} };
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

const std::vector<JPTranslations::Language>& JPTranslations::languages() {
    static const std::vector<Language> all = {
        { "en", "English (United States)" }, { "ru", "Russian" }, { "es", "Spanish" }, { "fr", "French" },
        { "it", "Italian" }, { "de", "German" }, { "zh_CN", "Chinese (China)" },
    };
    return all;
}

std::map<std::string, std::string> JPTranslations::parse(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line, logical;
    // Java's Properties.loadConvert: \t \n \r \f, \uXXXX, else the character itself.
    auto unescape = [](const std::string& raw) {
        std::string value;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '\\' || i + 1 >= raw.size()) {
                value += raw[i];
                continue;
            }
            const char c = raw[++i];
            if (c == 'u' && i + 4 < raw.size()) {
                utf8(value, uint32_t(std::stoul(raw.substr(i + 1, 4), nullptr, 16)));
                i += 4;
            } else if (c == 't') value += '\t';
            else if (c == 'n') value += '\n';
            else if (c == 'r') value += '\r';
            else if (c == 'f') value += '\f';
            else value += c;
        }
        return value;
    };
    // As Java's Properties.load: blanks before the key skipped; the key up to an unescaped '=', ':' or blank;
    // blanks, one '=' or ':', and blanks after it skipped; the value the rest, its trailing blanks kept.
    auto take = [&out, &unescape](const std::string& entry) {
        auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\f'; };
        size_t i = 0;
        while (i < entry.size() && blank(entry[i])) ++i;
        if (i >= entry.size() || entry[i] == '#' || entry[i] == '!') return;
        const size_t keyStart = i;
        while (i < entry.size() && entry[i] != '=' && entry[i] != ':' && !blank(entry[i])) i += entry[i] == '\\' ? 2 : 1;
        const size_t keyEnd = std::min(i, entry.size());
        while (i < entry.size() && blank(entry[i])) ++i;
        if (i < entry.size() && (entry[i] == '=' || entry[i] == ':')) ++i;
        while (i < entry.size() && blank(entry[i])) ++i;
        out[unescape(entry.substr(keyStart, keyEnd - keyStart))] = unescape(i < entry.size() ? entry.substr(i) : std::string());
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // A continued value's next line starts after its blanks.
        if (!logical.empty()) line.erase(0, std::min(line.size(), line.find_first_not_of(" \t")));
        // Continued: an odd number of backslashes at the end.
        size_t slashes = 0;
        while (slashes < line.size() && line[line.size() - 1 - slashes] == '\\') ++slashes;
        if (slashes % 2 == 1) {
            logical += line.substr(0, line.size() - 1);
            continue;
        }
        logical += line;
        take(logical);
        logical.clear();
    }
    if (!logical.empty()) take(logical);
    return out;
}

std::string JPTranslations::directory() {
    const std::string d = JPlacerPaths::bundled("translations");
    return !d.empty() && fs::exists(fs::path(d) / "translations.properties") ? d : std::string();
}

bool JPTranslations::load(const std::string& dir, const std::string& code, std::string& why) {
    std::lock_guard lk(s_lock);
    s_text.clear();
    if (code == "en") return true;
    const fs::path english = fs::path(dir) / "translations.properties", other = fs::path(dir) / ("translations_" + code + ".properties");
    if (!fs::exists(english) || !fs::exists(other)) {
        why = "OpenPnP's translations are not beside jplacer (" + other.string() + ")";
        return false;
    }
    const auto en = parse(read(english)), tr = parse(read(other));
    // Each English text to the translation most of its keys have (the same words can be translated
    // differently in different places).
    std::map<std::string, std::map<std::string, int>> votes;
    for (const auto& [key, value] : en) {
        const auto t = tr.find(key);
        if (t == tr.end() || value.rfind("<html>", 0) == 0) continue;
        const std::string from = bare(value).first, to = bare(t->second).first;
        if (!from.empty() && !to.empty() && from != to) ++votes[from][to];
    }
    JTranslationEngine& engine = JTranslationEngine::instance();
    for (const auto& [from, tos] : votes) {
        const auto best = std::max_element(tos.begin(), tos.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        s_text[from] = best->first;
        // Bare, and with each ending jplacer's labels have.
        engine.add(from, best->first);
        for (const char* end : { ":", "\xE2\x80\xA6", "..." }) engine.add(from + end, best->first + end);
    }
    return true;
}

std::vector<uint32_t> JPTranslations::codepoints() {
    std::lock_guard lk(s_lock);
    std::set<uint32_t> cps;
    for (const auto& [from, to] : s_text)
        for (size_t i = 0; i < to.size();) {
            const unsigned char c = to[i];
            uint32_t cp = c;
            size_t n = 1;
            if (c >= 0xF0) { cp = c & 0x07; n = 4; }
            else if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
            else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
            for (size_t k = 1; k < n && i + k < to.size(); ++k) cp = (cp << 6) | (uint8_t(to[i + k]) & 0x3F);
            i += n;
            if (cp >= 0x100) cps.insert(cp);
        }
    return { cps.begin(), cps.end() };
}

} // inline namespace jf
