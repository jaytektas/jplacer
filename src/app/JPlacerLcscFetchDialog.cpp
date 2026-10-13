// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerLcscFetchDialog.h"

#include "common/JPlacerLog.h"
#include "model/JPConfiguration.h"

#include <j/core/JButton.h>
#include <j/core/JStyle.h>
#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>

#include <algorithm>
#include <thread>

inline namespace jf {

namespace {

std::string wanted(const JPLcscFetch::Ask& a) {
    return a.pads && a.dimensions ? "footprint and dimensions" : a.pads ? "footprint" : "dimensions";
}

} // namespace

JPlacerLcscFetchDialog::JPlacerLcscFetchDialog(JPConfiguration& config, std::string cacheDir, std::vector<JPLcscFetch::Ask> asks,
                                               std::function<void()> onChanged, std::function<void(const std::string&)> onDone,
                                               JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Fetching from LCSC", kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_cacheDir(std::move(cacheDir))
    , m_onChanged(std::move(onChanged))
    , m_onDone(std::move(onDone)) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    m_what = std::make_unique<JLabel>(g, "", 0.f);
    m_what->setWordWrap(true);
    m_bar = std::make_unique<JProgressBar>(g, 0.f, JStyle::current().progressHeight);
    m_note = std::make_unique<JLabel>(g, "", 0.f);
    m_note->setWordWrap(true);
    m_problemsLabel = std::make_unique<JLabel>(g, "What went wrong", 0.f);
    m_problems = std::make_unique<JDataGrid>(g, std::vector<std::string>{ "Package", "LCSC Part", "Wanted", "What went wrong" });
    add(m_what.get());
    add(m_bar.get());
    add(m_note.get());
    add(m_problemsLabel.get());
    add(m_problems.get());
    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_retry = m_buttons->addButton("Retry", JDialogButtonBox::Role::Action);
    m_retry->setTooltip("Ask again for the packages that went wrong");
    m_retry->onClicked.connect([this] { retry(); });
    m_close = m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject);
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
    run(std::move(asks));
}

JPlacerLcscFetchDialog::~JPlacerLcscFetchDialog() {
    *m_alive = false;
    if (m_cancel) *m_cancel = true;   // the one being asked for is the last
    if (m_running) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "fetching from LCSC cancelled: " << m_given << " package(s) given what "
                                                 << "LCSC has, of " << m_asked;
        if (m_onDone)
            JMainThreadDispatcher::instance().post([done = m_onDone, given = m_given] {
                done("Fetching from LCSC cancelled: " + std::to_string(given) + " package(s) given what LCSC has");
            });
    }
    if (m_changed && m_onChanged) JMainThreadDispatcher::instance().post(m_onChanged);
}

void JPlacerLcscFetchDialog::run(std::vector<JPLcscFetch::Ask> asks) {
    m_found.clear();
    m_given = 0;
    m_kept = 0;
    m_asked = int(asks.size());
    m_running = true;
    showProblems();
    const bool pads = std::any_of(asks.begin(), asks.end(), [](const JPLcscFetch::Ask& a) { return a.pads; });
    const bool dims = std::any_of(asks.begin(), asks.end(), [](const JPLcscFetch::Ask& a) { return a.dimensions; });
    m_what->setText(std::string("Asking LCSC (EasyEDA) for ") + (pads && dims ? "the footprints and dimensions (from their 3D models)"
                                                                 : pads ? "the footprints" : "the dimensions (from their 3D models)")
                    + " of " + std::to_string(asks.size()) + " package(s), by their parts' LCSC part numbers (only the part "
                    "numbers are sent).");
    m_bar->setProgress(0.f);
    m_note->setText("Starting\xE2\x80\xA6");
    m_retry->setEnabled(false);
    m_close->setLabel("Cancel");
    m_close->setTooltip("Stop after the package being asked for: what was given is kept");
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    std::weak_ptr<bool> alive = m_alive;
    std::thread([this, alive, cancel = m_cancel, cache = m_cacheDir, asks] {
        for (size_t i = 0; i < asks.size() && !*cancel; ++i) {
            JMainThreadDispatcher::instance().post([this, alive, i, n = asks.size(), name = asks[i].packageName,
                                                    lcsc = asks[i].lcsc] {
                if (const auto a = alive.lock(); !a || !*a) return;
                m_note->setText("Asking for " + name + " (" + lcsc + "): " + std::to_string(i + 1) + " of " + std::to_string(n));
            });
            const JPLcscFetch::Answer answer = JPLcscFetch::fetch(cache, asks[i]);
            JMainThreadDispatcher::instance().post([this, alive, i, n = asks.size(), ask = asks[i], answer] {
                if (const auto a = alive.lock(); !a || !*a) return;
                answered(i, n, ask, answer);
            });
        }
        JMainThreadDispatcher::instance().post([this, alive, cancel] {
            if (const auto a = alive.lock(); !a || !*a || *cancel) return;
            finished();
        });
    }).detach();
}

