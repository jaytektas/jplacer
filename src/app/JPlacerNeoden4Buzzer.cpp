// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerNeoden4Buzzer.h"

#include "JPlacerMachine.h"

#include "common/JPlacerLog.h"
#include "machine/JPCell.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <chrono>

inline namespace jf {

JPlacerNeoden4Buzzer::JPlacerNeoden4Buzzer(JPlacerMachine& machine) : m_machine(machine) {}

JPlacerNeoden4Buzzer::~JPlacerNeoden4Buzzer() {
    *m_alive = false;
    {
        std::lock_guard lk(m_mutex);
        m_quit = true;
        m_playing = false;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void JPlacerNeoden4Buzzer::signal(bool error) {
    {
        std::lock_guard lk(m_mutex);
        if (m_playing) return;
    }
    // The last beeping has stopped (or is stopping, told so): over before the next starts.
    if (m_thread.joinable()) m_thread.join();
    {
        std::lock_guard lk(m_mutex);
        m_playing = true;
    }
    m_thread = std::thread([this, error] { run(error); });
    // Beeping until this is confirmed.
    std::weak_ptr<bool> alive = m_alive;
    JDialog::message(error ? "Job error!" : "Job success!", "Click ok to confirm", [this, alive] {
        if (const auto a = alive.lock(); !a || !*a) return;
        {
            std::lock_guard lk(m_mutex);
            m_playing = false;
        }
        m_wake.notify_all();
    });
}

void JPlacerNeoden4Buzzer::run(bool error) {
    double pauseMs = kFirstPauseMs;
    const int beeps = error ? kErrorBeeps : kFinishedBeeps, beepMs = error ? kErrorBeepMs : kFinishedBeepMs;
    for (;;) {
        pauseMs /= kPauseShrink;
        for (int i = 0; i < beeps; ++i) {
            beep(true);
            const bool still = waitFor(beepMs);
            beep(false);
            if (!still || !waitFor(beepMs)) return;
        }
        if (!waitFor(int(pauseMs))) return;
    }
}

bool JPlacerNeoden4Buzzer::waitFor(int ms) {
    std::unique_lock lk(m_mutex);
    m_wake.wait_for(lk, std::chrono::milliseconds(ms), [this] { return !m_playing || m_quit; });
    return m_playing && !m_quit;
}

void JPlacerNeoden4Buzzer::beep(bool on) {
    // The buzzer of the first NeoDen 4 controller (as OpenPnP takes its first NeoDen4Driver).
    std::weak_ptr<bool> alive = m_alive;
    JMainThreadDispatcher::instance().post([this, alive, on] {
        if (const auto a = alive.lock(); !a || !*a) return;
        JPCell* cell = m_machine.cell();
        if (!cell) return;
        for (const JPDriverConfig& d : cell->config().drivers)
            if (std::as_const(d.link)["type"].str() == "neoden4") {
                cell->sendLine(d.id, on ? "BUZZER ON" : "BUZZER OFF");
                return;
            }
        if (on) JLOGC(JPlacerLog::kJob, JLogLevel::Warn) << "Neoden4Signaler: the machine has no NeoDen 4 controller";
    });
}

} // inline namespace jf
