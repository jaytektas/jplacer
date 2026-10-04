// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPhotonSlotsDialog.h"

#include "tasks/JPPhotonCommands.h"

#include <j/core/JButton.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cstdio>

inline namespace jf {

namespace {

constexpr int kMostAddress = 254;

std::string said(const char* format, int address) {
    char text[96];
    std::snprintf(text, sizeof text, format, address);
    return text;
}

} // namespace

JPlacerPhotonSlotsDialog::JPlacerPhotonSlotsDialog(JPConfiguration& config, std::function<bool(Work)> runTask, JGpuHal& hal,
                                                   int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Program Feeder Slot Wizard", kW, kH, hal, sx, sy, parent), m_config(config), m_runTask(std::move(runTask)) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    m_text = std::make_unique<JLabel>(g, "To program your slots, begin by removing all the Photon feeders from your machine. "
                                         "Then, click Next.", 0.f);
    m_text->setWordWrap(true);
    add(m_text.get());
    // The second step's, added to it at Next.
    m_addressLabel = std::make_unique<JLabel>(g, "Current Slot Address", 0.f);
    m_address = std::make_unique<JSpinBox>(g, 1, kMostAddress, 0.f);
    m_address->setValue(1);
    m_address->onValueChanged.connect([this](int v) { setStatus(said("Please insert a feeder into slot %d.", v)); });
    m_status = std::make_unique<JLabel>(g, said("Please insert a feeder into slot %d.", 1), 0.f);
    m_status->setWordWrap(true);
    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_next = m_buttons->addButton("Next", JDialogButtonBox::Role::Action);
    m_next->onClicked.connect([this] { next(); });
    add(m_buttons.get());
}

JPlacerPhotonSlotsDialog::~JPlacerPhotonSlotsDialog() {
    m_shared->alive = false;
    m_shared->stop = true;
}

void JPlacerPhotonSlotsDialog::next() {
    if (m_programming) {
        // Finish.
        m_shared->stop = true;
        close();
        return;
    }
    m_programming = true;
    m_text->setText("Insert a feeder into the physical slot corresponding to the number shown below. You can change this "
                    "number as needed if you want to program a different address. This number will automatically "
                    "increment after the feeder slot is programmed. You can then move the current feeder or insert a "
                    "new feeder into the next slot.");
    add(m_addressLabel.get());
    add(m_address.get());
    add(m_status.get());
    m_next->setLabel("Finish");
    start();
}

void JPlacerPhotonSlotsDialog::start() {
    std::shared_ptr<Shared> shared = m_shared;
    JPConfiguration* config = &m_config;
    m_runTask([this, shared, config](JPJobMachine& machine, const OnMain& onMain, std::string&) {
        auto ui = [&](const std::function<void()>& fn) {
            onMain([&] {
                if (shared->alive) fn();
            });
        };
        JPPhotonBus bus(machine);
        while (!shared->stop) {
            std::string why;
            const auto found = JPPhotonCommands::uninitializedFeedersRespond(bus, why);
            if (!found || !found->valid) {
                if (why.empty()) continue;   // nobody waiting yet: asked again
                ui([&] { setStatus(why); });
                return true;
            }
            if (shared->stop) break;   // not programmed after Finish
            int address = 0;
            ui([&] {
                address = m_address->value();
                m_address->setEnabled(false);
                setStatus(said("Feeder found! Programming address %d.", address));
            });
            if (address == 0) break;
            const auto programmed = JPPhotonCommands::programFeederFloorAddress(bus, found->uuid, address, why);
            if (!programmed) {
                shared->stop = true;
                ui([&] { setStatus("Feeder address programming failed."); });
                return true;
            }
            ui([&] { setStatus("Programming done."); });
            const auto ready = JPPhotonCommands::initializeFeeder(bus, address, found->uuid, why);
            if (!ready || !ready->valid || ready->error != JPPhotonCommands::Error::None) {
                ui([&] { setStatus("Failed to initialize feeder after updating slot address."); });
                return true;
            }
            if (address == kMostAddress) {
                shared->stop = true;
                ui([&] { setStatus("Max feeder address reached."); });
                break;
            }
            // The search's highest address at least the one just programmed.
            onMain([&] { config->photon().setMaxFeederAddress(std::max(config->photon().maxFeederAddress(), address)); });
            ui([&] {
                m_address->setValue(address + 1);
                m_address->setEnabled(true);
            });
        }
        return true;
    });
}

void JPlacerPhotonSlotsDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    float y = contentTop();
    const float textH = std::max(st.labelHeight, m_text->heightFor(cw));
    m_text->setBounds({ pad, y, cw, textH });
    y += textH + st.spacing * 2;
    const float labelW = JTextHelper::measureWidth("Current Slot Address") + st.spacing * 2;
    m_addressLabel->setBounds({ pad, y, labelW, st.controlHeight });
    m_address->setBounds({ pad + labelW, y, std::min(cw - labelW, labelW), st.controlHeight });
    y += st.controlHeight + st.spacing * 2;
    m_status->setBounds({ pad, y, cw, std::max(0.f, buttonsY - st.spacing - y) });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
