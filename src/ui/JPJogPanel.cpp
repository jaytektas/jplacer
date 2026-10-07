// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJogPanel.h"

#include "common/JPWhen.h"

#include "model/JPSystemUnits.h"

#include "JPIconButton.h"
#include "JPIcons.h"
#include "JPUiParts.h"

#include "common/JPlacerLog.h"

#include <j/core/FrameTimer.h>
#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JScrollArea.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <tuple>

inline namespace jf {

namespace {

// The slowest a jog goes: a speed slid to nothing still moves.
constexpr double kLeastSpeed = 0.01;

std::string percent(double share) {
    return std::to_string(int(std::lround(share * 100))) + "%";
}

} // namespace

const std::vector<double>& JPJogPanel::defaultDistances() {
    // As OpenPnP offers, in millimetres or in inches (and a few between).
    static const std::vector<double> mm = { 0.01, 0.1, 1, 10, 25, 50, 100 };
    static const std::vector<double> inches = { 0.001, 0.01, 0.1, 1, 2, 5, 10 };
    return JPSystemUnits::inches() ? inches : mm;
}

const std::vector<double>& JPJogPanel::defaultSpeeds() {
    static const std::vector<double> steps = { 0.1, 0.25, 0.5, 0.75, 1 };
    return steps;
}

std::vector<double> JPJogPanel::parseSteps(const std::string& text, double least, double most, std::string& why) {
    std::vector<double> steps;
    std::istringstream in(text);
    std::string word;
    while (in >> word) {
        char* end = nullptr;
        const double v = std::strtod(word.c_str(), &end);
        if (end == word.c_str() || *end != '\0') {
            why = "'" + word + "' is not a number";
            return {};
        }
        if (v < least || v > most) {
            why = stepText(v) + " is outside " + stepText(least) + " to " + stepText(most);
            return {};
        }
        if (!steps.empty() && v <= steps.back()) {
            why = "the steps go smallest first, each larger than the one before";
            return {};
        }
        steps.push_back(v);
    }
    if (steps.empty()) why = "there must be at least one step";
    return steps;
}

std::string JPJogPanel::formatSteps(const std::vector<double>& steps) {
    std::string text;
    for (double v : steps) text += (text.empty() ? "" : " ") + stepText(v);
    return text;
}

std::string JPJogPanel::stepText(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

JPJogPanel::JPJogPanel(JSceneGraph& graph, JPCell& cell, Choices start) : JContainer(graph), m_cell(cell) {
    JPUiParts::asPanel(*this);
    m_distances = start.distances.empty() ? defaultDistances() : start.distances;
    m_speeds    = start.speeds.empty() ? defaultSpeeds() : start.speeds;
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

    m_stepThrough = start.stepThrough;
    // The tool, and beside it the chosen nozzle's tip menu. The buttons keep
    // their size; the tool's box takes what room is left.
    auto top = JPUiParts::row(graph);
    const float side = JStyle::current().buttonHeight;
    std::vector<std::string> labels;
    for (const Tool& t : m_tools) labels.push_back(t.label);
    JComboBox* tools = top->add(std::make_unique<JComboBox>(graph, labels, 0.f));
    m_toolBox = tools;
    tools->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    tools->setMinimumSize(3 * side, JStyle::current().controlHeight);   // a long name is cut short, not the buttons
    tools->setCurrentIndex(int(m_tool));
    auto tip = std::make_unique<JPIconButton>(graph, "Nozzle Tip", &JPIcons::nozzleTip,
                                              "The nozzle's tip: load one, unload it, or say which is on it");
    tip->setFramed(true);
    tip->setLeads(JPIconButton::Leads::Menu);
    tip->setFixedSize(side, side);
    tip->onClicked.connect([this] { showTipMenu(); });
    m_tipButton = top->add(std::move(tip));
    m_tipButton->setEnabled(m_tools[m_tool].nozzle);
    refreshTipButton();
    tools->onIndexChanged.connect([this](int i) {
        if (i < 0 || size_t(i) >= m_tools.size()) return;
        m_tool = size_t(i);
        if (m_tools[m_tool].nozzle) m_lastNozzle = m_tool;
        m_tipButton->setEnabled(m_tools[m_tool].nozzle);
        refreshTipButton();
        refreshRecycle();
        if (onChoicesChanged) onChoicesChanged();
    });
    add(std::move(top));

    m_distanceIndex = std::clamp(start.distance, 0, int(m_distances.size()) - 1);
    m_speedShare = std::clamp(start.speed, 0.0, 1.0);
    m_cell.setSpeed(speed());
    m_tabs = add(std::make_unique<JTabWidget>(graph, 0.f, 0.f));
    m_tabs->setVSizePolicy(JSizePolicyMode::Expanding, 1);

    m_note = add(std::make_unique<JLabel>(graph, ""));
    m_note->setWordWrap(true);
    m_watch.on(cell.onMotion, [this](bool ok, std::string why) { m_note->setText(ok ? std::string() : why); });
    // Unhomed: home it to move it, unless its controllers said where they are (Sync Initial Location), when it can be jogged.
    auto unhomedNote = [&cell] {
        for (const JPDriverConfig& d : cell.config().drivers)
            if (d.syncInitialLocation) return std::string("Not homed: jogging only, until the machine is homed.");
        return std::string("Home the machine to move it.");
    };
    m_watch.on(cell.onHomed, [this, unhomedNote](bool homed) { m_note->setText(homed ? std::string() : unhomedNote()); });
    if (!cell.isHomed()) m_note->setText(unhomedNote());
    refreshKeys();
}

std::string JPJogPanel::tip(const std::string& text, const std::string& action) const {
    const std::string key = keyFor ? keyFor(action) : std::string();
    return key.empty() ? text : text + " (" + key + ")";
}

void JPJogPanel::refreshKeys() {
    makePages();
}

void JPJogPanel::setSteps(std::vector<double> distances, std::vector<double> speeds) {
    if (distances.empty() || speeds.empty()) return;
    // The distance kept as near as the new steps allow to the one chosen.
    const double was = distance();
    m_distances = std::move(distances);
    m_speeds    = std::move(speeds);
    size_t nearest = 0;
    for (size_t i = 0; i < m_distances.size(); ++i)
        if (std::abs(m_distances[i] - was) < std::abs(m_distances[nearest] - was)) nearest = i;
    m_distanceIndex = int(nearest);
    makePages();
    if (onChoicesChanged) onChoicesChanged();
}

void JPJogPanel::setSpeedShare(double share) {
    if (m_speed) m_speed->setValue(std::clamp(share, 0.0, 1.0));   // its change says so
}

std::unique_ptr<JWidget> JPJogPanel::pad(float size, const char* name,
                                         void (*glyph)(JVectorCanvas&, float, float, float, const JColor&),
                                         const std::string& tooltip, const std::string& action) {
    auto b = std::make_unique<JPIconButton>(m_graph, name, glyph, tooltip);
    b->setFramed(true);
    b->setFixedSize(size, size);
    b->onClicked.connect([this, action] { act(action); });
    return b;
}

std::unique_ptr<JWidget> JPJogPanel::parkButton(float size, const std::string& tooltip, const std::string& action) {
    return pad(size, "Park", &JPIcons::park, tooltip, action);
}

std::unique_ptr<JWidget> JPJogPanel::gap(float size) {
    auto g = std::make_unique<JContainer>(m_graph, size, size);
    g->setFixedSize(size, size);
    return g;
}

std::vector<std::pair<double, std::string>> JPJogPanel::distanceMarks() const {
    // The distances evenly up the track, bottom first.
    std::vector<std::pair<double, std::string>> m;
    const double n = double(std::max<size_t>(m_distances.size(), 2) - 1);
    for (size_t i = 0; i < m_distances.size(); ++i) m.emplace_back(double(i) / n, stepText(m_distances[i]));
    return m;
}

std::vector<std::pair<double, std::string>> JPJogPanel::speedMarks() const {
    // Each speed step where it is on the track (a share of full speed), in %.
    std::vector<std::pair<double, std::string>> m;
    for (double v : m_speeds) m.emplace_back(v, stepText(v * 100));
    return m;
}


float JPJogPanel::padSizeFor(float width, float height) const {
    const JStyle& st = JStyle::current();
    // Five columns of pad (X/Y, Z, the position buttons) beside the two
    // sliders; a row of titles over four rows of pad.
    JPVerticalSlider d(m_graph, distanceMarks(), true), v(m_graph, speedMarks(), false);
    d.setCaption("Distance");
    v.setCaption("Speed 100%");   // its widest
    const float sliders = d.naturalWidth() + v.naturalWidth();
    const float across = (width - sliders - 8 * st.spacing - st.scrollBarWidth) / 5;
    const float down = (height - st.labelHeight - 6 * st.spacing) / 4;
    return std::clamp(std::min(across, down), 0.9f * st.buttonHeight, 2.5f * st.buttonHeight);
}

void JPJogPanel::makePages() {
    const int was = m_tabs->activeTab();
    while (m_tabs->tabCount() > 0) m_tabs->removeTab(m_tabs->tabCount() - 1);
    m_pages.clear();
    const JRect room = m_tabs->contentRect();
    m_builtW = room.width;
    m_builtH = room.height;
    m_pages.push_back(jogPage(padSizeFor(room.width, room.height)));
    m_tabs->addTab("Jog", m_pages.back().get());
    m_pages.push_back(specialPage());
    m_tabs->addTab("Special", m_pages.back().get());
    m_pages.push_back(safetyPage());
    m_tabs->addTab("Safety", m_pages.back().get());
    m_tabs->setActiveTab(std::max(0, was));
}

void JPJogPanel::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    JContainer::populateRenderPrimitives(buf);
    if (!m_tabs) return;
    // Resized: the pad made again for the new room, on the next frame (not
    // while this one is being drawn).
    const JRect room = m_tabs->contentRect();
    const float least = JStyle::current().spacing;
    if (std::abs(room.width - m_builtW) < least && std::abs(room.height - m_builtH) < least) return;
    m_builtW = room.width;
    m_builtH = room.height;
    std::weak_ptr<bool> alive = m_alive;
    jPostToNextFrame([this, alive] {
        if (alive.lock()) makePages();
    });
}

std::unique_ptr<JWidget> JPJogPanel::jogPage(float size) {
    const JStyle& st = JStyle::current();
    JSceneGraph& g = m_graph;
    // One block, stacked by a scroll area: a dock too short even for the
    // smallest pad scrolls rather than cutting it off.
    auto page = std::make_unique<JScrollArea>(g, 0.f, 0.f);
    auto row = [&](float h) {
        auto r = std::make_unique<JContainer>(g, 0.f, h);
        r->setDirection(JFlexDirection::JRow)->setGap(st.spacing)->setAlignItems(JAlignItems::Center);
        r->setFixedSize(5 * size + 4 * st.spacing, h);
        return r;
    };
    auto title = [&](const std::string& text, float w, float h) {
        auto box = std::make_unique<JContainer>(g, w, h);
        box->setFixedSize(w, h);
        box->setDirection(JFlexDirection::JRow)->setAlignItems(JAlignItems::Center);
        g.getLayout(box->getNodeId()).justifyContent = JJustifyContent::Center;
        auto l = std::make_unique<JLabel>(g, text, 0.f, st.labelHeight);
        l->setFixedSize(std::ceil(JTextHelper::measureWidth(text)) + st.spacing, st.labelHeight);
        box->add(std::move(l));
        return box;
    };
    using I = JPIcons;
    const float padH = st.labelHeight + 4 * size + 4 * st.spacing;
    auto block = std::make_unique<JContainer>(g, 0.f, padH);
    block->setDirection(JFlexDirection::JRow)->setGap(2 * st.spacing)->setAlignItems(JAlignItems::Start);
    block->setVSizePolicy(JSizePolicyMode::Fixed);
    block->setFixedSize(0.f, padH);
    block->setHSizePolicy(JSizePolicyMode::Expanding, 1);

    // OpenPnP's pad: the X/Y cross, Z, then the position buttons; C under X/Y.
    auto padColumn = std::make_unique<JContainer>(g, 0.f, padH);
    padColumn->setDirection(JFlexDirection::Column)->setGap(st.spacing)->setAlignItems(JAlignItems::Start);
    padColumn->setFixedSize(5 * size + 4 * st.spacing, padH);
    auto heads = row(st.labelHeight);
    heads->add(title("X/Y", 3 * size + 2 * st.spacing, st.labelHeight));
    heads->add(title("Z", size, st.labelHeight));
    padColumn->add(std::move(heads));
    auto top = row(size);
    top->add(gap(size));
    top->add(pad(size, "Y+", &I::arrowUp, tip("Y+", "y+"), "y+"));
    top->add(gap(size));
    top->add(pad(size, "Z+", &I::arrowUp, tip("Z+", "z+"), "z+"));
    top->add(pad(size, "Position Nozzle", &I::moveNozzle, "Put the nozzle where the camera is looking", "positionNozzle"));
    padColumn->add(std::move(top));
    auto middle = row(size);
    middle->add(pad(size, "X-", &I::arrowLeft, tip("X-", "x-"), "x-"));
    middle->add(parkButton(size, tip("Park the head", "parkXY"), "parkXY"));
    middle->add(pad(size, "X+", &I::arrowRight, tip("X+", "x+"), "x+"));
    middle->add(parkButton(size, tip("Up to safe Z", "parkZ"), "parkZ"));
    middle->add(pad(size, "Position Camera", &I::moveCamera, "Put the camera over the nozzle", "positionCamera"));
    padColumn->add(std::move(middle));
    auto bottom = row(size);
    bottom->add(gap(size));
    bottom->add(pad(size, "Y-", &I::arrowDown, tip("Y-", "y-"), "y-"));
    bottom->add(gap(size));
    bottom->add(pad(size, "Z-", &I::arrowDown, tip("Z-", "z-"), "z-"));
    padColumn->add(std::move(bottom));
    auto turn = row(size);
    turn->add(pad(size, "C+", &I::rotateAnticlockwise, tip("Turn anticlockwise", "c+"), "c+"));
    turn->add(parkButton(size, "Turn to 0", "parkC"));
    turn->add(pad(size, "C-", &I::rotateClockwise, tip("Turn clockwise", "c-"), "c-"));
    turn->add(title("C", size, size));
    padColumn->add(std::move(turn));
    block->add(std::move(padColumn));

    // Distance [mm/deg] and Speed [%], standing beside the pad, as tall as it.
    m_distance = block->add(std::make_unique<JPVerticalSlider>(g, distanceMarks(), true));
    m_distance->setCaption("Distance");
    m_distance->setFixedSize(m_distance->naturalWidth(), padH);
    m_distance->setTooltip(tip(tip("How far a press moves: mm, or degrees turning", "distance-"), "distance+"));
    const double steps = double(std::max<size_t>(m_distances.size(), 2) - 1);
    m_distance->setValue(double(m_distanceIndex) / steps);
    m_distance->onValueChanged.connect([this, steps](double v) {
        m_distanceIndex = std::clamp(int(std::lround(v * steps)), 0, int(m_distances.size()) - 1);
        if (onChoicesChanged) onChoicesChanged();
    });
    m_speed = block->add(std::make_unique<JPVerticalSlider>(g, speedMarks(), false));
    m_speed->setCaption("Speed 100%");   // as wide as it gets
    m_speed->setFixedSize(m_speed->naturalWidth(), padH);
    m_speed->setTooltip("The machine's speed: every move (a jog, a park, a task, a changer step) goes at this "
                        "share of its own speed");
    m_speed->setValue(m_speedShare);
    m_speed->setCaption("Speed " + percent(speed()));
    m_speed->onValueChanged.connect([this](double v) {
        m_speedShare = v;
        m_cell.setSpeed(speed());   // the machine's speed, for every move
        m_speed->setCaption("Speed " + percent(speed()));
        if (onChoicesChanged) onChoicesChanged();
    });
    page->addChildWidget(std::move(block));
    return page;
}

std::unique_ptr<JWidget> JPJogPanel::specialPage() {
    const JStyle& st = JStyle::current();
    auto page = std::make_unique<JContainer>(m_graph, 0.f, 0.f);
    page->setDirection(JFlexDirection::Column)->setGap(st.spacing)->setAlignItems(JAlignItems::Start)
        ->setPadding(JEdges(st.spacing));
    // As many to a line as the panel's width takes (OpenPnP's flow of them).
    auto buttons = std::make_unique<JContainer>(m_graph, 0.f, 0.f);
    buttons->setLayoutMode(JLayoutMode::Flow)->setGap(st.spacing);
    buttons->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    struct B { const char* label; const char* action; const char* tip; };
    for (const B& b : { B{ "Head Safe Z", "safeZ", "Every Z on the head up to safe Z" },
                        B{ "Discard", "discard", "Drop the nozzle's part at the discard location" },
                        B{ "Recycle", "recycle", "Put the part on the current nozzle back in a feeder." },
                        B{ "Pick", "pick", "Vacuum on where the nozzle is, as a pick does" },
                        B{ "Place", "place", "Vacuum off and blow off where the nozzle is, as a place does" } }) {
        JButton* button = buttons->add(JPUiParts::button(m_graph, b.label));
        button->setTooltip(tip(b.tip, b.action));
        button->onClicked.connect([this, action = std::string(b.action)] { act(action); });
        if (std::string(b.action) == "recycle") m_recycle = button;
    }
    page->add(std::move(buttons));
    refreshRecycle();
    return page;
}

std::unique_ptr<JWidget> JPJogPanel::safetyPage() {
    const JStyle& st = JStyle::current();
    auto page = std::make_unique<JContainer>(m_graph, 0.f, 0.f);
    page->setDirection(JFlexDirection::Column)->setGap(st.spacing)->setAlignItems(JAlignItems::Start)
        ->setPadding(JEdges(st.spacing));
    // OpenPnP's Board Protection: on for each session, as OpenPnP's opens.
    const std::string text = "Board Protection";
    JCheckBox* check = page->add(std::make_unique<JCheckBox>(
        m_graph, text, st.checkHeight + 2 * st.spacing + std::ceil(JTextHelper::measureWidth(text))));
    check->setChecked(m_boardProtection);
    check->setTooltip("Enable protection of the nozzle jogging closer than 1mm to any loaded board.");
    check->onStateChanged.connect([this](bool on) { m_boardProtection = on; });
    return page;
}

void JPJogPanel::refreshRecycle() {
    if (!m_recycle) return;
    const Tool* n = nozzle();
    m_recycle->setEnabled(n && canRecycle && canRecycle(n->id));
}

void JPJogPanel::showTipMenu() {
    if (m_tools.empty() || !m_tools[m_tool].nozzle || !openMenu) return;
    const JPCellConfig& c = m_cell.config();
    const JPNozzleConfig* nozzle = nullptr;
    for (const JPNozzleConfig& n : c.nozzles) if (n.id == m_tools[m_tool].id) nozzle = &n;
    if (!nozzle) return;
    auto name = [](const JPNozzleTipConfig& t) { return t.name.empty() ? t.id : t.name; };
    std::string onIt;
    for (const JPNozzleTipConfig& t : c.nozzleTips) if (t.id == nozzle->tipId) onIt = name(t);
    // Made afresh: the tips, and where each is, change.
    m_tipMenu = std::make_unique<JMenu>("Nozzle Tip");
    m_tipOnIt = std::make_unique<JMenu>("Manual Change");
    JSceneGraph& g = m_graph;
    m_tipMenu->add(g, nozzle->name + ": " + (onIt.empty() ? std::string("no tip on it") : onIt + " on it"))->setEnabled(false);
    m_tipMenu->addSeparator(g);
    const std::string nozzleId = nozzle->id;
    bool any = false;
    for (const JPNozzleTipConfig& t : c.nozzleTips) {
        if (!nozzle->fits(t.id)) continue;
        any = true;
        // Where it is now, when not free to load.
        std::string where;
        for (const JPNozzleConfig& n : c.nozzles) if (n.tipId == t.id) where = n.id == nozzleId ? "on it" : "on " + n.name;
        if (where.empty() && t.loadSteps.empty()) where = "no load steps";
        const std::string label = "Load " + name(t) + (where.empty() ? std::string() : " (" + where + ")");
        JMenuItem* item = m_tipMenu->add(g, label);
        item->setEnabled(where.empty());
        item->onTriggered.connect([this, nozzleId, id = t.id] {
            if (onChangeTip) onChangeTip(nozzleId, id, m_stepThrough);
        });
    }
    if (!any) m_tipMenu->add(g, "No tips fit " + nozzle->name + " (Machine Setup)")->setEnabled(false);
    m_tipMenu->addSeparator(g);
    JMenuItem* unload = m_tipMenu->add(g, onIt.empty() ? std::string("Unload") : "Unload " + onIt);
    unload->setEnabled(!onIt.empty());
    unload->onTriggered.connect([this, nozzleId] {
        if (onChangeTip) onChangeTip(nozzleId, "", m_stepThrough);
    });
    // The tip on it calibrated where it is (OpenPnP's nozzle tip Calibrate).
    std::string calibrateLabel = onIt.empty() ? std::string("Calibrate") : "Calibrate " + onIt;
    if (onIt.empty())             calibrateLabel += " (no tip on it)";
    else if (!m_cell.isHomed())   calibrateLabel += " (home the machine first)";
    JMenuItem* calibrate = m_tipMenu->add(g, calibrateLabel);
    calibrate->setEnabled(!onIt.empty() && m_cell.isHomed());
    calibrate->onTriggered.connect([this, nozzleId] {
        if (onCalibrateTip) onCalibrateTip(nozzleId);
    });
    m_tipMenu->addSeparator(g);
    JMenuItem* step = m_tipMenu->add(g, "Step Through");
    step->setCheckable(true);
    step->setChecked(m_stepThrough);
    step->onTriggered.connect([this] {
        m_stepThrough = !m_stepThrough;
        if (onChoicesChanged) onChoicesChanged();
    });
    // The nozzle taken where its tip is changed by hand: up to safe Z, across, down to the location's Z.
    std::string goLabel = "Move to Manual Change Location";
    if (!nozzle->manualChangeLocation) goLabel += " (not set: Machine Setup)";
    else if (!m_cell.isHomed())      goLabel += " (home the machine first)";
    JMenuItem* go = m_tipOnIt->add(g, goLabel);
    go->setEnabled(nozzle->manualChangeLocation && m_cell.isHomed());
    go->onTriggered.connect([this, mount = nozzle->mount, at = nozzle->manualChangeLocation] {
        if (at) m_cell.moveTool(mount, { at->x, at->y, at->z, at->rotation }, speed());
    });
    m_tipOnIt->addSeparator(g);
    // Saying which tip is on it: nothing moves.
    JMenuItem* none = m_tipOnIt->add(g, "None");
    none->setCheckable(true);
    none->setChecked(onIt.empty());
    none->onTriggered.connect([this, nozzleId] {
        if (onTipOnIt) onTipOnIt(nozzleId, "");
    });
    for (const JPNozzleTipConfig& t : c.nozzleTips) {
        if (!nozzle->fits(t.id)) continue;
        JMenuItem* item = m_tipOnIt->add(g, name(t));
        item->setCheckable(true);
        item->setChecked(t.id == nozzle->tipId);
        item->onTriggered.connect([this, nozzleId, id = t.id] {
            if (onTipOnIt) onTipOnIt(nozzleId, id);
        });
    }
    m_tipMenu->add(g, "Manual Change", {}, m_tipOnIt.get());
    // Z homed alone, with every nozzle on the same motor.
    m_tipMenu->addSeparator(g);
    std::string with;
    for (const std::string& id : m_cell.nozzlesHomedWith(nozzleId))
        for (const JPNozzleConfig& n : c.nozzles)
            if (n.id == id && id != nozzleId) with += (with.empty() ? "" : ", ") + n.name;
    const bool hasCommand = nozzle->homeCommand.find_first_not_of(" \t\r\n") != std::string::npos;
    std::string homeLabel = "Home Z";
    if (!hasCommand)               homeLabel += " (no home command: Machine Setup)";
    else if (!m_cell.isHomed())    homeLabel += " (home the machine first)";
    else if (!with.empty())        homeLabel += " (with " + with + ")";
    JMenuItem* homeZ = m_tipMenu->add(g, homeLabel);
    homeZ->setEnabled(hasCommand && m_cell.isHomed());
    homeZ->onTriggered.connect([this, nozzleId] {
        if (onHomeZ) onHomeZ(nozzleId);
    });
    const JRect b = m_graph.getLayoutConst(m_tipButton->getNodeId()).boundingBox;
    openMenu(m_tipMenu.get(), b.x, b.y + b.height);
}

double JPJogPanel::distance() const { return m_distances[size_t(m_distanceIndex)]; }
double JPJogPanel::lengthStep() const { return JPSystemUnits::stored(distance()); }
// No slower than the motion planner's Minimum Speed (OpenPnP's).
double JPJogPanel::speed() const    { return std::max({ kLeastSpeed, m_cell.config().motionPlanner.minimumSpeed, m_speedShare }); }

JPJogPanel::Choices JPJogPanel::choices() const {
    if (m_tools.empty()) return {};
    return { m_tools[m_tool].id, m_distanceIndex, m_speedShare, m_stepThrough, m_distances, m_speeds };
}

const std::string& JPJogPanel::toolId() const {
    static const std::string none;
    return m_tools.empty() ? none : m_tools[m_tool].id;
}

void JPJogPanel::refreshTipButton() {
    auto* button = static_cast<JPIconButton*>(m_tipButton);
    if (!button) return;
    // The tip on the chosen nozzle, calibrated there or not, when its calibration is on: green calibrated (or its
    // calibration off), red not calibrated; no tip on it, plain. Plain to see here, not only in Machine Setup.
    const Tool* n = nozzle();
    const JPCellConfig& c = m_cell.config();
    const JPNozzleTipConfig* tip = nullptr;
    std::string nozzleName;
    if (n)
        for (const JPNozzleConfig& nz : c.nozzles)
            if (nz.id == n->id) {
                nozzleName = nz.name;
                for (const JPNozzleTipConfig& t : c.nozzleTips)
                    if (t.id == nz.tipId) tip = &t;
            }
    std::string said = "The nozzle's tip: load one, unload it, or say which is on it";
    using Tone = JPIconButton::Tone;
    Tone tone = Tone::None;
    if (tip && !tip->runoutCalibration.enabled) {
        tone = Tone::Good;
        said += ".\n" + tip->name + " on " + nozzleName + ": its calibration is not enabled.";
    } else if (tip && tip->runout.count(n->id)) {
        tone = Tone::Good;
        said += ".\n" + tip->name + " on " + nozzleName + ": calibrated, " + JPWhen::withAgo(tip->runout.at(n->id).when) + ".";
    } else if (tip) {
        tone = Tone::Bad;
        said += ".\n" + tip->name + " on " + nozzleName + ": NOT calibrated (its calibration is enabled): Calibrate it (this menu).";
    }
    button->setTone(tone);
    button->setTooltip(said);
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
    // Along the axes in the System Units, turned in degrees.
    double d = lengthStep(), r = distance();
    // As OpenPnP's: with Shift held, two steps finer (a hundredth), no finer than the smallest distance.
    if (JWidget::s_shiftDown && !m_distances.empty() && r > 0) {
        const double k = std::min(1.0, std::max(kShiftFiner, m_distances.front() / r));
        d *= k;
        r *= k;
    }
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Jog: " << m_tools[m_tool].label << " by " << dx * d << ", " << dy * d << ", "
                                            << dz * d << ", " << dc * r;
    m_cell.jog(m_tools[m_tool].id, dx * d, dy * d, dz * d, dc * r, 1.0);
    if (onJogged) onJogged(m_tools[m_tool].id, dx * d, dy * d, dz * d);
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
    m_cell.moveTool(*tool.mount, { px->second + m.offsetX, py->second + m.offsetY, std::nullopt, std::nullopt }, 1.0);
}

void JPJogPanel::selectTool(const std::string& id) {
    for (size_t i = 0; i < m_tools.size(); ++i)
        if (m_tools[i].id == id && i != m_tool && m_toolBox) m_toolBox->setCurrentIndex(int(i));   // its change says so
}

bool JPJogPanel::act(const std::string& action) {
    if (m_tools.empty()) return false;
    const Tool& t = m_tools[m_tool];
    const std::string& head = t.mount->headId;
    if (action == "stop" || action == "emergencyStop") {
        if (onStop) onStop(action == "emergencyStop");
        return true;
    }
    if (action == "x+") jog(1, 0, 0, 0);
    else if (action == "x-") jog(-1, 0, 0, 0);
    else if (action == "y+") jog(0, 1, 0, 0);
    else if (action == "y-") jog(0, -1, 0, 0);
    else if (action == "z+") jog(0, 0, 1, 0);
    else if (action == "z-") jog(0, 0, -1, 0);
    else if (action == "c+") jog(0, 0, 0, 1);
    else if (action == "c-") jog(0, 0, 0, -1);
    else if (action == "parkXY") m_cell.park(head, 1.0);
    else if (action == "parkZ") m_cell.parkZ(*t.mount, 1.0);
    else if (action == "safeZ") m_cell.safeZ(head, 1.0);
    else if (action == "parkC") {
        if (!t.mount->axisRotation.empty()) m_cell.moveAxes({ { t.mount->axisRotation, 0.0 } }, 1.0);
    } else if (action == "positionNozzle" || action == "positionCamera") {
        const Tool* n = nozzle();
        const Tool* c = camera();
        if (!n || !c) {
            m_note->setText("There needs to be a nozzle and a camera on its head.");
            return true;
        }
        if (action == "positionNozzle") moveTo(*n, *c);
        else moveTo(*c, *n);
    } else if (action == "recycle") {
        if (const Tool* n = nozzle(); n && onRecycle) onRecycle(n->id);
    } else if (action == "discard" || action == "pick" || action == "place") {
        const Tool* n = nozzle();
        if (!n) {
            m_note->setText("Choose a nozzle.");
            return true;
        }
        if (action == "discard") m_cell.discard(n->id, 1.0);
        else if (action == "pick") m_cell.pick(n->id);
        else m_cell.place(n->id);
        if (action != "pick" && onPartGone) onPartGone(n->id);
    } else if (action == "distance+" || action == "distance-" || action.rfind("distance:", 0) == 0
               || action.rfind("increment:", 0) == 0) {
        // OpenPnP's First to Fifth Jog Increment: 0.01 mm (0.001 in) times ten each; the distance nearest it.
        auto nearest = [this](int n) {
            const double want = (JPSystemUnits::inches() ? kFirstIncrementIn : kFirstIncrementMm) * std::pow(10.0, n - 1);
            int best = 0;
            for (size_t k = 0; k < m_distances.size(); ++k)
                if (std::abs(std::log(m_distances[k] / want)) < std::abs(std::log(m_distances[size_t(best)] / want))) best = int(k);
            return best;
        };
        const int i = action == "distance+" ? m_distanceIndex + 1
                    : action == "distance-" ? m_distanceIndex - 1
                    : action.rfind("increment:", 0) == 0 ? nearest(std::atoi(action.c_str() + 10))
                    : std::atoi(action.c_str() + 9);
        const double steps = double(std::max<size_t>(m_distances.size(), 2) - 1);
        if (i >= 0 && i < int(m_distances.size()) && m_distance)
            m_distance->setValue(double(i) / steps);   // its change says so
    } else if (action == "speed+") {
        // The next step up from the speed now.
        for (double v : m_speeds)
            if (v > m_speedShare + 1e-9) { setSpeedShare(v); break; }
    } else if (action == "speed-") {
        for (auto it = m_speeds.rbegin(); it != m_speeds.rend(); ++it)
            if (*it < m_speedShare - 1e-9) { setSpeedShare(*it); break; }
    } else if (action.rfind("speed:", 0) == 0) {
        const int i = std::atoi(action.c_str() + 6);
        if (i >= 0 && i < int(m_speeds.size())) setSpeedShare(m_speeds[size_t(i)]);
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
    // A nozzle holding a part reads the part's angle (its rotation mode offset), as OpenPnP's DRO.
    const std::tuple<const char*, const std::string*, double> axes[] = {
        { "X", &m.axisX, m.offsetX }, { "Y", &m.axisY, m.offsetY }, { "Z", &m.axisZ, m.offsetZ },
        { "C", &m.axisRotation, m_cell.rotationModeOffsetOf(m) } };
    for (const auto& [name, axis, offset] : axes)
        if (const auto p = positions.find(*axis); !axis->empty() && p != positions.end())
            out.emplace_back(name, p->second + offset);
    return out;
}

} // inline namespace jf
