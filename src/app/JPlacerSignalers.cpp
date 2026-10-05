// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerSignalers.h"

#include "JPlacerMachine.h"
#include "JPlacerSound.h"

#include "common/JPlacerLog.h"
#include "machine/JPCell.h"

#include <j/core/Log.h>

#include <vector>

inline namespace jf {

void JPlacerSignalers::signal(JPSignalerConfig::JobState state,
                              const std::function<void(const std::function<void()>&)>& onMain) {
    using State = JPSignalerConfig::JobState;
    JPCell* cell = nullptr;
    std::vector<JPSignalerConfig> signalers;
    onMain([&] {
        cell = m_machine.cell();
        if (cell) signalers = cell->config().signalers;
    });
    for (const JPSignalerConfig& s : signalers) {
        if (s.kind == JPSignalerConfig::Kind::Sound) {
            if (state == State::Error && s.errorSound) JPlacerSound::play(JPlacerSound::Sound::Error);
            if (state == State::Finished && s.finishedSound) JPlacerSound::play(JPlacerSound::Sound::Success);
            continue;
        }
        if (s.actuatorId.empty() || !s.jobState || !cell) continue;
        const bool on = state == *s.jobState;
        if (const auto it = m_actuated.find(s.id); it != m_actuated.end() && it->second == on) continue;
        std::string why;
        if (cell->switchActuatorAndWait(s.actuatorId, on, why)) m_actuated[s.id] = on;
        else JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "signaler " << s.name << ": " << why;
    }
}

} // inline namespace jf
