// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

inline namespace jf {

// The NeoDen 4's own binary protocol, as OpenPnP's NeoDen4Driver speaks it:
// a command byte answered by its byte, an announcing byte answered by its
// byte, eight bytes of payload and their checksum (the low byte of a
// CRC-16/CCITT), then the command polled until it answers done. What the
// machine does (home, move X and Y in steps, each nozzle's Z and rotation,
// the move speed, the feeders and peelers, the air, lights, rails and the
// buzzer, the nozzles' air read) is each one such exchange, made again up to
// three times on a failure (the input flushed between) where OpenPnP makes
// it again: the feeders', the air's, lights' and rails', and the air read
// (five times); a move is made again whole by its caller, and homing once.
//
// Every call is made on the caller's thread and waits for the machine.
class JPNeoden4Protocol {
public:
    // The bytes to and from the machine (a serial port, or a stand-in for a test).
    class Port {
    public:
        virtual ~Port() = default;
        virtual bool write(uint8_t byte) = 0;
        // The next byte, or none within `timeoutMs`.
        virtual std::optional<uint8_t> read(int timeoutMs) = 0;
        virtual void flushInput() = 0;
    };

    explicit JPNeoden4Protocol(Port& port, int timeoutMs = kTimeoutMs);

    // OpenPnP's checksum: the low byte of the CRC-16/CCITT of `bytes`.
    static uint8_t checksum(const uint8_t* bytes, size_t n);

    // Each false with `why` when the machine did not answer as it should (three tries).
    bool home(std::string& why);
    // X and Y in the machine's steps (hundredths of a millimetre, scaled), and when it is done.
    bool moveSteps(int32_t sx, int32_t sy, std::string& why);
    // A nozzle's (1 to 4) Z: 0 up, `mm` down (as OpenPnP: Z made positive, in microns).
    bool moveZ(int nozzle, double mm, std::string& why);
    // A nozzle's (1 to 4; 0 every one, letting them go) rotation, in degrees.
    bool moveC(int nozzle, double degrees, std::string& why);
    // The move speed, a share 0..1 of the fastest (sent as 10 to 130).
    bool setMoveSpeed(double share, std::string& why);
    // A feeder fed (its id, strength and feed rate), a peeler peeling, a feeder's id changed.
    bool feed(int id, int strength, int feedRate, std::string& why);
    bool peel(int id, int strength, int feedRate, std::string& why);
    bool changeFeederId(int oldId, int newId, std::string& why);
    // A nozzle's (1 to 4) air: -128 full vacuum, 0 none, positive blowing.
    bool setAir(int nozzle, int value, std::string& why);
    // The down (top) and up (bottom) camera lights, each a level (0 off).
    bool lightsDown(int level, std::string& why);
    bool lightsUp(int level, std::string& why);
    // The rails: stopped (0), or run forwards (positive) or backwards at that speed (20 to 200).
    bool rails(int speed, std::string& why);
    bool buzzer(bool on, std::string& why);
    // A nozzle's (1 to 4) air as read (vacuum below 0).
    bool readAir(int nozzle, int& value, std::string& why);
    // Whether the machine has finished moving.
    bool ready(bool& isReady, std::string& why);

    static constexpr int kTimeoutMs = 5000;   // OpenPnP's
    static constexpr int kTries = 3;          // an exchange made again, at most
    static constexpr int kReadTries = 5;      // the air read, at most
    static constexpr int kPollMs = 500;       // polling for done
    static constexpr int kStatusSleepMs = 100, kStatusMostMs = 30000;   // waiting for ready

private:
    struct Failure {
        std::string why;
    };
    // The exchanges' parts; each throws Failure.
    uint8_t readByte();
    void    writeByte(uint8_t b);
    void    expect(uint8_t expected);
    void    pollFor(uint8_t command, uint8_t response, int timeoutMs = kPollMs);
    void    writePayload(const std::array<uint8_t, 8>& b);
    std::array<uint8_t, 8> readPayload();
    // command, its answer; announce, its answer; the payload; polled until done.
    void exchange(uint8_t command, uint8_t answer, uint8_t announce, uint8_t announced, const std::array<uint8_t, 8>& payload,
                  uint8_t poll, uint8_t done);
    void waitReady();
    bool isReady();
    void stopRail();
    void railSpeed(int speed);
    void runRail(bool forward);
    // `step` made up to `tries` times, the input flushed between (with OpenPnP's pauses); false and why when it never was.
    template <class Step>
    bool tried(const char* what, int tries, std::string& why, Step&& step);
    // `step` made once.
    template <class Step>
    bool once(std::string& why, Step&& step);

    Port& m_port;
    int   m_timeoutMs;
};

} // inline namespace jf
