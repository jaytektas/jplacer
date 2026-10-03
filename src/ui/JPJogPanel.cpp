// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJogPanel.h"

#include "JPIconButton.h"
#include "JPIcons.h"
#include "JPUiParts.h"

#include "common/JPlacerLog.h"

#include <j/core/JButton.h>
#include <j/core/JScrollArea.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <tuple>

inline namespace jf {

namespace {

// How far a press moves, as OpenPnP offers: mm, or degrees turning.
const std::vector<double>      kDistances      = { 0.01, 0.1, 1, 10, 25, 50, 100 };
const std::vector<std::string> kDistanceLabels = { "0.01", "0.1", "1", "10", "25", "50", "100" };
// The slowest a jog goes: a speed slid to nothing still moves.
constexpr double kLeastSpeed = 0.01;

std::string percent(double share) {
    return std::to_string(int(std::lround(share * 100))) + "%";
}

} // namespace

JPJogPanel::JPJogPanel(JSceneGraph& graph, JPCell& cell, Choices start) : JContainer(graph), m_cell(cell) {
    JPUiParts::asPanel(*this);
    const JPCellConfig& c = cell.config();
    auto hasAxes = [](const JPMountConfig& m) {
        return !m.axisX.empty() || !m.axisY.empty() || !m.axisZ.empty() || !m.axisRotation.empty();
    };
    auto headName = [&c](const JPMountConfig& m) {
        for (const JPHeadConfig& h : c.heads) if (h.id == m.headId) return " (Head: " + h.name + ")";
        return std::string();
    };
    // Named as OpenPnP names them: what it is, its name (a nozzle's tip), its head.
    for (const JPNozzleConfig& n : c.nozzles) {
        if (!hasAxes(n.mount)) continue;
        std::string tip;
        for (const JPNozzleTipConfig& t : c.nozzleTips) if (t.id == n.tipId) tip = " - " + t.name;
        m_tools.push_back({ n.id, "Nozzle: " + n.name + tip + headName(n.mount), &n.mount, true, false });
    }
    for (const JPCameraConfig& m : c.cameras)
        if (hasAxes(m.mount)) m_tools.push_back({ m.id, "Camera: " + m.name + headName(m.mount), &m.mount, false, true });
    for (const JPActuatorConfig& a : c.actuators)
        if (hasAxes(a.mount)) m_tools.push_back({ a.id, "Actuator: " + a.name + headName(a.mount), &a.mount, false, false });

    if (m_tools.empty()) {
        add(std::make_unique<JLabel>(graph, "Nothing in this cell moves on axes."));
        return;
    }
    for (size_t i = 0; i < m_tools.size(); ++i) {
        if (m_tools[i].id == start.tool) m_tool = i;
        if (m_tools[i].nozzle && (m_tools[m_lastNozzle].nozzle == false || m_tools[i].id == start.tool)) m_lastNozzle = i;
    }

    std::vector<std::string> labels;
    for (const Tool& t : m_tools) labels.push_back(t.label);
    JComboBox* tools = add(std::make_unique<JComboBox>(graph, labels, 0.f));
    tools->setCurrentIndex(int(m_tool));
    tools->onIndexChanged.connect([this](int i) {
        if (i < 0 || size_t(i) >= m_tools.size()) return;
        m_tool = size_t(i);
        if (m_tools[m_tool].nozzle) m_lastNozzle = m_tool;
        if (onChoicesChanged) onChoicesChanged();
    });

    m_tabs = add(std::make_unique<JTabWidget>(graph, 0.f, 0.f));
    m_tabs->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_pages.push_back(jogPage());
    m_tabs->addTab("Jog", m_pages.back().get());
    m_pages.push_back(specialPage());
    m_tabs->addTab("Special", m_pages.back().get());

    m_distance->choose(std::clamp(start.distance, 0, int(kDistances.size()) - 1));
    m_speed->setValue(float(std::clamp(start.speed, 0.0, 1.0)));
    m_speedLabel->setText(percent(speed()));

    m_note = add(std::make_unique<JLabel>(graph, ""));
    m_note->setWordWrap(true);
    m_watch.on(cell.onMotion, [this](bool ok, std::string why) { m_note->setText(ok ? std::string() : why); });
    m_watch.on(cell.onHomed, [this](bool homed) {
        m_note->setText(homed ? std::string() : "Home the machine to move it.");
    });
    if (!cell.isHomed()) m_note->setText("Home the machine to move it.");
}

float JPJogPanel::padSize() {
    return 1.05f * JStyle::current().buttonHeight;
}

std::unique_ptr<JWidget> JPJogPanel::pad(const char* name, void (*glyph)(JVectorCanvas&, float, float, float, const JColor&),
                                         const std::string& tooltip, const std::string& action) {
    auto b = std::make_unique<JPIconButton>(m_graph, name, glyph, tooltip);
    b->setFramed(true);
    b->setFixedSize(padSize(), padSize());
    b->onClicked.connect([this, action] { act(action); });
    return b;
}

std::unique_ptr<JWidget> JPJogPanel::parkButton(const std::string& tooltip, const std::string& action) {
    auto b = JPUiParts::button(m_graph, "P");
    b->setFixedSize(padSize(), padSize());
    b->setTooltip(tooltip);
    b->onClicked.connect([this, action] { act(action); });
    return b;
}

std::unique_ptr<JWidget> JPJogPanel::gap() {
    auto g = std::make_unique<JContainer>(m_graph, padSize(), padSize());
    g->setFixedSize(padSize(), padSize());
    return g;
}

std::unique_ptr<JWidget> JPJogPanel::jogPage() {
    const JStyle& st = JStyle::current();
    JSceneGraph& g = m_graph;
    // Rows of fixed height, stacked by a scroll area: a short dock scrolls
    // rather than cutting the last rows off.
    auto page = std::make_unique<JScrollArea>(g, 0.f, 0.f);
    auto row = [&](float h) {
        auto r = std::make_unique<JContainer>(g, 0.f, h);
        r->setDirection(JFlexDirection::JRow)->setGap(st.spacing)->setAlignItems(JAlignItems::Center);
        r->setVSizePolicy(JSizePolicyMode::Fixed);
        r->setFixedSize(0.f, h);
        r->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        return r;
    };
    auto title = [&](const std::string& text, float h) {
        auto box = std::make_unique<JContainer>(g, padSize(), h);
        box->setFixedSize(padSize(), h);
        box->setDirection(JFlexDirection::JRow)->setAlignItems(JAlignItems::Center);
        g.getLayout(box->getNodeId()).justifyContent = JJustifyContent::Center;
        auto l = std::make_unique<JLabel>(g, text, 0.f, st.labelHeight);
        l->setFixedSize(std::ceil(JTextHelper::measureWidth(text)) + st.spacing, st.labelHeight);
        box->add(std::move(l));
        return box;
    };
    using I = JPIcons;
    // OpenPnP's pad, in six columns: Home and C | the X/Y cross | Z | the
    // position buttons; Distance in a column beside it.
    const float padH = st.labelHeight + 4 * padSize() + 4 * st.spacing;
    auto block = row(padH);
    block->setAlignItems(JAlignItems::Start);
    auto padColumn = std::make_unique<JContainer>(g, 0.f, padH);
    padColumn->setDirection(JFlexDirection::Column)->setGap(st.spacing)->setAlignItems(JAlignItems::Start);
    padColumn->setFixedSize(6 * padSize() + 5 * st.spacing, padH);
    auto heads = row(st.labelHeight);
    heads->add(title("", st.labelHeight)); heads->add(title("", st.labelHeight)); heads->add(title("X/Y", st.labelHeight));
    heads->add(title("", st.labelHeight)); heads->add(title("Z", st.labelHeight));
    padColumn->add(std::move(heads));
    auto top = row(padSize());
    top->add(pad("Home", &I::home, "Home all axes (Ctrl+H)", "home"));
    top->add(gap());
    top->add(pad("Y+", &I::arrowUp, "Y+ (Ctrl+Up)", "y+"));
    top->add(gap());
    top->add(pad("Z+", &I::arrowUp, "Z+ (Ctrl+')", "z+"));
    top->add(pad("Position Nozzle", &I::moveNozzle, "Put the nozzle where the camera is looking", "positionNozzle"));
    padColumn->add(std::move(top));
    auto middle = row(padSize());
    middle->add(gap());
    middle->add(pad("X-", &I::arrowLeft, "X- (Ctrl+Left)", "x-"));
    middle->add(parkButton("Park the head (Ctrl+Shift+P)", "parkXY"));
    middle->add(pad("X+", &I::arrowRight, "X+ (Ctrl+Right)", "x+"));
    middle->add(parkButton("Up to safe Z (Ctrl+Shift+L)", "parkZ"));
    middle->add(pad("Position Camera", &I::moveCamera, "Put the camera over the nozzle", "positionCamera"));
    padColumn->add(std::move(middle));
    auto bottom = row(padSize());
    bottom->add(gap());
    bottom->add(gap());
    bottom->add(pad("Y-", &I::arrowDown, "Y- (Ctrl+Down)", "y-"));
    bottom->add(gap());
    bottom->add(pad("Z-", &I::arrowDown, "Z- (Ctrl+/)", "z-"));
    padColumn->add(std::move(bottom));
    auto turn = row(padSize());
    turn->add(title("C", padSize()));
    turn->add(pad("C+", &I::rotateAnticlockwise, "Turn anticlockwise (Ctrl+,)", "c+"));
    turn->add(parkButton("Turn to 0", "parkC"));
    turn->add(pad("C-", &I::rotateClockwise, "Turn clockwise (Ctrl+.)", "c-"));
    padColumn->add(std::move(turn));
    block->add(std::move(padColumn));

    // Distance [mm/deg]: its title, and the steps two to a row.
    auto distanceColumn = std::make_unique<JContainer>(g, 0.f, padH);
    distanceColumn->setDirection(JFlexDirection::Column)->setGap(st.spacing)->setAlignItems(JAlignItems::Start);
    distanceColumn->add(std::make_unique<JLabel>(g, "Distance", 0.f, st.labelHeight));
    m_distance = distanceColumn->add(std::make_unique<JPChoiceRow>(g, kDistanceLabels, kDistanceFirst, 2));
    distanceColumn->setFixedSize(m_graph.getLayoutConst(m_distance->getNodeId()).boundingBox.width, padH);
    m_distance->setTooltip("How far a press moves: mm, or degrees turning (Ctrl+- / Ctrl+=)");
    m_distance->onChosen.connect([this](int) {
        if (onChoicesChanged) onChoicesChanged();
    });
    block->add(std::move(distanceColumn));
    page->addChildWidget(std::move(block));

    const float rowH = std::max(st.buttonHeight, st.controlHeight);
    auto speedRow = row(rowH);
    speedRow->add(std::make_unique<JLabel>(g, "Speed [%]"));
    m_speed = speedRow->add(std::make_unique<JSlider>(g, 0.f, 0.f));
    m_speed->setFixedSize(4 * padSize(), st.sliderHeight);
    m_speedLabel = speedRow->add(std::make_unique<JLabel>(g, "100%"));
    m_speedLabel->setMinWidthFollowsText(true);
    m_speed->onValueChanged.connect([this](float) {
        m_speedLabel->setText(percent(speed()));
        if (onChoicesChanged) onChoicesChanged();
    });
    page->addChildWidget(std::move(speedRow));
    return page;
}

std::unique_ptr<JWidget> JPJogPanel::specialPage() {
    const JStyle& st = JStyle::current();
    auto page = std::make_unique<JContainer>(m_graph, 0.f, 0.f);
    page->setDirection(JFlexDirection::Column)->setGap(st.spacing)->setAlignItems(JAlignItems::Start)
        ->setPadding(JEdges(st.spacing));
    auto buttons = JPUiParts::row(m_graph);
    struct B { const char* label; const char* action; const char* tip; };
    for (const B& b : { B{ "Head Safe Z", "safeZ", "Every Z on the head up to safe Z (Ctrl+Shift+Z)" },
                        B{ "Discard", "discard", "Drop the nozzle's part at the discard location (Ctrl+Shift+D)" },
                        B{ "Pick", "pick", "Vacuum on where the nozzle is, as a pick does" },
                        B{ "Place", "place", "Vacuum off and blow off where the nozzle is, as a place does" } }) {
        JButton* button = buttons->add(JPUiParts::button(m_graph, b.label));
        button->setTooltip(b.tip);
        button->onClicked.connect([this, action = std::string(b.action)] { act(action); });
    }
    page->add(std::move(buttons));
    return page;
}

double JPJogPanel::distance() const { return kDistances[size_t(m_distance->chosen())]; }
double JPJogPanel::speed() const    { return std::max(kLeastSpeed, double(m_speed->getValue())); }

JPJogPanel::Choices JPJogPanel::choices() const {
    if (m_tools.empty()) return {};
    return { m_tools[m_tool].id, m_distance->chosen(), double(m_speed->getValue()) };
}

const std::string& JPJogPanel::toolId() const {
    static const std::string none;
    return m_tools.empty() ? none : m_tools[m_tool].id;
}

const JPJogPanel::Tool* JPJogPanel::nozzle() const {
    if (m_tools.empty()) return nullptr;
    if (m_tools[m_tool].nozzle) return &m_tools[m_tool];
    return m_tools[m_lastNozzle].nozzle ? &m_tools[m_lastNozzle] : nullptr;
}

const JPJogPanel::Tool* JPJogPanel::camera() const {
    for (const Tool& t : m_tools)
        if (t.camera && t.mount->headId == (nozzle() ? nozzle()->mount->headId : t.mount->headId)) return &t;
    return nullptr;
}

void JPJogPanel::jog(double dx, double dy, double dz, double dc) {
    if (m_tools.empty()) return;
    const double d = distance();
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Jog: " << m_tools[m_tool].label << " by " << dx * d << ", " << dy * d << ", "
                                            << dz * d << ", " << dc * d;
    m_cell.jog(m_tools[m_tool].id, dx * d, dy * d, dz * d, dc * d, speed());
}

void JPJogPanel::moveTo(const Tool& tool, const Tool& over) {
    // Where `over` is now (its axes and its offset), for `tool` to go to.
    const auto p = m_cell.jogBase();
    const JPMountConfig& m = *over.mount;
    const auto px = p.find(m.axisX), py = p.find(m.axisY);
    if (px == p.end() || py == p.end()) {
        m_note->setText(over.label + " has no X and Y position yet.");
        return;
    }
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Jog: " << tool.label << " to where " << over.label << " is";
    m_cell.moveTool(*tool.mount, { px->second + m.offsetX, py->second + m.offsetY, std::nullopt, std::nullopt }, speed());
}

bool JPJogPanel::act(const std::string& action) {
    if (m_tools.empty()) return false;
    const Tool& t = m_tools[m_tool];
    const std::string& head = t.mount->headId;
    if (action == "x+") jog(1, 0, 0, 0);
    else if (action == "x-") jog(-1, 0, 0, 0);
    else if (action == "y+") jog(0, 1, 0, 0);
    else if (action == "y-") jog(0, -1, 0, 0);
    else if (action == "z+") jog(0, 0, 1, 0);
    else if (action == "z-") jog(0, 0, -1, 0);
    else if (action == "c+") jog(0, 0, 0, 1);
    else if (action == "c-") jog(0, 0, 0, -1);
    else if (action == "home") m_cell.home();
    else if (action == "parkXY") m_cell.park(head, speed());
    else if (action == "parkZ" || action == "safeZ") m_cell.safeZ(head, speed());
    else if (action == "parkC") {
        if (!t.mount->axisRotation.empty()) m_cell.moveAxes({ { t.mount->axisRotation, 0.0 } }, speed());
    } else if (action == "positionNozzle" || action == "positionCamera") {
        const Tool* n = nozzle();
        const Tool* c = camera();
        if (!n || !c) {
            m_note->setText("There needs to be a nozzle and a camera on its head.");
            return true;
        }
        if (action == "positionNozzle") moveTo(*n, *c);
        else moveTo(*c, *n);
    } else if (action == "discard" || action == "pick" || action == "place") {
        const Tool* n = nozzle();
        if (!n) {
            m_note->setText("Choose a nozzle.");
            return true;
        }
        if (action == "discard") m_cell.discard(n->id, speed());
        else if (action == "pick") m_cell.pick(n->id);
        else m_cell.place(n->id);
    } else if (action == "distance+" || action == "distance-") {
        const int i = m_distance->chosen() + (action == "distance+" ? 1 : -1);
        if (i >= 0 && i < int(kDistances.size())) {
            m_distance->choose(i);
            if (onChoicesChanged) onChoicesChanged();
        }
    } else {
        return false;
    }
    return true;
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

} // inline namespace jf
