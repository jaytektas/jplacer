// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLink.h"
#include "JPNeoden4Protocol.h"
#include "JPSerialLink.h"

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>

inline namespace jf {

// A NeoDen 4 on its serial port, driven as OpenPnP's NeoDen4Driver drives
// it: the lines the driver sends (the `neoden4` firmware profile's) are done
// in the machine's own binary protocol (JPNeoden4Protocol), each answered
// "ok", or "error: " and why, as a G-code controller answers.
//
//   M115                    answered "FIRMWARE_NAME:NeoDen4"
//   G28                     every nozzle up, let go, homed (only the first
//                           time; again, only the offsets are forgotten)
//   G92 X.. Y.. …           where the axes are: once homed, only that; then
//                           X and Y offset by the difference
//   G0/G1 X Y Z U V W A B C D F
//                           as OpenPnP's moveTo: the rotations (A to D, of
//                           nozzles 1 to 4) that change, the Zs (Z, U, V, W)
//                           that change, then X and Y at F over 250 mm/s
//                           (G0: full speed), each followed by 100 ms; three
//                           tries, a second either side of flushing; refused
//                           before homing
//   G4 P<ms>                waited
//   M400                    answered (every move has ended before its ok)
//   ?                       "<Idle|Pos:x,y,z,u,v,w,a,b,c,d>", the positions
//                           last sent
//   VACUUM <n> ON|OFF       nozzle n's air -128; off: 20, 100 ms, then 0
//   AIR <n> <value>         nozzle n's air set (-128 vacuum to 127 blowing)
//   AIR? <n>                answered "AIR:<value>", as read
//   LIGHTS DOWN|UP <level>  the down (top) or up (bottom) camera's lights
//   RAILS <speed>           stopped (0), forwards or backwards (negative)
//   RELEASEC                every nozzle's rotation let go
//   BUZZER ON|OFF
//   FEED|PEEL <id> <strength> <rate>
//   FEEDERID <old> <new>    a feeder's id changed
//   NEOFEED <feeder> <strength> <peeler> <strength> <peel %> <length>
//                           OpenPnP's NeoDen4FeederActuator: fed `length`,
//                           peeled peel % of 5 × length; 0 or less refused
//
// Used only from the owning driver's I/O thread; a command is done before
// write() returns.
class JPNeoden4Link : public JPLink {
public:
    // OpenPnP's NeoDen4Driver settings: its serial port, and the scale of X
    // and Y (the machine's steps are hundredths of a millimetre, times these).
    struct Settings {
        JPSerialLink::Settings serial;
        double scaleX = kScaleX, scaleY = kScaleY;
    };
    static constexpr double kScaleX = 1.0501, kScaleY = 1.04947526;   // OpenPnP's
    static constexpr int    kSettleMs = 100;       // after each kind of move, and between vacuum off's two levels
    static constexpr int    kRetryPauseMs = 1000;  // either side of flushing, between a move's tries
    static constexpr int    kMoveTries = 3;
    static constexpr int    kFlushSliceMs = 50;    // flushing: read until nothing comes for this long
    static constexpr double kFastestMmPerS = 250;  // OpenPnP's: the feed rate full speed is taken as
    static constexpr int    kVacuumOn = -128, kVacuumRelease = 20;
    // The letters of its axes, in the order its status gives them.
    static constexpr std::array<const char*, 10> kLetters { "X", "Y", "Z", "U", "V", "W", "A", "B", "C", "D" };

    explicit JPNeoden4Link(Settings settings);
    // On `port` instead of a serial port (a stand-in machine).
    JPNeoden4Link(Settings settings, JPNeoden4Protocol::Port& port, int timeoutMs);
    ~JPNeoden4Link() override;

    bool open(std::string& error) override;
    void close() override;
    bool isOpen() const override;
    bool write(const std::string& bytes) override;
    std::optional<std::string> readLine(int timeoutMs) override;
    std::string describe() const override;

private:
    // One line done, its answer queued.
    void run(const std::string& line);
    // Each false with `why` when it could not be done.
    bool home(std::string& why);
    bool setPositions(const std::map<std::string, double>& to, std::string& why);
    bool move(const std::map<std::string, double>& to, std::optional<double> feedPerMin, std::string& why);
    bool moveOnce(const std::map<std::string, double>& to, double speed, std::string& why);
    bool vacuum(int nozzle, bool on, std::string& why);
    bool neoFeed(const std::vector<double>& args, std::string& why);
    void pause(int ms);
    std::string status() const;

    Settings                           m_settings;
    std::unique_ptr<JPSerialLink>      m_serial;     // none on a stand-in port
    std::unique_ptr<JPNeoden4Protocol::Port> m_serialBytes;
    JPNeoden4Protocol::Port*           m_port = nullptr;
    std::unique_ptr<JPNeoden4Protocol> m_protocol;
    int                                m_timeoutMs;
    bool                               m_open = false;
    std::string                        m_partial;
    std::deque<std::string>            m_replies;
    // OpenPnP's isAlreadyHomed, its homing offsets (X and Y), the positions
    // last sent (by letter), whether the next G92 only says where homing
    // left the axes, and the feed rate last given.
    bool                               m_homed = false;
    bool                               m_justHomed = false;
    std::map<std::string, double>      m_offsets;
    std::map<std::string, double>      m_at;
    double                             m_feedPerMin = 0;
};

} // inline namespace jf
