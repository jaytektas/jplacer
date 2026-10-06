// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPPlacementsHolderLocation.h"

#include <j/core/JControl.h>
#include <j/core/MenuSystem.h>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// The drawing of OpenPnP's board and panel viewer
// (PlacementsHolderLocationViewer): each board's and panel's outline (top
// side up in one colour, bottom side up in another, struck through when
// it is not enabled), and as chosen its placements (a 1 mm square), its
// fiducials (a 1 mm disc), the boards' and panels' locations and origins,
// and a reticle; millimetre scales along the top and left. The wheel
// zooms about the pointer, a left drag pans; a right-click on a board,
// panel, placement or fiducial opens its menu (Enabled?, Check Fids?).
class JPPlacementsViewerCanvas : public JControl {
public:
    enum class Viewing { All, Children, Selected };

    explicit JPPlacementsViewerCanvas(JSceneGraph& graph);

    // What is shown: `root` and what is under it (its children, or all its
    // descendants). `isJob`: the job's root, whose own outline is not drawn.
    void setRoot(JPPlacementsHolderLocation* root, bool isJob);
    JPPlacementsHolderLocation* root() const { return m_root; }
    void setSelections(std::vector<const JPPlacementsHolderLocation*> s) { m_selections = std::move(s); }

    bool    viewFromTop = true;
    bool    showReticle = false, showLocations = true, showOrigins = false, showFiducials = false,
            showPlacements = false;
    Viewing viewing = Viewing::Children;
    // The array generator's: the board or panel copied, drawn dashed long,
    // and its copies, dashed short.
    const JPPlacementsHolderLocation*              arrayRoot = nullptr;
    std::vector<const JPPlacementsHolderLocation*> arrayMembers;

    // What is drawn worked out again (the boards, panels or placements changed).
    void regenerate();

    // A right-click menu's changes, made by whoever owns what is shown:
    // a placement's Enabled?, a board's or panel's Enabled? or Check Fids?.
    std::function<void(JPPlacementsHolderLocation* where, const std::string& placementId, bool on)> onPlacementEnabled;
    std::function<void(JPPlacementsHolderLocation* where, bool on)> onLocationEnabled;
    std::function<void(JPPlacementsHolderLocation* where, bool on)> onCheckFiducials;
    // The job's viewer's, as OpenPnP's: a placement's Placed? (and what it
    // is now), the head camera centred on a place, and a board's or panel's
    // fiducial check.
    std::function<bool(const JPPlacementsHolderLocation* where, const std::string& placementId)> placedOf;
    std::function<void(JPPlacementsHolderLocation* where, const std::string& placementId, bool placed)> onPlacementPlaced;
    std::function<void(const JPLocation& at)> onCenterCamera;
    std::function<void(JPPlacementsHolderLocation* where)> onFiducialCheck;

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
    void handleMousePress(float mx, float my) override;
    void handleMouseRelease(float mx, float my) override;
    void handleMouseMove(float mx, float my) override;
    bool handleScroll(float mx, float my, float wheel) override;
    void prepareContextMenu(float mx, float my) override;

    struct Vec { double x, y; };
    struct Rect {
        double x = 0, y = 0, w = 0, h = 0;
        void add(const Vec& p);
        void add(const Rect& r);
    };

private:
    using Polygon = std::vector<Vec>;
    struct Outline {
        JPPlacementsHolderLocation* where;
        std::vector<Polygon>        polygons;   // in the job's (or root's) millimetres
        Rect                        bounds;
    };
    struct Mark {
        JPPlacementsHolderLocation* where;
        const JPPlacement*          placement;
        Polygon                     area;       // the 1 mm square or disc
    };

    void generate(JPPlacementsHolderLocation* l);
    std::vector<const JPPlacement*> placementsOf(JPPlacementsHolderLocation* l);
    bool atRootOrJobTop(const JPPlacementsHolderLocation* l) const;
    const Mark* markOf(const JPPlacement* p) const;

    // The view: the area drawn in (the scales off), its edge gap, and the
    // object (millimetre) rectangle shown.
    JRect drawingArea() const;
    float edgeGap() const;
    void  fitView();
    void  resized();
    Vec   toScreen(const Vec& p) const;
    Vec   toObject(float sx, float sy) const;
    double scale() const;

    void drawOutlines(JPrimitiveBuffer& buf);
    void drawLocationMark(JPrimitiveBuffer& buf, double sizePx, const JPLocation& at);
    void drawOriginMark(JPrimitiveBuffer& buf, const JPPlacementsHolderLocation& l);
    void drawPlacementMarks(JPrimitiveBuffer& buf, JPPlacement::Type type);
    void drawReticleAndScales(JPrimitiveBuffer& buf);

    JPPlacementsHolderLocation*                    m_root = nullptr;
    bool                                           m_isJob = false;
    std::vector<const JPPlacementsHolderLocation*> m_selections;
    std::vector<Outline>                           m_outlines;
    std::vector<Mark>                              m_marks;
    std::deque<JPPlacement>                        m_pseudo;   // pseudo-placements drawn, kept while drawn
    std::optional<Rect>                            m_graphics;
    Rect                                           m_viewable, m_default;
    double                                         m_zoom = 1.0;
    float                                          m_lastW = 0, m_lastH = 0;
    bool                                           m_pressed = false;
    float                                          m_dragX = 0, m_dragY = 0;
    std::unique_ptr<JMenu>                         m_menu;
};

} // inline namespace jf
