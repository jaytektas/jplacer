// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellWatch.h"
#include "JPChoiceRow.h"

#include "machine/JPCell.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JSlider.h>
#include <j/core/JTabWidget.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// Moving the machine by hand, as OpenPnP's Machine Controls do. The tool is
// chosen at the top (a nozzle, a camera, anything on the head with axes).
//
//  - Jog: Home; an X / Y pad of arrows with Park (the head to its park
//    place) in its middle; Z up and down with Park (to safe Z) between;
//    the rotation either way with Park (to 0) between; buttons to put the
//    nozzle where the camera is looking and the camera over the nozzle; the
//    distance a press moves (mm, or degrees turning) and the speed (a share
//    of the slowest moving axis's rate).
//  - Special: Head Safe Z, Discard (the part to the discard location), and
//    Pick and Place where the nozzle is.
//
// Moves are in the TOOL's coordinates: a nozzle's Z is its own even where two
// nozzles share one motor, and the cell works out which axes turn. Every
// button is also an action (act()), which Machine > Jog gives a key.
class JPJogPanel : public JContainer {
public:
    static constexpr int    kDistanceFirst = 2;      // 1 mm
    static constexpr double kSpeedFirst    = 0.25;
    // What was chosen: the tool (its id), the distance (its index in the
    // row of distances) and the speed (a share of top speed).
    struct Choices {
        std::string tool;
        int         distance = kDistanceFirst;
        double      speed    = kSpeedFirst;
    };

    JPJogPanel(JSceneGraph& graph, JPCell& cell, Choices start);

    Choices choices() const;
    // A choice changed (to keep it for next time).
    std::function<void()> onChoicesChanged;

    // The chosen tool's coordinates now, by name (X, Y, Z, C): where its
    // axes are plus its offset on the head.
    std::vector<std::pair<std::string, double>> where() const;
    // The chosen tool (its id), and the speed chosen, as a share of top speed.
    const std::string& toolId() const;
    double speed() const;

    // An action, as its button does: "x+", "x-", "y+", "y-", "z+", "z-",
    // "c+", "c-", "parkXY", "parkZ", "parkC", "home", "safeZ", "discard",
    // "pick", "place", "positionNozzle", "positionCamera", "distance+",
    // "distance-". False when there is no such action.
    bool act(const std::string& action);

private:
    struct Tool {
        std::string id, label;
        const JPMountConfig* mount;
        bool nozzle, camera;
    };

    std::unique_ptr<JWidget> jogPage();
    std::unique_ptr<JWidget> specialPage();
    // A square pad button showing `glyph` that does `action`.
    std::unique_ptr<JWidget> pad(const char* name, void (*glyph)(JVectorCanvas&, float, float, float, const JColor&),
                                 const std::string& tooltip, const std::string& action);
    std::unique_ptr<JWidget> parkButton(const std::string& tooltip, const std::string& action);
    std::unique_ptr<JWidget> gap();
    static float padSize();
    void jog(double dx, double dy, double dz, double dc);
    void moveTo(const Tool& tool, const Tool& over);
    // The nozzle the position buttons use: the chosen tool when it is one,
    // else the nozzle chosen last (else the first).
    const Tool* nozzle() const;
    const Tool* camera() const;
    double distance() const;

    JPCell&                 m_cell;
    std::vector<Tool>       m_tools;
    size_t                  m_tool = 0;
    size_t                  m_lastNozzle = 0;
    JTabWidget*             m_tabs = nullptr;
    std::vector<std::unique_ptr<JWidget>> m_pages;
    JPChoiceRow*            m_distance = nullptr;
    JSlider*                m_speed = nullptr;
    JLabel*                 m_speedLabel = nullptr;
    JLabel*                 m_note  = nullptr;
    JPCellWatch             m_watch;
};

} // inline namespace jf
