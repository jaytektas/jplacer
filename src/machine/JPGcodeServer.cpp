// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPGcodeServer.h"

#include <j/config/Json.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

inline namespace jf {

namespace {

// OpenPnP's GcodeServer's codes and their modal groups (LinuxCNC's), group 0 the non-modal ones.
struct Code {
    const char* name;
    int         group;
};
constexpr Code kCodes[] = {
    { "G0", 1 },   { "G1", 1 },   { "G2", 1 },   { "G3", 1 },   { "G33", 1 },  { "G38", 1 },  { "G73", 1 },  { "G76", 1 },
    { "G80", 1 },  { "G81", 1 },  { "G82", 1 },  { "G83", 1 },  { "G84", 1 },  { "G85", 1 },  { "G86", 1 },  { "G87", 1 },
    { "G88", 1 },  { "G89", 1 },  { "G17", 2 },  { "G18", 2 },  { "G19", 2 },  { "G7", 3 },   { "G8", 3 },   { "G90", 4 },
    { "G91", 4 },  { "G93", 5 },  { "G94", 5 },  { "G20", 6 },  { "G21", 6 },  { "G40", 7 },  { "G41", 7 },  { "G42", 7 },
    { "G43", 8 },  { "G49", 8 },  { "G98", 9 },  { "G99", 9 },  { "G54", 10 }, { "G55", 10 }, { "G56", 10 }, { "G57", 10 },
    { "G58", 10 }, { "G59", 10 }, { "M0", 11 },  { "M1", 11 },  { "M2", 11 },  { "M30", 11 }, { "M60", 11 }, { "M6", 12 },
    { "Tn", 12 },  { "M3", 13 },  { "M4", 13 },  { "M5", 13 },  { "M7", 14 },  { "M8", 14 },  { "M9", 14 },  { "M48", 15 },
    { "M49", 15 }, { "O", 16 },   { "G4", 0 },   { "G10", 0 },  { "G28", 0 },  { "G30", 0 },  { "G53", 0 },  { "G92", 0 },
    { "M1nn", 0 }, { "M114", 0 }, { "M115", 0 }, { "M204", 12 }, { "M400", 0 } };

const char* kFirmware = "FIRMWARE_NAME:GcodeServer, FIRMWARE_URL:http%3A//openpnp.org, "
                        "X-SOURCE_CODE_URL:https%3A//github.com/openpnp/openpnp, FIRMWARE_VERSION:";
const char* kBuildDate = ", X-FIRMWARE_BUILD_DATE:Oct 23 2020 00:00:00";

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    const size_t b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

} // namespace

void JPGcodeServer::Word::recognize() {
    if (decimal == 0) decimal = 1;
    const std::string name = std::string(1, letter) + std::to_string(integral());
    for (const Code& c : kCodes)
        if (name == c.name) {
            code = c.name;
            group = c.group;
        }
    if (code.empty()) {
        const char* other = letter == 'T' ? "Tn" : letter == 'O' ? "O" : letter == 'M' && integral() / 100 == 1 ? "M1nn" : nullptr;
        if (other)
            for (const Code& c : kCodes)
                if (std::string(other) == c.name) {
                    code = c.name;
                    group = c.group;
                }
    }
}

void JPGcodeServer::configure(const JJson& config) {
    m_axes.clear();
    m_location.clear();
    for (const JJson& a : config["axes"].arr()) {
        Axis axis { a["letter"].str(), a["home"].number(0.0), a["rotational"].boolean(false) };
        m_location[axis.letter] = 0;
        m_axes.push_back(std::move(axis));
    }
    m_replies.clear();
    for (const auto& [cmd, reply] : config["replies"].obj()) m_replies[cmd] = reply.str();
    m_version = config["version"].str();
}

void JPGcodeServer::receive(const std::string& bytes) {
    for (char c : bytes) {
        if (c == '\n' || c == '\r') {
            if (!m_input.empty()) execute(m_input);
            m_input.clear();
        } else {
            m_input += c;
        }
    }
}

std::string JPGcodeServer::takeLine() {
    std::string line = std::move(m_out.front());
    m_out.pop_front();
    return line;
}

void JPGcodeServer::execute(const std::string& input) {
    // Canned replies first.
    if (const auto canned = m_replies.find(trimmed(input)); canned != m_replies.end()) {
        m_out.push_back(canned->second);
        return;
    }
    m_response = "ok";
    try {
        std::vector<Word> command;
        Word word;
        bool  have = false, insideComment = false, dollar = false;
        int   col = 0;
        auto syntax = [&](char ch) {
            throw std::runtime_error("Syntax error at col " + std::to_string(col) + " '" + std::string(1, ch) + "' unexpected: " + input);
        };
        for (const char ch : input) {
            ++col;
            const char up = char(std::toupper(static_cast<unsigned char>(ch)));
            if (ch == ' ') continue;
            if (ch == '$') {
                dollar = true;
            } else if (ch == '(') {
                if (insideComment) throw std::runtime_error("Nested comment at " + std::to_string(col) + ": " + input);
                insideComment = true;
            } else if (ch == ')') {
                if (!insideComment) throw std::runtime_error("Mismatched comment at " + std::to_string(col) + ": " + input);
                insideComment = false;
            } else if (insideComment) {
                continue;
            } else if (ch == ';') {
                break;   // a comment ends the line
            } else if (up >= 'A' && up <= 'Z') {
                if (dollar && up != 'H') break;   // $H only; the other $-commands are ignored
                finish(have ? &word : nullptr, command);
                word = Word {};
                word.letter = up;
                word.dollar = dollar;
                have = true;
                dollar = false;
            } else if (!have) {
                if (dollar) break;   // a custom $-command (a TinyG setting), ignored
                syntax(ch);
            } else if (ch == '+' || ch == '-') {
                if (word.signum != 0) syntax(ch);
                word.signum = ch == '+' ? 1 : -1;
            } else if (ch >= '0' && ch <= '9') {
                word.number = word.number * 10 + (ch - '0');
                word.decimal *= 10;
                if (word.signum == 0) word.signum = 1;
            } else if (ch == '.') {
                if (word.decimal != 0) syntax(ch);
                word.decimal = 1;
                if (word.signum == 0) word.signum = 1;
            }
        }
        finish(have ? &word : nullptr, command);
        simulate(command);
    } catch (const std::exception& e) {
        m_out.push_back(std::string("*** Unknown syntax: ") + e.what());
        return;
    }
    // A reply of several lines (M115's), each its own.
    size_t start = 0;
    for (size_t nl; (nl = m_response.find('\n', start)) != std::string::npos; start = nl + 1) m_out.push_back(m_response.substr(start, nl - start));
    m_out.push_back(m_response.substr(start));
}

void JPGcodeServer::finish(Word* word, std::vector<Word>& command) {
    if (!word) return;
    word->recognize();
    if (word->group != -1)
        for (const Word& w : command)
            if (w.group == word->group) {
                // The same modal group: a new command begins (not RS274/NGC, but many controllers take it so).
                simulate(command);
                command.clear();
                break;
            }
    command.push_back(*word);
}

void JPGcodeServer::simulate(const std::vector<Word>& command) {
    if (command.empty()) return;
    auto letter = [&](char l) -> const Word* {
        for (const Word& w : command)
            if (w.letter == l) return &w;
        return nullptr;
    };
    auto code = [&](const char* c) -> const Word* {
        for (const Word& w : command)
            if (w.code == c) return &w;
        return nullptr;
    };
    const Word* s = letter('S');
    const Word* p = letter('P');
    std::map<std::string, double> target = m_location, given;
    for (const Axis& a : m_axes)
        if (const Word* w = letter(a.letter.empty() ? '\0' : a.letter[0])) {
            const double v = w->value() * (a.rotational ? 1.0 : m_unit);
            target[a.letter] = m_absolute ? v : target[a.letter] + v;
            given[a.letter] = v;
        }
    if (const Word* f = letter('F')) m_feedRate = f->value() * m_unit / 60;

    if (const Word* m114 = code("M114")) {
        // Where the axes are (a move is over as soon as it is sent), in the units in use.
        std::string r = m114->fraction() == 1 ? "ok WCS:" : "ok C:";
        char buf[64];
        for (const Axis& a : m_axes) {
            std::snprintf(buf, sizeof buf, " %s:%.4f", a.letter.c_str(), m_location[a.letter] / (a.rotational ? 1.0 : m_unit));
            r += buf;
        }
        m_response = r;
    }
    if (code("M115")) {
        const long axes = long(m_axes.size());
        const long paxes = long(std::count_if(m_axes.begin(), m_axes.end(), [](const Axis& a) { return !a.rotational; }));
        m_response = std::string(kFirmware) + m_version + kBuildDate + ", X-AXES:" + std::to_string(axes) + ", X-PAXES:"
                   + std::to_string(paxes) + "\nok";
    }
    if (code("M204") && s) m_acceleration = s->value() * m_unit;

    // A dwell: its own time (motion takes none).
    if (code("G4")) {
        long ms = 0;
        if (p) ms += long(p->integral());
        if (s) ms += long(s->value() * 1000);
        ms = std::min<long>(ms, kMaxDwellMs);
        if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    // Where the axes are, said.
    if (code("G92"))
        for (const auto& [l, v] : target) m_location[l] = v;
    if (code("G21")) m_unit = 1.0;
    if (code("G20")) m_unit = 25.4;
    if (code("G90")) m_absolute = true;
    if (code("G91")) m_absolute = false;

    const Word* h = letter('H');
    const bool homing = code("G28") || (h && h->dollar);
    if (code("G0") || code("G1") || homing) {
        if (homing) {
            // To the home coordinates; where the words say, always absolute.
            if (given.empty())
                for (const Axis& a : m_axes) target[a.letter] = a.home;
            else {
                target = m_location;
                for (const auto& [l, v] : given) target[l] = v;
            }
        }
        m_location = target;
    }
}

} // inline namespace jf
