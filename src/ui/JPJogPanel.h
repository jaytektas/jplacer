// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellWatch.h"
#include "JPChoiceRow.h"

#include "machine/JPCell.h"

#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JLineEdit.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// Moving a tool by hand. Choose the tool (a nozzle, a camera, anything on
// the head with axes), and each of its coordinates is a row: where it is
// now, a box to type where it should go (Return goes there), and − / + to
// step it. A nozzle with a vacuum has Pick and Place (JPCell::pick). Step
// and speed are chosen from rows that show their values.
//
// Moves are in the TOOL's coordinates: a nozzle's Z is its own even where two
// nozzles share one motor, and the cell works out which axes turn.
class JPJogPanel : public JContainer {
public:
    JPJogPanel(JSceneGraph& graph, JPCell& cell);

    // The chosen tool's coordinates now, by name (X, Y, Z, C): where its
    // axes are plus its offset on the head.
    std::vector<std::pair<std::string, double>> where() const;
    // The chosen tool (its id), and the speed chosen, as a share of top speed.
    const std::string& toolId() const;
    double speed() const;

private:
    struct Tool {
        std::string id, name;
        const JPMountConfig* mount;
    };
    struct Coordinate {
        std::string axisId;
        double      offset;   // the tool's offset on the head along it: tool = axis + offset
        JLineEdit*  field;
    };

    void showTool(size_t index);
    void showPositions(const std::map<std::string, double>& positions);
    void step(int coordinate, double direction);
    double stepSize() const;
    static float labelWidth();

    JPCell&                 m_cell;
    std::vector<Tool>       m_tools;
    size_t                  m_tool = 0;
    JContainer*             m_coords = nullptr;
    std::vector<Coordinate> m_coordinates;
    JPChoiceRow*            m_step  = nullptr;
    JPChoiceRow*            m_speed = nullptr;
    JLabel*                 m_note  = nullptr;
    JPCellWatch             m_watch;
};

} // inline namespace jf
