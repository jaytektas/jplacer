// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// One of OpenPnP's signalers: told how a job runs (stopped, running, in
// error, finished), it says so. A SoundSignaler plays a sound when a job
// meets an error, and when one is finished (each if ticked); an
// ActuatorSignaler switches its actuator on while the job is in its job
// state and off otherwise; a Neoden4Signaler beeps a NeoDen 4's buzzer (as
// ticked, as a SoundSignaler) until its message is confirmed.
struct JPSignalerConfig {
    enum class Kind { Sound, Actuator, Neoden4 };
    // OpenPnP's AbstractJobProcessor.State.
    enum class JobState { Stopped, Running, Error, Finished };

    Kind        kind = Kind::Sound;
    std::string id;
    std::string name;
    bool        errorSound = false, finishedSound = false;   // Sound, Neoden4
    std::string actuatorId;                                  // Actuator
    std::optional<JobState> jobState;                        // Actuator: none, it is not switched

    // OpenPnP's class names ("SoundSignaler", "ActuatorSignaler", "Neoden4Signaler"), in the order New Signaler… offers them.
    static const std::vector<std::string>& classNames();
    std::string className() const { return classNames()[size_t(kind)]; }
    // The job states as OpenPnP writes them ("STOPPED", "RUNNING", "ERROR", "FINISHED").
    static const std::vector<std::string>& jobStateKeys();

    JJson toJson() const;
    static JPSignalerConfig fromJson(const JJson& j);
};

} // inline namespace jf
