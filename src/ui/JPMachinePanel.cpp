// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMachinePanel.h"

#include "JPUiParts.h"

#include "common/JPlacerLog.h"
#include "machine/JPSerialPorts.h"

#include <j/core/JComboBox.h>
#include <j/core/Log.h>

#include <cstdio>

inline namespace jf {

JPMachinePanel::JPMachinePanel(JSceneGraph& graph, JPCell& cell)
    : JContainer(graph), m_cell(cell) {
    JPUiParts::asPanel(*this);

    auto top = JPUiParts::row(graph);
    m_status = top->add(std::make_unique<JLabel>(graph, cell.config().name));
    m_status->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    add(std::move(top));

    m_state = add(std::make_unique<JLabel>(graph, ""));

    // Where each serial controller is plugged in: the ports there are now,
    // by the stable name a reboot does not change.
    std::vector<JPSerialPorts::Port> ports;
    for (const JPDriverConfig& d : cell.config().drivers) {
        if (d.link["type"].str() != "serial") continue;
        if (ports.empty()) ports = JPSerialPorts::list();
        const std::string current = d.link["port"].str();
        std::vector<std::string> labels, paths;
        int selected = -1;
        for (const JPSerialPorts::Port& p : ports) {
            if (p.path == current || JPSerialPorts::stablePath(current) == p.path) selected = int(labels.size());
            labels.push_back(p.label);
            paths.push_back(p.path);
        }
        if (selected < 0) {   // configured, but not plugged in now: still shown, so it is not silently lost
            selected = int(labels.size());
            labels.push_back(current + " (not found)");
            paths.push_back(current);
        }
        auto portRow = JPUiParts::row(graph);
        portRow->add(std::make_unique<JLabel>(graph, d.name + " port"));
        JComboBox* combo = portRow->add(std::make_unique<JComboBox>(graph, labels));
        combo->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        combo->setCurrentIndex(selected);
        const std::string id = d.id;
        combo->onIndexChanged.connect([this, id, paths](int i) {
            if (i < 0 || size_t(i) >= paths.size()) return;
            JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Machine: port " << paths[size_t(i)] << " for " << id;
            if (onPortChosen) onPortChosen(id, paths[size_t(i)]);
        });
        add(std::move(portRow));
    }

    add(std::make_unique<JLabel>(graph, "Calibration"));
    for (const JPCameraConfig& c : cell.config().cameras) {
        JLabel* l = add(std::make_unique<JLabel>(graph, ""));
        l->setWordWrap(true);
        m_cameras.push_back({ c.id, l });
    }
    m_squareness = add(std::make_unique<JLabel>(graph, ""));
    m_squareness->setWordWrap(true);
    m_homing = add(std::make_unique<JLabel>(graph, ""));
    m_homing->setWordWrap(true);

    m_watch.on(cell.onCalibration, [this] { refreshCalibration(); });
    m_watch.on(cell.onConnection, [this](bool, std::string why) { refresh(why); });
    m_watch.on(cell.onHomed,      [this](bool) { refresh(std::string()); });
    m_watch.on(cell.onState,      [this](std::string, std::string) { refresh(std::string()); });
    m_watch.on(cell.onMotion,     [this](bool ok, std::string why) { refresh(ok ? std::string() : why); });
    refresh(std::string());
    refreshCalibration();
}

void JPMachinePanel::refreshCalibration() {
    auto fixed = [](double v, int decimals) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
        return std::string(buf);
    };
    const JPCellConfig& config = m_cell.config();
    for (const auto& [id, label] : m_cameras) {
        std::string name = id;
        bool up = false;
        for (const JPCameraConfig& c : config.cameras)
            if (c.id == id) {
                name = c.name;
                up = c.looksUp;
            }
        // A calibration for each picture size it was measured at.
        std::string text;
        for (const JPCameraCalibration& k : m_cell.cameraCalibrations(id))
            text += (text.empty() ? "" : "; ") + std::to_string(k.width) + "\xC3\x97" + std::to_string(k.height) + " "
                  + fixed(k.scaleX(), 3) + " x " + fixed(k.scaleY(), 3) + " px/mm, turned " + fixed(k.rotationDeg(up), 2)
                  + " deg" + (k.mirrored(up) ? ", mirrored" : "") + ", lens " + fixed(k.lensK1, 3) + ", fit to "
                  + fixed(k.rmsPx, 2) + " px (" + k.when + ")";
        label->setText(name + ": " + (text.empty() ? "not calibrated" : text));
    }
    const JPSquarenessConfig q = m_cell.squareness();
    m_squareness->setText(q.active() ? "Squareness: corrected for Y leaning " + fixed(q.xPerY * 100, 3)
                                           + " mm in X per 100 mm"
                                     : "Squareness: not corrected (a board located by three or more fiducials measures it)");
    bool visual = false;
    for (const JPHeadConfig& h : config.heads) visual = visual || h.visualHoming;
    m_homing->setText(visual ? "Homing: by the switches, then by the homing mark with the head camera"
                             : "Homing: by the switches");
}

void JPMachinePanel::refresh(const std::string& why) {
    const bool connected = m_cell.isConnected();
    std::string text = m_cell.config().name + (connected ? ": connected" : ": not connected");
    if (connected) {
        for (const auto& [id, fw] : m_cell.firmware()) text += " \xC2\xB7 " + fw;
        text += m_cell.isHoming() ? " \xC2\xB7 homing" : m_cell.isHomed() ? " \xC2\xB7 homed" : " \xC2\xB7 not homed";
    }
    m_status->setText(text);

    std::string state;
    if (connected)
        for (const auto& [id, s] : m_cell.states()) {
            const JPDriverConfig* d = m_cell.config().driver(id);
            state += (state.empty() ? "" : ", ") + (d ? d->name : id) + ": " + s;
        }
    if (!why.empty()) state = why;
    m_state->setText(state);
}

} // inline namespace jf
