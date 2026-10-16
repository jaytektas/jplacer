// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "machine/JPSignalerConfig.h"

#include <functional>
#include <map>
#include <optional>
#include <string>

inline namespace jf {

class JPlacerMachine;

// The cell's signalers told how a job runs, as OpenPnP's job processor tells
// them (fireJobState): stopped when a job is set up or stopped, running before
// each step, in error when a step fails, finished when the last is done. A
// SoundSignaler plays its sound for an error or a finish; an ActuatorSignaler
// switches its actuator on in its job state and off in any other, only when
// that changes what it last switched it to; a Neoden4Signaler beeps the
// NeoDen 4's buzzer for an error or a finish (JPlacerNeoden4Buzzer).
class JPlacerSignalers {
public:
    explicit JPlacerSignalers(JPlacerMachine& machine) : m_machine(machine) {}

    // On the job's thread; `onMain` runs a function on the main thread and waits for it. The same state told again
    // (running, before each step) does nothing once it has been told in full: no visit to the main thread, which
    // waits for its next frame (before every step, 31-160 ms of the 1.4 s between a place and the next pick).
    void signal(JPSignalerConfig::JobState state, const std::function<void(const std::function<void()>&)>& onMain);

private:
    JPlacerMachine&             m_machine;
    std::map<std::string, bool> m_actuated;   // by signaler id: what it last switched its actuator to
    // The state last told, every signaler done with it (an actuator that failed to switch is tried again).
    std::optional<JPSignalerConfig::JobState> m_told;
};

} // inline namespace jf
