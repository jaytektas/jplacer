// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <map>

inline namespace jf {

// What a camera's controls were last set to, so a setting is sent only when it changes. Each setting is a request
// to the camera over USB, and an Auto-Tune's search, a picture's exposure and a look's tune put back each set every
// control again: hundreds a minute, which cheap cameras' firmware does not survive (bench, 2026-10-13: the cameras
// reset and dropped off the bus as they were tuned). A value whose automatic mode changes is sent again (the camera
// may have moved it itself), and all of it once the camera is opened again or asked to be set afresh.
class JPControlWrites {
public:
    // Whether `value` must be sent for control `id`.
    bool needed(uint32_t id, int value) const {
        const auto it = m_sent.find(id);
        return it == m_sent.end() || it->second != value;
    }
    // Sent and taken.
    void sent(uint32_t id, int value) { m_sent[id] = value; }
    // Its value no longer known (its automatic mode changed, or setting it failed).
    void forget(uint32_t id) { m_sent.erase(id); }
    // Nothing known: everything sent next time.
    void clear() { m_sent.clear(); }

private:
    std::map<uint32_t, int> m_sent;
};

} // inline namespace jf
