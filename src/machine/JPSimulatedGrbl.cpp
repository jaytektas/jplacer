// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSimulatedGrbl.h"

#include <j/config/Json.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

// Grbl's real-time bytes.
constexpr char kStatusQuery = '?';
constexpr char kSoftReset   = 0x18;
constexpr char kFeedHold    = '!';

// Grbl's error numbers for what the simulator rejects.
constexpr const char* kErrorUnsupported = "error:20";   // unsupported or invalid g-code command
constexpr const char* kErrorBadNumber   = "error:2";    // bad number format
constexpr const char* kErrorNoSetting   = "error:3";    // invalid '$' statement

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}

} // namespace

void JPSimulatedGrbl::configure(const JJson& config) {
    m_identity.clear();
    for (const JJson& l : config["identity"].arr()) m_identity.push_back(l.str());
    m_letters.clear();
    m_machine.clear();
    m_offset.clear();
    for (const JJson& l : config["axisLetters"].arr()) {
        m_letters.push_back(l.str());
        m_machine[l.str()] = 0.0;
        m_offset[l.str()]  = 0.0;
    }
    m_settings.clear();
    for (const auto& [id, value] : config["settings"].obj()) m_settings[std::atoi(id.c_str())] = value.str();
    m_replies.clear();
    for (const auto& [cmd, reply] : config["replies"].obj()) m_replies[upper(cmd)] = reply.str();
    m_silent = config["silent"].boolean();
    m_garble = config["garbleFirstLine"].boolean();
    m_stallDwell = config["stallDwell"].boolean();
    m_holdNeverStill = config["holdNeverStill"].boolean();
}

void JPSimulatedGrbl::receive(const std::string& bytes) {
    if (m_silent) return;
    for (char c : bytes) {
        if (c == kStatusQuery) {
            m_out.push_back(statusReport());
        } else if (c == kFeedHold) {
            m_held = true;
        } else if (c == kSoftReset) {
            m_input.clear();
            m_relative = false;
            m_held = false;
            m_moved = false;
            m_out.push_back("Grbl 1.1f ['$' for help]");
        } else if (c == '\n' || c == '\r') {
            if (!m_input.empty()) execute(m_input);
            m_input.clear();
        } else {
            m_input += c;
        }
    }
}

std::string JPSimulatedGrbl::takeLine() {
    std::string line = std::move(m_out.front());
    m_out.pop_front();
    return line;
}

std::string JPSimulatedGrbl::statusReport() const {
    auto list = [this](const std::map<std::string, double>& values) {
        std::string out;
        char buf[32];
        for (const std::string& l : m_letters) {
            std::snprintf(buf, sizeof buf, "%.3f", values.at(l));
            if (!out.empty()) out += ',';
            out += buf;
        }
        return out;
    };
    return std::string(m_held ? (m_holdNeverStill ? "<Hold:1" : "<Hold:0") : "<Idle") + "|MPos:" + list(m_machine) + "|FS:0,0|WCO:" + list(m_offset) + ">";
}

