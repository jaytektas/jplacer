// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellWatch.h"
#include "JPVerticalSlider.h"

#include "machine/JPCell.h"

#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JTabWidget.h>
#include <j/core/MenuSystem.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// Moving the machine by hand, as OpenPnP's Machine Controls do. The tool is
// chosen at the top (a nozzle, a camera, anything on the head with axes);
// beside it, for a nozzle, the tip menu: the tips that fit it to load (the
// one on it unloaded first), Unload, Step Through (each changer step asked
// before it runs), and Tip On It (say which tip is on it; nothing moves).
//
//  - Jog: an X / Y pad of arrows with Park (the head to its park place) in
//    its middle; Z up and down with Park (to safe Z) between; the rotation
//    either way with Park (to 0) between; buttons to put the nozzle where
//    the camera is looking and the camera over the nozzle; sliders for the
//    distance a press moves (mm, or degrees turning) and the machine's speed
//    (JPCell::setSpeed: a share every move is scaled by, as in OpenPnP). The pad's buttons are as big as the
//    dock lets them be, and are made again when it is resized.
//  - Special: Head Safe Z, Discard (the part to the discard location), and
//    Pick and Place where the nozzle is.
//
// Moves are in the TOOL's coordinates: a nozzle's Z is its own even where two
// nozzles share one motor, and the cell works out which axes turn. Every
// button is also an action (act()), which Machine > Jog gives a key. Homing
// is on the toolbar.
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
        bool        stepThrough = true;   // tip changes asked about step by step
    };

    JPJogPanel(JSceneGraph& graph, JPCell& cell, Choices start);

    Choices choices() const;
    // A choice changed (to keep it for next time).
    std::function<void()> onChoicesChanged;
    // The tip menu: put `tipId` on the nozzle by its changer steps ("" to
    // unload), asking before each step when `everyStep`; say which tip is on
    // it, moving nothing; show `menu` at the panel's (x, y).
    std::function<void(const std::string& nozzleId, const std::string& tipId, bool everyStep)> onChangeTip;
    std::function<void(const std::string& nozzleId, const std::string& tipId)> onTipOnIt;
    std::function<void(JMenu* menu, float x, float y)> openMenu;

    // The chosen tool's coordinates now, by name (X, Y, Z, C): where its
    // axes are plus its offset on the head.
    std::vector<std::pair<std::string, double>> where() const;
    // The chosen tool (its id), and the machine's speed, as a share of full
    // speed (it sets JPCell::setSpeed).
    const std::string& toolId() const;
    double speed() const;

    // An action, as its button does: "x+", "x-", "y+", "y-", "z+", "z-",
    // "c+", "c-", "parkXY", "parkZ", "parkC", "safeZ", "discard",
    // "pick", "place", "positionNozzle", "positionCamera", "distance+",
    // "distance-". False when there is no such action.
    bool act(const std::string& action);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    struct Tool {
        std::string id, label;
        const JPMountConfig* mount;
        bool nozzle, camera;
    };

    // The Jog page with pad buttons `size` across; the pages made again for
    // the room the tabs have now.
    std::unique_ptr<JWidget> jogPage(float size);
    void makePages();
    // The pad buttons' size for a page `width` x `height`.
    float padSizeFor(float width, float height) const;
    std::unique_ptr<JWidget> specialPage();
    // The tip menu for the chosen nozzle, made afresh each time it opens.
    void showTipMenu();
    // A square pad button showing `glyph` that does `action`.
    std::unique_ptr<JWidget> pad(float size, const char* name,
                                 void (*glyph)(JVectorCanvas&, float, float, float, const JColor&),
                                 const std::string& tooltip, const std::string& action);
    std::unique_ptr<JWidget> parkButton(float size, const std::string& tooltip, const std::string& action);
    std::unique_ptr<JWidget> gap(float size);
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
    JWidget*                m_tipButton = nullptr;
    std::unique_ptr<JMenu>  m_tipMenu, m_tipOnIt;
    bool                    m_stepThrough = true;
    std::vector<std::unique_ptr<JWidget>> m_pages;
    JPVerticalSlider*       m_distance = nullptr;
    JPVerticalSlider*       m_speed = nullptr;
    int                     m_distanceIndex = kDistanceFirst;
    double                  m_speedShare = kSpeedFirst;
    float                   m_builtW = 0, m_builtH = 0;   // the room the pages were made for
    std::shared_ptr<bool>   m_alive = std::make_shared<bool>(true);   // for a remake posted to the next frame
    JLabel*                 m_note  = nullptr;
    JPCellWatch             m_watch;
};

} // inline namespace jf