void JPlacerLcscFetchDialog::answered(size_t index, size_t of, const JPLcscFetch::Ask& ask, const JPLcscFetch::Answer& answer) {
    m_bar->setProgress(float(index + 1) / float(std::max<size_t>(of, 1)));
    const std::string given = JPLcscFetch::give(m_config, ask, answer);
    if (!given.empty()) {
        ++m_given;
        m_changed = true;
        JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "LCSC " << ask.lcsc << ": package " << ask.packageName << " given " << given
                                                 << " (the part " << (answer.partKept ? "as kept" : "from EasyEDA")
                                                 << (ask.dimensions ? std::string(", its 3D model ") + (answer.modelKept ? "as kept" : "from EasyEDA")
                                                                    : std::string())
                                                 << ")";
        if (answer.partKept && (!ask.dimensions || answer.modelKept)) ++m_kept;
    }
    for (const std::string& why : answer.problems) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "LCSC " << ask.lcsc << ": package " << ask.packageName << ": " << why;
        m_found.push_back({ ask, why });
    }
    if (!answer.problems.empty()) showProblems();
}

void JPlacerLcscFetchDialog::finished() {
    m_running = false;
    m_cancel.reset();
    if (m_changed && m_onChanged) {
        m_onChanged();
        m_changed = false;
    }
    std::string summary = std::to_string(m_given) + " of " + std::to_string(m_asked) + " package(s) given what LCSC has";
    if (m_kept > 0) summary += " (" + std::to_string(m_kept) + " from what was kept from before)";
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << summary << "; what LCSC gives is kept in " << m_cacheDir;
    if (m_found.empty()) {
        if (m_onDone) m_onDone(summary);
        close();
        return;
    }
    m_note->setText(summary + "; " + std::to_string(m_found.size()) + " problem(s), below. Retry asks again for those packages.");
    m_retry->setEnabled(true);
    m_close->setLabel("Close");
    m_close->setTooltip("Leave the packages that went wrong as they are");
}

void JPlacerLcscFetchDialog::retry() {
    if (m_running) return;
    std::vector<JPLcscFetch::Ask> again;
    for (const Problem& p : m_found)
        if (std::none_of(again.begin(), again.end(), [&p](const JPLcscFetch::Ask& a) { return a.packageId == p.ask.packageId; }))
            again.push_back(p.ask);
    run(std::move(again));
}

void JPlacerLcscFetchDialog::showProblems() {
    std::vector<std::vector<std::string>> rows;
    for (const Problem& p : m_found) rows.push_back({ p.ask.packageName, p.ask.lcsc, wanted(p.ask), p.why });
    m_problems->setRows(rows);
    m_problemsLabel->setText(m_found.empty() ? "Nothing has gone wrong" : "What went wrong (" + std::to_string(m_found.size()) + ")");
}

void JPlacerLcscFetchDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad, gap = st.spacing * 2;
    float y = contentTop();
    const float whatH = std::max(st.labelHeight, m_what->heightFor(cw));
    m_what->setBounds({ pad, y, cw, whatH });
    y += whatH + gap;
    m_bar->setBounds({ pad, y, cw, st.progressHeight });
    y += st.progressHeight + gap;
    const float noteH = std::max(st.labelHeight, m_note->heightFor(cw));
    m_note->setBounds({ pad, y, cw, noteH });
    y += noteH + gap;
    m_problemsLabel->setBounds({ pad, y, cw, st.labelHeight });
    y += st.labelHeight + st.spacing;
    const float buttonsY = h - pad - st.buttonHeight;
    m_problems->setBounds({ pad, y, cw, std::max(0.f, buttonsY - gap - y) });
    m_problems->setColumnWidths({ cw * 0.22f, cw * 0.12f, cw * 0.26f, cw * 0.4f - st.scrollBarWidth });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
