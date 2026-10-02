// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <deque>
#include <map>
#include <string>
#include <vector>

inline namespace jf {

class JJson;

// An in-process stand-in for a Grbl-family controller, so the driver, the
// machine and the GUI run with no hardware.
//
// Everything that would differ between real boards is configuration (see
// configure()): what it says it is ($I), its axes, its stored settings ($$),
// and fixed replies to other commands (a plugin's sensor reading). Motion is
// instantaneous; the position is what was last commanded.
class JPSimulatedGrbl {
public:
    // {
    //   "identity":    ["[VER:1.1f.20250101:]", "[FIRMWARE:grblHAL]"],
    //   "axisLetters": ["X", "Y", "Z"],
    //   "settings":    { "100": "80", "110": "5000" },
    //   "replies":     { "M1000 P0": "-12000" },
    //   "silent":          false,   // answers nothing: a port with no controller behind it
    //   "garbleFirstLine": false    // the first line fails (error:2), as after junk on the line
    // }
    void configure(const JJson& config);

    // Bytes from the host: real-time bytes act at once, the rest is
    // collected into lines and executed.
    void receive(const std::string& bytes);

    // Lines the controller has sent back, oldest first.
    bool hasOutput() const { return !m_out.empty(); }
    std::string takeLine();

private:
    void execute(const std::string& line);
    void gcode(const std::string& line);
    std::string statusReport() const;

    std::vector<std::string>           m_identity;
    std::vector<std::string>           m_letters;
    std::map<int, std::string>         m_settings;
    std::map<std::string, std::string> m_replies;

    std::map<std::string, double> m_position;
    bool                          m_relative = false;
    bool                          m_silent   = false;
    bool                          m_garble   = false;
    std::string                   m_input;
    std::deque<std::string>       m_out;
};

} // inline namespace jf
