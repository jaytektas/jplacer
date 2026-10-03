// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJogPanel.h"

#include "JPUiParts.h"

#include "common/JPlacerLog.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/Log.h>

#include <algorithm>
#include <cstdlib>
#include <tuple>

inline namespace jf {

namespace {

// What a step and a speed can be. Steps are mm, or degrees on a rotation;
// speeds are shares of the slowest moving axis's rate.
const std::vector<double>      kSteps      = { 0.01, 0.1, 1, 10, 100 };
const std::vector<std::string> kStepLabels = { "0.01", "0.1", "1", "10", "100" };
constexpr int                  kStepFirst  = 2;
const std::vector<double>      kSpeeds      = { 0.1, 0.25, 0.5, 1.0 };
const std::vector<std::string> kSpeedLabels = { "10%", "25%", "50%", "100%" };
constexpr int                  kSpeedFirst  = 1;

} // namespace

JPJogPanel::JPJogPanel(JSceneGraph& graph, JPCell& cell)
    : JContainer(graph), m_cell(cell) {
    JPUiParts::asPanel(*this);
    const JPCellConfig& c = cell.config();
    auto hasAxes = [](const JPMountConfig& m) {
        return !m.axisX.empty() || !m.axisY.empty() || !m.axisZ.empty() || !m.axisRotation.empty();
    };
    for (const JPNozzleConfig& n : c.nozzles)     if (hasAxes(n.mount)) m_tools.push_back({ n.id, n.name, &n.mount });
    for (const JPCameraConfig& m : c.cameras)     if (hasAxes(m.mount)) m_tools.push_back({ m.id, m.name, &m.mount });
    for (const JPActuatorConfig& a : c.actuators) if (hasAxes(a.mount)) m_tools.push_back({ a.id, a.name, &a.mount });

    if (m_tools.empty()) {
        add(std::make_unique<JLabel>(graph, "Nothing in this cell moves on axes."));
        return;
    }

    std::vector<std::string> names;
    for (const Tool& t : m_tools) names.push_back(t.name);
    JPChoiceRow* tools = add(std::make_unique<JPChoiceRow>(graph, names, 0));
    tools->onChosen.connect([this](int i) { showTool(size_t(i)); });

    m_coords = add(std::make_unique<JContainer>(graph));
    m_coords->setDirection(JFlexDirection::Column)->setGap(JStyle::current().spacing)->setAlignItems(JAlignItems::Stretch);

    auto stepRow = JPUiParts::row(graph);
    stepRow->add(std::make_unique<JLabel>(graph, "Step", labelWidth()));
    m_step = stepRow->add(std::make_unique<JPChoiceRow>(graph, kStepLabels, kStepFirst));
    add(std::move(stepRow));
    auto speedRow = JPUiParts::row(graph);
    speedRow->add(std::make_unique<JLabel>(graph, "Speed", labelWidth()));
    m_speed = speedRow->add(std::make_unique<JPChoiceRow>(graph, kSpeedLabels, kSpeedFirst));
    add(std::move(speedRow));

    m_note = add(std::make_unique<JLabel>(graph, ""));
    m_note->setWordWrap(true);

    m_watch.on(cell.onPositions, [this](std::map<std::string, double> p) { showPositions(p); });
    m_watch.on(cell.onMotion, [this](bool ok, std::string why) { m_note->setText(ok ? std::string() : why); });
    m_watch.on(cell.onHomed, [this](bool homed) {
        m_note->setText(homed ? std::string() : "Home the machine to move it.");
    });
    showTool(0);
    if (!cell.isHomed()) m_note->setText("Home the machine to move it.");
}

void JPJogPanel::showTool(size_t index) {
    if (index >= m_tools.size()) return;
    m_tool = index;
    m_coords->clear();
    m_coordinates.clear();
    JSceneGraph& graph = m_graph;
    const JPMountConfig& m = *m_tools[index].mount;
    // The tool's own coordinates: where its axes are, plus its offset on the head.
    const std::tuple<const char*, const std::string*, double> axes[] = {
        { "X", &m.axisX, m.offsetX }, { "Y", &m.axisY, m.offsetY }, { "Z", &m.axisZ, m.offsetZ },
        { "Rotation", &m.axisRotation, 0.0 } };
    for (const auto& [label, axis, offset] : axes) {
        if (axis->empty()) continue;
        const int i = int(m_coordinates.size());
        auto r = JPUiParts::row(graph);
        r->add(std::make_unique<JLabel>(graph, label, labelWidth()));
        JLineEdit* field = r->add(std::make_unique<JLineEdit>(graph));
        field->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        field->onReturnPressed.connect([this, i] {
            const Coordinate& co = m_coordinates[size_t(i)];
            const std::string text = co.field->text();
            char* end = nullptr;
            const double target = std::strtod(text.c_str(), &end);
            if (text.empty() || end == text.c_str()) return;
            JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Jog: " << m_tools[m_tool].name << " " << co.axisId << " to " << target;
            co.field->setText("");
            m_cell.moveAxes({ { co.axisId, target - co.offset } }, speed());
        });
        r->add(JPUiParts::button(graph, "-"))->onClicked.connect([this, i] { step(i, -1); });
        r->add(JPUiParts::button(graph, "+"))->onClicked.connect([this, i] { step(i, +1); });
        m_coordinates.push_back({ *axis, offset, field });
        m_coords->add(std::move(r));
    }
    // A nozzle with a vacuum picks and places where it is, as in OpenPnP.
    for (const JPNozzleConfig& n : m_cell.config().nozzles) {
        if (n.id != m_tools[index].id || n.vacuumActuatorId.empty()) continue;
        auto r = JPUiParts::row(graph);
        r->add(std::make_unique<JLabel>(graph, "Vacuum", labelWidth()));
        r->add(JPUiParts::button(graph, "Pick"))->onClicked.connect([this, id = n.id, name = n.name] {
            JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Jog: pick with " << name;
            m_cell.pick(id);
        });
        r->add(JPUiParts::button(graph, "Place"))->onClicked.connect([this, id = n.id, name = n.name] {
            JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Jog: place with " << name;
            m_cell.place(id);
        });
        m_coords->add(std::move(r));
    }
    showPositions(m_cell.positions());
}

void JPJogPanel::showPositions(const std::map<std::string, double>& positions) {
    // The box shows where the coordinate is now until something is typed in it.
    for (const Coordinate& co : m_coordinates)
        if (const auto p = positions.find(co.axisId); p != positions.end())
            co.field->setPlaceholderText(JPUiParts::coordinate(p->second + co.offset));
}

void JPJogPanel::step(int coordinate, double direction) {
    const Coordinate& co = m_coordinates[size_t(coordinate)];
    const auto now = m_cell.jogBase();
    const auto p = now.find(co.axisId);
    if (p == now.end()) return;
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Jog: " << m_tools[m_tool].name << " " << co.axisId << " by "
                                            << direction * stepSize();
    m_cell.moveAxes({ { co.axisId, p->second + direction * stepSize() } }, speed());
}

std::vector<std::pair<std::string, double>> JPJogPanel::where() const {
    std::vector<std::pair<std::string, double>> out;
    if (m_tool >= m_tools.size()) return out;
    const JPMountConfig& m = *m_tools[m_tool].mount;
    const auto positions = m_cell.positions();
    const std::tuple<const char*, const std::string*, double> axes[] = {
        { "X", &m.axisX, m.offsetX }, { "Y", &m.axisY, m.offsetY }, { "Z", &m.axisZ, m.offsetZ },
        { "C", &m.axisRotation, 0.0 } };
    for (const auto& [name, axis, offset] : axes)
        if (const auto p = positions.find(*axis); !axis->empty() && p != positions.end())
            out.emplace_back(name, p->second + offset);
    return out;
}

// One width for every label in the panel, so fields and buttons line up: the
// widest label's text and a gap.
float JPJogPanel::labelWidth() {
    float w = 0;
    for (const char* l : { "X", "Y", "Z", "Rotation", "Step", "Speed" }) w = std::max(w, JTextHelper::measureWidth(l));
    return w + 2 * JStyle::current().spacing;
}

double JPJogPanel::stepSize() const { return kSteps[size_t(m_step->chosen())]; }
double JPJogPanel::speed() const    { return kSpeeds[size_t(m_speed->chosen())]; }

const std::string& JPJogPanel::toolId() const {
    static const std::string none;
    return m_tools.empty() ? none : m_tools[m_tool].id;
}

} // inline namespace jf
