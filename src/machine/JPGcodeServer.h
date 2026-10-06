// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

inline namespace jf {

class JJson;

// OpenPnP's GcodeServer: an in-process stand-in for a generic G-code
// controller, what OpenPnP connects a G-code controller to when it is
// simulated (Simulation Mode's Replace Drivers, Communications "simulated",
// or TCP to the address "GcodeServer"). It reads each line as RS274/NGC
// words (comments in parentheses or after ';', "$H" the only $-command
// taken, a word of a modal group already in the line beginning a new
// command, as many controllers take it) and answers "ok", or what a canned
// reply says for that very line, or "*** Unknown syntax: ..." when it cannot
// be read. It keeps where its axes are: G0 / G1 move them (absolute or
// relative, G90 / G91, in millimetres or inches, G21 / G20), G28 or $H homes
// them (to their home coordinates, or where the words say), G92 sets where
// they are; M114 reports it ("ok C: X:0.0000 ..."), M115 says what it is.
// Motion is instantaneous; G4 dwells its P milliseconds or S seconds (at most
// 20 s), as a wait for motion (M400) has nothing left to wait for.
class JPGcodeServer {
public:
    // {
    //   "axes":    [ { "letter": "X", "home": 0, "rotational": false }, ... ],   // the controller's, in order
    //   "replies": { "M105": "ok T:21" },                                     // canned replies, by line
    //   "version": "..."                                                      // for M115's FIRMWARE_VERSION
    // }
    void configure(const JJson& config);

    // Bytes from the host, collected into lines and executed.
    void receive(const std::string& bytes);

    // Lines the controller has sent back, oldest first.
    bool hasOutput() const { return !m_out.empty(); }
    std::string takeLine();

    static constexpr int kMaxDwellMs = 20000;   // OpenPnP's maxDwellTimeMilliseconds

private:
    // A word as OpenPnP's GcodeServer reads it: its letter, its number as
    // digits over a power of ten, and the code it is (G1, M114, ...).
    struct Word {
        char        letter = 0;
        bool        dollar = false;
        int64_t     number = 0;
        int         signum = 0;
        int64_t     decimal = 0;
        std::string code;   // "G1", "M114", "Tn", "O", "M1nn"; empty: an argument (X, F, P, ...)
        int         group = -1;

        int64_t integral() const { return number / decimal; }
        int64_t fraction() const { return number % decimal; }
        double  value() const { return double(signum) * double(number) / double(decimal); }
        void    recognize();
    };
    struct Axis {
        std::string letter;
        double      home = 0;
        bool        rotational = false;
    };

    void execute(const std::string& line);
    // A finished word added to the command; a word of a modal group the command has begins a new one.
    void finish(Word* word, std::vector<Word>& command);
    void simulate(const std::vector<Word>& command);

    std::vector<Axis>                  m_axes;
    std::map<std::string, std::string> m_replies;
    std::string                        m_version;

    std::map<std::string, double> m_location;   // by letter, mm (rotations in degrees)
    double                        m_unit = 1.0;  // mm per unit: 1 or 25.4
    bool                          m_absolute = true;
    double                        m_feedRate = 0, m_acceleration = 0;   // as told, per second
    std::string                   m_response;
    std::string                   m_input;
    std::deque<std::string>       m_out;
};

} // inline namespace jf
