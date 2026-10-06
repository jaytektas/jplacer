// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

inline namespace jf {

class JPlacerMachine;

// OpenPnP's Neoden4Signaler at work: a job's error or finish beeps the
// buzzer of the machine's (first) NeoDen 4 controller, over and over, the
// pauses between shrinking (10 s, each 1.2 times shorter), until the message
// saying so ("Job error!" or "Job success!") is confirmed. An error beeps
// twice, 250 ms each; a finish three times, 30 ms each.
class JPlacerNeoden4Buzzer {
public:
    explicit JPlacerNeoden4Buzzer(JPlacerMachine& machine);
    ~JPlacerNeoden4Buzzer();

    // On the main thread: beeping for an error, or a finish (already beeping: as it is).
    void signal(bool error);

    static constexpr int    kFirstPauseMs = 10000;
    static constexpr double kPauseShrink = 1.2;
    static constexpr int    kErrorBeeps = 2, kErrorBeepMs = 250;
    static constexpr int    kFinishedBeeps = 3, kFinishedBeepMs = 30;

private:
    void run(bool error);
    // Waits `ms`, or less when stopped; false once stopped.
    bool waitFor(int ms);
    void beep(bool on);

    JPlacerMachine&          m_machine;
    std::mutex               m_mutex;
    std::condition_variable  m_wake;
    bool                     m_playing = false;
    bool                     m_quit = false;
    std::thread              m_thread;
    std::shared_ptr<bool>    m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