void JPSimulatedGrbl::execute(const std::string& raw) {
    if (m_garble) {
        m_garble = false;
        m_out.push_back(kErrorBadNumber);
        return;
    }
    std::string line = upper(raw);
    if (const size_t semi = line.find(';'); semi != std::string::npos) line.erase(semi);   // a comment
    line.erase(std::remove_if(line.begin(), line.end(), [](char c) { return c == ' ' || c == '\t'; }), line.end());
    if (line.empty()) {
        m_out.push_back("ok");
        return;
    }

    if (const auto it = m_replies.find(upper(raw)); it != m_replies.end()) {
        m_out.push_back(it->second);
        m_out.push_back("ok");
        return;
    }
    if (line == "$I") {
        for (const std::string& l : m_identity) m_out.push_back(l);
        m_out.push_back("ok");
    } else if (line == "$$") {
        for (const auto& [id, value] : m_settings) m_out.push_back("$" + std::to_string(id) + "=" + value);
        m_out.push_back("ok");
    } else if (line == "$H") {
        for (auto& [l, v] : m_machine) v = 0.0;
        m_out.push_back("ok");
    } else if (line.size() > 2 && line.rfind("$H", 0) == 0 && std::isalpha(static_cast<unsigned char>(line[2]))) {
        // grblHAL's homing of single axes: $HZ, $HXY.
        for (size_t i = 2; i < line.size(); ++i)
            if (const auto it = m_machine.find(std::string(1, line[i])); it != m_machine.end()) it->second = 0.0;
        m_out.push_back("ok");
    } else if (line == "$X") {
        m_out.push_back("ok");
    } else if (line.size() > 1 && line[0] == '$') {
        const size_t eq = line.find('=');
        const int id = std::atoi(line.c_str() + 1);
        if (eq == std::string::npos || !m_settings.count(id)) {
            m_out.push_back(kErrorNoSetting);
            return;
        }
        m_settings[id] = line.substr(eq + 1);
        m_out.push_back("ok");
    } else {
        gcode(line);
    }
}

void JPSimulatedGrbl::gcode(const std::string& line) {
    // Motors on or off (M17 / M18, axis letters bare): nothing to simulate.
    if (line.rfind("M17", 0) == 0 || line.rfind("M18", 0) == 0) {
        m_out.push_back("ok");
        return;
    }
    // Words: a letter and a number each.
    std::vector<std::pair<char, double>> words;
    for (size_t i = 0; i < line.size();) {
        const char letter = line[i];
        if (!std::isalpha(static_cast<unsigned char>(letter))) {
            m_out.push_back(kErrorBadNumber);
            return;
        }
        // A G-code number: sign, digits, one point. (strtod would read the
        // "0X12" of "G0X12" as hexadecimal.)
        size_t j = i + 1;
        if (j < line.size() && (line[j] == '-' || line[j] == '+')) ++j;
        const size_t digits = j;
        bool point = false;
        while (j < line.size() && (std::isdigit(static_cast<unsigned char>(line[j])) || (line[j] == '.' && !point))) {
            point |= line[j] == '.';
            ++j;
        }
        if (j == digits) {
            m_out.push_back(kErrorBadNumber);
            return;
        }
        words.emplace_back(letter, std::strtod(line.substr(i + 1, j - i - 1).c_str(), nullptr));
        i = j;
    }

    bool motion = false, setPosition = false;
    // A long move: the wait for motion to end after one is not answered
    // (until a reset).
    if (m_stallDwell && m_moved)
        for (const auto& [letter, value] : words)
            if (letter == 'G' && int(value * 10 + 0.5) == 40) return;
    for (const auto& [letter, value] : words) {
        if (letter == 'G') {
            const int g = int(value * 10 + 0.5);   // G92.1 -> 921
            if (g == 0 || g == 10) motion = m_moved = true;
            else if (g == 900) m_relative = false;
            else if (g == 910) m_relative = true;
            else if (g == 920) setPosition = true;
            else if (g != 40 && g != 210) {   // G4 (dwell; motion is already done) and G21 (mm) need nothing
                m_out.push_back(kErrorUnsupported);
                return;
            }
        } else if (letter == 'M') {
            const int m = int(value + 0.5);
            if (m != 64 && m != 65) {
                m_out.push_back(kErrorUnsupported);
                return;
            }
        }
    }
    for (const auto& [letter, value] : words) {
        const std::string l(1, letter);
        const auto it = m_machine.find(l);
        if (it == m_machine.end()) continue;
        if (setPosition)      m_offset[l] = it->second - value;      // G92: here is now `value`
        else if (!motion)     continue;
        else if (m_relative)  it->second += value;
        else                  it->second = value + m_offset[l];
    }
    m_out.push_back("ok");
}

} // inline namespace jf
