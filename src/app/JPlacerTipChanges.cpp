// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerTipChanges.h"

#include "common/JPlacerLog.h"
#include "tasks/JPTipChanger.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

inline namespace jf {

namespace {

// How long the status bar shows a step, and a result.
constexpr int kStepMs   = 8000;
constexpr int kResultMs = 15000;

const JPNozzleTipConfig* tipOf(const JPCellConfig& c, const std::string& id) {
    for (const JPNozzleTipConfig& t : c.nozzleTips) if (t.id == id) return &t;
    return nullptr;
}

std::string nameOf(const JPNozzleTipConfig* t) {
    return t ? (t->name.empty() ? t->id : t->name) : std::string("no tip");
}

// A promise answered once only (a question can be answered and also stopped).
void answer(const std::shared_ptr<std::promise<bool>>& p, bool yes) {
    try {
        p->set_value(yes);
    } catch (const std::future_error&) {
    }
}

} // namespace

JPlacerTipChanges::JPlacerTipChanges(JAppWindow& window, JPCell& cell,
                                     std::function<void(const std::string&, const std::string&)> setTip)
    : m_window(window), m_cell(cell), m_setTip(std::move(setTip)) {}

JPlacerTipChanges::~JPlacerTipChanges() {
    *m_alive = false;
    m_quitting = true;
    {
        std::lock_guard lk(m_mutex);
        if (m_waiting) answer(m_waiting, false);
    }
    if (m_worker.joinable()) m_worker.join();
}

std::string JPlacerTipChanges::refusal(const std::string& nozzleId, const std::string& tipId) const {
    const JPCellConfig& c = m_cell.config();
    if (!m_cell.isConnected()) return "connect the machine first";
    if (!m_cell.isHomed()) return "home the machine first";
    const JPNozzleConfig* nozzle = nullptr;
    for (const JPNozzleConfig& n : c.nozzles) if (n.id == nozzleId) nozzle = &n;
    if (!nozzle) return "there is no such nozzle";
    if (tipId == nozzle->tipId) return nameOf(tipOf(c, tipId)) + " is on " + nozzle->name + " already";
    if (const JPNozzleTipConfig* on = tipOf(c, nozzle->tipId)) {
        if (on->unloadingSteps().empty()) return nameOf(on) + " has no unload steps to take it off with";
        if (!on->problems().empty()) return on->problems().front();
    }
    if (tipId.empty()) return "";
    const JPNozzleTipConfig* tip = tipOf(c, tipId);
    if (!tip) return "there is no such nozzle tip";
    if (!nozzle->fits(tipId)) return tip->name + " does not fit " + nozzle->name;
    for (const JPNozzleConfig& n : c.nozzles)
        if (n.id != nozzleId && n.tipId == tipId) return tip->name + " is on " + n.name;
    if (tip->loadSteps.empty()) return tip->name + " has no load steps (Machine Setup, under the tip)";
    if (!tip->problems().empty()) return tip->problems().front();
    return "";
}

void JPlacerTipChanges::ask(const std::string& question, std::shared_ptr<std::promise<bool>> p) {
    JDialog::confirm("Nozzle Tip Change", question, [p] { answer(p, true); }, [p] { answer(p, false); });
}

void JPlacerTipChanges::change(const std::string& nozzleId, const std::string& tipId, bool everyStep) {
    if (m_busy) {
        m_window.showStatus("A nozzle tip change is under way", kStepMs);
        return;
    }
    if (const std::string why = refusal(nozzleId, tipId); !why.empty()) {
        m_window.showStatus("Nozzle tip: " + why, kResultMs);
        return;
    }
    if (m_worker.joinable()) m_worker.join();   // the last one has finished: m_busy says so
    // What is needed, copied now: the cell's settings may change meanwhile.
    const JPCellConfig& c = m_cell.config();
    JPNozzleConfig nozzle;
    for (const JPNozzleConfig& n : c.nozzles) if (n.id == nozzleId) nozzle = n;
    const JPNozzleTipConfig* on = tipOf(c, nozzle.tipId);
    const JPNozzleTipConfig* wanted = tipOf(c, tipId);
    struct Half { std::string what, after; std::vector<JPChangerStep> steps; };
    std::vector<Half> halves;
    if (on) halves.push_back({ "Unloading " + nameOf(on) + " from " + nozzle.name, "", on->unloadingSteps() });
    if (wanted) halves.push_back({ "Loading " + nameOf(wanted) + " on " + nozzle.name, tipId, wanted->loadSteps });

    m_busy = true;
    std::weak_ptr<bool> alive = m_alive;
    auto onMain = [alive](std::function<void()> fn) {
        JMainThreadDispatcher::instance().post([alive, fn] {
            if (const auto a = alive.lock(); a && *a) fn();
        });
    };
    m_worker = std::thread([this, names = c, nozzle, halves, everyStep, onMain] {
        JPTipChanger::Hooks hooks;
        hooks.ask = [this, onMain](const std::string& question) {
            if (m_quitting) return false;
            auto p = std::make_shared<std::promise<bool>>();
            auto reply = p->get_future();
            {
                std::lock_guard lk(m_mutex);
                m_waiting = p;
            }
            onMain([this, question, p] { ask(question, p); });
            const bool yes = reply.get();
            std::lock_guard lk(m_mutex);
            m_waiting.reset();
            return yes && !m_quitting;
        };
        hooks.progress = [this, onMain](const std::string& step) {
            onMain([this, step] { m_window.showStatus(step, kStepMs); });
        };
        std::string done;
        for (const Half& h : halves) {
            std::string why;
            if (!JPTipChanger::run(m_cell, names, nozzle, h.steps, h.what, everyStep, hooks, why)) {
                JLOGC(JPlacerLog::kCell, JLogLevel::Warn) << h.what << ": " << why;
                const std::string text = h.what + ": " + why
                    + ".\n\nLook at " + nozzle.name + " and say which tip is on it (the Jog panel's tip menu, "
                      "Tip On It): that moves nothing.";
                onMain([text] { JDialog::message("Nozzle tip change stopped", text); });
                m_busy = false;
                return;
            }
            // That half done: what is on the nozzle now.
            onMain([this, id = nozzle.id, tip = h.after] {
                if (m_setTip) m_setTip(id, tip);
            });
            done = h.what;
        }
        onMain([this, done] { m_window.showStatus(done + ": done", kResultMs); });
        m_busy = false;
    });
}

} // inline namespace jf
