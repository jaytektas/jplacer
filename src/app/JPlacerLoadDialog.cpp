// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerLoadDialog.h"

#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cstdio>

inline namespace jf {

namespace {

std::string laneWords(const JPLaneChoice::Lane& l) {
    char width[32];
    std::snprintf(width, sizeof width, "%g mm", l.widthMm);
    std::string s = l.name + " (" + width;
    if (!l.widthFits) s += ", not the part's width";
    s += l.removePartId.empty() ? ", empty)" : ", holds " + l.removePartId + ")";
    return s;
}

} // namespace

JPlacerLoadDialog::JPlacerLoadDialog(const JPConfiguration& config, const std::string& partId,
                                     std::vector<JPLaneChoice::Lane> lanes, Actions actions, JGpuHal& hal, int sx, int sy,
                                     NativeWinHandleType parent)
    : JDialogWindow("Load " + partId, kW, kH, hal, sx, sy, parent)
    , m_partId(partId)
    , m_packaging(JPLaneChoice::packagingWords(config, partId))
    , m_lanes(std::move(lanes))
    , m_actions(std::move(actions)) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_what = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_what->setWordWrap(true);
    m_how = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_how->setWordWrap(true);

    const float labelW = JTextHelper::measureWidth("Lane") + st.spacing * 2;
    auto row = [&](const std::string& label, std::vector<std::string> items, const std::string& tip) {
        auto r = JPUiParts::row(g);
        JLabel* l = r->add(std::make_unique<JLabel>(g, label, 0.f));
        l->setFixedSize(labelW, st.controlHeight);
        JComboBox* c = r->add(std::make_unique<JComboBox>(g, std::move(items)));
        c->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        c->setCurrentIndex(0);
        c->setTooltip(tip);
        m_content->add(std::move(r));
        return c;
    };
    if (!m_lanes.empty()) {
        std::vector<std::string> names;
        for (const JPLaneChoice::Lane& l : m_lanes) names.push_back(laneWords(l));
        m_lane = row("Lane", names, "The free lanes, best first: empty ones, then those whose part this run is done with; "
                                    "the part's tape width first");
        m_lane->onIndexChanged.connect([this](int) { describe(); });
    }
    if (const JPPart* part = config.libraryPart(partId)) {
        const auto lots = config.stock().lots(part->uuid, false);
        if (!lots.empty()) {
            std::vector<std::string> names { "None (not counted against a lot)" };
            for (const JPStockLot& l : lots) {
                names.push_back(l.label + " (" + std::to_string(l.onHand) + (l.location.empty() ? "" : ", " + l.location) + ")");
                m_lotUuids.push_back(l.uuid);
            }
            m_lot = row("Lot", names, "Which of the part's stock lots is being loaded: the run counts it down");
            if (m_lotUuids.size() == 1) m_lot->setCurrentIndex(1);
        }
    }
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    JButton* stop = m_buttons->addButton("Stop", JDialogButtonBox::Role::Action);
    stop->setTooltip("Stop the run: the nozzles emptied, the head parked");
    stop->onClicked.connect([this] {
        close();
        if (m_actions.stop) m_actions.stop();
    });
    JButton* skip = m_buttons->addButton("Skip This Part", JDialogButtonBox::Role::Action);
    skip->setTooltip("Go on without it: its placements are left unplaced and listed at the end");
    skip->onClicked.connect([this] {
        close();
        if (m_actions.skip) m_actions.skip();
    });
    JButton* go = m_buttons->addButton("Continue", JDialogButtonBox::Role::Accept);
    go->setTooltip(m_lanes.empty() ? "Set up a feeder for it on the Feeders tab first; Continue asks again if none holds it"
                                   : "It is laid in the lane: the run goes on");
    m_buttons->onAccept.connect([this] {
        const int lane = m_lane ? m_lane->currentIndex() : -1;
        const int lot = m_lot ? m_lot->currentIndex() : 0;
        close();
        if (m_actions.loaded)
            m_actions.loaded(lane >= 0 && size_t(lane) < m_lanes.size() ? m_lanes[size_t(lane)].feederId : std::string(),
                             lot > 0 && size_t(lot) <= m_lotUuids.size() ? m_lotUuids[size_t(lot) - 1] : std::string());
    });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
    describe();
}

void JPlacerLoadDialog::describe() {
    const int i = m_lane ? m_lane->currentIndex() : -1;
    if (i < 0 || size_t(i) >= m_lanes.size()) {
        m_what->setText("Load " + m_partId + ": the run has placed what is loaded and needs it next. No lane is free: every strip "
                        "lane holds a part this run still needs.");
        m_how->setText("Set a feeder up for it on the Feeders tab, then Continue; or Skip This Part.");
        return;
    }
    const JPLaneChoice::Lane& l = m_lanes[size_t(i)];
    m_what->setText("Load " + m_partId + (m_packaging.empty() ? "" : " (" + m_packaging + ")") + " into " + l.name +
                    (l.removePartId.empty() ? "." : ", taking " + l.removePartId + " off first."));
    m_how->setText("Lay the strip as the lane's strips lie, its first part at the lane's first hole. Continue sets the lane to "
                   "the part, starts its count again, and the run goes on." +
                   std::string(l.widthFits ? "" : " This lane is not the part's tape width."));
}

void JPlacerLoadDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    for (JLabel* l : { m_what, m_how }) l->setMinimumSize(0.f, std::max(st.labelHeight, l->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, 0.f, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
