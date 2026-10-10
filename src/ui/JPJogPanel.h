// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCellWatch.h"
#include "JPVerticalSlider.h"

#include "machine/JPCell.h"

#include <j/core/JComboBox.h>
#include <j/core/JButton.h>
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
// before it runs), Manual Change (say which tip was put on by hand;
// nothing moves), and Home Z (the nozzle's Z homed alone, with any nozzle
// sharing its motor).
//
//  - Jog: an X / Y pad of arrows with Park (the head to its park place) in
//    its middle; Z up and down with Park (to safe Z) between; the rotation
//    either way with Park (to 0) between; buttons to put the nozzle where
//    the camera is looking and the camera over the nozzle; sliders for the
//    distance a press moves (mm, or degrees turning) and the machine's speed
//    (JPCell::setSpeed: a share every move is scaled by, as in OpenPnP),
//    each marked with its steps (the person's own, Preferences > Jog). The
//    pad's buttons are as big as the dock lets them be, and are made again
//    when it is resized.
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
    // OpenPnP's: its first jog increment (each next ten times it), and Shift's two steps finer.
    static constexpr double kFirstIncrementMm = 0.01, kFirstIncrementIn = 0.001, kShiftFiner = 0.01;
    static constexpr double kSpeedFirst    = 0.25;
    // The steps until the person sets their own: distances (mm or degrees),
    // and speeds (shares of full speed).
    static const std::vector<double>& defaultDistances();
    static const std::vector<double>& defaultSpeeds();
    // Steps as a person types and reads them ("0.01 0.1 1 10"), each within
    // [least, most], in rising order. Empty with `why` when they are not.
    static std::vector<double> parseSteps(const std::string& text, double least, double most, std::string& why);
    static std::string formatSteps(const std::vector<double>& steps);
    // One step as shown ("0.01", "25"): no trailing zeros.
    static std::string stepText(double v);

    // What was chosen: the tool (its id), the distance (its index in the
    // row of distances) and the speed (a share of top speed); and the steps
    // to choose from.
    struct Choices {
        std::string tool;
        int         distance = kDistanceFirst;
        double      speed    = kSpeedFirst;
        bool        stepThrough = true;   // tip changes asked about step by step
        std::vector<double> distances = defaultDistances();
        std::vector<double> speeds    = defaultSpeeds();
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
    // Home the nozzle's Z alone (JPCell::homeNozzle).
    std::function<void(const std::string& nozzleId)> onHomeZ;
    // The tip on a nozzle calibrated (its runout measured over the camera looking up), from its tip menu.
    std::function<void(const std::string& nozzleId)> onCalibrateTip;
    // A tool jogged (its id, and by how much: X, Y, Z in mm): for the camera looking at it to be shown.
    std::function<void(const std::string& toolId, double dx, double dy, double dz)> onJogged;
    // A nozzle's part put down or dropped (Place, Discard): it holds none.
    std::function<void(const std::string& nozzleId)> onPartGone;
    // OpenPnP's Recycle: the part on the nozzle put back into a feeder;
    // offered only when some enabled feeder holding it can take it back.
    std::function<void(const std::string& nozzleId)> onRecycle;
    std::function<bool(const std::string& nozzleId)> canRecycle;
    // Recycle offered or not again (a nozzle's part, or a feeder, changed).
    void refreshRecycle();
    // The part a nozzle holds (empty: none), named in its entry as OpenPnP's NozzleItem names it; the entries
    // named again when a nozzle's part changes.
    std::function<std::string(const std::string& nozzleId)> partOn;
    void refreshNames();
    // Stop (the move held and dropped) or, `emergency`, reset every controller.
    std::function<void(bool emergency)> onStop;
    // The key an action has now (Preferences > Keys), for the tooltips; "" none.
    std::function<std::string(const std::string& action)> keyFor;
    // OpenPnP's Board Protection (the Safety tab): a jog that would take a
    // tool on the head too near a board of the job is refused.
    bool boardProtection() const { return m_boardProtection; }

    // New steps (Preferences > Jog): the sliders marked with them, the
    // distance chosen kept to one of them.
    void setSteps(std::vector<double> distances, std::vector<double> speeds);
    // The keys changed: the tooltips say the new ones.
    void refreshKeys();

    // The chosen tool's coordinates now, by name (X, Y, Z, C): where its
    // axes are plus its offset on the head.
    std::vector<std::pair<std::string, double>> where() const;
    // The chosen tool (its id), and the machine's speed, as a share of full
    // speed (it sets JPCell::setSpeed).
    const std::string& toolId() const;
    // The tool of `id` chosen (OpenPnP's auto tool select); one not listed is not.
    void selectTool(const std::string& id);
    double speed() const;

    // An action, as its button does: "stop", "emergencyStop" (onStop),
    // "x+", "x-", "y+", "y-", "z+", "z-",
    // "c+", "c-", "parkXY", "parkZ", "parkC", "safeZ", "discard",
    // "pick", "place", "positionNozzle", "positionCamera", "distance+",
    // "distance-", "distance:<n>" (the n-th step, from 0), "speed+",
    // "speed-" (the next step up or down from the speed now), "speed:<n>".
    // False when there is no such action.
    bool act(const std::string& action);

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

    // The distance chosen for a jog step (mm).
    double distance() const;     // as chosen: in the System Units (a turn's, degrees)
    double lengthStep() const;   // the distance as a length, in millimetres

private:
    struct Tool {
        std::string id, label;   // a nozzle's label without its part (refreshNames adds it)
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
    std::unique_ptr<JWidget> safetyPage();
    // The tip menu for the chosen nozzle, made afresh each time it opens.
    void showTipMenu();
    // A square pad button showing `glyph` that does `action`.
    std::unique_ptr<JWidget> pad(float size, const char* name,
                                 void (*glyph)(JVectorCanvas&, float, float, float, const JColor&),
                                 const std::string& tooltip, const std::string& action);
    std::unique_ptr<JWidget> parkButton(float size, const std::string& tooltip, const std::string& action);
    // `text`, with the key `action` has now after it, when it has one.
    std::string tip(const std::string& text, const std::string& action) const;
    std::vector<std::pair<double, std::string>> distanceMarks() const;
    std::vector<std::pair<double, std::string>> speedMarks() const;
    void setSpeedShare(double share);
    std::unique_ptr<JWidget> gap(float size);
    void jog(double dx, double dy, double dz, double dc);
    void moveTo(const Tool& tool, const Tool& over);
    // The nozzle the position buttons use: the chosen tool when it is one,
    // else the nozzle chosen last (else the first).
    const Tool* nozzle() const;
    const Tool* camera() const;

    JPCell&                 m_cell;
    std::vector<Tool>       m_tools;
    size_t                  m_tool = 0;
    JComboBox*              m_toolBox = nullptr;
    size_t                  m_lastNozzle = 0;
    JTabWidget*             m_tabs = nullptr;
    JWidget*                m_tipButton = nullptr;   // a JPIconButton
    // The tip button's colour and tooltip: the chosen nozzle's tip calibrated there or not.
    void refreshTipButton();
    void keepToolBoxNarrow();
    JButton*                m_recycle = nullptr;
    std::vector<double>     m_distances, m_speeds;
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
    bool                    m_boardProtection = true;
};

} // inline namespace jf
